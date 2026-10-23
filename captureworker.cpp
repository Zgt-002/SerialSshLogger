#include "captureworker.h"
#include "logwriter.h"

#include <QSerialPort>
#include <QTextCodec>
#include <QThread>
#include <QTimer>
#include <utility>

CaptureWorker::CaptureWorker(CaptureOptions options) : m_options(std::move(options))
{
}

bool CaptureWorker::interrupted() const
{
    return m_stopped || QThread::currentThread()->isInterruptionRequested();
}

void CaptureWorker::start()
{
    if (interrupted()) {
        stop();
        return;
    }
    m_terminal.reset(new TerminalStream(m_options.encoding));
    if (m_options.kind == SourceKind::Serial)
        startSerial();
    else
        startSsh();
}

bool CaptureWorker::beginCapture(const QString &prefix)
{
    m_writer = new LogWriter(this);
    connect(m_writer, &LogWriter::fileChanged, this, &CaptureWorker::fileChanged);
    connect(m_writer, &LogWriter::failed, this, &CaptureWorker::fail);
    if (!m_writer->start(m_options.directory, prefix, m_options.intervalSeconds,
                         m_options.timestamps))
        return false;
    emit statusChanged(tr("已连接，正在记录"));
    emit connected();
    return true;
}

void CaptureWorker::startSerial()
{
    m_serial = new QSerialPort(this);
    connect(m_serial, &QSerialPort::readyRead, this, [this] {
        if (m_writer && !m_stopped)
            receive(m_serial->readAll());
    });
    m_serial->setPortName(m_options.serialPort);
    const bool configured = m_serial->setBaudRate(m_options.baudRate)
        && m_serial->setDataBits(QSerialPort::DataBits(m_options.dataBits))
        && m_serial->setParity(QSerialPort::Parity(m_options.parity))
        && m_serial->setStopBits(QSerialPort::StopBits(m_options.stopBits))
        && m_serial->setFlowControl(QSerialPort::FlowControl(m_options.flowControl));
    if (!configured || !m_serial->open(QIODevice::ReadOnly)) {
        fail(tr("串口打开失败（%1）：%2")
                 .arg(int(m_serial->error())).arg(m_serial->errorString()));
        return;
    }
    connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error != QSerialPort::NoError)
            fail(tr("串口接收失败（%1）：%2")
                     .arg(int(error)).arg(m_serial->errorString()));
    });
    if (!beginCapture("serial_" + m_options.serialPort))
        return;
    receive(m_serial->readAll());
}

void CaptureWorker::startSsh()
{
    emit statusChanged(tr("正在连接 SSH…"));
    m_session = ssh_new();
    if (!m_session) {
        fail(tr("无法创建 SSH 会话"));
        return;
    }
    const QByteArray host = m_options.host.toUtf8();
    const QByteArray user = m_options.username.toUtf8();
    const QByteArray knownHosts = m_options.knownHostsPath.toUtf8();
    const long timeout = 10;
    const bool processConfig = false;
    if (ssh_options_set(m_session, SSH_OPTIONS_HOST, host.constData()) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_PORT, &m_options.sshPort) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_USER, user.constData()) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &timeout) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_PROCESS_CONFIG, &processConfig) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_HOSTKEYS, "+ssh-rsa") != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_KNOWNHOSTS, knownHosts.constData()) != SSH_OK
        || ssh_options_set(m_session, SSH_OPTIONS_GLOBAL_KNOWNHOSTS, knownHosts.constData()) != SSH_OK) {
        fail(tr("SSH 配置失败：%1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return;
    }
    if (ssh_connect(m_session) != SSH_OK) {
        fail(tr("SSH 连接失败：%1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return;
    }
    if (interrupted()) {
        stop();
        return;
    }
    const auto state = ssh_session_is_known_server(m_session);
    if (state == SSH_KNOWN_HOSTS_OK) {
        authenticate();
        return;
    }
    if (state != SSH_KNOWN_HOSTS_UNKNOWN && state != SSH_KNOWN_HOSTS_NOT_FOUND) {
        fail(tr("SSH 主机密钥校验失败，连接已停止。请核对设备身份和 known_hosts 文件。"));
        return;
    }
    ssh_key key = nullptr;
    unsigned char *hash = nullptr;
    size_t size = 0;
    if (ssh_get_server_publickey(m_session, &key) != SSH_OK
        || ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &size) != SSH_OK) {
        if (key)
            ssh_key_free(key);
        fail(tr("无法读取 SSH 主机指纹"));
        return;
    }
    const QString fingerprint = "SHA256:" + QString::fromLatin1(
        QByteArray(reinterpret_cast<const char *>(hash), int(size))
            .toBase64(QByteArray::OmitTrailingEquals));
    ssh_clean_pubkey_hash(&hash);
    ssh_key_free(key);
    m_waitingForHostKey = true;
    emit statusChanged(tr("等待确认 SSH 主机指纹"));
    emit hostKeyApprovalRequested(m_options.host, fingerprint);
}

void CaptureWorker::approveHostKey(bool approved)
{
    if (!m_waitingForHostKey || m_stopped)
        return;
    m_waitingForHostKey = false;
    if (!approved || interrupted()) {
        stop();
        return;
    }
    if (ssh_session_update_known_hosts(m_session) != SSH_OK) {
        fail(tr("无法保存 SSH 主机密钥：%1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return;
    }
    authenticate();
}

void CaptureWorker::authenticate()
{
    emit statusChanged(tr("正在验证 SSH 身份…"));
    int result = ssh_userauth_none(m_session, nullptr);
    if (result != SSH_AUTH_SUCCESS) {
        QByteArray password = m_options.password.toUtf8();
        result = ssh_userauth_password(m_session, nullptr, password.constData());
        password.fill('\0');
    }
    m_options.password.clear();
    if (interrupted()) {
        stop();
        return;
    }
    if (result != SSH_AUTH_SUCCESS) {
        fail(tr("SSH 密码认证失败：%1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return;
    }
    m_channel = ssh_channel_new(m_session);
    if (!m_channel || ssh_channel_open_session(m_channel) != SSH_OK
        || ssh_channel_request_pty_size(m_channel, "dumb", 160, 40) != SSH_OK
        || ssh_channel_request_shell(m_channel) != SSH_OK) {
        fail(tr("SSH 终端打开失败：%1").arg(QString::fromUtf8(ssh_get_error(m_session))));
        return;
    }
    if (interrupted()) {
        stop();
        return;
    }
    ssh_set_blocking(m_session, 0);
    if (!beginCapture("ssh_" + m_options.host + "_" + QString::number(m_options.sshPort)))
        return;
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(10);
    connect(m_pollTimer, &QTimer::timeout, this, &CaptureWorker::pollSsh);
    m_pollTimer->start();
    pollSsh();
}

void CaptureWorker::receive(const QByteArray &data)
{
    if (data.isEmpty() || !m_writer)
        return;
    m_receivedBytes += data.size();
    emit receivedBytesChanged(m_receivedBytes);
    const auto lines = m_terminal->feed(data, QDateTime::currentDateTime());
    QString completed;
    QString currentLine;
    for (const TerminalLine &line : lines) {
        if (line.complete)
            completed += line.text + QLatin1Char('\n');
        else
            currentLine = line.text;
    }
    if (!lines.isEmpty())
        emit terminalUpdated(completed, currentLine);
    for (const TerminalLine &line : lines) {
        if (!m_writer->append(line))
            return;
    }
}

void CaptureWorker::sendInput(const QByteArray &data)
{
    if (!m_stopped && m_channel) {
        m_pendingInput.append(QTextCodec::codecForName(m_options.encoding)
                                 ->fromUnicode(QString::fromUtf8(data)));
        pollSsh();
    }
}

void CaptureWorker::pollSsh()
{
    if (interrupted()) {
        stop();
        return;
    }
    char buffer[16384];
    for (int stream = 0; stream < 2; ++stream) {
        for (int i = 0; i < 16; ++i) {
            const int count = ssh_channel_read_nonblocking(m_channel, buffer, sizeof(buffer), stream);
            if (count > 0)
                receive(QByteArray(buffer, count));
            else if (count == SSH_ERROR) {
                fail(tr("SSH 接收失败：%1").arg(QString::fromUtf8(ssh_get_error(m_session))));
                return;
            } else
                break;
            if (m_stopped)
                return;
        }
    }
    if (ssh_channel_is_eof(m_channel)) {
        stop();
        return;
    }
    if (!ssh_is_connected(m_session)) {
        fail(tr("SSH 连接已中断"));
        return;
    }
    if (!m_pendingInput.isEmpty()) {
        const int count = ssh_channel_write(m_channel, m_pendingInput.constData(),
                                            uint32_t(qMin<qint64>(m_pendingInput.size(), 16384)));
        if (count > 0)
            m_pendingInput.remove(0, count);
        else if (count != SSH_AGAIN && count != 0)
            fail(tr("SSH 输入发送失败：%1").arg(QString::fromUtf8(ssh_get_error(m_session))));
    }
}

void CaptureWorker::setLogOptions(int intervalSeconds, bool timestamps)
{
    m_options.intervalSeconds = intervalSeconds;
    m_options.timestamps = timestamps;
    if (m_writer && !m_stopped)
        m_writer->setOptions(intervalSeconds, timestamps);
}

void CaptureWorker::fail(const QString &message)
{
    if (m_stopped)
        return;
    emit errorOccurred(message);
    stop();
}

void CaptureWorker::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_serial && m_serial->isOpen()) {
        receive(m_serial->readAll());
        m_serial->close();
    }
    if (m_channel) {
        if (m_writer) {
            char buffer[16384];
            for (int stream = 0; stream < 2; ++stream) {
                for (int i = 0; i < 64; ++i) {
                    const int count = ssh_channel_read_nonblocking(m_channel, buffer,
                                                                   sizeof(buffer), stream);
                    if (count <= 0)
                        break;
                    receive(QByteArray(buffer, count));
                }
            }
        }
        ssh_channel_send_eof(m_channel);
        ssh_channel_close(m_channel);
        ssh_channel_free(m_channel);
        m_channel = nullptr;
    }
    if (m_session) {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }
    if (m_writer && !m_writer->finish())
        emit errorOccurred(m_writer->errorString());
    m_options.password.clear();
    emit finished();
}

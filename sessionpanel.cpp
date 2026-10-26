#include "sessionpanel.h"
#include "terminalview.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>
#include <utility>

SessionPanel::SessionPanel(SourceKind kind, QWidget *parent) : QWidget(parent), m_kind(kind)
{
    auto *layout = new QVBoxLayout(this);
    m_sourceGroup = new QGroupBox(kind == SourceKind::Serial ? tr("串口连接") : tr("SSH 连接"), this);
    auto *source = new QGridLayout(m_sourceGroup);
    if (kind == SourceKind::Serial) {
        m_serialPort = new QComboBox;
        m_serialPort->setObjectName("serialPort");
        m_serialPort->setEditable(false);
        m_serialPort->setMinimumWidth(170);
        m_serialPort->setMinimumContentsLength(26);
        m_serialPort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        auto *refresh = new QPushButton(tr("刷新"));
        m_baud = new QComboBox;
        m_baud->setEditable(true);
        m_baud->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"});
        m_baud->setCurrentText("115200");
        m_baud->setValidator(new QIntValidator(1, 4000000, m_baud));
        m_dataBits = new QComboBox;
        for (int bits = 5; bits <= 8; ++bits)
            m_dataBits->addItem(QString::number(bits), bits);
        m_dataBits->setCurrentIndex(3);
        m_parity = new QComboBox;
        m_parity->addItem(tr("无"), QSerialPort::NoParity);
        m_parity->addItem(tr("偶校验"), QSerialPort::EvenParity);
        m_parity->addItem(tr("奇校验"), QSerialPort::OddParity);
        m_stopBits = new QComboBox;
        m_stopBits->addItem("1", QSerialPort::OneStop);
        m_stopBits->addItem("2", QSerialPort::TwoStop);
        m_flow = new QComboBox;
        m_flow->addItem(tr("无"), QSerialPort::NoFlowControl);
        m_flow->addItem("RTS/CTS", QSerialPort::HardwareControl);
        m_flow->addItem("XON/XOFF", QSerialPort::SoftwareControl);
        source->addWidget(new QLabel(tr("串口")), 0, 0);
        source->addWidget(m_serialPort, 0, 1);
        source->addWidget(refresh, 0, 2);
        source->addWidget(new QLabel(tr("波特率")), 0, 3);
        source->addWidget(m_baud, 0, 4);
        source->addWidget(new QLabel(tr("数据位")), 0, 5);
        source->addWidget(m_dataBits, 0, 6);
        source->addWidget(new QLabel(tr("校验位")), 1, 0);
        source->addWidget(m_parity, 1, 1);
        source->addWidget(new QLabel(tr("停止位")), 1, 3);
        source->addWidget(m_stopBits, 1, 4);
        source->addWidget(new QLabel(tr("流控")), 1, 5);
        source->addWidget(m_flow, 1, 6);
        connect(refresh, &QPushButton::clicked, this, &SessionPanel::refreshPorts);
        refreshPorts();
    } else {
        m_host = new QLineEdit("192.168.1.10");
        m_host->setObjectName("sshHost");
        m_sshPort = new QSpinBox;
        m_sshPort->setRange(1, 65535);
        m_sshPort->setValue(22);
        m_user = new QLineEdit("root");
        m_password = new QLineEdit;
        m_password->setEchoMode(QLineEdit::Password);
        source->addWidget(new QLabel(tr("主机")), 0, 0);
        source->addWidget(m_host, 0, 1);
        source->addWidget(new QLabel(tr("端口")), 0, 2);
        source->addWidget(m_sshPort, 0, 3);
        source->addWidget(new QLabel(tr("用户名")), 1, 0);
        source->addWidget(m_user, 1, 1);
        source->addWidget(new QLabel(tr("密码")), 1, 2);
        source->addWidget(m_password, 1, 3);
    }
    layout->addWidget(m_sourceGroup);

    auto *logGroup = new QGroupBox(tr("日志保存"));
    auto *log = new QGridLayout(logGroup);
    m_directory = new QLineEdit(QDir(QCoreApplication::applicationDirPath()).filePath(
        kind == SourceKind::Serial ? "logs/serial" : "logs/ssh"));
    m_directory->setObjectName("logDirectory");
    m_browse = new QPushButton(tr("选择目录…"));
    m_interval = new QSpinBox;
    m_interval->setObjectName("rotationInterval");
    m_interval->setRange(1, 86400);
    m_interval->setValue(10);
    m_interval->setToolTip(tr("修改后从当前时刻重新计时，持续接收全部数据。"));
    m_unit = new QComboBox;
    m_unit->addItem(tr("秒"), 1);
    m_unit->addItem(tr("分钟"), 60);
    m_unit->addItem(tr("小时"), 3600);
    m_unit->setCurrentIndex(1);
    m_timestamps = new QCheckBox(tr("添加时间戳"));
    m_timestamps->setObjectName("timestamps");
    m_timestamps->setToolTip(tr("使用电脑本地时间，精确到毫秒。修改后从下一行生效。"));
    m_encoding = new QComboBox;
    m_encoding->addItem("UTF-8", QByteArray("UTF-8"));
    m_encoding->addItem("GB18030 / GBK", QByteArray("GB18030"));
    log->addWidget(new QLabel(tr("保存目录")), 0, 0);
    log->addWidget(m_directory, 0, 1, 1, 6);
    log->addWidget(m_browse, 0, 7);
    log->addWidget(new QLabel(tr("分文件间隔")), 1, 0);
    log->addWidget(m_interval, 1, 1);
    log->addWidget(m_unit, 1, 2);
    log->addWidget(m_timestamps, 1, 3);
    log->setColumnStretch(4, 1);
    log->addWidget(new QLabel(tr("设备文本编码")), 1, 5);
    log->addWidget(m_encoding, 1, 6, 1, 2);
    layout->addWidget(logGroup);

    auto *actions = new QHBoxLayout;
    m_start = new QPushButton(tr("连接并记录"));
    m_start->setObjectName("startCapture");
    m_stop = new QPushButton(tr("停止"));
    m_stop->setObjectName("stopCapture");
    m_stop->setEnabled(false);
    m_status = new QLabel(tr("未连接"));
    m_status->setObjectName("captureStatus");
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_received = new QLabel(tr("已接收：0 字节"));
    m_received->setObjectName("receivedBytes");
    actions->addWidget(m_start);
    actions->addWidget(m_stop);
    actions->addSpacing(12);
    actions->addWidget(m_status, 1);
    actions->addWidget(m_received);
    layout->addLayout(actions);
    m_file = new QLabel(tr("当前文件：—"));
    m_file->setObjectName("currentFile");
    m_file->setWordWrap(true);
    m_file->setTextFormat(Qt::PlainText);
    m_file->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_file);
    m_terminal = new TerminalView;
    auto *displayActions = new QHBoxLayout;
    m_pause = new QPushButton(tr("暂停显示"));
    m_pause->setObjectName("pauseDisplay");
    m_pause->setCheckable(true);
    m_pause->setToolTip(tr("仅冻结界面，接收和日志保存继续；恢复时显示缓存中的最新数据。"));
    auto *clearDisplay = new QPushButton(tr("清空显示"));
    clearDisplay->setObjectName("clearDisplay");
    clearDisplay->setToolTip(tr("只清空界面，不清空日志文件或接收计数。"));
    auto *autoScroll = new QCheckBox(tr("自动滚动"));
    autoScroll->setObjectName("autoScroll");
    autoScroll->setChecked(true);
    displayActions->addWidget(m_pause);
    displayActions->addWidget(clearDisplay);
    displayActions->addWidget(autoScroll);
    displayActions->addStretch();
    layout->addLayout(displayActions);
    layout->addWidget(m_terminal, 1);
    auto *hint = new QLabel(kind == SourceKind::Serial
        ? tr("持续接收串口文本；分文件期间不中断接收。文件保存为 UTF-8。")
        : tr("点击终端直接输入；Ctrl+C 中断（有选中文字时复制），Ctrl+V 粘贴。文件保存终端文本，无方向标签。"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(m_pause, &QPushButton::toggled, this, [this](bool paused) {
        m_pause->setText(paused ? tr("继续显示") : tr("暂停显示"));
        m_terminal->setDisplayPaused(paused);
    });
    connect(clearDisplay, &QPushButton::clicked, m_terminal, &TerminalView::clearDisplay);
    connect(autoScroll, &QCheckBox::toggled, m_terminal, &TerminalView::setAutoScroll);

    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString directory = QFileDialog::getExistingDirectory(this, tr("选择日志目录"),
                                                                     m_directory->text());
        if (!directory.isEmpty())
            m_directory->setText(directory);
    });
    connect(m_start, &QPushButton::clicked, this, &SessionPanel::start);
    connect(m_stop, &QPushButton::clicked, this, &SessionPanel::requestStop);
    connect(m_interval, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SessionPanel::updateLogOptions);
    connect(m_unit, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SessionPanel::updateLogOptions);
    connect(m_timestamps, &QCheckBox::toggled, this, &SessionPanel::updateLogOptions);
    connect(m_terminal, &TerminalView::inputData, this, [this](const QByteArray &data) {
        if (m_worker && !m_stopping)
            QMetaObject::invokeMethod(m_worker, "sendInput", Qt::QueuedConnection,
                                      Q_ARG(QByteArray, data));
    });
}

void SessionPanel::loadSettings(QSettings &settings)
{
    settings.beginGroup(m_kind == SourceKind::Serial ? "serial" : "ssh");
    const auto restoreChoice = [&settings](QComboBox *combo, const char *key) {
        const int index = combo->findData(settings.value(key, combo->currentData()).toInt());
        if (index >= 0)
            combo->setCurrentIndex(index);
    };
    if (m_kind == SourceKind::Ssh) {
        m_host->setText(settings.value("host", m_host->text()).toString());
        m_sshPort->setValue(settings.value("port", m_sshPort->value()).toInt());
        m_user->setText(settings.value("username", m_user->text()).toString());
    }
    m_directory->setText(settings.value("directory", m_directory->text()).toString());
    m_interval->setValue(settings.value("interval", m_interval->value()).toInt());
    restoreChoice(m_unit, "intervalUnitSeconds");
    m_timestamps->setChecked(settings.value("timestamps", m_timestamps->isChecked()).toBool());
    const int encoding = m_encoding->findData(
        settings.value("encoding", m_encoding->currentData()).toByteArray());
    if (encoding >= 0)
        m_encoding->setCurrentIndex(encoding);
    settings.endGroup();
}

void SessionPanel::saveSettings(QSettings &settings) const
{
    settings.beginGroup(m_kind == SourceKind::Serial ? "serial" : "ssh");
    if (m_kind == SourceKind::Serial) {
        // 清理旧版留下的连接参数，日志选项仍继续保存。
        for (const char *key : {"port", "baudRate", "dataBits", "parity", "stopBits", "flowControl"})
            settings.remove(key);
    } else {
        settings.setValue("host", m_host->text().trimmed());
        settings.setValue("port", m_sshPort->value());
        settings.setValue("username", m_user->text().trimmed());
    }
    settings.setValue("directory", m_directory->text().trimmed());
    settings.setValue("interval", m_interval->value());
    settings.setValue("intervalUnitSeconds", m_unit->currentData());
    settings.setValue("timestamps", m_timestamps->isChecked());
    settings.setValue("encoding", QString::fromLatin1(m_encoding->currentData().toByteArray()));
    settings.endGroup();
}

QString SessionPanel::serialPortName() const
{
    // 显示文字包含设备描述，连接和配置只读取列表项中的原始端口名。
    return m_serialPort->currentData().toString();
}

void SessionPanel::refreshPorts()
{
    const QString previous = serialPortName();
    m_serialPort->clear();
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts()) {
        QString label = port.portName();
        if (!port.description().isEmpty())
            label += QStringLiteral("(%1)").arg(port.description());
        m_serialPort->addItem(label, port.portName());
        QString details = tr("端口：%1\n设备：%2\n制造商：%3\n序列号：%4")
            .arg(port.systemLocation(), port.description(), port.manufacturer(), port.serialNumber());
        if (port.hasVendorIdentifier())
            details += QStringLiteral("\nVID: %1").arg(port.vendorIdentifier(), 4, 16, QLatin1Char('0'));
        if (port.hasProductIdentifier())
            details += QStringLiteral("\nPID: %1").arg(port.productIdentifier(), 4, 16, QLatin1Char('0'));
        m_serialPort->setItemData(m_serialPort->count() - 1, details, Qt::ToolTipRole);
    }
    if (!previous.isEmpty())
        m_serialPort->setCurrentIndex(m_serialPort->findData(previous));
}

int SessionPanel::intervalSeconds() const
{
    return m_interval->value() * m_unit->currentData().toInt();
}

void SessionPanel::updateLogOptions()
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setLogOptions", Qt::QueuedConnection,
            Q_ARG(int, intervalSeconds()), Q_ARG(bool, m_timestamps->isChecked()));
}

void SessionPanel::start()
{
    CaptureOptions options;
    options.kind = m_kind;
    options.directory = m_directory->text().trimmed();
    options.intervalSeconds = intervalSeconds();
    options.timestamps = m_timestamps->isChecked();
    options.encoding = m_encoding->currentData().toByteArray();
    if (options.directory.isEmpty()) {
        m_status->setText(tr("请选择日志保存目录"));
        return;
    }
    if (m_kind == SourceKind::Serial) {
        options.serialPort = serialPortName();
        options.baudRate = m_baud->currentText().toInt();
        options.dataBits = m_dataBits->currentData().toInt();
        options.parity = m_parity->currentData().toInt();
        options.stopBits = m_stopBits->currentData().toInt();
        options.flowControl = m_flow->currentData().toInt();
        if (options.serialPort.isEmpty() || options.baudRate <= 0) {
            m_status->setText(tr("请填写有效的串口和波特率"));
            return;
        }
    } else {
        options.host = m_host->text().trimmed();
        options.sshPort = m_sshPort->value();
        options.username = m_user->text().trimmed();
        options.password = m_password->text();
        options.knownHostsPath = QDir(QCoreApplication::applicationDirPath()).filePath("known_hosts");
        if (options.host.isEmpty() || options.username.isEmpty()) {
            m_status->setText(tr("请填写 SSH 主机和用户名"));
            return;
        }
        m_password->clear();
    }
    emit settingsSaveRequested();
    m_failed = false;
    m_stopping = false;
    m_terminal->clearDisplay();
    m_pause->setChecked(false);
    m_status->setStyleSheet(QString());
    m_status->setText(tr("正在连接…"));
    m_received->setText(tr("已接收：0 字节"));
    m_file->setText(tr("当前文件：—"));
    m_sourceGroup->setEnabled(false);
    m_directory->setEnabled(false);
    m_browse->setEnabled(false);
    m_encoding->setEnabled(false);
    m_start->setEnabled(false);
    m_stop->setEnabled(true);
    m_thread = new QThread(this);
    m_worker = new CaptureWorker(std::move(options));
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &CaptureWorker::start);
    connect(m_worker, &CaptureWorker::statusChanged, this, [this](const QString &message) {
        if (!m_stopping)
            m_status->setText(message);
    });
    connect(m_worker, &CaptureWorker::fileChanged, this, [this](const QString &path) {
        m_file->setText(tr("当前文件：%1").arg(QDir::toNativeSeparators(path)));
    });
    connect(m_worker, &CaptureWorker::terminalUpdated, m_terminal, &TerminalView::updateTerminal);
    connect(m_terminal, &TerminalView::displayConsumed, m_worker, &CaptureWorker::acknowledgeDisplay);
    connect(m_worker, &CaptureWorker::receivedBytesChanged, this, [this](qint64 bytes) {
        m_received->setText(tr("已接收：%1 字节").arg(bytes));
    });
    connect(m_worker, &CaptureWorker::connected, this, [this] {
        if (m_kind == SourceKind::Ssh && !m_stopping) {
            m_terminal->setInputEnabled(true);
            m_terminal->setFocus();
        }
    });
    connect(m_worker, &CaptureWorker::errorOccurred, this, [this](const QString &message) {
        m_failed = true;
        m_status->setStyleSheet("color: #b42318;");
        m_status->setText(message);
    });
    connect(m_worker, &CaptureWorker::hostKeyApprovalRequested, this,
            [this](const QString &host, const QString &fingerprint) {
        auto *box = new QMessageBox(QMessageBox::Question, tr("确认 SSH 主机"),
            tr("首次连接 %1，请核对设备指纹：\n\n%2\n\n是否信任并保存此主机密钥？")
                .arg(host, fingerprint), QMessageBox::Yes | QMessageBox::No, this);
        box->setTextFormat(Qt::PlainText);
        box->setDefaultButton(QMessageBox::No);
        box->setAttribute(Qt::WA_DeleteOnClose);
        const QPointer<CaptureWorker> pending = m_worker;
        connect(box, &QMessageBox::finished, this, [pending](int result) {
            if (pending)
                QMetaObject::invokeMethod(pending, "approveHostKey", Qt::QueuedConnection,
                                          Q_ARG(bool, result == QMessageBox::Yes));
        });
        connect(m_thread, &QThread::finished, box, &QMessageBox::close);
        box->open();
    });
    connect(m_worker, &CaptureWorker::finished, m_thread, &QThread::quit, Qt::DirectConnection);
    connect(m_worker, &CaptureWorker::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, this, [this] {
        m_thread = nullptr;
        m_worker = nullptr;
        m_terminal->setInputEnabled(false);
        m_sourceGroup->setEnabled(true);
        m_directory->setEnabled(true);
        m_browse->setEnabled(true);
        m_encoding->setEnabled(true);
        m_start->setEnabled(true);
        m_stop->setEnabled(false);
        if (!m_failed)
            m_status->setText(tr("已停止"));
        emit stopped();
    });
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}

void SessionPanel::requestStop()
{
    if (!m_thread || m_stopping)
        return;
    m_stopping = true;
    m_terminal->setInputEnabled(false);
    m_stop->setEnabled(false);
    if (!m_failed)
        m_status->setText(tr("正在停止…"));
    m_thread->requestInterruption();
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
}

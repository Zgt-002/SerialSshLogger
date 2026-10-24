#pragma once

#include "terminalstream.h"
#include "terminalbuffer.h"

#include <QObject>
#include <libssh/libssh.h>
#include <memory>

class LogWriter;
class QSerialPort;
class QTimer;

enum class SourceKind { Serial, Ssh };

struct CaptureOptions
{
    SourceKind kind = SourceKind::Serial;
    QString directory;
    int intervalSeconds = 600;
    bool timestamps = false;
    QByteArray encoding = "UTF-8";
    QString serialPort;
    int baudRate = 115200;
    int dataBits = 8;
    int parity = 0;
    int stopBits = 1;
    int flowControl = 0;
    QString host;
    int sshPort = 22;
    QString username;
    QString password;
    QString knownHostsPath;
};

class CaptureWorker : public QObject
{
    Q_OBJECT
public:
    explicit CaptureWorker(CaptureOptions options);

public slots:
    void start();
    void stop();
    void sendInput(const QByteArray &data);
    void approveHostKey(bool approved);
    void setLogOptions(int intervalSeconds, bool timestamps);
    void acknowledgeDisplay();

signals:
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);
    void fileChanged(const QString &path);
    void terminalUpdated(const QString &completed, const QString &currentLine, int cursor, bool reset);
    void receivedBytesChanged(qint64 bytes);
    void hostKeyApprovalRequested(const QString &host, const QString &fingerprint);
    void connected();
    void finished();

private:
    void startSerial();
    void startSsh();
    void authenticate();
    bool beginCapture(const QString &prefix);
    void pollSsh();
    void receive(const QByteArray &data);
    void flushDisplay(bool force = false);
    void fail(const QString &message);
    bool interrupted() const;

    CaptureOptions m_options;
    std::unique_ptr<TerminalStream> m_terminal;
    LogWriter *m_writer = nullptr;
    QSerialPort *m_serial = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_displayTimer = nullptr;
    TerminalBuffer m_display;
    bool m_displayInFlight = false;
    qint64 m_displayedBytes = 0;
    ssh_session m_session = nullptr;
    ssh_channel m_channel = nullptr;
    QByteArray m_pendingInput;
    qint64 m_receivedBytes = 0;
    bool m_waitingForHostKey = false;
    bool m_stopped = false;
};

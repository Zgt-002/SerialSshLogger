#pragma once

#include "captureworker.h"

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QThread;
class TerminalView;

class SessionPanel : public QWidget
{
    Q_OBJECT
public:
    explicit SessionPanel(SourceKind kind, QWidget *parent = nullptr);
    bool isRunning() const { return m_thread != nullptr; }
    void requestStop();

signals:
    void stopped();

private:
    void refreshPorts();
    void start();
    void updateLogOptions();
    int intervalSeconds() const;

    SourceKind m_kind;
    QGroupBox *m_sourceGroup;
    QComboBox *m_serialPort = nullptr;
    QComboBox *m_baud = nullptr;
    QComboBox *m_dataBits = nullptr;
    QComboBox *m_parity = nullptr;
    QComboBox *m_stopBits = nullptr;
    QComboBox *m_flow = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_sshPort = nullptr;
    QLineEdit *m_user = nullptr;
    QLineEdit *m_password = nullptr;
    QLineEdit *m_directory;
    QPushButton *m_browse;
    QSpinBox *m_interval;
    QComboBox *m_unit;
    QComboBox *m_encoding;
    QCheckBox *m_timestamps;
    QPushButton *m_start;
    QPushButton *m_stop;
    QLabel *m_status;
    QLabel *m_file;
    TerminalView *m_terminal;
    QThread *m_thread = nullptr;
    QPointer<CaptureWorker> m_worker;
    bool m_failed = false;
    bool m_stopping = false;
};

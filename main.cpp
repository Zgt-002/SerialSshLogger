#include "sessionpanel.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMainWindow>
#include <QMessageBox>
#include <QTabWidget>
#include <QTimer>

class MainWindow : public QMainWindow
{
public:
    MainWindow()
    {
        setWindowTitle(QStringLiteral("串口与 SSH 日志记录工具"));
        resize(1120, 800);
        setMinimumSize(820, 580);
        auto *tabs = new QTabWidget(this);
        m_serial = new SessionPanel(SourceKind::Serial, tabs);
        m_ssh = new SessionPanel(SourceKind::Ssh, tabs);
        tabs->addTab(m_serial, QStringLiteral("串口"));
        tabs->addTab(m_ssh, "SSH");
        setCentralWidget(tabs);
        for (SessionPanel *panel : {m_serial, m_ssh})
            connect(panel, &SessionPanel::stopped, this, [this] {
                if (m_closing && !m_serial->isRunning() && !m_ssh->isRunning())
                    QTimer::singleShot(0, this, &QWidget::close);
            });
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (!m_serial->isRunning() && !m_ssh->isRunning()) {
            event->accept();
            return;
        }
        m_closing = true;
        m_serial->requestStop();
        m_ssh->requestStop();
        event->ignore();
    }

private:
    SessionPanel *m_serial;
    SessionPanel *m_ssh;
    bool m_closing = false;
};

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("SerialSshLogger");
    QCoreApplication::setApplicationVersion("1.0.0");
    if (ssh_init() != SSH_OK) {
        QMessageBox::critical(nullptr, QStringLiteral("启动失败"),
                              QStringLiteral("SSH library initialization failed."));
        return 1;
    }
    MainWindow window;
    window.show();
    const int result = app.exec();
    ssh_finalize();
    return result;
}

#include "sessionpanel.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QMainWindow>
#include <QMessageBox>
#include <QSettings>
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
        QSettings settings(QDir(QCoreApplication::applicationDirPath()).filePath("config.ini"),
                           QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        settings.setIniCodec("UTF-8");
#endif
        m_serial->loadSettings(settings);
        m_ssh->loadSettings(settings);
        resize(settings.value("window/size", size()).toSize());
        for (SessionPanel *panel : {m_serial, m_ssh}) {
            connect(panel, &SessionPanel::settingsSaveRequested, this, &MainWindow::saveSettings);
            connect(panel, &SessionPanel::stopped, this, [this] {
                if (m_closing && !m_serial->isRunning() && !m_ssh->isRunning())
                    QTimer::singleShot(0, this, &QWidget::close);
            });
        }
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (!m_serial->isRunning() && !m_ssh->isRunning()) {
            saveSettings();
            event->accept();
            return;
        }
        m_closing = true;
        m_serial->requestStop();
        m_ssh->requestStop();
        event->ignore();
    }

private:
    void saveSettings()
    {
        QSettings settings(QDir(QCoreApplication::applicationDirPath()).filePath("config.ini"),
                           QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        settings.setIniCodec("UTF-8");
#endif
        m_serial->saveSettings(settings);
        m_ssh->saveSettings(settings);
        settings.setValue("window/size", isMaximized() ? normalGeometry().size() : size());
        settings.sync();
        if (settings.status() != QSettings::NoError)
            QMessageBox::warning(this, QStringLiteral("参数保存失败"),
                QStringLiteral("无法保存配置文件：\n%1\n\n请检查文件格式和目录写入权限。")
                    .arg(QDir::toNativeSeparators(settings.fileName())));
    }

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

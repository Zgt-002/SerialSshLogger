#include "captureworker.h"
#include "logwriter.h"
#include "sessionpanel.h"
#include "terminalstream.h"
#include "terminalview.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QtTest>

#ifdef Q_OS_MACOS
#include <util.h>
#elif defined(Q_OS_UNIX)
#include <pty.h>
#endif
#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

class LoggerTests : public QObject
{
    Q_OBJECT
private:
    QString m_root;
    QProcess m_server;
    const QDateTime m_time = QDateTime::fromString("2026-10-23T14:30:01.125", Qt::ISODateWithMs);

    QString directory(const QString &name)
    {
        QTemporaryDir temporary(m_root + '/' + name + "-XXXXXX");
        temporary.setAutoRemove(false);
        return temporary.path();
    }

    QByteArray read(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray();
        return file.readAll();
    }

    int startServer()
    {
        m_server.start(qEnvironmentVariable("LOGGER_PYTHON"),
                        {qEnvironmentVariable("LOGGER_SSH_FIXTURE")});
        if (!m_server.waitForStarted(5000) || !m_server.waitForReadyRead(15000))
            return 0;
        return QString::fromUtf8(m_server.readLine()).trimmed().section(' ', 1).toInt();
    }

    CaptureOptions sshOptions(int port, const QString &path)
    {
        CaptureOptions options;
        options.kind = SourceKind::Ssh;
        options.host = "127.0.0.1";
        options.sshPort = port;
        options.username = "logger";
        options.password = "fixture-password";
        options.directory = path;
        options.knownHostsPath = path + "/known_hosts";
        return options;
    }

private slots:
    void initTestCase()
    {
        m_root = qEnvironmentVariable("LOGGER_TEST_ROOT");
        QVERIFY2(!m_root.isEmpty(), "Set LOGGER_TEST_ROOT to a directory inside .codex-tmp.");
        QVERIFY(QDir().mkpath(m_root));
        QCOMPARE(ssh_init(), SSH_OK);
    }

    void cleanup()
    {
        if (m_server.state() != QProcess::NotRunning) {
            m_server.terminate();
            m_server.waitForFinished(5000);
        }
    }

    void utf8SplitAcrossEveryByte()
    {
        TerminalStream stream;
        const QByteArray input = QStringLiteral("设备# ./程序\r\n启动成功\r\n").toUtf8();
        QString output;
        for (char byte : input) {
            for (const auto &line : stream.feed(QByteArray(1, byte), m_time)) {
                if (line.complete) {
                    output += line.text + '\n';
                    QCOMPARE(line.receivedAt, m_time);
                }
            }
        }
        QCOMPARE(output, QStringLiteral("设备# ./程序\n启动成功\n"));
    }

    void gb18030SplitAcrossEveryByte()
    {
        TerminalStream stream("GB18030");
        const QByteArray input = QTextCodec::codecForName("GB18030")
                                     ->fromUnicode(QStringLiteral("设备运行正常\r\n"));
        QString output;
        for (char byte : input)
            for (const auto &line : stream.feed(QByteArray(1, byte), m_time))
                if (line.complete)
                    output += line.text;
        QCOMPARE(output, QStringLiteral("设备运行正常"));
    }

    void fragmentedAnsiAndCommandEditing()
    {
        TerminalStream stream;
        const QByteArray input = "\x1b]0;title\x1b\\\x1b[32mdevice# \x1b[0m./xxz"
                                 "\x1b[D\x1b[Kx\r\n\x1b[?2004h";
        QString output;
        for (char byte : input)
            for (const auto &line : stream.feed(QByteArray(1, byte), m_time))
                if (line.complete)
                    output += line.text + '\n';
        QCOMPARE(output, QString("device# ./xxx\n"));
    }

    void carriageReturnAndBackspace()
    {
        TerminalStream stream;
        const auto lines = stream.feed("progress: 10%\rprogress: 20%\r\nabc\b \bZ\r\n", m_time);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].text, QString("progress: 20%"));
        QCOMPARE(lines[1].text, QString("abZ"));
    }

    void promptAndEchoSavedOnce()
    {
        LogWriter writer;
        QVERIFY(writer.start(directory("prompt"), "ssh_test", 600, false));
        TerminalStream stream;
        for (const auto &line : stream.feed("device# ./xxz", m_time))
            QVERIFY(writer.append(line));
        QTest::qWait(250);
        QCOMPARE(read(writer.fileName()), QByteArray("device# ./xxz"));
        for (const auto &line : stream.feed("\b \bx\r\nstarted\r\ndevice# ", m_time))
            QVERIFY(writer.append(line));
        QVERIFY(writer.finish());
        QCOMPARE(read(writer.fileName()), QByteArray("device# ./xxx\nstarted\ndevice# "));
    }

    void timestamps_data()
    {
        QTest::addColumn<bool>("enabled");
        QTest::newRow("off") << false;
        QTest::newRow("on") << true;
    }

    void timestamps()
    {
        QFETCH(bool, enabled);
        LogWriter writer;
        QVERIFY(writer.start(directory("timestamps"), "serial_test", 600, enabled));
        QVERIFY(writer.append({"first", m_time, false}));
        QVERIFY(writer.append({"first line", m_time, true}));
        QVERIFY(writer.append({"", m_time, true}));
        QVERIFY(writer.finish());
        const QByteArray prefix = enabled ? "[2026-10-23 14:30:01.125] " : "";
        QCOMPARE(read(writer.fileName()), prefix + "first line\n" + prefix + "\n");
    }

    void rotationPreservesSplitPromptAndUnicode()
    {
        LogWriter writer;
        QSignalSpy files(&writer, &LogWriter::fileChanged);
        QVERIFY(writer.start(directory("rotation"), "ssh_test", 1, false));
        TerminalStream stream;
        for (const auto &line : stream.feed("device# ./", m_time))
            QVERIFY(writer.append(line));
        const QByteArray chinese = QStringLiteral("程序\r\n完成\r\n").toUtf8();
        for (const auto &line : stream.feed(chinese.left(1), m_time))
            QVERIFY(writer.append(line));
        QTRY_VERIFY_WITH_TIMEOUT(files.size() >= 2, 2000);
        for (const auto &line : stream.feed(chinese.mid(1), m_time))
            QVERIFY(writer.append(line));
        QVERIFY(writer.finish());
        QByteArray joined;
        for (const auto &file : files)
            joined += read(file[0].toString());
        QCOMPARE(joined, QStringLiteral("device# ./程序\n完成\n").toUtf8());
    }

    void optionsChangeDuringRecording()
    {
        LogWriter writer;
        QSignalSpy files(&writer, &LogWriter::fileChanged);
        QVERIFY(writer.start(directory("options"), "serial_test", 600, false));
        QVERIFY(writer.append({"first", m_time, false}));
        writer.setOptions(1, true);
        QVERIFY(writer.append({"first line", m_time, true}));
        QVERIFY(writer.append({"second", m_time, true}));
        QTRY_VERIFY_WITH_TIMEOUT(files.size() >= 2, 2000);
        QVERIFY(writer.finish());
        QCOMPARE(read(files[0][0].toString()),
                 QByteArray("first line\n[2026-10-23 14:30:01.125] second\n"));
    }

    void invalidDirectoryDoesNotOverwriteFile()
    {
        const QString path = directory("failure") + "/existing.txt";
        QFile existing(path);
        QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        existing.write("preserve");
        existing.close();
        LogWriter writer;
        QSignalSpy errors(&writer, &LogWriter::failed);
        QVERIFY(!writer.start(path, "serial", 600, false));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(read(path), QByteArray("preserve"));
    }

    void terminalKeyboardHasNoLocalEcho()
    {
        TerminalView view;
        view.updateTerminal(QString(), "device# ");
        view.setInputEnabled(true);
        QSignalSpy input(&view, &TerminalView::inputData);
        QTest::keyClicks(&view, "ls");
        QTest::keyClick(&view, Qt::Key_Return);
        QTest::keyClick(&view, Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(&view, Qt::Key_Up);
        QTest::keyClick(&view, Qt::Key_Backspace);
        QByteArray sent;
        for (const auto &item : input)
            sent += item[0].toByteArray();
        QCOMPARE(sent, QByteArray("ls\r\x03\x1b[A\x7f"));
        QCOMPARE(view.toPlainText(), QString("device# "));
        view.updateTerminal("device# ls\nfile.txt\n", "device# ");
        QCOMPARE(view.toPlainText(), QString("device# ls\nfile.txt\ndevice# "));
    }

    void terminalDisplayIsBounded()
    {
        TerminalView view;
        view.updateTerminal(QString("line\n").repeated(11000), "prompt# ");
        QCOMPARE(view.document()->blockCount(), 10000);
        QVERIFY(view.toPlainText().endsWith("prompt# "));
    }

    void panelScreenshots()
    {
        QTabWidget tabs;
        tabs.addTab(new SessionPanel(SourceKind::Serial), QStringLiteral("串口"));
        tabs.addTab(new SessionPanel(SourceKind::Ssh), "SSH");
        tabs.resize(1120, 800);
        tabs.show();
        QTest::qWait(100);
        QVERIFY(tabs.grab().save(m_root + "/serial-panel.png"));
        tabs.setCurrentIndex(1);
        QTest::qWait(100);
        QVERIFY(tabs.grab().save(m_root + "/ssh-panel.png"));
    }

    void terminalEditingTracksUnicodeColumns()
    {
        TerminalStream stream;
        stream.feed(QStringLiteral("中ab").toUtf8(), m_time);
        QCOMPARE(stream.cursorPosition(), 3);
        auto lines = stream.feed("\x1b[2D", m_time);
        QCOMPARE(lines.last().text, QStringLiteral("中ab"));
        QCOMPARE(stream.cursorPosition(), 1);
        lines = stream.feed("\x1b[P\x1b[@Z", m_time);
        QCOMPARE(lines.last().text, QStringLiteral("中Zb"));
        QCOMPARE(stream.cursorPosition(), 2);
        lines = stream.feed("\r\x1b[2P", m_time);
        QCOMPARE(lines.last().text, QStringLiteral("Zb"));

        TerminalStream combining;
        combining.feed(QStringLiteral("e\u0301x").toUtf8(), m_time);
        combining.feed("\x1b[D", m_time);
        QCOMPARE(combining.cursorPosition(), 2);
        lines = combining.feed("\x1b[P", m_time);
        QCOMPARE(lines.last().text, QStringLiteral("e\u0301"));

        TerminalStream supplementary;
        supplementary.feed(QStringLiteral("\U00020000x").toUtf8(), m_time);
        supplementary.feed("\x1b[D", m_time);
        QCOMPARE(supplementary.cursorPosition(), 2);
        lines = supplementary.feed("\x1b[2D\x1b[2P", m_time);
        QCOMPARE(lines.last().text, QStringLiteral("x"));
    }

    void eraseInsertAndSavedCursor()
    {
        TerminalStream stream;
        auto lines = stream.feed("abc\x1b[2D\x1b[X", m_time);
        QCOMPARE(lines.last().text, QString("a c"));
        QCOMPARE(stream.cursorPosition(), 1);
        lines = stream.feed("\x1b[2@xy", m_time);
        QCOMPARE(lines.last().text, QString("axy c"));
        stream.feed("\x1b[s\r", m_time);
        QCOMPARE(stream.cursorPosition(), 0);
        stream.feed("\x1b[u", m_time);
        QCOMPARE(stream.cursorPosition(), 3);
        lines = stream.feed("\x1b[2KZ", m_time);
        QCOMPARE(lines.last().text, QString("   Z"));
        lines = stream.feed("\x1b[2K\x1b[3C", m_time);
        QCOMPARE(lines.last().text, QString());
        QCOMPARE(stream.cursorPosition(), 7);
    }

    void longLinesKeepExactFileContents()
    {
        const QString input = QString(TerminalLimits::LineColumns * 3 + 7, QLatin1Char('a'))
            + QStringLiteral("中文\n") + QStringLiteral("e")
            + QString(TerminalLimits::LineUnits + 7, QChar(0x0301)) + QLatin1Char('\n');
        const QByteArray bytes = input.toUtf8();
        LogWriter writer;
        QVERIFY(writer.start(directory("long-lines"), "serial", 600, true));
        TerminalStream stream;
        for (int offset = 0; offset < bytes.size(); offset += 4096) {
            for (const auto &line : stream.feed(bytes.mid(offset, 4096), m_time)) {
                QVERIFY(line.text.size() <= TerminalLimits::LineUnits);
                QVERIFY(writer.append(line));
            }
        }
        QVERIFY(writer.finish());
        QString expected = input;
        expected.replace("\ne", "\n[2026-10-23 14:30:01.125] e");
        expected.prepend("[2026-10-23 14:30:01.125] ");
        QCOMPARE(read(writer.fileName()), expected.toUtf8());
    }

    void displayPauseClearAndMemoryLimit()
    {
        TerminalView view;
        view.updateTerminal("old\n", "prompt# ");
        view.setDisplayPaused(true);
        view.updateTerminal("prompt# ls\nresult\n", "next# ");
        QCOMPARE(view.toPlainText(), QString("old\nprompt# "));
        view.setDisplayPaused(false);
        QCOMPARE(view.toPlainText(), QString("old\nprompt# ls\nresult\nnext# "));
        view.setDisplayPaused(true);
        view.updateTerminal("discard\n", "pending");
        view.clearDisplay();
        view.updateTerminal("new\n", "tail");
        view.setDisplayPaused(false);
        QCOMPARE(view.toPlainText(), QString("new\ntail"));

        const QString batch = QString(4095, QLatin1Char('x')) + QLatin1Char('\n');
        for (int i = 0; i < 400; ++i)
            view.updateTerminal(batch, QString::number(i));
        QVERIFY(view.document()->characterCount() - 1 <= TerminalLimits::DisplayUnits);
        QVERIFY(view.toPlainText().endsWith("399"));
        view.setDisplayPaused(true);
        for (int i = 0; i < 400; ++i)
            view.updateTerminal(batch, QString::number(i));
        view.setDisplayPaused(false);
        QVERIFY(view.document()->characterCount() - 1 <= TerminalLimits::DisplayUnits);
        QVERIFY(view.toPlainText().endsWith("399"));
    }

    void settingsPreservePortIdentityAndExcludePassword()
    {
        QSettings settings(directory("settings") + "/config.ini", QSettings::IniFormat);
        SessionPanel serial(SourceKind::Serial);
        auto *ports = serial.findChild<QComboBox *>("serialPort");
        ports->clear();
        ports->addItem(QStringLiteral("COM42(USB-SERIAL CH340)"), "COM42");
        ports->addItem(QStringLiteral("COM99(USB Serial Port)"), "COM99");
        QVERIFY(!ports->isEditable());
        QTest::keyClick(ports, Qt::Key_Backspace);
        QTest::keyClick(ports, Qt::Key_Delete);
        QCOMPARE(ports->currentText(), QStringLiteral("COM42(USB-SERIAL CH340)"));
        QCOMPARE(ports->count(), 2);
        serial.findChild<QLineEdit *>("logDirectory")->setText(QStringLiteral("D:/日志"));
        serial.saveSettings(settings);
        QCOMPARE(settings.value("serial/port").toString(), QString("COM42"));
        ports->setCurrentIndex(1);
        serial.saveSettings(settings);
        QCOMPARE(settings.value("serial/port").toString(), QString("COM99"));
        settings.setValue("serial/port", "COM42");
        serial.loadSettings(settings);
        QCOMPARE(ports->currentIndex(), 0);
        QVERIFY(ports->currentText().contains("CH340"));
        settings.setValue("serial/port", "COM404");
        serial.loadSettings(settings);
        QCOMPARE(ports->currentIndex(), -1);

        SessionPanel ssh(SourceKind::Ssh);
        for (QLineEdit *edit : ssh.findChildren<QLineEdit *>())
            if (edit->echoMode() == QLineEdit::Password)
                edit->setText("not-for-disk");
        ssh.saveSettings(settings);
        settings.sync();
        QVERIFY(!settings.contains("ssh/password"));
        QVERIFY(!read(settings.fileName()).contains("not-for-disk"));
    }

    void serialPseudoTerminalCapture()
    {
#ifdef Q_OS_UNIX
        int master = -1;
        int slave = -1;
        char name[256] = {};
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        CaptureOptions options;
        options.kind = SourceKind::Serial;
        options.serialPort = QString::fromLocal8Bit(name);
        options.directory = directory("serial-integration");
        CaptureWorker worker(options);
        QSignalSpy ready(&worker, &CaptureWorker::connected);
        QSignalSpy errors(&worker, &CaptureWorker::errorOccurred);
        QSignalSpy files(&worker, &CaptureWorker::fileChanged);
        worker.start();
        QCOMPARE(ready.count(), 1);
        const QByteArray bytes = QStringLiteral("串口测试\r\nsecond line\r\nlast").toUtf8();
        QCOMPARE(::write(master, bytes.constData(), size_t(bytes.size())), ssize_t(bytes.size()));
        QTRY_VERIFY_WITH_TIMEOUT(read(files[0][0].toString()).contains("second line"), 2000);
        worker.stop();
        QCOMPARE(errors.count(), 0);
        QCOMPARE(read(files[0][0].toString()), QStringLiteral("串口测试\nsecond line\nlast").toUtf8());
        ::close(master);
        ::close(slave);
#else
        QSKIP("Pseudo-terminal integration test requires a Unix host.");
#endif
    }

    void sessionPanelCanStopAndReconnect()
    {
#ifdef Q_OS_UNIX
        int master = -1;
        int slave = -1;
        char name[256] = {};
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        SessionPanel panel(SourceKind::Serial);
        const QString path = directory("panel-reconnect");
        panel.findChild<QLineEdit *>("logDirectory")->setText(path);
        auto *ports = panel.findChild<QComboBox *>("serialPort");
        ports->addItem(QStringLiteral("PTY(Test serial device)"), QString::fromLocal8Bit(name));
        ports->setCurrentIndex(ports->count() - 1);
        auto *start = panel.findChild<QPushButton *>("startCapture");
        auto *stop = panel.findChild<QPushButton *>("stopCapture");
        auto *status = panel.findChild<QLabel *>("captureStatus");
        auto *pause = panel.findChild<QPushButton *>("pauseDisplay");
        auto *clearDisplay = panel.findChild<QPushButton *>("clearDisplay");
        for (int i = 0; i < 2; ++i) {
            QTest::mouseClick(start, Qt::LeftButton);
            QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("已连接，正在记录"), 2000);
            QVERIFY(panel.isRunning());
            QCOMPARE(::write(master, "test\r\n", 6), ssize_t(6));
            auto *view = panel.findChild<TerminalView *>("terminal");
            QTRY_VERIFY_WITH_TIMEOUT(view->toPlainText().contains("test"), 2000);
            QTest::mouseClick(pause, Qt::LeftButton);
            QCOMPARE(::write(master, "paused\r\n", 8), ssize_t(8));
            QTest::qWait(250);
            QVERIFY(!view->toPlainText().contains("paused"));
            QTest::mouseClick(pause, Qt::LeftButton);
            QVERIFY(view->toPlainText().contains("paused"));
            QTest::mouseClick(clearDisplay, Qt::LeftButton);
            QVERIFY(view->toPlainText().isEmpty());
            QTest::mouseClick(stop, Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(!panel.isRunning(), 2000);
            QVERIFY(start->isEnabled());
        }
        const auto logs = QDir(path).entryList({"*.log"}, QDir::Files);
        QCOMPARE(logs.size(), 2);
        for (const auto &file : logs)
            QCOMPARE(read(QDir(path).filePath(file)), QByteArray("test\npaused\n"));
        ::close(master);
        ::close(slave);
#else
        QSKIP("Pseudo-terminal integration test requires a Unix host.");
#endif
    }

    void slowDisplayDoesNotBlockRecording()
    {
#ifdef Q_OS_UNIX
        int master = -1;
        int slave = -1;
        char name[256] = {};
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        CaptureOptions options;
        options.serialPort = QString::fromLocal8Bit(name);
        options.directory = directory("slow-display");
        CaptureWorker worker(options);
        QSignalSpy updates(&worker, &CaptureWorker::terminalUpdated);
        QSignalSpy files(&worker, &CaptureWorker::fileChanged);
        worker.start();
        QCOMPARE(files.size(), 1);
        QCOMPARE(::write(master, "first\n", 6), ssize_t(6));
        QTRY_COMPARE_WITH_TIMEOUT(updates.size(), 1, 2000);
        // 故意不确认首批显示，模拟界面卡住；后续日志仍必须完整写入。
        const QByteArray chunk = QByteArray(4095, 'x') + '\n';
        QVERIFY(fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK) != -1);
        QElapsedTimer deadline;
        deadline.start();
        for (int i = 0; i < 300; ++i) {
            int offset = 0;
            while (offset < chunk.size()) {
                // 测试发送端与接收端共享事件循环，PTY 写满时必须让出执行权。
                const ssize_t count = ::write(master, chunk.constData() + offset,
                                               size_t(chunk.size() - offset));
                if (count > 0)
                    offset += int(count);
                else
                    QVERIFY(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
                QVERIFY(deadline.elapsed() < 10000);
                QTest::qWait(1);
            }
        }
        QTRY_COMPARE_WITH_TIMEOUT(read(files[0][0].toString()).size(), 6 + chunk.size() * 300, 2000);
        QCOMPARE(updates.size(), 1);
        worker.stop();
        QCOMPARE(updates.size(), 2);
        QVERIFY(updates.last()[0].toString().size() + updates.last()[1].toString().size()
                <= TerminalLimits::DisplayUnits);
        QVERIFY(updates.last()[3].toBool());
        QCOMPARE(read(files[0][0].toString()), QByteArray("first\n") + chunk.repeated(300));
        ::close(master);
        ::close(slave);
#else
        QSKIP("Pseudo-terminal integration test requires a Unix host.");
#endif
    }

    void sshInteractiveCaptureAndRotation()
    {
        if (qEnvironmentVariableIsEmpty("LOGGER_PYTHON"))
            QSKIP("Set LOGGER_PYTHON and LOGGER_SSH_FIXTURE for the SSH integration test.");
        const int port = startServer();
        QVERIFY2(port > 0, m_server.readAllStandardError().constData());
        CaptureOptions options = sshOptions(port, directory("ssh-integration"));
        options.intervalSeconds = 1;
        CaptureWorker worker(options);
        TerminalView view;
        connect(&worker, &CaptureWorker::terminalUpdated, &view, &TerminalView::updateTerminal);
        connect(&view, &TerminalView::displayConsumed, &worker, &CaptureWorker::acknowledgeDisplay);
        connect(&worker, &CaptureWorker::hostKeyApprovalRequested, &worker,
                [&worker](const QString &, const QString &) { worker.approveHostKey(true); });
        QSignalSpy ready(&worker, &CaptureWorker::connected);
        QSignalSpy errors(&worker, &CaptureWorker::errorOccurred);
        QSignalSpy files(&worker, &CaptureWorker::fileChanged);
        QSignalSpy done(&worker, &CaptureWorker::finished);
        worker.start();
        QCOMPARE(ready.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(view.toPlainText().endsWith("device# "), 2000);
        worker.sendInput("uname\r");
        QTRY_VERIFY_WITH_TIMEOUT(view.toPlainText().contains("result: uname"), 2000);
        worker.sendInput(QByteArray(1, '\x03'));
        QTRY_VERIFY_WITH_TIMEOUT(view.toPlainText().contains("^C"), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(files.size() >= 2, 2500);
        worker.sendInput("exit\r");
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 2000);
        QCOMPARE(errors.count(), 0);
        QByteArray joined;
        for (const auto &file : files)
            joined += read(file[0].toString());
        QCOMPARE(joined, QByteArray("device# uname\nresult: uname\ndevice# ^C\ndevice# exit\nbye\n"));
        QCOMPARE(QString::fromUtf8(joined), view.toPlainText());
        QVERIFY(!joined.contains("INPUT"));
        QVERIFY(!joined.contains("OUTPUT"));
        QVERIFY(!joined.contains("fixture-password"));
        QVERIFY(QFile::exists(options.knownHostsPath));
        QVERIFY(m_server.state() == QProcess::NotRunning || m_server.waitForFinished(5000));
    }

    void sshUnknownHostRejected()
    {
        if (qEnvironmentVariableIsEmpty("LOGGER_PYTHON"))
            QSKIP("SSH fixture is not configured.");
        const int port = startServer();
        QVERIFY2(port > 0, m_server.readAllStandardError().constData());
        const auto options = sshOptions(port, directory("ssh-rejected"));
        CaptureWorker worker(options);
        connect(&worker, &CaptureWorker::hostKeyApprovalRequested, &worker,
                [&worker](const QString &, const QString &) { worker.approveHostKey(false); });
        QSignalSpy ready(&worker, &CaptureWorker::connected);
        QSignalSpy done(&worker, &CaptureWorker::finished);
        worker.start();
        QCOMPARE(ready.count(), 0);
        QCOMPARE(done.count(), 1);
        QVERIFY(!QFile::exists(options.knownHostsPath));
        QVERIFY(QDir(options.directory).entryList({"*.log"}, QDir::Files).isEmpty());
    }

    void sshAuthenticationFailure()
    {
        if (qEnvironmentVariableIsEmpty("LOGGER_PYTHON"))
            QSKIP("SSH fixture is not configured.");
        const int port = startServer();
        QVERIFY2(port > 0, m_server.readAllStandardError().constData());
        auto options = sshOptions(port, directory("ssh-auth-failed"));
        options.password = "incorrect-fixture-password";
        CaptureWorker worker(options);
        connect(&worker, &CaptureWorker::hostKeyApprovalRequested, &worker,
                [&worker](const QString &, const QString &) { worker.approveHostKey(true); });
        QSignalSpy errors(&worker, &CaptureWorker::errorOccurred);
        QSignalSpy done(&worker, &CaptureWorker::finished);
        worker.start();
        QCOMPARE(errors.count(), 1);
        QCOMPARE(done.count(), 1);
        QVERIFY(QDir(options.directory).entryList({"*.log"}, QDir::Files).isEmpty());
    }

    void cleanupTestCase()
    {
        ssh_finalize();
    }
};

QTEST_MAIN(LoggerTests)
#include "test_logger.moc"

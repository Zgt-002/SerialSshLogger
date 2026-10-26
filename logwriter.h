#pragma once

#include "terminalstream.h"

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QTimer>

class LogWriter : public QObject
{
    Q_OBJECT
public:
    explicit LogWriter(QObject *parent = nullptr);
    bool start(const QString &directory, const QString &prefix,
               int intervalSeconds, bool timestamps);
    bool append(const TerminalLine &line);
    bool finish();
    void setOptions(int intervalSeconds, bool timestamps);
    QString errorString() const { return m_error; }
    QString fileName() const { return m_file.fileName(); }

signals:
    void fileChanged(const QString &path);
    void failed(const QString &message);

private:
    struct LinePart {
        QString path;
        qint64 offset;
        int units;
        QByteArray bytes;
    };

    bool openNext();
    bool syncLine(bool complete);
    bool rewrite(QFile &file, qint64 offset, const QByteArray &bytes);
    bool fail(const QString &message);
    void tick();

    QFile m_file;
    QTimer m_timer;
    QElapsedTimer m_rotationClock;
    QString m_directory;
    QString m_prefix;
    QString m_error;
    QString m_line;
    QDateTime m_receivedAt;
    // 只保留尚可编辑的当前行；已完成行及超长行固定前段不再追踪。
    QVector<LinePart> m_parts;
    QByteArray m_currentBytes;
    qint64 m_lineOffset = 0;
    qint64 m_intervalMs = 600000;
    int m_currentUnits = 0;
    bool m_timestamps = false;
    bool m_lineTimestamp = false;
    bool m_hasLine = false;
    bool m_dirty = false;
};

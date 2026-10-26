#include "logwriter.h"

#include <QDir>
#include <QRegularExpression>

LogWriter::LogWriter(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &LogWriter::tick);
}

bool LogWriter::start(const QString &directory, const QString &prefix,
                      int intervalSeconds, bool timestamps)
{
    m_directory = directory;
    m_prefix = prefix;
    m_prefix.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), "_");
    m_intervalMs = qint64(intervalSeconds) * 1000;
    m_timestamps = timestamps;
    if (!QDir().mkpath(directory))
        return fail(tr("无法创建日志目录：%1").arg(directory));
    if (!openNext())
        return false;
    m_timer.start();
    return true;
}

bool LogWriter::fail(const QString &message)
{
    m_error = message;
    m_timer.stop();
    emit failed(message);
    return false;
}

bool LogWriter::openNext()
{
    const QString stem = m_prefix + QLatin1Char('_')
                         + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    int suffix = 0;
    QString path;
    do {
        path = QDir(m_directory).filePath(stem
                + (suffix == 0 ? QString() : QStringLiteral("_%1").arg(suffix)) + ".log");
        ++suffix;
    } while (QFile::exists(path));
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(tr("无法创建日志文件：%1\n%2").arg(path, m_file.errorString()));
    m_lineOffset = 0;
    m_rotationClock.restart();
    emit fileChanged(path);
    return true;
}

void LogWriter::setOptions(int intervalSeconds, bool timestamps)
{
    const qint64 intervalMs = qint64(intervalSeconds) * 1000;
    if (intervalMs != m_intervalMs) {
        m_intervalMs = intervalMs;
        m_rotationClock.restart();
    }
    m_timestamps = timestamps;
}

bool LogWriter::append(const TerminalLine &line)
{
    if (!m_file.isOpen() || !m_error.isEmpty())
        return false;
    if (!m_hasLine) {
        m_lineTimestamp = m_timestamps;
        m_hasLine = true;
    }
    m_line = line.text;
    m_receivedAt = line.receivedAt;
    m_dirty = true;
    if (line.continued) {
        // 超长行前段已不可编辑：固定文件偏移，但不增加换行或下一段时间戳。
        if (!syncLine(false))
            return false;
        m_lineOffset = m_file.pos();
        m_line.clear();
        m_parts.clear();
        m_currentBytes.clear();
        m_currentUnits = 0;
        m_lineTimestamp = false;
        return true;
    }
    if (line.complete)
        return syncLine(true);
    return true;
}

bool LogWriter::syncLine(bool complete)
{
    if (!m_dirty)
        return true;
    int start = 0;
    bool timestampPending = m_lineTimestamp;
    for (int index = 0; index < m_parts.size();) {
        LinePart &part = m_parts[index];
        int end = qMin(start + part.units, int(m_line.size()));
        // 编辑后字符宽度可能改变，文件边界不能切开 UTF-16 代理对。
        if (end > start && end < m_line.size() && m_line.at(end).isLowSurrogate()
            && m_line.at(end - 1).isHighSurrogate())
            --end;
        QString text = m_line.mid(start, end - start);
        if (timestampPending && !text.isEmpty()) {
            text.prepend(m_receivedAt.toString("[yyyy-MM-dd HH:mm:ss.zzz] "));
            timestampPending = false;
        }
        const QByteArray bytes = text.toUtf8();
        if (bytes != part.bytes) {
            // 已切换的文件只重写当前行尾部，保留其中的完整历史行。
            QFile file(part.path);
            if (!file.open(QIODevice::ReadWrite | QIODevice::ExistingOnly))
                return fail(tr("无法更新跨文件日志：%1\n%2").arg(part.path, file.errorString()));
            if (!rewrite(file, part.offset, bytes))
                return false;
            if (!file.flush())
                return fail(tr("日志刷新失败：%1\n%2").arg(part.path, file.errorString()));
        }
        part.units = end - start;
        part.bytes = bytes;
        start = end;
        // 已删空的片段不再占据边界，之后的新输入进入当前文件。
        if (part.units == 0)
            m_parts.removeAt(index);
        else
            ++index;
    }
    QString text = m_line.mid(start);
    if (timestampPending && (!text.isEmpty() || complete))
        text.prepend(m_receivedAt.toString("[yyyy-MM-dd HH:mm:ss.zzz] "));
    if (complete)
        text.append(QLatin1Char('\n'));
    const QByteArray bytes = text.toUtf8();
    if (!rewrite(m_file, m_lineOffset, bytes))
        return false;
    m_currentUnits = m_line.size() - start;
    m_currentBytes = bytes;
    m_dirty = false;
    if (complete) {
        m_lineOffset = m_file.pos();
        m_line.clear();
        m_parts.clear();
        m_currentBytes.clear();
        m_currentUnits = 0;
        m_hasLine = false;
    }
    return true;
}

bool LogWriter::rewrite(QFile &file, qint64 offset, const QByteArray &bytes)
{
    if (!file.seek(offset))
        return fail(tr("日志文件定位失败：%1\n%2").arg(file.fileName(), file.errorString()));
    qint64 written = 0;
    while (written < bytes.size()) {
        const qint64 count = file.write(bytes.constData() + written, bytes.size() - written);
        if (count <= 0)
            return fail(tr("日志写入失败：%1\n%2").arg(file.fileName(), file.errorString()));
        written += count;
    }
    if (!file.resize(file.pos()))
        return fail(tr("日志文件更新失败：%1\n%2").arg(file.fileName(), file.errorString()));
    return true;
}

void LogWriter::tick()
{
    if (!m_error.isEmpty() || !m_file.isOpen())
        return;
    if (!syncLine(false))
        return;
    if (!m_file.flush()) {
        fail(tr("日志刷新失败：%1").arg(m_file.errorString()));
        return;
    }
    if (m_rotationClock.elapsed() >= m_intervalMs) {
        if (m_currentUnits > 0)
            m_parts.append({m_file.fileName(), m_lineOffset, m_currentUnits, m_currentBytes});
        m_file.close();
        m_currentUnits = 0;
        m_currentBytes.clear();
        openNext();
    }
}

bool LogWriter::finish()
{
    m_timer.stop();
    if (!m_file.isOpen())
        return m_error.isEmpty();
    bool ok = m_error.isEmpty() && syncLine(false);
    if (ok && !m_file.flush())
        ok = fail(tr("日志刷新失败：%1").arg(m_file.errorString()));
    m_file.close();
    return ok;
}

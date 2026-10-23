#pragma once

#include <QDateTime>
#include <QVector>
#include <memory>

class QTextDecoder;

struct TerminalLine
{
    QString text;
    QDateTime receivedAt;
    bool complete = false;
};

class TerminalStream
{
public:
    explicit TerminalStream(const QByteArray &encoding = "UTF-8");
    ~TerminalStream();
    QVector<TerminalLine> feed(const QByteArray &data, const QDateTime &receivedAt);

private:
    enum class State { Text, Escape, Csi, Osc, OscEscape, Charset };
    void applyCsi(QChar command);
    void put(QChar character, const QDateTime &receivedAt);

    std::unique_ptr<QTextDecoder> m_decoder;
    State m_state = State::Text;
    QString m_sequence;
    QString m_line;
    QDateTime m_receivedAt;
    int m_column = 0;
    bool m_dirty = false;
};

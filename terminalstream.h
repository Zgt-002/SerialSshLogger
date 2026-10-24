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
    // 超长行已固定的前段：允许落盘，但不能添加换行或重复的行时间戳。
    bool continued = false;
};

class TerminalStream
{
public:
    explicit TerminalStream(const QByteArray &encoding = "UTF-8");
    ~TerminalStream();
    QVector<TerminalLine> feed(const QByteArray &data, const QDateTime &receivedAt);
    int cursorPosition() const;

private:
    enum class State { Text, Escape, Csi, Osc, OscEscape, Charset };
    void applyCsi(QChar command);
    struct Cell {
        uint code = ' ';
        QString combining;
        bool continuation = false;
    };
    QString lineText() const;
    void eraseCells(int first, int end);
    void put(uint code, const QDateTime &receivedAt, QVector<TerminalLine> &result);
    void finishLine(bool complete, const QDateTime &receivedAt, QVector<TerminalLine> &result);

    std::unique_ptr<QTextDecoder> m_decoder;
    State m_state = State::Text;
    QString m_sequence;
    // 宽字符占两列，第二列仅作占位；组合字符附着在第一列，不单独移动光标。
    QVector<Cell> m_cells;
    QDateTime m_receivedAt;
    int m_column = 0;
    int m_savedColumn = 0;
    int m_textUnits = 0;
    bool m_dirty = false;
};

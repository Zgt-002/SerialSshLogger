#include "terminalstream.h"
#include "terminalbuffer.h"

#include <QTextCodec>

TerminalStream::TerminalStream(const QByteArray &encoding)
    : m_decoder(QTextCodec::codecForName(encoding)->makeDecoder())
{
}

TerminalStream::~TerminalStream() = default;

QString TerminalStream::lineText() const
{
    QString text;
    text.reserve(qMax(0, m_textUnits));
    for (const Cell &cell : m_cells) {
        if (cell.continuation)
            continue;
        if (cell.code <= 0xffff)
            text.append(QChar(ushort(cell.code)));
        else {
            text.append(QChar(QChar::highSurrogate(cell.code)));
            text.append(QChar(QChar::lowSurrogate(cell.code)));
        }
        text += cell.combining;
    }
    return text;
}

int TerminalStream::cursorPosition() const
{
    int position = 0;
    for (int i = 0; i < m_column && i < m_cells.size(); ++i) {
        const Cell &cell = m_cells.at(i);
        // 光标落入宽字符的第二列时显示在字符起点，不拆开 UTF-16 代理对。
        if (cell.continuation || (i + 1 == m_column && i + 1 < m_cells.size()
                                  && m_cells.at(i + 1).continuation))
            continue;
        position += (cell.code > 0xffff ? 2 : 1) + cell.combining.size();
    }
    return position + qMax(0, m_column - int(m_cells.size()));
}

void TerminalStream::eraseCells(int first, int end)
{
    first = qBound(0, first, int(m_cells.size()));
    end = qBound(first, end, int(m_cells.size()));
    if (first == end)
        return;
    // 擦除宽字符的一半时清掉另一半，用空格保持其它字符的列位置。
    if (m_cells.at(first).continuation)
        m_cells[first - 1] = Cell();
    if (end < m_cells.size() && m_cells.at(end).continuation)
        m_cells[end] = Cell();
    for (int i = first; i < end; ++i)
        m_cells[i] = Cell();
}

void TerminalStream::finishLine(bool complete, const QDateTime &receivedAt,
                                QVector<TerminalLine> &result)
{
    result.append({lineText(), m_receivedAt.isValid() ? m_receivedAt : receivedAt,
                   complete, !complete});
    m_cells.clear();
    m_textUnits = 0;
    m_column = 0;
    m_savedColumn = 0;
    m_dirty = false;
    if (complete)
        m_receivedAt = QDateTime();
}

void TerminalStream::put(uint code, const QDateTime &receivedAt, QVector<TerminalLine> &result)
{
    const auto category = QChar::category(code);
    const bool combining = category == QChar::Mark_NonSpacing
        || category == QChar::Mark_Enclosing || category == QChar::Mark_SpacingCombining
        || code == 0x200d;
    // 常见中日韩、全角字符按两列；模糊宽度按一列，不模拟完整字形簇终端。
    const bool wide = (code >= 0x1100 && code <= 0x115f) || code == 0x2329 || code == 0x232a
        || (code >= 0x2e80 && code <= 0xa4cf && code != 0x303f)
        || (code >= 0xac00 && code <= 0xd7a3) || (code >= 0xf900 && code <= 0xfaff)
        || (code >= 0xfe10 && code <= 0xfe19) || (code >= 0xfe30 && code <= 0xfe6f)
        || (code >= 0xff01 && code <= 0xff60) || (code >= 0xffe0 && code <= 0xffe6)
        || (code >= 0x20000 && code <= 0x3fffd);
    const int width = combining && m_column > 0 ? 0 : (wide ? 2 : 1);
    const int units = code > 0xffff ? 2 : 1;
    if (m_column + width > TerminalLimits::LineColumns
        || m_textUnits + qMax(0, m_column - int(m_cells.size())) + units > TerminalLimits::LineUnits) {
        // 无换行数据达到上限时固定前段，文件不加换行；后续仅能编辑新的尾段。
        finishLine(false, receivedAt, result);
    }
    if (!m_receivedAt.isValid())
        m_receivedAt = receivedAt;
    if (combining && m_column > 0 && m_column <= m_cells.size()) {
        int index = m_column - 1;
        if (m_cells.at(index).continuation)
            --index;
        if (code <= 0xffff)
            m_cells[index].combining.append(QChar(ushort(code)));
        else {
            m_cells[index].combining.append(QChar(QChar::highSurrogate(code)));
            m_cells[index].combining.append(QChar(QChar::lowSurrogate(code)));
        }
        m_textUnits += units;
    } else {
        const int columns = wide ? 2 : 1;
        if (m_column >= m_cells.size()) {
            m_textUnits += m_column - m_cells.size() + units;
            m_cells.resize(m_column + columns);
        } else {
            eraseCells(m_column, m_column + columns);
            if (m_column + columns > m_cells.size())
                m_cells.resize(m_column + columns);
            m_textUnits = -1;
        }
        m_cells[m_column] = {code, QString(), false};
        if (wide)
            m_cells[m_column + 1] = {0, QString(), true};
        m_column += columns;
        if (m_textUnits < 0)
            m_textUnits = lineText().size();
    }
    m_dirty = true;
}

void TerminalStream::applyCsi(QChar command)
{
    // 私有模式及中间字节不能误当成普通行内编辑参数。
    for (QChar character : m_sequence)
        if (!character.isDigit() && character != QLatin1Char(';'))
            return;
    const QStringList parameters = m_sequence.split(QLatin1Char(';'));
    const qint64 value = parameters.value(0).toLongLong();
    const int count = int(qBound<qint64>(1, value, TerminalLimits::LineColumns));
    switch (command.unicode()) {
    case 'C':
    case 'a': m_column = qMin(TerminalLimits::LineColumns, m_column + count); break;
    case 'D': m_column = qMax(0, m_column - count); break;
    case 'G':
    case 0x60: m_column = count - 1; break;
    case 's': m_savedColumn = m_column; break;
    case 'u': m_column = m_savedColumn; break;
    case 'K':
        if (value == 0 && m_column < m_cells.size()) {
            eraseCells(m_column, m_cells.size());
            m_cells.resize(m_column);
        } else if (value == 1)
            eraseCells(0, m_column + 1);
        else if (value == 2)
            m_cells.clear();
        break;
    case 'P': {
        const int end = qMin(m_column + count, int(m_cells.size()));
        eraseCells(m_column, end);
        if (m_column < end)
            m_cells.remove(m_column, end - m_column);
        break;
    }
    case '@': {
        const int spaces = qMin(count, TerminalLimits::LineColumns - m_column);
        if (m_column > m_cells.size())
            m_cells.resize(m_column);
        if (m_column < m_cells.size() && m_cells.at(m_column).continuation)
            eraseCells(m_column, m_column + 1);
        m_cells.insert(m_column, spaces, Cell());
        if (m_cells.size() > TerminalLimits::LineColumns) {
            eraseCells(TerminalLimits::LineColumns, m_cells.size());
            m_cells.resize(TerminalLimits::LineColumns);
        }
        break;
    }
    case 'X': eraseCells(m_column, m_column + count); break;
    default: return;
    }
    m_textUnits = lineText().size();
    // 仅移动光标也刷新界面，避免方向键回显后光标仍留在行尾。
    m_dirty = true;
}

QVector<TerminalLine> TerminalStream::feed(const QByteArray &data, const QDateTime &receivedAt)
{
    QVector<TerminalLine> result;
    const auto text = m_decoder->toUnicode(data).toUcs4();
    for (uint code : text) {
        const QChar character = code <= 0xffff ? QChar(ushort(code)) : QChar();
        if (m_state == State::Osc) {
            if (code == 7)
                m_state = State::Text;
            else if (code == 27)
                m_state = State::OscEscape;
            continue;
        }
        if (m_state == State::OscEscape) {
            m_state = code == '\\' ? State::Text : State::Osc;
            continue;
        }
        if (m_state == State::Charset) {
            m_state = State::Text;
            continue;
        }
        if (m_state == State::Escape) {
            m_state = State::Text;
            if (code == '[') {
                m_sequence.clear();
                m_state = State::Csi;
            } else if (code == ']' || code == 'P' || code == '_' || code == '^')
                m_state = State::Osc;
            else if (code == '(' || code == ')')
                m_state = State::Charset;
            else if (code == '7')
                m_savedColumn = m_column;
            else if (code == '8') {
                m_column = m_savedColumn;
                m_dirty = true;
            }
            continue;
        }
        if (m_state == State::Csi) {
            if (code >= 0x40 && code <= 0x7e) {
                applyCsi(character);
                m_state = State::Text;
            } else if (m_sequence.size() < 128)
                m_sequence.append(character);
            else
                m_state = State::Text;
            continue;
        }
        if (code == 27)
            m_state = State::Escape;
        else if (code == '\r') {
            m_column = 0;
            m_dirty = true;
        } else if (code == '\n')
            finishLine(true, receivedAt, result);
        else if (code == 8) {
            m_column = qMax(0, m_column - 1);
            m_dirty = true;
        } else if (code == '\t') {
            const int spaces = 8 - m_column % 8;
            for (int i = 0; i < spaces; ++i)
                put(' ', receivedAt, result);
        } else if (code >= 0x20 && !(code >= 0x7f && code <= 0x9f))
            put(code, receivedAt, result);
    }
    if (m_dirty) {
        result.append({lineText(), m_receivedAt, false});
        m_dirty = false;
    }
    return result;
}

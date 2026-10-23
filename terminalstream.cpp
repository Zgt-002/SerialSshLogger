#include "terminalstream.h"

#include <QTextCodec>

TerminalStream::TerminalStream(const QByteArray &encoding)
    : m_decoder(QTextCodec::codecForName(encoding)->makeDecoder())
{
}

TerminalStream::~TerminalStream() = default;

void TerminalStream::put(QChar character, const QDateTime &receivedAt)
{
    if (!m_receivedAt.isValid())
        m_receivedAt = receivedAt;
    if (m_column > m_line.size())
        m_line.append(QString(m_column - m_line.size(), QLatin1Char(' ')));
    if (m_column == m_line.size())
        m_line.append(character);
    else
        m_line[m_column] = character;
    ++m_column;
    m_dirty = true;
}

void TerminalStream::applyCsi(QChar command)
{
    const QStringList parameters = m_sequence.split(QLatin1Char(';'));
    const int value = parameters.value(0).toInt();
    const int count = qMax(1, value);
    switch (command.unicode()) {
    case 'C': m_column = qMin(1048576, m_column + count); break;
    case 'D': m_column = qMax(0, m_column - count); break;
    case 'G': m_column = qBound(0, count - 1, 1048576); break;
    case 'K':
        if (value == 0)
            m_line.truncate(m_column);
        else if (value == 1) {
            const int end = int(qMin<qint64>(m_column + 1, m_line.size()));
            for (int i = 0; i < end; ++i)
                m_line[i] = QLatin1Char(' ');
        } else if (value == 2)
            m_line.clear();
        m_dirty = true;
        break;
    case 'P':
        m_line.remove(m_column, count);
        m_dirty = true;
        break;
    default: break;
    }
}

QVector<TerminalLine> TerminalStream::feed(const QByteArray &data,
                                         const QDateTime &receivedAt)
{
    QVector<TerminalLine> result;
    const QString text = m_decoder->toUnicode(data);
    for (QChar character : text) {
        const ushort code = character.unicode();
        if (m_state == State::Osc) {
            if (code == 7)
                m_state = State::Text;
            else if (code == 27)
                m_state = State::OscEscape;
            continue;
        }
        if (m_state == State::OscEscape) {
            m_state = character == QLatin1Char('\\') ? State::Text : State::Osc;
            continue;
        }
        if (m_state == State::Charset) {
            m_state = State::Text;
            continue;
        }
        if (m_state == State::Escape) {
            if (character == QLatin1Char('[')) {
                m_sequence.clear();
                m_state = State::Csi;
            } else if (character == QLatin1Char(']'))
                m_state = State::Osc;
            else if (character == QLatin1Char('(') || character == QLatin1Char(')'))
                m_state = State::Charset;
            else
                m_state = State::Text;
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
        else if (character == QLatin1Char('\r'))
            m_column = 0;
        else if (character == QLatin1Char('\n')) {
            result.append({m_line, m_receivedAt.isValid() ? m_receivedAt : receivedAt, true});
            m_line.clear();
            m_receivedAt = QDateTime();
            m_column = 0;
            m_dirty = false;
        } else if (code == 8)
            m_column = qMax(0, m_column - 1);
        else if (character == QLatin1Char('\t')) {
            const int spaces = 8 - m_column % 8;
            for (int i = 0; i < spaces; ++i)
                put(QLatin1Char(' '), receivedAt);
        } else if (code >= 0x20 && code != 0x7f)
            put(character, receivedAt);
    }
    if (m_dirty) {
        result.append({m_line, m_receivedAt, false});
        m_dirty = false;
    }
    return result;
}

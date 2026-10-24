#include "terminalview.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>

TerminalView::TerminalView(QWidget *parent) : QPlainTextEdit(parent)
{
    setObjectName("terminal");
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setMaximumBlockCount(TerminalLimits::DisplayBlocks);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setStyleSheet("QPlainTextEdit { background: #101820; color: #e6edf3; "
                  "selection-background-color: #264f78; border: 1px solid #46515c; }");
}

void TerminalView::setInputEnabled(bool enabled)
{
    m_inputEnabled = enabled;
    setAttribute(Qt::WA_InputMethodEnabled, enabled);
    viewport()->update();
}

void TerminalView::updateTerminal(const QString &completed, const QString &currentLine,
                                  int cursor, bool reset)
{
    m_pending.append(completed, currentLine, cursor, reset);
    if (!m_paused)
        renderPending();
    // 暂停时也确认已接收显示批次，让采集线程继续工作而不积压跨线程事件。
    emit displayConsumed();
}

void TerminalView::setDisplayPaused(bool paused)
{
    m_paused = paused;
    if (!paused)
        renderPending();
    viewport()->update();
}

void TerminalView::setAutoScroll(bool enabled)
{
    m_autoScroll = enabled;
    if (enabled)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalView::clearDisplay()
{
    // 只清空界面和待显示历史，绝不重置接收解析器或触碰日志文件。
    m_pending = TerminalBuffer();
    m_remotePosition = 0;
    clear();
}

QTextCursor TerminalView::remoteCursor() const
{
    const QTextBlock block = document()->lastBlock();
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + qBound(0, m_remotePosition, block.length() - 1));
    return cursor;
}

void TerminalView::renderPending()
{
    if (!m_pending.dirty)
        return;
    const bool atBottom = verticalScrollBar()->value() >= verticalScrollBar()->maximum() - 2;
    const int oldScroll = verticalScrollBar()->value();
    if (m_pending.reset)
        clear();
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(m_pending.completed + m_pending.currentLine);
    // 同时限制字符数与行数，不能只靠 10000 行限制长行造成的内存增长。
    const int excess = document()->characterCount() - 1 - TerminalLimits::DisplayUnits;
    if (excess > 0) {
        QTextCursor trim(document());
        const QTextBlock block = document()->findBlock(excess);
        trim.setPosition(block.position() + block.length(), QTextCursor::KeepAnchor);
        trim.removeSelectedText();
    }
    m_remotePosition = m_pending.cursor;
    m_pending.consume();
    if (m_autoScroll && atBottom) {
        if (!textCursor().hasSelection())
            setTextCursor(remoteCursor());
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    } else
        verticalScrollBar()->setValue(oldScroll);
    viewport()->update();
}

void TerminalView::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);
    if (m_inputEnabled && !m_paused && hasFocus() && !textCursor().hasSelection()) {
        // 文本控件保持只读；单独绘制远端光标，输入仍只依赖 SSH 回显。
        QPainter painter(viewport());
        QRect rectangle = cursorRect(remoteCursor());
        rectangle.setWidth(2);
        painter.fillRect(rectangle, QColor("#e6edf3"));
    }
}

void TerminalView::pasteInput()
{
    QString text = QApplication::clipboard()->text();
    text.replace("\r\n", "\n");
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    if (!text.isEmpty())
        emit inputData(text.toUtf8());
}

void TerminalView::keyPressEvent(QKeyEvent *event)
{
    if (!m_inputEnabled) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        if (textCursor().hasSelection())
            copy();
        else
            emit inputData(QByteArray(1, '\x03'));
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        pasteInput();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        return;
    }
    QByteArray data;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: data = "\r"; break;
    case Qt::Key_Backspace: data = QByteArray(1, '\x7f'); break;
    case Qt::Key_Tab: data = "\t"; break;
    case Qt::Key_Backtab: data = "\x1b[Z"; break;
    case Qt::Key_Escape: data = "\x1b"; break;
    case Qt::Key_Up: data = "\x1b[A"; break;
    case Qt::Key_Down: data = "\x1b[B"; break;
    case Qt::Key_Right: data = "\x1b[C"; break;
    case Qt::Key_Left: data = "\x1b[D"; break;
    case Qt::Key_Home: data = "\x1b[H"; break;
    case Qt::Key_End: data = "\x1b[F"; break;
    case Qt::Key_Delete: data = "\x1b[3~"; break;
    default:
        if ((event->modifiers() & Qt::ControlModifier)
            && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z)
            data = QByteArray(1, char(event->key() - Qt::Key_A + 1));
        else if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
            data = event->text().toUtf8();
        break;
    }
    if (!data.isEmpty()) {
        emit inputData(data);
        event->accept();
    } else
        QPlainTextEdit::keyPressEvent(event);
}

void TerminalView::inputMethodEvent(QInputMethodEvent *event)
{
    if (m_inputEnabled && !event->commitString().isEmpty())
        emit inputData(event->commitString().toUtf8());
    event->accept();
}

void TerminalView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();
    if (m_inputEnabled) {
        menu->addSeparator();
        menu->addAction(tr("粘贴到终端"), this, &TerminalView::pasteInput);
    }
    menu->exec(event->globalPos());
    delete menu;
}

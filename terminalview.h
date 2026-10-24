#pragma once

#include "terminalbuffer.h"

#include <QPlainTextEdit>

class TerminalView : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit TerminalView(QWidget *parent = nullptr);
    void setInputEnabled(bool enabled);
    void updateTerminal(const QString &completed, const QString &currentLine,
                        int cursor = -1, bool reset = false);
    void setDisplayPaused(bool paused);
    void setAutoScroll(bool enabled);
    void clearDisplay();

signals:
    void inputData(const QByteArray &data);
    void displayConsumed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void pasteInput();
    void renderPending();
    QTextCursor remoteCursor() const;
    TerminalBuffer m_pending;
    int m_remotePosition = 0;
    bool m_paused = false;
    bool m_autoScroll = true;
    bool m_inputEnabled = false;
};

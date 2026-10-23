#pragma once

#include <QPlainTextEdit>

class TerminalView : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit TerminalView(QWidget *parent = nullptr);
    void setInputEnabled(bool enabled);
    void updateTerminal(const QString &completed, const QString &currentLine);

signals:
    void inputData(const QByteArray &data);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void pasteInput();
    bool m_inputEnabled = false;
};

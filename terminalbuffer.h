#pragma once

#include <QString>

namespace TerminalLimits {
constexpr int DisplayBlocks = 10000;
// QString 按 UTF-16 存储；此处限制文本载荷约 2 MiB，不包含 Qt 控件的额外开销。
constexpr int DisplayUnits = 1024 * 1024;
constexpr int LineColumns = 16384;
constexpr int LineUnits = 32768;
}

// 采集线程和暂停显示各持有一份缓冲，只保存最新的有界显示数据。
// 裁剪只作用于显示通道；完整日志在 CaptureWorker 中独立送给 LogWriter。
struct TerminalBuffer
{
    QString completed;
    QString currentLine;
    int cursor = 0;
    bool reset = false;
    bool dirty = false;

    void append(const QString &lines, const QString &current, int position, bool replace = false)
    {
        if (replace) {
            completed.clear();
            reset = true;
        }
        completed += lines;
        int start = qMax(0, int(current.size()) - TerminalLimits::LineUnits);
        if (start > 0 && current.at(start).isLowSurrogate())
            ++start;
        currentLine = current.mid(start);
        cursor = qBound(0, (position < 0 ? int(current.size()) : position) - start,
                        int(currentLine.size()));
        const int excess = int(completed.size() + currentLine.size()) - TerminalLimits::DisplayUnits;
        if (excess > 0) {
            const int end = completed.indexOf(QLatin1Char('\n'), excess - 1);
            completed.remove(0, end >= 0 ? end + 1 : int(completed.size()));
            reset = true;
        }
        int extraLines = completed.count(QLatin1Char('\n')) - (TerminalLimits::DisplayBlocks - 1);
        int end = -1;
        while (extraLines-- > 0)
            end = completed.indexOf(QLatin1Char('\n'), end + 1);
        if (end >= 0) {
            completed.remove(0, end + 1);
            reset = true;
        }
        dirty = true;
    }

    void consume()
    {
        completed.clear();
        reset = false;
        dirty = false;
    }
};

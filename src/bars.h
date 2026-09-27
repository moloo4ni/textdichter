#pragma once

// Shared look of the bars around the text: the banner, the find bar and the
// status bar.

#include <QFontMetrics>
#include <QWidget>

// Space between the window edge and the contents of a bar. It follows the font,
// and with common styles lines the text up with the menu titles.
inline int barInset(const QWidget *widget)
{
    return widget->fontMetrics().averageCharWidth() * 3 / 2;
}

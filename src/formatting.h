#pragma once

#include <QString>

class QTextCursor;

// Commands of the Format menu. Each one edits the document through the
// cursor as a single undo step and leaves the cursor where typing continues.
namespace formatting {

// Wraps the selection in the marker (`**`, `*` or `` ` ``), or unwraps it if it
// is already wrapped. Without a selection inserts a pair of markers.
void toggleInline(QTextCursor &cursor, const QString &marker);

// `[selection](|)`, or `[|](url)` if the selection is a URL.
void insertLink(QTextCursor &cursor);

// Makes the selected lines headings of the level, replacing any heading
// marker they have; level 0 or the level they already have makes them text.
void setHeading(QTextCursor &cursor, int level);

enum class LinePrefix { Quote, Bullet, Numbered };

// Adds the prefix to the selected lines, or removes it if they all have it.
void toggleLinePrefix(QTextCursor &cursor, LinePrefix prefix);

// Puts the selected lines between ``` fences.
void wrapCodeBlock(QTextCursor &cursor);

// A whole string that is a web or mail address.
bool isUrl(const QString &text);

} // namespace formatting

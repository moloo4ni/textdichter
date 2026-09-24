#pragma once

// Line-by-line editing shared by the Format menu and typing.

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace lines {

// The lines touched by the selection. A selection that ends at the start of a
// line does not touch that line.
inline std::pair<QTextBlock, QTextBlock> selected(const QTextCursor &cursor)
{
    QTextDocument *doc = cursor.document();
    const QTextBlock first = doc->findBlock(cursor.selectionStart());
    QTextBlock last = doc->findBlock(cursor.selectionEnd());
    if (cursor.hasSelection() && last != first && cursor.selectionEnd() == last.position())
        last = last.previous();
    return {first, last};
}

template <typename Fn>
void forEach(const std::pair<QTextBlock, QTextBlock> &range, Fn fn)
{
    for (QTextBlock block = range.first; block.isValid(); block = block.next()) {
        fn(block);
        if (block == range.second)
            break;
    }
}

inline void replace(const QTextBlock &block, int offset, int length, const QString &text)
{
    QTextCursor edit(block);
    edit.setPosition(block.position() + offset);
    edit.setPosition(block.position() + offset + length, QTextCursor::KeepAnchor);
    edit.insertText(text);
}

// Edits lines as one undo step while the selection follows the text it
// covered: cursors in the document move with insertions and removals by themselves.
template <typename Fn>
void edit(QTextCursor &cursor, Fn fn)
{
    QTextDocument *doc = cursor.document();
    QTextCursor anchor(doc), position(doc);
    anchor.setPosition(cursor.anchor());
    position.setPosition(cursor.position());

    cursor.beginEditBlock();
    fn();
    cursor.endEditBlock();

    cursor.setPosition(anchor.position());
    cursor.setPosition(position.position(), QTextCursor::KeepAnchor);
}

} // namespace lines

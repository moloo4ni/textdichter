#include "formatting.h"

#include "lines.h"

#include <QRegularExpression>

namespace formatting {

namespace {

const QRegularExpression &atxHeading()
{
    static const QRegularExpression re(QStringLiteral(R"(^ {0,3}(#{1,6})(?:[ \t]+|$))"));
    return re;
}

const QRegularExpression &quoteMarker()
{
    static const QRegularExpression re(QStringLiteral(R"(^ {0,3}> ?)"));
    return re;
}

// Captures the indent and the marker.
const QRegularExpression &listMarker()
{
    static const QRegularExpression re(QStringLiteral(R"(^( *)([-+*]|\d{1,9}[.)])(?: +|$))"));
    return re;
}

bool isBullet(const QString &marker)
{
    return marker.size() == 1 && !marker.front().isDigit();
}

} // namespace

bool isUrl(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(?:[a-zA-Z][a-zA-Z0-9+.-]*://|mailto:|www\.)\S+$)"));
    return re.match(text).hasMatch();
}

void toggleInline(QTextCursor &cursor, const QString &marker)
{
    QTextDocument *doc = cursor.document();
    const int m = marker.size();
    const QChar c = marker.front();

    // Emphasis cannot start or end with a space, so markers go around words only.
    int start = cursor.selectionStart();
    int end = cursor.selectionEnd();
    while (start < end && doc->characterAt(start).isSpace())
        ++start;
    while (end > start && doc->characterAt(end - 1).isSpace())
        --end;

    const auto runFrom = [&](int pos, int step, int limit) {
        int run = 0;
        for (int p = pos; p != limit && doc->characterAt(p) == c; p += step)
            ++run;
        return run;
    };
    // `*` inside `**` is not an italic marker: italic runs are odd.
    const auto isMarker = [&](int run) { return run >= m && (c != QLatin1Char('*') || m != 1 || run % 2); };

    cursor.beginEditBlock();
    QTextCursor edit(doc);
    const auto remove = [&](int pos) {
        edit.setPosition(pos);
        edit.setPosition(pos + m, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
    };
    if (end - start > 2 * m && isMarker(runFrom(start, 1, end)) && isMarker(runFrom(end - 1, -1, start - 1))) {
        remove(end - m);
        remove(start);
        end -= 2 * m;
    } else if (start >= m && isMarker(runFrom(start - 1, -1, -1))
               && isMarker(runFrom(end, 1, doc->characterCount()))) {
        remove(end);
        remove(start - m);
        start -= m;
        end -= m;
    } else {
        edit.setPosition(end);
        edit.insertText(marker);
        edit.setPosition(start);
        edit.insertText(marker);
        start += m;
        end += m;
    }
    cursor.endEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
}

void insertLink(QTextCursor &cursor)
{
    const QString text = cursor.selectedText();
    const int start = cursor.selectionStart();
    if (isUrl(text)) {
        cursor.insertText(QStringLiteral("[](%1)").arg(text));
        cursor.setPosition(start + 1);
    } else {
        cursor.insertText(QStringLiteral("[%1]()").arg(text));
        cursor.setPosition(start + text.size() + 3);
    }
}

void setHeading(QTextCursor &cursor, int level)
{
    const auto range = lines::selected(cursor);
    const bool single = range.first == range.second;
    const auto currentLevel = [](const QTextBlock &block) {
        const auto match = atxHeading().match(block.text());
        return match.hasMatch() ? int(match.capturedLength(1)) : 0;
    };

    bool same = level > 0;
    lines::forEach(range, [&](const QTextBlock &block) {
        if ((single || !block.text().isEmpty()) && currentLevel(block) != level)
            same = false;
    });
    if (same)
        level = 0;

    lines::edit(cursor, [&] {
        lines::forEach(range, [&](const QTextBlock &block) {
            if (!single && block.text().isEmpty())
                return;
            const auto match = atxHeading().match(block.text());
            lines::replace(block, 0, match.hasMatch() ? int(match.capturedLength()) : 0,
                    level > 0 ? QString(level, QLatin1Char('#')) + QLatin1Char(' ') : QString());
        });
    });
}

void toggleLinePrefix(QTextCursor &cursor, LinePrefix prefix)
{
    const auto range = lines::selected(cursor);
    const bool single = range.first == range.second;
    const auto has = [&](const QString &text) {
        if (prefix == LinePrefix::Quote)
            return quoteMarker().match(text).hasMatch();
        const auto match = listMarker().match(text);
        return match.hasMatch() && isBullet(match.captured(2)) == (prefix == LinePrefix::Bullet);
    };

    bool all = true;
    lines::forEach(range, [&](const QTextBlock &block) {
        if ((single || !block.text().isEmpty()) && !has(block.text()))
            all = false;
    });

    lines::edit(cursor, [&] {
        int number = 1;
        lines::forEach(range, [&](const QTextBlock &block) {
            const QString text = block.text();
            if (prefix == LinePrefix::Quote) {
                const auto match = quoteMarker().match(text);
                if (all)
                    lines::replace(block, 0, match.capturedLength(), {});
                else if (!match.hasMatch())
                    // An empty line inside keeps the quote in one piece.
                    lines::replace(block, 0, 0, text.isEmpty() && !single ? QStringLiteral(">") : QStringLiteral("> "));
                return;
            }
            if (text.isEmpty() && !single)
                return;
            const auto match = listMarker().match(text);
            const int indent = match.hasMatch() ? match.capturedLength(1) : 0;
            const int length = match.hasMatch() ? match.capturedLength() - indent : 0;
            const QString marker = prefix == LinePrefix::Bullet ? QStringLiteral("- ")
                                                                : QStringLiteral("%1. ").arg(number++);
            lines::replace(block, indent, length, all ? QString() : marker);
        });
    });
}

void wrapCodeBlock(QTextCursor &cursor)
{
    QTextDocument *doc = cursor.document();
    const auto [first, last] = lines::selected(cursor);
    const int firstNumber = first.blockNumber();
    const int lastNumber = last.blockNumber();
    const bool selection = cursor.hasSelection();
    const int column = cursor.positionInBlock();

    cursor.beginEditBlock();
    QTextCursor edit(last);
    edit.movePosition(QTextCursor::EndOfBlock);
    edit.insertText(QStringLiteral("\n```"));
    edit.setPosition(first.position());
    edit.insertText(QStringLiteral("```\n"));
    cursor.endEditBlock();

    const QTextBlock top = doc->findBlockByNumber(firstNumber + 1);
    if (selection) {
        const QTextBlock bottom = doc->findBlockByNumber(lastNumber + 1);
        cursor.setPosition(top.position());
        cursor.setPosition(bottom.position() + bottom.length() - 1, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(top.position() + std::min(column, top.length() - 1));
    }
}

} // namespace formatting

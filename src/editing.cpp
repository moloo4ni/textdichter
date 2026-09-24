#include "editing.h"

#include "formatting.h"
#include "lines.h"

#include <QRegularExpression>

#include <optional>

namespace editing {

namespace {

// The start of a line: quote markers, then maybe a list marker.
struct Prefix
{
    QString quote; // as written, e.g. "> > "
    int indent = -1; // spaces before the list marker; -1 if not a list item
    QString marker; // "-" or "3."
    QString spacing; // spaces after the marker
    int length = 0;

    bool isItem() const { return indent >= 0; }
    // Where the text of the item starts, relative to the quote.
    int contentColumn() const { return indent + marker.size() + std::max<int>(1, spacing.size()); }
};

Prefix parse(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(^((?: {0,3}> ?)*)(?:( *)([-+*]|\d{1,9}[.)])( +|$))?)"));
    const auto match = re.match(text);
    Prefix prefix;
    prefix.quote = match.captured(1);
    prefix.length = match.capturedLength();
    if (match.hasCaptured(3)) {
        prefix.indent = match.capturedLength(2);
        prefix.marker = match.captured(3);
        prefix.spacing = match.captured(4);
    }
    return prefix;
}

bool isBlank(const QString &text, const Prefix &prefix)
{
    return text.mid(prefix.length).trimmed().isEmpty();
}

// The nearest item above the block that matches, within the same list.
template <typename Pred>
std::optional<Prefix> itemAbove(const QTextBlock &block, const QString &quote, Pred matches)
{
    for (QTextBlock b = block.previous(); b.isValid(); b = b.previous()) {
        const QString text = b.text();
        const Prefix prefix = parse(text);
        if (prefix.quote != quote)
            return std::nullopt;
        if (prefix.isItem()) {
            if (matches(prefix))
                return prefix;
        } else if (!isBlank(text, prefix) && !text.mid(prefix.length).startsWith(QLatin1Char(' '))) {
            return std::nullopt; // a paragraph after the list
        }
    }
    return std::nullopt;
}

} // namespace

bool continueBlock(QTextCursor &cursor)
{
    if (cursor.hasSelection())
        return false;
    const QTextBlock block = cursor.block();
    const QString text = block.text();
    const Prefix prefix = parse(text);
    if ((prefix.quote.isEmpty() && !prefix.isItem()) || cursor.positionInBlock() < prefix.length)
        return false;

    if (isBlank(text, prefix)) {
        // An empty item ends the list and an empty quote line ends the quote;
        // a quote around the list stays.
        const QString rest = prefix.isItem() ? prefix.quote : QString();
        lines::replace(block, 0, text.size(), rest);
        cursor.setPosition(block.position() + rest.size());
        return true;
    }

    QString next = prefix.quote;
    if (prefix.isItem()) {
        QString marker = prefix.marker;
        if (marker.front().isDigit())
            marker = QString::number(marker.chopped(1).toInt() + 1) + marker.back();
        next += QString(prefix.indent, QLatin1Char(' ')) + marker
                + (prefix.spacing.isEmpty() ? QStringLiteral(" ") : prefix.spacing);
    }
    cursor.insertText(QLatin1Char('\n') + next);
    return true;
}

bool indentListItems(QTextCursor &cursor, bool outdent)
{
    const auto range = lines::selected(cursor);
    const Prefix first = parse(range.first.text());
    if (!first.isItem())
        return false;

    // CommonMark nests an item by indenting it to the text of the item above.
    int target;
    if (!outdent) {
        const auto above = itemAbove(range.first, first.quote,
                                     [&](const Prefix &p) { return p.indent <= first.indent; });
        if (!above || above->indent != first.indent)
            return true; // the first item of a list has nothing to nest under
        target = above->contentColumn();
    } else {
        if (first.indent == 0)
            return true;
        const auto parent = itemAbove(range.first, first.quote,
                                      [&](const Prefix &p) { return p.indent < first.indent; });
        target = parent ? parent->indent : 0;
    }

    const int delta = target - first.indent;
    lines::edit(cursor, [&] {
        lines::forEach(range, [&](const QTextBlock &block) {
            const QString text = block.text();
            const Prefix prefix = parse(text);
            if (isBlank(text, prefix) && !prefix.isItem())
                return;
            const int at = prefix.quote.size();
            if (delta > 0) {
                lines::replace(block, at, 0, QString(delta, QLatin1Char(' ')));
            } else {
                int spaces = 0;
                while (at + spaces < text.size() && text.at(at + spaces) == QLatin1Char(' '))
                    ++spaces;
                lines::replace(block, at, std::min(-delta, spaces), {});
            }
        });
    });
    return true;
}

bool linkSelection(QTextCursor &cursor, const QString &pasted)
{
    const QString url = pasted.trimmed();
    const QString text = cursor.selectedText();
    if (text.isEmpty() || text.contains(QChar::ParagraphSeparator) || !formatting::isUrl(url)
        || formatting::isUrl(text))
        return false;
    cursor.insertText(QStringLiteral("[%1](%2)").arg(text, url));
    return true;
}

} // namespace editing

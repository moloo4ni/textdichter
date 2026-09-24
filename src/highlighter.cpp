#include "highlighter.h"

#include "views.h"

#include <QPalette>
#include <QRegularExpression>
#include <QTextDocument>

#include <algorithm>
#include <functional>

#include <cmark.h>

namespace {

QString sourceOf(const QTextDocument *document)
{
    // Not toPlainText(): U+2028 must stay inside its line, as in the document.
    return document->toRawText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
}

// cmark counts columns in UTF-8 bytes.
int charColumn(const QString &line, int byteColumn)
{
    return int(QString::fromUtf8(line.toUtf8().left(byteColumn)).size());
}

QRegularExpressionMatch matchAt(const QRegularExpression &re, const QString &text, int offset)
{
    return re.match(text, offset, QRegularExpression::NormalMatch,
                    QRegularExpression::AnchorAtOffsetMatchOption);
}

} // namespace

Highlighter::Highlighter(QTextDocument *document)
    : QSyntaxHighlighter(static_cast<QObject *>(document))
{
    // Connected before setDocument(), so the lines are parsed again before
    // QSyntaxHighlighter restyles the changed ones.
    connect(document, &QTextDocument::contentsChange, this, &Highlighter::documentChanged);
    m_lines = parse(sourceOf(document));
    setDocument(document);
}

void Highlighter::setPalette(const QPalette &palette)
{
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);
    m_markup = {};
    m_markup.setForeground(mix(base, text, 0.45));
    m_heading = {};
    m_heading.setFontWeight(QFont::Bold);
    m_heading.setForeground(palette.color(QPalette::Link));
    m_bold = {};
    m_bold.setFontWeight(QFont::Bold);
    m_italic = {};
    m_italic.setFontItalic(true);
    m_code = {};
    m_code.setBackground(mix(base, text, 0.08));
    m_linkText = {};
    m_linkText.setForeground(palette.color(QPalette::Link));
    rehighlight();
}

QList<Highlighter::Line> Highlighter::parse(const QString &source)
{
    const QStringList text = source.split(QLatin1Char('\n'));
    QList<Line> lines(text.size());
    const QByteArray utf8 = source.toUtf8();
    cmark_node *root = cmark_parse_document(utf8.constData(), size_t(utf8.size()), CMARK_OPT_DEFAULT);
    cmark_iter *iter = cmark_iter_new(root);

    static const QRegularExpression openingFence(QStringLiteral(R"( {0,3}(`{3,}|~{3,}))"));
    static const QRegularExpression closingFence(QStringLiteral(R"(^(?: {0,3}> ?)*\s*(`{3,}|~{3,})\s*$)"));
    const auto set = [&](int from, int to, Kind kind) {
        for (int i = from; i <= to; ++i)
            lines[i].kind = kind;
    };

    for (cmark_event_type event; (event = cmark_iter_next(iter)) != CMARK_EVENT_DONE;) {
        if (event != CMARK_EVENT_ENTER)
            continue;
        cmark_node *node = cmark_iter_get_node(iter);
        const int first = std::clamp(cmark_node_get_start_line(node) - 1, 0, int(lines.size()) - 1);
        const int last = std::clamp(cmark_node_get_end_line(node) - 1, first, int(lines.size()) - 1);
        const int column = charColumn(text[first], cmark_node_get_start_column(node) - 1);
        switch (cmark_node_get_type(node)) {
        case CMARK_NODE_BLOCK_QUOTE:
            for (int i = first; i <= last; ++i)
                ++lines[i].quoteDepth;
            break;
        case CMARK_NODE_ITEM:
            lines[first].marker = qint16(column);
            break;
        case CMARK_NODE_HEADING: {
            // cmark ends a setext heading on the blank line after it, if any.
            int end = last;
            while (end > first && text[end].trimmed().isEmpty())
                --end;
            if (end > first) { // setext: the text, then a line of = or -
                set(first, end - 1, Kind::Heading);
                lines[end].kind = Kind::HeadingUnderline;
            } else {
                lines[first].kind = Kind::Heading;
            }
            break;
        }
        case CMARK_NODE_CODE_BLOCK: {
            set(first, last, Kind::Code);
            const auto opening = matchAt(openingFence, text[first], column);
            if (!opening.hasMatch())
                break; // indented
            lines[first].kind = Kind::Fence;
            const auto closing = closingFence.match(text[last]);
            if (last > first && closing.hasMatch() && closing.captured(1).front() == opening.captured(1).front()
                && closing.capturedLength(1) >= opening.capturedLength(1))
                lines[last].kind = Kind::Fence;
            break;
        }
        case CMARK_NODE_HTML_BLOCK:
            set(first, last, Kind::Code);
            break;
        case CMARK_NODE_THEMATIC_BREAK:
            lines[first].kind = Kind::Break;
            break;
        default:
            break;
        }
    }
    cmark_iter_free(iter);
    cmark_node_free(root);
    return lines;
}

void Highlighter::documentChanged(int position, int charsRemoved, int charsAdded)
{
    if (!charsRemoved && !charsAdded)
        return;
    const QList<Line> old = std::exchange(m_lines, parse(sourceOf(document())));

    // QSyntaxHighlighter restyles the changed lines itself. Lines elsewhere are
    // restyled when their kind changed: a fence opened above, text under
    // a new `===`.
    const int first = document()->findBlock(position).blockNumber();
    const int last = document()->findBlock(position + charsAdded).blockNumber();
    const qsizetype shift = m_lines.size() - old.size();
    int i = 0;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next(), ++i) {
        if (i >= first && i <= last)
            continue;
        const qsizetype j = i < first ? i : i - shift;
        if (i >= m_lines.size() || j < 0 || j >= old.size() || old[j] != m_lines[i])
            rehighlightBlock(block);
    }
}

void Highlighter::highlightBlock(const QString &text)
{
    const int number = currentBlock().blockNumber();
    if (number >= m_lines.size())
        return;
    const Line line = m_lines[number];

    static const QRegularExpression quoteMarker(QStringLiteral(R"( {0,3}> ?)"));
    int pos = 0;
    for (int depth = 0; depth < line.quoteDepth; ++depth) {
        const auto match = matchAt(quoteMarker, text, pos);
        if (!match.hasMatch())
            break; // a lazy continuation line
        merge(pos, match.capturedLength(), m_markup);
        pos = match.capturedEnd();
    }

    switch (line.kind) {
    case Kind::Code:
        return;
    case Kind::Fence:
    case Kind::Break:
    case Kind::HeadingUnderline:
        merge(pos, text.size() - pos, m_markup);
        return;
    case Kind::Text:
    case Kind::Heading:
        break;
    }

    static const QRegularExpression listMarker(QStringLiteral(R"([-+*]|\d{1,9}[.)])"));
    if (line.marker >= pos) {
        const auto match = matchAt(listMarker, text, line.marker);
        if (match.hasMatch()) {
            merge(match.capturedStart(), match.capturedLength(), m_markup);
            pos = match.capturedEnd();
        }
    }

    if (line.kind == Kind::Heading) {
        merge(pos, text.size() - pos, m_heading);
        static const QRegularExpression opening(QStringLiteral(R"( {0,3}#{1,6}(?=\s|$))"));
        static const QRegularExpression closing(QStringLiteral(R"([ \t]#+[ \t]*$)"));
        const auto open = matchAt(opening, text, pos);
        if (open.hasMatch()) {
            merge(pos, open.capturedLength(), m_markup);
            pos = open.capturedEnd();
            const auto close = closing.match(text, pos);
            if (close.hasMatch())
                merge(close.capturedStart(), close.capturedLength(), m_markup);
        }
    }
    highlightInlines(text, pos);
}

void Highlighter::highlightInlines(const QString &text, int from)
{
    // Code spans and URLs are taken first: no other markup inside them.
    QList<bool> taken(text.size(), false);
    const auto take = [&](int start, int length) {
        std::fill_n(taken.begin() + start, length, true);
    };
    const auto isFree = [&](int start, int length) {
        return std::none_of(taken.cbegin() + start, taken.cbegin() + start + length, std::identity());
    };
    const auto each = [&](const QRegularExpression &re, const QString &in, auto fn) {
        for (const auto &match : re.globalMatch(in, from))
            fn(match);
    };

    static const QRegularExpression codeSpan(QStringLiteral(R"((?<!`)(`+)(?!`)(.+?)(?<!`)\1(?!`))"));
    each(codeSpan, text, [&](const QRegularExpressionMatch &m) {
        merge(m.capturedStart(), m.capturedLength(1), m_markup);
        merge(m.capturedStart(2), m.capturedLength(2), m_code);
        merge(m.capturedEnd(2), m.capturedLength(1), m_markup);
        take(m.capturedStart(), m.capturedLength());
    });

    static const QRegularExpression autolink(
        QStringLiteral(R"(<(?:[a-zA-Z][a-zA-Z0-9+.-]{1,31}:[^\s<>]*|[^\s<>@]+@[^\s<>@]+)>)"));
    each(autolink, text, [&](const QRegularExpressionMatch &m) {
        if (!isFree(m.capturedStart(), m.capturedLength()))
            return;
        merge(m.capturedStart(), m.capturedLength(), m_markup);
        merge(m.capturedStart() + 1, m.capturedLength() - 2, m_linkText);
        take(m.capturedStart(), m.capturedLength());
    });

    static const QRegularExpression link(QStringLiteral(R"((!?\[)([^\]]*)(\]\([^)]*\)))"));
    each(link, text, [&](const QRegularExpressionMatch &m) {
        if (!isFree(m.capturedStart(), m.capturedLength(1)) || !isFree(m.capturedStart(3), m.capturedLength(3)))
            return;
        merge(m.capturedStart(1), m.capturedLength(1), m_markup);
        merge(m.capturedStart(2), m.capturedLength(2), m_linkText);
        merge(m.capturedStart(3), m.capturedLength(3), m_markup);
        take(m.capturedStart(1), m.capturedLength(1));
        take(m.capturedStart(3), m.capturedLength(3));
    });

    // Emphasis markers must be free; bold ones are hidden from the italic
    // pattern, so `***both***` is bold around italic.
    QString rest = text;
    const auto emphasis = [&](const QRegularExpression &re, int markerLength, const QTextCharFormat &format) {
        each(re, rest, [&](const QRegularExpressionMatch &m) {
            const int open = m.capturedStart();
            const int close = m.capturedEnd() - markerLength;
            if (!isFree(open, markerLength) || !isFree(close, markerLength))
                return;
            merge(open + markerLength, close - open - markerLength, format);
            merge(open, markerLength, m_markup);
            merge(close, markerLength, m_markup);
            rest.replace(open, markerLength, QString(markerLength, QChar::Null));
            rest.replace(close, markerLength, QString(markerLength, QChar::Null));
        });
    };
    static const QRegularExpression boldStars(QStringLiteral(R"((?<![\\*])\*\*(?=\S)(.+?\**)(?<=\S)\*\*)"));
    static const QRegularExpression boldUnderscores(
        QStringLiteral(R"((?<![\\\w])__(?=\S)(.+?_*)(?<=\S)__(?!\w))"));
    static const QRegularExpression italicStar(QStringLiteral(R"((?<![\\*])\*(?![\s*])(.+?)(?<![\s\\*])\*(?!\*))"));
    static const QRegularExpression italicUnderscore(
        QStringLiteral(R"((?<![\\\w])_(?![\s_])(.+?)(?<![\s\\_])_(?!\w))"));
    emphasis(boldStars, 2, m_bold);
    emphasis(boldUnderscores, 2, m_bold);
    emphasis(italicStar, 1, m_italic);
    emphasis(italicUnderscore, 1, m_italic);
}

void Highlighter::merge(int start, int length, const QTextCharFormat &format)
{
    for (int i = start; i < start + length; ++i) {
        QTextCharFormat f = this->format(i);
        f.merge(format);
        setFormat(i, 1, f);
    }
}

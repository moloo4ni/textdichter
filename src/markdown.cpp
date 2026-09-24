#include "markdown.h"

#include <QRegularExpression>

#include <algorithm>
#include <cstdlib>

#include <cmark.h>

namespace markdown {

namespace {

// Raw HTML is part of CommonMark, so it is passed through (CMARK_OPT_UNSAFE).
// The preview is a QTextBrowser, which never runs scripts.
QString render(const QString &source, int options)
{
    const QByteArray utf8 = source.toUtf8();
    char *raw = cmark_markdown_to_html(utf8.constData(), size_t(utf8.size()),
                                       options | CMARK_OPT_UNSAFE);
    QString html = QString::fromUtf8(raw);
    std::free(raw);
    return html;
}

} // namespace

QString toHtml(const QString &source)
{
    return render(source, CMARK_OPT_DEFAULT);
}

QString toStandaloneHtml(const QString &source, const QString &title)
{
    return QStringLiteral("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
                          "<title>%1</title>\n</head>\n<body>\n%2</body>\n</html>\n")
        .arg(title.toHtmlEscaped(), toHtml(source));
}

QString toPreviewHtml(const QString &source, QList<int> *anchorLines)
{
    QString html = render(source, CMARK_OPT_SOURCEPOS);

    // QTextBrowser drops data-sourcepos attributes, so the line numbers move into
    // anchors inside leaf blocks. A <li> gets one only when text follows directly:
    // an empty anchor in front of a nested block renders as a blank line.
    static const QRegularExpression leaf(
        R"re(<(p|h[1-6]|pre)((?: [^>]*?)?) data-sourcepos="(\d+):[^"]*"([^>]*)>)re");
    static const QRegularExpression item(
        R"re(<li data-sourcepos="(\d+):[^"]*">(?!\s*<(?:p|ul|ol|pre|blockquote|h\d|hr|div)\b))re");
    static const QRegularExpression rest(R"re( data-sourcepos="[^"]*")re");

    html.replace(leaf, QStringLiteral(R"(<\1\2\4><a name="L\3"></a>)"));
    html.replace(item, QStringLiteral(R"(<li><a name="L\1"></a>)"));
    html.remove(rest);

    // cmark ends code blocks with a newline, which QTextBrowser shows as a blank line.
    html.replace(QLatin1String("\n</code></pre>"), QLatin1String("</code></pre>"));

    if (anchorLines) {
        anchorLines->clear();
        static const QRegularExpression anchor(R"re(<a name="L(\d+)"></a>)re");
        for (const auto &m : anchor.globalMatch(html))
            anchorLines->append(m.captured(1).toInt());
        std::sort(anchorLines->begin(), anchorLines->end());
    }
    return html;
}

} // namespace markdown

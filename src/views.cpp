#include "views.h"

#include "markdown.h"

#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QTextBlock>

Editor::Editor(QWidget *parent)
    : Centered<QPlainTextEdit>(parent)
{
    setFrameShape(QFrame::NoFrame);
    setBaseFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

int Editor::cursorLine() const
{
    return textCursor().blockNumber() + 1;
}

void Editor::scrollToLine(int line)
{
    // QPlainTextEdit scrolls by blocks, so the scroll bar value is a line index.
    verticalScrollBar()->setValue(line - 1);
    if (!viewport()->rect().contains(cursorRect().center()))
        setTextCursor(QTextCursor(document()->findBlockByNumber(line - 1)));
}

Preview::Preview(QWidget *parent)
    : Centered<QTextBrowser>(parent)
{
    setFrameShape(QFrame::NoFrame);
    setOpenLinks(false);
    setBaseFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    applyStyleSheet();
    connect(verticalScrollBar(), &QScrollBar::actionTriggered, this,
            [this] { m_scrolledByUser = true; });
}

void Preview::render(const QString &source, const QUrl &baseUrl)
{
    m_source = source;
    m_baseUrl = baseUrl;
    document()->setBaseUrl(baseUrl);
    setHtml(markdown::toPreviewHtml(source, &m_anchorLines));
}

void Preview::scrollToLine(int line)
{
    auto it = std::upper_bound(m_anchorLines.cbegin(), m_anchorLines.cend(), line);
    if (it != m_anchorLines.cbegin())
        scrollToAnchor(QStringLiteral("L%1").arg(*std::prev(it)));
}

int Preview::topLine() const
{
    // A block of which only the bottom edge is still visible does not count.
    const int top = verticalScrollBar()->value();
    QAbstractTextDocumentLayout *layout = document()->documentLayout();
    QTextBlock first = cursorForPosition(QPoint(0, 0)).block();
    while (first.isValid() && layout->blockBoundingRect(first).bottom() <= top + 2)
        first = first.next();

    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            for (const QString &name : it.fragment().charFormat().anchorNames()) {
                if (name.startsWith(QLatin1Char('L')))
                    return name.mid(1).toInt();
            }
        }
    }
    return 1;
}

QString Preview::styleSheet(const QColor &base, const QColor &text)
{
    const auto mix = [&](qreal t) {
        return QColor::fromRgbF(base.redF() + (text.redF() - base.redF()) * t,
                                base.greenF() + (text.greenF() - base.greenF()) * t,
                                base.blueF() + (text.blueF() - base.blueF()) * t)
            .name();
    };
    // QTextBrowser supports neither border-left nor padding here, so quotes are
    // told apart by a dimmed color and code blocks by a background.
    return QStringLiteral("code, pre { font-family: '%1'; }"
                          "pre { background-color: %2; }"
                          "blockquote { color: %3; }")
        .arg(QFontDatabase::systemFont(QFontDatabase::FixedFont).family(), mix(0.08), mix(0.65));
}

void Preview::changeEvent(QEvent *event)
{
    Centered<QTextBrowser>::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) {
        // The style sheet only applies on setHtml(), so render again.
        applyStyleSheet();
        if (!m_source.isNull()) {
            const int line = topLine();
            render(m_source, m_baseUrl);
            scrollToLine(line);
        }
    }
}

void Preview::applyStyleSheet()
{
    document()->setDefaultStyleSheet(
        styleSheet(palette().color(QPalette::Base), palette().color(QPalette::Text)));
}

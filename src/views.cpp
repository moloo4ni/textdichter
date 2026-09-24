#include "views.h"

#include "editing.h"
#include "highlighter.h"
#include "markdown.h"

#include <QAbstractTextDocumentLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QScrollBar>
#include <QTextBlock>

Editor::Editor(QWidget *parent)
    : Centered<QPlainTextEdit>(parent)
    , m_highlighter(new Highlighter(document()))
{
    setFrameShape(QFrame::NoFrame);
    setBaseFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_highlighter->setPalette(palette());
}

QString Editor::text() const
{
    return document()->toRawText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
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

void Editor::changeEvent(QEvent *event)
{
    Centered<QPlainTextEdit>::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        m_highlighter->setPalette(palette());
}

void Editor::keyPressEvent(QKeyEvent *event)
{
    QTextCursor cursor = textCursor();
    const auto handled = [&](bool done) {
        if (done)
            setTextCursor(cursor);
        return done;
    };
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (modifiers == Qt::NoModifier && handled(editing::continueBlock(cursor)))
            return;
        // Shift+Enter would insert U+2028, which is not a line break in a file.
        if (modifiers == Qt::ShiftModifier) {
            insertPlainText(QStringLiteral("\n"));
            return;
        }
        break;
    case Qt::Key_Tab:
        if (modifiers == Qt::NoModifier && handled(editing::indentListItems(cursor, false)))
            return;
        break;
    case Qt::Key_Backtab:
        if (handled(editing::indentListItems(cursor, true)))
            return;
        break;
    }
    Centered<QPlainTextEdit>::keyPressEvent(event);
}

void Editor::insertFromMimeData(const QMimeData *source)
{
    QTextCursor cursor = textCursor();
    if (source->hasText() && editing::linkSelection(cursor, source->text())) {
        setTextCursor(cursor);
        return;
    }
    Centered<QPlainTextEdit>::insertFromMimeData(source);
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
    // QTextBrowser supports neither border-left nor padding here, so quotes are
    // told apart by a dimmed color and code blocks by a background.
    return QStringLiteral("code, pre { font-family: '%1'; }"
                          "pre { background-color: %2; }"
                          "blockquote { color: %3; }")
        .arg(QFontDatabase::systemFont(QFontDatabase::FixedFont).family(), mix(base, text, 0.08).name(), mix(base, text, 0.65).name());
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

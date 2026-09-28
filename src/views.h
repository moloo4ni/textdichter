#pragma once

#include <QColor>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBrowser>
#include <QtMath>

#include <algorithm>
#include <cmath>

class Highlighter;

// A color between two others: 0 gives `from`, 1 gives `to`.
inline QColor mix(const QColor &from, const QColor &to, qreal t)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t);
}

// Width of the text column, in characters of the monospace font.
constexpr int kColumnChars = 80;

// The monospace font at a text size: it sets the character grid in both modes.
inline QFont gridFont(qreal pointSize)
{
    QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fixed.setPointSizeF(pointSize);
    return fixed;
}

// A text view that keeps its text in a centered column of kColumnChars
// characters and zooms relative to a base font.
template <typename Base>
class Centered : public Base
{
public:
    explicit Centered(QWidget *parent = nullptr)
        : Base(parent)
    {
        // The margins around the column get the text background too, so the
        // column does not look like a card on the window.
        this->setBackgroundRole(QPalette::Base);
        this->setAutoFillBackground(true);
        // The scroll bar comes and goes with the length of the text.
        QObject::connect(this->verticalScrollBar(), &QScrollBar::rangeChanged, this,
                         [this] { updateColumn(); });
    }

    void setBaseFont(const QFont &font)
    {
        m_baseFont = font;
        applyZoom();
    }

    // Zoom in points relative to the base font; 0 resets.
    void setZoom(int points)
    {
        m_zoom = points;
        applyZoom();
    }

    // Just wide enough for the column and a scroll bar.
    QSize sizeHint() const override
    {
        return {columnWidth(gridMetrics()) + this->verticalScrollBar()->sizeHint().width(), Base::sizeHint().height()};
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        updateColumn();
        Base::resizeEvent(event);
    }

    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::FontChange)
            updateColumn();
        Base::changeEvent(event);
    }

private:
    void applyZoom()
    {
        QFont font = m_baseFont;
        font.setPointSizeF(std::max(4.0, m_baseFont.pointSizeF() + m_zoom));
        this->setFont(font);
    }

    QFontMetricsF gridMetrics() const { return QFontMetricsF(gridFont(this->font().pointSizeF())); }

    // A line of air around the text, in both modes alike. It is the
    // document's own margin, so it scrolls away with the text.
    static qreal textMargin(const QFontMetricsF &grid) { return std::round(grid.lineSpacing()); }

    // The column is wider than its characters by the margin on both sides.
    static int columnWidth(const QFontMetricsF &grid)
    {
        return qCeil(grid.horizontalAdvance(QLatin1Char('m')) * kColumnChars + 2 * textMargin(grid));
    }

    void updateColumn()
    {
        const QFontMetricsF grid = gridMetrics();
        const qreal margin = textMargin(grid);
        if (this->document()->documentMargin() != margin)
            this->document()->setDocumentMargin(margin);
        // The column is centered in the window, and a scroll bar takes its
        // width from the right margin rather than from the column. With no
        // room for both, the column moves left.
        const int column = columnWidth(grid);
        const QScrollBar *bar = this->verticalScrollBar();
        const int barWidth = bar->maximum() > bar->minimum() ? bar->sizeHint().width() : 0;
        const int left = std::clamp((this->width() - column) / 2, 0, std::max(0, this->width() - barWidth - column));
        const int right = std::max(0, this->width() - barWidth - column - left);
        const QMargins margins(left, 0, right, 0);
        if (this->viewportMargins() != margins)
            this->setViewportMargins(margins);
    }

    QFont m_baseFont;
    int m_zoom = 0;
};

// The "Code" mode: the Markdown source.
class Editor : public Centered<QPlainTextEdit>
{
public:
    explicit Editor(QWidget *parent = nullptr);

    // The source exactly as typed. toPlainText() is not: it turns non-breaking
    // spaces into spaces and U+2028 into line breaks.
    QString text() const;

    // Lines are 1-based, as in the source file.
    int cursorLine() const;

    // Puts the line at the top of the view. The cursor stays where it is unless
    // it would end up off screen.
    void scrollToLine(int line);

protected:
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;

private:
    Highlighter *m_highlighter;
};

// The "Preview" mode: the rendered document, read-only.
class Preview : public Centered<QTextBrowser>
{
public:
    explicit Preview(QWidget *parent = nullptr);

    void render(const QString &source, const QUrl &baseUrl);

    // Scrolls to the nearest block at or above the source line.
    void scrollToLine(int line);

    // Source line of the first block at the top of the view.
    int topLine() const;

    // Whether the user scrolled since the last reset. Tracked through
    // QScrollBar::actionTriggered: comparing scroll values does not work,
    // because the document is laid out lazily and moves the scroll bar itself.
    bool scrolledByUser() const { return m_scrolledByUser; }
    void resetScrolledByUser() { m_scrolledByUser = false; }

    // Styles a document for rendered Markdown in the given font and colors.
    // Applies on the next setHtml(). Rich text CSS has no palette() and no em,
    // so colors and sizes are computed here.
    static void styleDocument(QTextDocument *document, const QFont &font, const QColor &base,
                              const QColor &text);

protected:
    void changeEvent(QEvent *event) override;

private:
    void applyStyle();

    QList<int> m_anchorLines;
    QString m_source;
    QUrl m_baseUrl;
    bool m_scrolledByUser = false;
};

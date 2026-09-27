#pragma once

#include <QColor>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBrowser>
#include <QtMath>

#include <algorithm>

class Highlighter;

// A color between two others: 0 gives `from`, 1 gives `to`.
inline QColor mix(const QColor &from, const QColor &to, qreal t)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t);
}

// Width of the text column, in characters of the monospace font.
constexpr int kColumnChars = 72;

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

    void updateColumn()
    {
        QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        fixed.setPointSizeF(this->font().pointSizeF());
        // The document keeps a margin inside the viewport, so the column is
        // wider than its characters by that margin on both sides.
        const qreal chars = QFontMetricsF(fixed).horizontalAdvance(QLatin1Char('m')) * kColumnChars;
        const int column = qCeil(chars + 2 * this->document()->documentMargin());
        // The column is centered in the window, and a scroll bar takes its
        // width from the right margin rather than from the column.
        const QScrollBar *bar = this->verticalScrollBar();
        const int barWidth = bar->maximum() > bar->minimum() ? bar->sizeHint().width() : 0;
        const int left = std::max(0, (this->width() - column) / 2);
        const int right = std::max(0, this->width() - barWidth - column - left);
        this->setViewportMargins(left, 0, right, 0);
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

    // Style sheet for rendered Markdown in the given colors. Rich text CSS has
    // no palette(), so the colors are computed here.
    static QString styleSheet(const QColor &base, const QColor &text);

protected:
    void changeEvent(QEvent *event) override;

private:
    void applyStyleSheet();

    QList<int> m_anchorLines;
    QString m_source;
    QUrl m_baseUrl;
    bool m_scrolledByUser = false;
};

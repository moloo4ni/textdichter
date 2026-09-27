#include "formatbar.h"

#include <QAction>
#include <QHBoxLayout>
#include <QIconEngine>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QToolButton>

FormatBar::FormatBar(const QList<QAction *> &actions, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    // Beside the buttons' own padding, no side margins: the icons line up with
    // the text of the menu items.
    layout->setContentsMargins(0, 3, 0, 3);
    layout->setSpacing(0);
    // The grid the icons are drawn on, so their lines fall on whole pixels;
    // bigger than a menu icon and easier to hit.
    const int size = 24;
    for (QAction *action : actions) {
        auto *button = new QToolButton(this);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setIconSize(QSize(size, size));
        if (action->menu())
            button->setPopupMode(QToolButton::InstantPopup);
        connect(button, &QToolButton::triggered, this, [this](QAction *chosen) {
            if (!chosen->menu())
                emit triggered();
        });
        // Spread over the width of a menu wider than the row.
        layout->addWidget(button, 1, Qt::AlignCenter);
    }
}

QSize FormatBar::sizeHint() const
{
    return fitMenu(QWidget::sizeHint());
}

QSize FormatBar::minimumSizeHint() const
{
    return fitMenu(QWidget::minimumSizeHint());
}

QSize FormatBar::fitMenu(QSize size) const
{
    // QMenu widens a widget by its column of shortcuts, as if the widget had
    // one too, which leaves empty space next to the row. Taken back here, the
    // menu is as wide as the row or its items, whichever is wider.
    const auto *menu = qobject_cast<const QMenu *>(parentWidget());
    if (!menu)
        return size;
    const QFontMetrics metrics(menu->font());
    int shortcuts = 0;
    for (const QAction *action : menu->actions()) {
        if (action->isVisible() && !action->shortcut().isEmpty())
            shortcuts = std::max(shortcuts, metrics.horizontalAdvance(
                                                action->shortcut().toString(QKeySequence::NativeText)));
    }
    size.setWidth(std::max(0, size.width() - shortcuts));
    return size;
}

namespace {

// Draws the icon anew at each size asked for, so it is never scaled from
// another size, which would blur its lines.
class TintedIconEngine : public QIconEngine
{
public:
    TintedIconEngine(const QString &file, const QPalette &palette)
        : m_source(file)
        , m_palette(palette)
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal scale = painter->device()->devicePixelRatioF();
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State, qreal scale) override
    {
        // The icons are black shapes, painted over in the text color.
        QPixmap pixmap = m_source.pixmap(size, scale);
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), m_palette.color(mode == QIcon::Disabled ? QPalette::Disabled
                                                                                 : QPalette::Active,
                                                        QPalette::WindowText));
        return pixmap;
    }

    QIconEngine *clone() const override { return new TintedIconEngine(*this); }

private:
    QIcon m_source;
    QPalette m_palette;
};

} // namespace

QIcon formatIcon(const QString &file, const QPalette &palette)
{
    return QIcon(new TintedIconEngine(file, palette));
}

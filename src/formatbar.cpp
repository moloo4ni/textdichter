#include "formatbar.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QToolButton>

FormatBar::FormatBar(const QList<QAction *> &actions, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    // Toolbar-sized icons are easier to hit.
    const int size = style()->pixelMetric(QStyle::PM_ToolBarIconSize, nullptr, this);
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

QIcon formatIcon(const QString &file, const QPalette &palette)
{
    // The icons are black shapes, painted over in the text color.
    const QIcon source(file);
    QIcon icon;
    for (const int size : {16, 22, 32, 44, 64}) {
        for (const auto &[mode, group] : {std::pair(QIcon::Normal, QPalette::Active),
                                          std::pair(QIcon::Disabled, QPalette::Disabled)}) {
            QPixmap pixmap = source.pixmap(QSize(size, size), 1.0);
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), palette.color(group, QPalette::WindowText));
            painter.end();
            icon.addPixmap(pixmap, mode);
        }
    }
    return icon;
}

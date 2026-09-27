#include "formatbar.h"

#include <QAction>
#include <QGridLayout>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QToolButton>

FormatBar::FormatBar(const QList<QList<QAction *>> &rows, QWidget *parent)
    : QWidget(parent)
{
    auto *grid = new QGridLayout(this);
    grid->setSpacing(0);
    // Toolbar-sized icons are easier to hit.
    const int size = style()->pixelMetric(QStyle::PM_ToolBarIconSize, nullptr, this);
    int columns = 0;
    for (int row = 0; row < rows.size(); ++row) {
        for (int column = 0; column < rows[row].size(); ++column) {
            QAction *action = rows[row][column];
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
            grid->addWidget(button, row, column);
        }
        columns = std::max(columns, int(rows[row].size()));
    }
    grid->setColumnStretch(columns, 1);
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

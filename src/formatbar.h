#pragma once

#include <QIcon>
#include <QWidget>

class QAction;
class QPalette;

// Rows of icon buttons at the top of the editor's context menu. Each button
// stands for an existing action, with its icon, name and shortcut; an action
// with a menu, such as the headings, opens that menu.
class FormatBar : public QWidget
{
    Q_OBJECT

public:
    explicit FormatBar(const QList<QList<QAction *>> &rows, QWidget *parent = nullptr);

signals:
    // A command was chosen, so the menu around the bar can close.
    void triggered();
};

// The theme's icon of that name or, if the theme has none, the bundled one
// drawn in the palette's text color.
QIcon formatIcon(const QString &themeName, const QString &fallback, const QPalette &palette);

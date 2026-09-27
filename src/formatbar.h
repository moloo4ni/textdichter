#pragma once

#include <QIcon>
#include <QWidget>

class QAction;
class QPalette;

// A row of icon buttons at the top of the editor's context menu. Each button
// stands for an existing action, with its icon, name and shortcut; an action
// with a menu, such as the headings, opens that menu.
class FormatBar : public QWidget
{
    Q_OBJECT

public:
    explicit FormatBar(const QList<QAction *> &actions, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    // A command was chosen, so the menu around the bar can close.
    void triggered();

private:
    QSize fitMenu(QSize size) const;
};

// A bundled icon, drawn in the palette's text color.
QIcon formatIcon(const QString &file, const QPalette &palette);

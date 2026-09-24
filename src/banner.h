#pragma once

#include <QWidget>

#include <functional>
#include <utility>

class QHBoxLayout;
class QLabel;

// An unobtrusive message above the text with a few buttons. Any button closes it.
class Banner : public QWidget
{
    Q_OBJECT

public:
    using Action = std::pair<QString, std::function<void()>>;

    explicit Banner(QWidget *parent = nullptr);

    void showMessage(const QString &text, const QList<Action> &actions);
    QString text() const;

private:
    QLabel *m_label;
    QHBoxLayout *m_buttons;
};

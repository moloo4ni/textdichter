#include "banner.h"

#include "bars.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

Banner::Banner(QWidget *parent)
    : QWidget(parent)
    , m_label(new QLabel(this))
    , m_buttons(new QHBoxLayout)
{
    setBackgroundRole(QPalette::AlternateBase);
    setAutoFillBackground(true);
    m_label->setWordWrap(true);

    auto *layout = new QHBoxLayout(this);
    const QMargins margins = layout->contentsMargins();
    layout->setContentsMargins(barInset(this), margins.top(), barInset(this), margins.bottom());
    layout->addWidget(m_label, 1);
    layout->addLayout(m_buttons);
    hide();
}

void Banner::showMessage(const QString &text, const QList<Action> &actions)
{
    m_label->setText(text);
    // deleteLater(): the button clicked may be the one showing the next message.
    while (QLayoutItem *item = m_buttons->takeAt(0)) {
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }
    for (const auto &[label, action] : actions) {
        auto *button = new QPushButton(label, this);
        // The focus stays in the text: the banner must not get in the way of typing.
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, [this, action] {
            hide();
            action();
        });
        m_buttons->addWidget(button);
    }
    show();
}

QString Banner::text() const
{
    return m_label->text();
}

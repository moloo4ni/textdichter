#include "findbar.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTextEdit>
#include <QToolButton>

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
    , m_find(new QLineEdit(this))
    , m_count(new QLabel(this))
    , m_replaceLabel(new QLabel(tr("Replace:"), this))
    , m_replace(new QLineEdit(this))
    , m_replaceButton(new QPushButton(tr("Replace"), this))
    , m_replaceAllButton(new QPushButton(tr("All"), this))
{
    const auto toolButton = [this](const QString &toolTip) {
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(toolTip);
        return button;
    };
    QToolButton *previous = toolButton(tr("Previous (%1)").arg(QKeySequence(QKeySequence::FindPrevious).toString(QKeySequence::NativeText)));
    previous->setArrowType(Qt::UpArrow);
    QToolButton *next = toolButton(tr("Next (%1)").arg(QKeySequence(QKeySequence::FindNext).toString(QKeySequence::NativeText)));
    next->setArrowType(Qt::DownArrow);
    m_caseButton = toolButton(tr("Match case"));
    m_caseButton->setObjectName(QStringLiteral("matchCase"));
    m_caseButton->setText(QStringLiteral("Aa"));
    m_caseButton->setCheckable(true);
    QToolButton *close = toolButton(tr("Close (%1)").arg(QKeySequence(Qt::Key_Escape).toString(QKeySequence::NativeText)));
    close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));

    // Wide enough for a phrase, not the whole window.
    const int fieldWidth = fontMetrics().averageCharWidth() * 32;
    m_find->setFixedWidth(fieldWidth);
    m_replace->setFixedWidth(fieldWidth);
    m_find->setObjectName(QStringLiteral("find"));
    m_replace->setObjectName(QStringLiteral("replace"));
    m_count->setObjectName(QStringLiteral("count"));
    m_find->installEventFilter(this);
    m_replace->installEventFilter(this);

    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setVerticalSpacing(4);
    layout->addWidget(new QLabel(tr("Find:"), this), 0, 0);
    layout->addWidget(m_find, 0, 1);
    auto *findButtons = new QHBoxLayout;
    findButtons->addWidget(previous);
    findButtons->addWidget(next);
    findButtons->addWidget(m_caseButton);
    findButtons->addWidget(m_count);
    layout->addLayout(findButtons, 0, 2);
    layout->addWidget(close, 0, 4, Qt::AlignRight);
    layout->addWidget(m_replaceLabel, 1, 0);
    layout->addWidget(m_replace, 1, 1);
    auto *replaceButtons = new QHBoxLayout;
    replaceButtons->addWidget(m_replaceButton);
    replaceButtons->addWidget(m_replaceAllButton);
    replaceButtons->addStretch();
    layout->addLayout(replaceButtons, 1, 2);
    layout->setColumnStretch(3, 1);

    connect(m_find, &QLineEdit::textEdited, this, [this] { find({}, true); });
    connect(previous, &QToolButton::clicked, this, &FindBar::findPrevious);
    connect(next, &QToolButton::clicked, this, &FindBar::findNext);
    connect(m_caseButton, &QToolButton::toggled, this, [this] { find({}, true); });
    connect(close, &QToolButton::clicked, this, &FindBar::dismiss);
    connect(m_replaceButton, &QPushButton::clicked, this, &FindBar::replace);
    connect(m_replaceAllButton, &QPushButton::clicked, this, &FindBar::replaceAll);

    setReplaceVisible(false);
    hide();
}

void FindBar::setView(QPlainTextEdit *editor)
{
    m_editor = editor;
    m_textView = nullptr;
    watchDocument();
}

void FindBar::setView(QTextEdit *view)
{
    m_editor = nullptr;
    m_textView = view;
    setReplaceVisible(false);
    watchDocument();
}

void FindBar::showFind()
{
    setReplaceVisible(false);
    open();
}

void FindBar::showReplace()
{
    setReplaceVisible(m_editor != nullptr);
    open();
}

void FindBar::open()
{
    // A short selection is what the user wants to find.
    const QString selection = viewCursor().selectedText();
    if (!selection.isEmpty() && !selection.contains(QChar::ParagraphSeparator) && selection.size() < 200)
        m_find->setText(selection);
    show();
    m_find->setFocus();
    m_find->selectAll();
    updateCount();
}

void FindBar::findNext()
{
    if (m_find->text().isEmpty())
        open();
    else
        find({});
}

void FindBar::findPrevious()
{
    if (m_find->text().isEmpty())
        open();
    else
        find(QTextDocument::FindBackward);
}

bool FindBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (watched == m_replace)
                replace();
            else if (key->modifiers() & Qt::ShiftModifier)
                findPrevious();
            else
                findNext();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QTextDocument *FindBar::document() const
{
    return m_editor ? m_editor->document() : m_textView ? m_textView->document() : nullptr;
}

QTextCursor FindBar::viewCursor() const
{
    return m_editor ? m_editor->textCursor() : m_textView ? m_textView->textCursor() : QTextCursor();
}

void FindBar::setViewCursor(const QTextCursor &cursor)
{
    if (m_editor)
        m_editor->setTextCursor(cursor);
    else if (m_textView)
        m_textView->setTextCursor(cursor);
}

void FindBar::watchDocument()
{
    disconnect(m_documentConnection);
    if (QTextDocument *doc = document()) {
        m_documentConnection = connect(doc, &QTextDocument::contentsChanged, this, [this] {
            if (isVisible())
                updateCount();
        });
    }
    if (isVisible())
        updateCount();
}

QTextDocument::FindFlags FindBar::flags() const
{
    return m_caseButton->isChecked() ? QTextDocument::FindCaseSensitively : QTextDocument::FindFlags();
}

void FindBar::find(QTextDocument::FindFlags direction, bool fromSelectionStart)
{
    QTextDocument *doc = document();
    const QString text = m_find->text();
    if (!doc || text.isEmpty()) {
        updateCount();
        return;
    }

    QTextCursor from = viewCursor();
    // While typing the match grows in place instead of moving on.
    if (fromSelectionStart)
        from.setPosition(from.selectionStart());
    QTextCursor found = doc->find(text, from, flags() | direction);
    if (found.isNull()) {
        QTextCursor edge(doc);
        if (direction & QTextDocument::FindBackward)
            edge.movePosition(QTextCursor::End);
        found = doc->find(text, edge, flags() | direction);
    }
    if (!found.isNull())
        setViewCursor(found);
    updateCount();
}

void FindBar::replace()
{
    QTextCursor cursor = viewCursor();
    const Qt::CaseSensitivity cs = m_caseButton->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (m_editor && !m_find->text().isEmpty() && cursor.selectedText().compare(m_find->text(), cs) == 0) {
        cursor.insertText(m_replace->text());
        setViewCursor(cursor);
    }
    findNext();
}

void FindBar::replaceAll()
{
    QTextDocument *doc = document();
    const QString text = m_find->text();
    if (!m_editor || text.isEmpty())
        return;

    QTextCursor edit(doc);
    edit.beginEditBlock();
    int count = 0;
    for (QTextCursor found = doc->find(text, 0, flags()); !found.isNull();
         found = doc->find(text, found, flags())) {
        found.insertText(m_replace->text());
        ++count;
    }
    edit.endEditBlock();
    m_count->setText(tr("Replaced: %1").arg(count));
}

void FindBar::updateCount()
{
    QTextDocument *doc = document();
    const QString text = m_find->text();
    if (!doc || text.isEmpty()) {
        m_count->clear();
        return;
    }

    const QTextCursor current = viewCursor();
    int total = 0;
    int index = 0;
    for (QTextCursor found = doc->find(text, 0, flags()); !found.isNull();
         found = doc->find(text, found, flags())) {
        ++total;
        if (found.selectionStart() == current.selectionStart() && found.selectionEnd() == current.selectionEnd())
            index = total;
    }
    if (total == 0)
        m_count->setText(tr("No results"));
    else if (index > 0)
        m_count->setText(tr("%1 of %2").arg(index).arg(total));
    else
        m_count->setText(tr("%n match(es)", nullptr, total));
}

void FindBar::setReplaceVisible(bool visible)
{
    for (QWidget *widget : {static_cast<QWidget *>(m_replaceLabel), static_cast<QWidget *>(m_replace),
                            static_cast<QWidget *>(m_replaceButton), static_cast<QWidget *>(m_replaceAllButton)})
        widget->setVisible(visible);
}

void FindBar::dismiss()
{
    hide();
    setReplaceVisible(false);
    emit closed();
}

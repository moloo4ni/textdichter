#pragma once

#include <QTextDocument>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTextEdit;
class QToolButton;

// The find and replace bar under the text. Searches the current view;
// replacing is only offered in the editor.
class FindBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindBar(QWidget *parent = nullptr);

    void setView(QPlainTextEdit *editor);
    void setView(QTextEdit *view);

    void showFind();
    void showReplace();
    void findNext();
    void findPrevious();
    void dismiss();

signals:
    // The view should get the focus back.
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void open();
    QTextDocument *document() const;
    QTextCursor viewCursor() const;
    void setViewCursor(const QTextCursor &cursor);
    void watchDocument();

    QTextDocument::FindFlags flags() const;
    // Searches from the cursor, going round the end of the document.
    void find(QTextDocument::FindFlags direction, bool fromSelectionStart = false);
    void replace();
    void replaceAll();
    void updateCount();
    void setReplaceVisible(bool visible);

    QPlainTextEdit *m_editor = nullptr;
    QTextEdit *m_textView = nullptr;
    QMetaObject::Connection m_documentConnection;

    QLineEdit *m_find;
    QLabel *m_count;
    QToolButton *m_caseButton;
    QLabel *m_replaceLabel;
    QLineEdit *m_replace;
    QPushButton *m_replaceButton;
    QPushButton *m_replaceAllButton;
};

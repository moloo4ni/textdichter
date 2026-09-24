#include "editing.h"
#include "findbar.h"
#include "formatting.h"
#include "highlighter.h"
#include "mainwindow.h"
#include "markdown.h"
#include "textfile.h"
#include "views.h"

#include <QClipboard>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTest>
#include <QTextLayout>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(TESTS_DIR "/fixtures/") + name;
}

QString readFixture(const QString &name)
{
    QString error;
    const auto file = TextFile::read(fixture(name), &error);
    return file ? file->text : QString();
}

QAction *previewAction(QMainWindow &window)
{
    for (QAction *action : window.menuBar()->findChildren<QAction *>()) {
        if (action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Slash))
            return action;
    }
    return nullptr;
}

// Runs a command on text where `|` marks the cursor and `⟨…⟩` the selection,
// and returns the result marked the same way.
// The text of a block styled with the format property, e.g. "**bold**" -> "bold".
QString styled(const QTextBlock &block, const std::function<bool(const QTextCharFormat &)> &has)
{
    QString result;
    for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
        if (has(range.format))
            result += block.text().mid(range.start, range.length);
    }
    return result;
}

bool isBold(const QTextCharFormat &format)
{
    return format.fontWeight() == QFont::Bold;
}

QString edited(QString text, const std::function<void(QTextCursor &)> &command)
{
    const QChar open(0x27E8), close(0x27E9), bar(QLatin1Char('|'));
    int anchor = text.indexOf(open);
    int position;
    if (anchor >= 0) {
        text.remove(anchor, 1);
        position = text.indexOf(close);
        text.remove(position, 1);
    } else {
        anchor = position = text.indexOf(bar);
        text.remove(position, 1);
    }

    QTextDocument doc(text);
    QTextCursor cursor(&doc);
    cursor.setPosition(anchor);
    cursor.setPosition(position, QTextCursor::KeepAnchor);
    command(cursor);

    QString result = doc.toPlainText();
    if (!cursor.hasSelection())
        return result.insert(cursor.position(), bar);
    result.insert(cursor.selectionEnd(), close);
    return result.insert(cursor.selectionStart(), open);
}

} // namespace

class Tests : public QObject
{
    Q_OBJECT

private slots:
    void textFileRoundTrip_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("crlf");
        QTest::addColumn<bool>("bom");
        QTest::newRow("lf") << QByteArray("# Title\n\ntext\n") << false << false;
        QTest::newRow("crlf") << QByteArray("# Title\r\n\r\ntext\r\n") << true << false;
        QTest::newRow("bom") << QByteArray("\xEF\xBB\xBF# Title\n") << false << true;
        QTest::newRow("no final newline") << QByteArray("text") << false << false;
    }

    void textFileRoundTrip()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(bool, crlf);
        QFETCH(bool, bom);

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("file.md"));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(bytes);
        out.close();

        QString error;
        auto file = TextFile::read(path, &error);
        QVERIFY2(file, qPrintable(error));
        QCOMPARE(file->crlf, crlf);
        QCOMPARE(file->bom, bom);
        QVERIFY(!file->text.contains(QLatin1Char('\r')));

        QVERIFY2(file->write(path, &error), qPrintable(error));
        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QCOMPARE(in.readAll(), bytes);
    }

    void textFileRefusesInvalidUtf8()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("latin1.md"));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("caf\xE9\n");
        out.close();

        QString error;
        QVERIFY(!TextFile::read(path, &error));
        QVERIFY(!error.isEmpty());
    }

    void gfmStaysText()
    {
        const QString html = markdown::toHtml(QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |\n\n- [ ] task\n"));
        QVERIFY(!html.contains(QLatin1String("<table")));
        QVERIFY(!html.contains(QLatin1String("<input")));
        QVERIFY(html.contains(QLatin1String("<li>[ ] task</li>")));
    }

    void exportHasNoPreviewMarkup()
    {
        const QString html = markdown::toHtml(QStringLiteral("# Title\n\ntext\n"));
        QCOMPARE(html, QStringLiteral("<h1>Title</h1>\n<p>text</p>\n"));
    }

    void previewAnchors()
    {
        QList<int> lines;
        const QString html = markdown::toPreviewHtml(
            QStringLiteral("# Title\n\ntext\n\n- item\n\n```\ncode\n```\n"), &lines);
        QCOMPARE(lines, (QList<int>{1, 3, 5, 7}));
        QVERIFY(!html.contains(QLatin1String("data-sourcepos")));
        QVERIFY(html.contains(QLatin1String("<p><a name=\"L3\"></a>text</p>")));
        QVERIFY(html.contains(QLatin1String("code</code></pre>"))); // no trailing blank line
    }

    // Code → preview: the preview opens at the nearest block at or above the line.
    // Preview → code: after the user scrolls, the block at the top of the preview
    // lands at the top of the editor.
    void syncViews()
    {
        const QString source = readFixture(QStringLiteral("sync.md"));
        QVERIFY(!source.isEmpty());
        QList<int> anchors;
        markdown::toPreviewHtml(source, &anchors);

        Editor editor;
        Preview preview;
        for (QWidget *view : {static_cast<QWidget *>(&editor), static_cast<QWidget *>(&preview)}) {
            view->resize(900, 700);
            view->show();
        }
        editor.setPlainText(source);
        preview.render(source, QUrl::fromLocalFile(fixture({})));
        QCoreApplication::processEvents();

        const int lines = editor.document()->blockCount();
        for (int line = 1; line <= lines; ++line) {
            preview.scrollToLine(line);
            QCoreApplication::processEvents();
            auto it = std::upper_bound(anchors.cbegin(), anchors.cend(), line);
            const int expected = it == anchors.cbegin() ? 1 : *std::prev(it);
            QScrollBar *bar = preview.verticalScrollBar();
            // Near the end the preview cannot bring the block to the top.
            if (bar->value() != bar->maximum())
                QVERIFY2(preview.topLine() == expected,
                         qPrintable(QStringLiteral("line %1: top L%2, expected L%3")
                                        .arg(line).arg(preview.topLine()).arg(expected)));
        }

        for (int line = 1; line <= lines; line += 10) {
            preview.scrollToLine(line);
            preview.verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepAdd);
            QCoreApplication::processEvents();
            const int top = preview.topLine();
            editor.scrollToLine(top);
            QScrollBar *bar = editor.verticalScrollBar();
            if (bar->value() != bar->maximum())
                QCOMPARE(bar->value() + 1, top);
        }
    }

    // Switching to the preview and back without scrolling must not move anything,
    // even where the preview cannot scroll the block to the top.
    void roundTripKeepsPosition()
    {
        MainWindow window;
        window.resize(900, 700);
        window.openFromCommandLine(fixture(QStringLiteral("sync.md")));
        window.show();
        QCoreApplication::processEvents();

        auto *editor = window.findChild<QPlainTextEdit *>();
        QAction *toggle = previewAction(window);
        QVERIFY(editor && toggle);

        for (int line : {1, 20, 60, 110, 140, 149}) {
            editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(line - 1)));
            editor->ensureCursorVisible();
            const int scroll = editor->verticalScrollBar()->value();
            toggle->trigger();
            QCoreApplication::processEvents();
            QVERIFY(toggle->isChecked());
            toggle->trigger();
            QCoreApplication::processEvents();
            QVERIFY(!toggle->isChecked());
            QCOMPARE(editor->textCursor().blockNumber() + 1, line);
            QCOMPARE(editor->verticalScrollBar()->value(), scroll);
        }
    }

    // On a Russian layout the "/" key types "." — Ctrl+/ must still switch modes,
    // while Ctrl+. on the actual period key must not.
    void previewShortcutOnRussianLayout()
    {
        MainWindow window;
        window.show();
        auto *editor = window.findChild<QPlainTextEdit *>();
        QAction *toggle = previewAction(window);
        QVERIFY(editor && toggle);

        const auto press = [&](quint32 keycode) {
            QKeyEvent event(QEvent::KeyPress, Qt::Key_Period, Qt::ControlModifier, keycode, 0, 0,
                            QStringLiteral("."));
            QApplication::sendEvent(editor, &event);
        };
        press(60); // the period key
        QVERIFY(!toggle->isChecked());
        press(61); // the slash key
        QVERIFY(toggle->isChecked());
    }

    void formatInline_data()
    {
        QTest::addColumn<QString>("marker");
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("wrap") << u"**"_s << u"a ⟨word⟩ b"_s << u"a **⟨word⟩** b"_s;
        QTest::newRow("unwrap outside") << u"**"_s << u"a **⟨word⟩** b"_s << u"a ⟨word⟩ b"_s;
        QTest::newRow("unwrap inside") << u"**"_s << u"a ⟨**word**⟩ b"_s << u"a ⟨word⟩ b"_s;
        QTest::newRow("spaces stay out") << u"*"_s << u"a⟨ word ⟩b"_s << u"a *⟨word⟩* b"_s;
        QTest::newRow("empty pair") << u"`"_s << u"a |"_s << u"a `|`"_s;
        QTest::newRow("remove empty pair") << u"`"_s << u"a `|`"_s << u"a |"_s;
        QTest::newRow("italic inside bold") << u"*"_s << u"**⟨word⟩**"_s << u"***⟨word⟩***"_s;
        QTest::newRow("italic off bold") << u"*"_s << u"***⟨word⟩***"_s << u"**⟨word⟩**"_s;
        QTest::newRow("bold off bold italic") << u"**"_s << u"***⟨word⟩***"_s << u"*⟨word⟩*"_s;
    }

    void formatInline()
    {
        QFETCH(QString, marker);
        QFETCH(QString, before);
        QFETCH(QString, after);
        QCOMPARE(edited(before, [&](QTextCursor &c) { formatting::toggleInline(c, marker); }), after);
    }

    void formatLink()
    {
        const auto link = [](QTextCursor &c) { formatting::insertLink(c); };
        QCOMPARE(edited(u"see ⟨docs⟩"_s, link), u"see [docs](|)"_s);
        QCOMPARE(edited(u"see ⟨https://commonmark.org⟩"_s, link), u"see [|](https://commonmark.org)"_s);
        QCOMPARE(edited(u"see |"_s, link), u"see [](|)"_s);
    }

    void formatHeading()
    {
        const auto heading = [](int level) {
            return [level](QTextCursor &c) { formatting::setHeading(c, level); };
        };
        QCOMPARE(edited(u"Ti|tle"_s, heading(2)), u"## Ti|tle"_s);
        QCOMPARE(edited(u"### Ti|tle"_s, heading(1)), u"# Ti|tle"_s);
        QCOMPARE(edited(u"## Ti|tle"_s, heading(2)), u"Ti|tle"_s);
        QCOMPARE(edited(u"## Ti|tle"_s, heading(0)), u"Ti|tle"_s);
        QCOMPARE(edited(u"|"_s, heading(1)), u"# |"_s);
        QCOMPARE(edited(u"⟨a\n\nb⟩"_s, heading(3)), u"### ⟨a\n\n### b⟩"_s);
        QCOMPARE(edited(u"#hashtag|"_s, heading(1)), u"# #hashtag|"_s);
    }

    void formatLinePrefix()
    {
        using formatting::LinePrefix;
        const auto prefix = [](LinePrefix p) {
            return [p](QTextCursor &c) { formatting::toggleLinePrefix(c, p); };
        };
        QCOMPARE(edited(u"⟨a\nb⟩"_s, prefix(LinePrefix::Quote)), u"> ⟨a\n> b⟩"_s);
        QCOMPARE(edited(u"⟨a\n\nb⟩"_s, prefix(LinePrefix::Quote)), u"> ⟨a\n>\n> b⟩"_s);
        QCOMPARE(edited(u"⟨> a\n>\n> b⟩"_s, prefix(LinePrefix::Quote)), u"⟨a\n\nb⟩"_s);
        QCOMPARE(edited(u"⟨a\nb⟩"_s, prefix(LinePrefix::Bullet)), u"- ⟨a\n- b⟩"_s);
        QCOMPARE(edited(u"⟨- a\n- b⟩"_s, prefix(LinePrefix::Bullet)), u"⟨a\nb⟩"_s);
        QCOMPARE(edited(u"⟨- a\n  - b⟩"_s, prefix(LinePrefix::Numbered)), u"1. ⟨a\n  2. b⟩"_s);
        QCOMPARE(edited(u"⟨1. a\n2. b⟩"_s, prefix(LinePrefix::Numbered)), u"⟨a\nb⟩"_s);
        // The selection ends at the start of u"c"_s, so u"c"_s is not touched.
        QCOMPARE(edited(u"⟨a\nb\n⟩c"_s, prefix(LinePrefix::Bullet)), u"- ⟨a\n- b\n⟩c"_s);
        QCOMPARE(edited(u"---|"_s, prefix(LinePrefix::Bullet)), u"- ---|"_s);
    }

    void formatCodeBlock()
    {
        const auto block = [](QTextCursor &c) { formatting::wrapCodeBlock(c); };
        QCOMPARE(edited(u"a\nx = |1\nb"_s, block), u"a\n```\nx = |1\n```\nb"_s);
        QCOMPARE(edited(u"⟨x\ny⟩"_s, block), u"```\n⟨x\ny⟩\n```"_s);
        QCOMPARE(edited(u"|"_s, block), u"```\n|\n```"_s);
    }

    void formatIsOneUndoStep()
    {
        QTextDocument doc(QStringLiteral("a\nb\nc"));
        QTextCursor cursor(&doc);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        formatting::toggleLinePrefix(cursor, formatting::LinePrefix::Numbered);
        QCOMPARE(doc.toPlainText(), QStringLiteral("1. a\n2. b\n3. c"));
        doc.undo();
        QCOMPARE(doc.toPlainText(), QStringLiteral("a\nb\nc"));
    }

    // Format shortcuts reach the editor and are off in the preview.
    void formatShortcuts()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *editor = window.findChild<QPlainTextEdit *>();
        editor->setFocus();
        editor->setPlainText(QStringLiteral("word"));
        editor->selectAll();
        QTest::keyClick(editor, Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), QStringLiteral("**word**"));
        QTest::keyClick(editor, Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), QStringLiteral("# **word**"));
        QTest::keyClick(editor, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), QStringLiteral("# **[word]()**"));

        previewAction(window)->trigger();
        QTest::keyClick(window.focusWidget() ? window.focusWidget() : &window, Qt::Key_0, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), QStringLiteral("# **[word]()**"));
    }

    void findAndReplace()
    {
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *editor = new QPlainTextEdit(u"one two one ONE"_s);
        auto *bar = new FindBar;
        layout->addWidget(editor);
        layout->addWidget(bar);
        bar->setView(editor);
        window.show();

        auto *find = bar->findChild<QLineEdit *>(u"find"_s);
        auto *replace = bar->findChild<QLineEdit *>(u"replace"_s);
        auto *count = bar->findChild<QLabel *>(u"count"_s);
        const auto selection = [&] {
            return std::pair(editor->textCursor().selectionStart(), editor->textCursor().selectionEnd());
        };

        bar->showReplace();
        QTest::keyClicks(find, u"one"_s);
        QCOMPARE(selection(), std::pair(0, 3));
        QCOMPARE(count->text(), u"1 of 3"_s);
        QTest::keyClick(find, Qt::Key_Return);
        QCOMPARE(selection(), std::pair(8, 11));
        QTest::keyClick(find, Qt::Key_Return);
        QCOMPARE(selection(), std::pair(12, 15));
        QTest::keyClick(find, Qt::Key_Return); // round the end
        QCOMPARE(selection(), std::pair(0, 3));
        QTest::keyClick(find, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(selection(), std::pair(12, 15));

        bar->findChild<QToolButton *>(u"matchCase"_s)->setChecked(true);
        QCOMPARE(count->text(), u"1 of 2"_s);

        QTest::keyClicks(replace, u"1"_s);
        QTest::keyClick(replace, Qt::Key_Return); // replaces the current match, finds the next
        QCOMPARE(editor->toPlainText(), u"1 two one ONE"_s);
        QCOMPARE(selection(), std::pair(6, 9));
        for (QPushButton *button : bar->findChildren<QPushButton *>()) {
            if (button->text() == u"All"_s)
                button->click();
        }
        QCOMPARE(editor->toPlainText(), u"1 two 1 ONE"_s);
        editor->undo(); // "All" is one step
        QCOMPARE(editor->toPlainText(), u"1 two one ONE"_s);
    }

    // Escape closes the bar from the bar and from the text, and the text gets
    // the focus back. There is nothing to replace in the preview.
    void findBarInWindow()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *editor = window.findChild<QPlainTextEdit *>();
        auto *bar = window.findChild<FindBar *>();
        auto *find = bar->findChild<QLineEdit *>(u"find"_s);
        auto *replace = bar->findChild<QLineEdit *>(u"replace"_s);
        editor->setFocus();

        QTest::keyClick(editor, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(bar->isVisible());
        QVERIFY(find->hasFocus());
        QVERIFY(!replace->isVisible());
        QTest::keyClick(find, Qt::Key_Escape);
        QVERIFY(!bar->isVisible());
        QVERIFY(editor->hasFocus());

        QTest::keyClick(editor, Qt::Key_H, Qt::ControlModifier);
        QVERIFY(replace->isVisible());
        editor->setFocus();
        QTest::keyClick(editor, Qt::Key_Escape);
        QVERIFY(!bar->isVisible());

        previewAction(window)->trigger();
        QTest::keyClick(window.focusWidget(), Qt::Key_H, Qt::ControlModifier);
        QVERIFY(!bar->isVisible());
        QTest::keyClick(window.focusWidget(), Qt::Key_F, Qt::ControlModifier);
        QVERIFY(bar->isVisible());
        QVERIFY(!replace->isVisible());
    }

    void continueBlock_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("bullet") << u"- one|"_s << u"- one\n- |"_s;
        QTest::newRow("star, wide spacing") << u"*   one|"_s << u"*   one\n*   |"_s;
        QTest::newRow("numbered") << u"9. nine|"_s << u"9. nine\n10. |"_s;
        QTest::newRow("paren") << u"1) one|"_s << u"1) one\n2) |"_s;
        QTest::newRow("nested") << u"- a\n  - b|"_s << u"- a\n  - b\n  - |"_s;
        QTest::newRow("quote") << u"> text|"_s << u"> text\n> |"_s;
        QTest::newRow("list in quote") << u"> - a|"_s << u"> - a\n> - |"_s;
        QTest::newRow("splits the item") << u"- one| two"_s << u"- one\n- | two"_s;
        QTest::newRow("empty item ends list") << u"- a\n- |"_s << u"- a\n|"_s;
        QTest::newRow("empty item in quote") << u"> - a\n> - |"_s << u"> - a\n> |"_s;
        QTest::newRow("empty quote line ends quote") << u"> a\n> |"_s << u"> a\n|"_s;
    }

    void continueBlock()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        QCOMPARE(edited(before, [](QTextCursor &c) { QVERIFY(editing::continueBlock(c)); }), after);
    }

    void continueBlockDoesNotApply()
    {
        for (const QString &text : {u"plain|"_s, u"-| a"_s, u"---|"_s, u"**bold**|"_s}) {
            QTextDocument doc(QString(text).remove(u'|'));
            QTextCursor cursor(&doc);
            cursor.setPosition(text.indexOf(u'|'));
            QVERIFY2(!editing::continueBlock(cursor), qPrintable(text));
        }
    }

    void indentListItems_data()
    {
        QTest::addColumn<bool>("outdent");
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::newRow("nest under bullet") << false << u"- a\n- b|"_s << u"- a\n  - b|"_s;
        QTest::newRow("nest under number") << false << u"1. a\n2. b|"_s << u"1. a\n   2. b|"_s;
        QTest::newRow("across a blank line") << false << u"- a\n\n- b|"_s << u"- a\n\n  - b|"_s;
        QTest::newRow("first item stays") << false << u"- a|\n- b"_s << u"- a|\n- b"_s;
        QTest::newRow("already nested stays") << false << u"- a\n  - b\n  - |c"_s << u"- a\n  - b\n    - |c"_s;
        QTest::newRow("in quote") << false << u"> - a\n> - b|"_s << u"> - a\n>   - b|"_s;
        QTest::newRow("outdent") << true << u"- a\n  - b|"_s << u"- a\n- b|"_s;
        QTest::newRow("outdent to parent") << true << u"1. a\n   - b\n     - c|"_s << u"1. a\n   - b\n   - c|"_s;
        QTest::newRow("outdent top") << true << u"- a|"_s << u"- a|"_s;
        QTest::newRow("selection") << false << u"- a\n⟨- b\n- c⟩"_s << u"- a\n  ⟨- b\n  - c⟩"_s;
    }

    void indentListItems()
    {
        QFETCH(bool, outdent);
        QFETCH(QString, before);
        QFETCH(QString, after);
        QCOMPARE(edited(before, [&](QTextCursor &c) { QVERIFY(editing::indentListItems(c, outdent)); }), after);
    }

    void linkSelection()
    {
        const auto paste = [](const QString &text) {
            return [text](QTextCursor &c) { editing::linkSelection(c, text); };
        };
        QCOMPARE(edited(u"see ⟨docs⟩"_s, paste(u"https://commonmark.org "_s)), u"see [docs](https://commonmark.org)|"_s);
        QCOMPARE(edited(u"see ⟨docs⟩"_s, paste(u"plain text"_s)), u"see ⟨docs⟩"_s);
        QCOMPARE(edited(u"see |"_s, paste(u"https://commonmark.org"_s)), u"see |"_s);
    }

    // Typing goes through the editor: Enter, Tab, Shift+Tab, Shift+Enter, paste.
    void editorKeys()
    {
        Editor editor;
        editor.show();
        editor.setFocus();
        QTest::keyClicks(&editor, u"- a"_s);
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::keyClicks(&editor, u"b"_s);
        QTest::keyClick(&editor, Qt::Key_Tab);
        QCOMPARE(editor.text(), u"- a\n  - b"_s);
        QTest::keyClick(&editor, Qt::Key_Backtab, Qt::ShiftModifier);
        QCOMPARE(editor.text(), u"- a\n- b"_s);
        QTest::keyClick(&editor, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClick(&editor, Qt::Key_Tab); // not a list item: a tab as usual
        QCOMPARE(editor.text(), u"- a\n- b\n\t"_s);
        editor.undo();
        editor.undo();
        QCOMPARE(editor.text(), u"- a\n- b"_s);

        editor.moveCursor(QTextCursor::End);
        editor.moveCursor(QTextCursor::Left, QTextCursor::KeepAnchor);
        QApplication::clipboard()->setText(u"https://example.org"_s);
        editor.paste();
        QCOMPARE(editor.text(), u"- a\n- [b](https://example.org)"_s);
    }

    // Non-breaking spaces and U+2028 must survive a save.
    void saveKeepsText()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"file.md"_s);
        const QByteArray bytes = "a\xC2\xA0" "b\xE2\x80\xA8" "c\n";
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(bytes);
        out.close();

        MainWindow window;
        window.openFromCommandLine(path);
        auto *editor = window.findChild<QPlainTextEdit *>();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(u"d"_s);
        editor->undo(); // modified, same text
        editor->document()->setModified(true);
        for (QAction *action : window.menuBar()->findChildren<QAction *>()) {
            if (action->shortcut() == QKeySequence::Save)
                action->trigger();
        }
        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QCOMPARE(in.readAll(), bytes);
    }

    void highlighterLines()
    {
        using Kind = Highlighter::Kind;
        const QList<Highlighter::Line> lines = Highlighter::parse(
            u"# a\n```\n# b\n```\ntext\n===\n\n    # c\n> - item\n>   more\n---"_s);
        const QList<Kind> kinds{Kind::Heading, Kind::Fence, Kind::Code, Kind::Fence, Kind::Heading,
                                Kind::HeadingUnderline, Kind::Text, Kind::Code, Kind::Text, Kind::Text,
                                Kind::Break};
        QCOMPARE(lines.size(), kinds.size());
        for (qsizetype i = 0; i < kinds.size(); ++i)
            QVERIFY2(lines[i].kind == kinds[i], qPrintable(QString::number(i)));
        QCOMPARE(lines[8].quoteDepth, 1);
        QCOMPARE(lines[8].marker, 2);
        QCOMPARE(lines[9].marker, -1);
        // An unclosed fence runs to the end.
        QVERIFY(Highlighter::parse(u"```\n# a"_s).at(1).kind == Kind::Code);
    }

    void highlighterStyles()
    {
        Editor editor;
        editor.setPlainText(u"## Title ##\nSome **bold**, *it*, `**code**` and [link](url).\n> quote"_s);
        QTextBlock block = editor.document()->firstBlock();
        QCOMPARE(styled(block, isBold), u"## Title ##"_s);
        const QColor dim = mix(editor.palette().color(QPalette::Base), editor.palette().color(QPalette::Text), 0.45);
        const auto isDim = [&](const QTextCharFormat &f) { return f.foreground().color() == dim; };
        QCOMPARE(styled(block, isDim), u"## ##"_s);

        block = block.next();
        QCOMPARE(styled(block, isBold), u"bold"_s);
        QCOMPARE(styled(block, [](const QTextCharFormat &f) { return f.fontItalic(); }), u"it"_s);
        QCOMPARE(styled(block, [](const QTextCharFormat &f) { return f.hasProperty(QTextFormat::BackgroundBrush); }),
                 u"**code**"_s);
        QCOMPARE(styled(block, isDim), u"******``[](url)"_s);
        QCOMPARE(styled(block.next(), isDim), u"> "_s);
    }

    // Opening a fence restyles every line below; closing it restores them.
    void highlighterFollowsEdits()
    {
        Editor editor;
        editor.setPlainText(u"one\n# two\n# three"_s);
        QTextDocument *doc = editor.document();
        QCOMPARE(styled(doc->lastBlock(), isBold), u"# three"_s);

        QTextCursor cursor(doc);
        cursor.insertText(u"```\n"_s);
        QCOMPARE(styled(doc->findBlockByNumber(2), isBold), QString());
        QCOMPARE(styled(doc->lastBlock(), isBold), QString());

        editor.undo();
        QCOMPARE(styled(doc->lastBlock(), isBold), u"# three"_s);

        // A setext underline makes the line above a heading.
        cursor.setPosition(3);
        cursor.insertText(u"\n==="_s);
        QCOMPARE(styled(doc->firstBlock(), isBold), u"one"_s);
    }

    void newFileFromCommandLine()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("notes.md"));
        MainWindow window;
        window.openFromCommandLine(path);
        QVERIFY(window.windowTitle().startsWith(QLatin1String("notes.md")));
        QVERIFY(!QFile::exists(path)); // created on the first save, not before
    }
};

QTEST_MAIN(Tests)
#include "tests.moc"

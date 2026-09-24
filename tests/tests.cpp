#include "mainwindow.h"
#include "markdown.h"
#include "textfile.h"
#include "views.h"

#include <QMenuBar>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTest>

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

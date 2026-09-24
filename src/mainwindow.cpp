#include "mainwindow.h"

#include "markdown.h"
#include "views.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include <cmark.h>

namespace {

constexpr int kMaxRecentFiles = 10;

const QString kMarkdownFilter = QStringLiteral("Markdown (*.md *.markdown)");

bool isMarkdownPath(const QString &path)
{
    return path.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)
           || path.endsWith(QLatin1String(".markdown"), Qt::CaseInsensitive);
}

// The single local file being dragged, if that is what the drag carries.
QString draggedFile(const QMimeData *mime)
{
    if (!mime->hasUrls() || mime->urls().size() != 1 || !mime->urls().first().isLocalFile())
        return {};
    return mime->urls().first().toLocalFile();
}

int countWords(const QString &text)
{
    static const QRegularExpression word(QStringLiteral(R"([\p{L}\p{N}]+(?:['’-][\p{L}\p{N}]+)*)"));
    int count = 0;
    for (auto it = word.globalMatch(text); it.hasNext(); it.next())
        ++count;
    return count;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_editor(new Editor(this))
    , m_preview(new Preview(this))
    , m_stack(new QStackedWidget(this))
    , m_fileLabel(new QLabel(this))
    , m_infoLabel(new QLabel(this))
    , m_statusTimer(new QTimer(this))
{
    m_stack->addWidget(m_editor);
    m_stack->addWidget(m_preview);
    setCentralWidget(m_stack);

    createMenus();
    createStatusBar();

    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &MainWindow::updateTitle);
    connect(m_editor, &QPlainTextEdit::undoAvailable, this, &MainWindow::updateActions);
    connect(m_editor, &QPlainTextEdit::redoAvailable, this, &MainWindow::updateActions);
    connect(m_preview, &QTextBrowser::anchorClicked, this, &MainWindow::followLink);

    // Counting words on every keystroke is wasteful; wait for a pause.
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(300);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    connect(m_editor, &QPlainTextEdit::textChanged, m_statusTimer, qOverload<>(&QTimer::start));

    // A dropped file opens in this window instead of being inserted as text.
    m_preview->setAcceptDrops(true);
    m_editor->viewport()->installEventFilter(this);
    m_preview->viewport()->installEventFilter(this);
    qApp->installEventFilter(this); // Ctrl+/ on non-US layouts, see eventFilter()

    QSettings settings;
    if (!restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray()))
        resize(900, 700);

    setDocument({}, {});
    updateActions();
}

void MainWindow::openFromCommandLine(const QString &path)
{
    openFile(path, true);
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), QKeySequence::New, this, [this] {
        if (confirmDiscard())
            newDocument();
    });
    file->addAction(tr("&Open…"), QKeySequence::Open, this, &MainWindow::openWithDialog);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::fillRecentMenu);
    file->addSeparator();
    file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(tr("Save &As…"), QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    file->addAction(tr("&Export as HTML…"), this, &MainWindow::exportHtml);
    file->addAction(tr("&Print…"), QKeySequence::Print, this, &MainWindow::print);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    m_undoAction = edit->addAction(tr("&Undo"), QKeySequence::Undo, m_editor, &QPlainTextEdit::undo);
    m_redoAction = edit->addAction(tr("&Redo"), QKeySequence::Redo, m_editor, &QPlainTextEdit::redo);
    edit->addSeparator();
    m_cutAction = edit->addAction(tr("Cu&t"), QKeySequence::Cut, m_editor, &QPlainTextEdit::cut);
    edit->addAction(tr("&Copy"), QKeySequence::Copy, this, [this] {
        if (m_mode == Mode::Code)
            m_editor->copy();
        else
            m_preview->copy();
    });
    m_pasteAction = edit->addAction(tr("&Paste"), QKeySequence::Paste, m_editor, &QPlainTextEdit::paste);
    edit->addAction(tr("Copy as &HTML"), this, &MainWindow::copyAsHtml);
    edit->addSeparator();
    edit->addAction(tr("Select &All"), QKeySequence::SelectAll, this, [this] {
        if (m_mode == Mode::Code)
            m_editor->selectAll();
        else
            m_preview->selectAll();
    });

    QMenu *view = menuBar()->addMenu(tr("&View"));
    m_previewAction = view->addAction(tr("&Preview"));
    m_previewAction->setCheckable(true);
    m_previewAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Slash));
    connect(m_previewAction, &QAction::triggered, this,
            [this](bool checked) { setMode(checked ? Mode::Preview : Mode::Code); });
    view->addSeparator();
    m_statusBarAction = view->addAction(tr("&Status Bar"));
    m_statusBarAction->setCheckable(true);
    connect(m_statusBarAction, &QAction::toggled, this, [this](bool visible) {
        statusBar()->setVisible(visible);
        QSettings().setValue(QStringLiteral("statusBar"), visible);
    });
    view->addSeparator();
    QAction *zoomIn = view->addAction(tr("Zoom &In"), this, [this] { setZoom(m_zoom + 1); });
    zoomIn->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    view->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut, this, [this] { setZoom(m_zoom - 1); });
    // Ctrl+0 is kept free for "Normal text" in the Format menu.
    view->addAction(tr("&Reset Zoom"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_0), this,
                    [this] { setZoom(0); });
    view->addSeparator();
    QAction *fullScreen = view->addAction(tr("&Full Screen"), QKeySequence::FullScreen, this,
                                          [this](bool on) { setWindowState(windowState().setFlag(Qt::WindowFullScreen, on)); });
    fullScreen->setCheckable(true);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("CommonMark &Cheat Sheet"), this, &MainWindow::showCheatSheet);
    help->addAction(tr("&About Textdichter"), this, &MainWindow::showAbout);
}

void MainWindow::createStatusBar()
{
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(m_fileLabel, 1);
    statusBar()->addPermanentWidget(m_infoLabel);

    const bool visible = QSettings().value(QStringLiteral("statusBar"), true).toBool();
    m_statusBarAction->setChecked(visible);
    statusBar()->setVisible(visible);
}

bool MainWindow::confirmDiscard()
{
    if (!m_editor->document()->isModified())
        return true;

    QMessageBox box(QMessageBox::Warning, tr("Unsaved Changes"),
                    tr("Save changes to “%1”?").arg(displayName()), QMessageBox::NoButton, this);
    // Action buttons keep the order they were added in, so "Don't Save" stays
    // right before "Save" whatever the platform's button order.
    QPushButton *dontSave = box.addButton(tr("Do&n't Save"), QMessageBox::ActionRole);
    QPushButton *saveButton = box.addButton(tr("&Save"), QMessageBox::ActionRole);
    QPushButton *cancel = box.addButton(tr("Cancel"), QMessageBox::ActionRole);
    box.setDefaultButton(saveButton);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == saveButton)
        return save();
    return box.clickedButton() == dontSave;
}

void MainWindow::newDocument()
{
    setDocument({}, {});
}

void MainWindow::openWithDialog()
{
    if (!confirmDiscard())
        return;
    const QString dir = m_path.isEmpty() ? QDir::homePath() : QFileInfo(m_path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open"), dir, kMarkdownFilter + QStringLiteral(";;") + tr("All files (*)"));
    if (!path.isEmpty())
        openFile(path, false);
}

void MainWindow::openPath(const QString &path)
{
    if (confirmDiscard())
        openFile(path, false);
}

bool MainWindow::openFile(const QString &path, bool allowNew)
{
    const QFileInfo info(path);
    const QString absolute = info.absoluteFilePath();

    if (!info.exists() && allowNew) {
        setDocument(absolute, {});
        return true;
    }

    QString error;
    std::optional<TextFile> file;
    if (!info.exists())
        error = tr("The file does not exist.");
    else if (info.isDir())
        error = tr("This is a folder, not a file.");
    else
        file = TextFile::read(absolute, &error);

    if (!file) {
        QMessageBox::warning(this, tr("Cannot Open File"),
                             tr("Cannot open “%1”.").arg(info.fileName()) + QStringLiteral("\n\n") + error);
        return false;
    }
    setDocument(absolute, *file);
    addRecent(absolute);
    return true;
}

void MainWindow::setDocument(const QString &path, const TextFile &file)
{
    m_path = path;
    m_file = file;
    m_file.text.clear(); // the editor owns the text
    m_editor->setPlainText(file.text); // also clears undo history and the modified flag
    if (m_mode == Mode::Preview) {
        m_preview->render(file.text, baseUrl());
        m_preview->resetScrolledByUser();
    }
    m_editorScrollOnEntry = 0;
    updateTitle();
    updateStatus();
}

bool MainWindow::save()
{
    return m_path.isEmpty() ? saveAs() : saveTo(m_path);
}

bool MainWindow::saveAs()
{
    QFileDialog dialog(this, tr("Save As"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(kMarkdownFilter);
    dialog.setDefaultSuffix(QStringLiteral("md"));
    if (m_path.isEmpty())
        dialog.setDirectory(QDir::homePath());
    else
        dialog.selectFile(m_path);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return false;
    return saveTo(dialog.selectedFiles().first());
}

bool MainWindow::saveTo(const QString &path)
{
    TextFile file = m_file;
    file.text = m_editor->toPlainText();
    QString error;
    if (!file.write(path, &error)) {
        QMessageBox::warning(this, tr("Cannot Save File"),
                             tr("Cannot save “%1”.").arg(QFileInfo(path).fileName())
                                 + QStringLiteral("\n\n") + error);
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    m_editor->document()->setModified(false);
    addRecent(m_path);
    updateTitle();
    return true;
}

void MainWindow::exportHtml()
{
    const QFileInfo info(m_path);
    const QString suggested = m_path.isEmpty()
                                  ? QDir::home().filePath(tr("Untitled") + QStringLiteral(".html"))
                                  : info.absoluteDir().filePath(info.completeBaseName() + QStringLiteral(".html"));

    QFileDialog dialog(this, tr("Export as HTML"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter(QStringLiteral("HTML (*.html *.htm)"));
    dialog.setDefaultSuffix(QStringLiteral("html"));
    dialog.selectFile(suggested);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;

    const QString path = dialog.selectedFiles().first();
    const QString title = m_path.isEmpty() ? tr("Untitled") : info.completeBaseName();
    const TextFile html{markdown::toStandaloneHtml(m_editor->toPlainText(), title)};
    QString error;
    if (!html.write(path, &error)) {
        QMessageBox::warning(this, tr("Cannot Export"),
                             tr("Cannot save “%1”.").arg(QFileInfo(path).fileName())
                                 + QStringLiteral("\n\n") + error);
    }
}

void MainWindow::copyAsHtml()
{
    // The selection in code mode, the whole document otherwise.
    QString source = m_editor->toPlainText();
    if (m_mode == Mode::Code && m_editor->textCursor().hasSelection())
        source = m_editor->textCursor().selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));

    const QString html = markdown::toHtml(source);
    auto *mime = new QMimeData;
    mime->setHtml(html); // rich text for word processors and mail
    mime->setText(html); // HTML source for plain text fields
    QApplication::clipboard()->setMimeData(mime);
}

void MainWindow::print()
{
    QPrinter printer;
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Paper is white regardless of the screen theme.
    QTextDocument document;
    document.setDefaultFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    document.setDefaultStyleSheet(Preview::styleSheet(Qt::white, Qt::black));
    document.setBaseUrl(baseUrl());
    document.setHtml(markdown::toHtml(m_editor->toPlainText()));
    document.print(&printer);
}

void MainWindow::setMode(Mode mode)
{
    if (mode != m_mode) {
        if (mode == Mode::Preview) {
            const int line = m_editor->cursorLine();
            m_editorScrollOnEntry = m_editor->verticalScrollBar()->value();
            m_preview->render(m_editor->toPlainText(), baseUrl());
            m_stack->setCurrentWidget(m_preview);
            m_preview->scrollToLine(line);
            m_preview->resetScrolledByUser();
            m_preview->setFocus();
        } else {
            // Without scrolling in the preview, going back must not move anything.
            const int line = m_preview->topLine();
            m_stack->setCurrentWidget(m_editor);
            if (m_preview->scrolledByUser())
                m_editor->scrollToLine(line);
            else
                m_editor->verticalScrollBar()->setValue(m_editorScrollOnEntry);
            m_editor->setFocus();
        }
        m_mode = mode;
    }
    updateActions();
    updateStatus();
}

void MainWindow::followLink(const QUrl &url)
{
    // Headings have no ids in CommonMark, but raw HTML anchors may exist.
    if (url.isRelative() && url.path().isEmpty() && url.hasFragment()) {
        m_preview->scrollToAnchor(url.fragment());
        return;
    }

    const QUrl resolved = baseUrl().resolved(url);
    if (resolved.isLocalFile() && isMarkdownPath(resolved.path()))
        openPath(resolved.toLocalFile()); // one document, one window
    else
        QDesktopServices::openUrl(resolved);
}

void MainWindow::setZoom(int points)
{
    m_zoom = points;
    m_editor->setZoom(points);
    m_preview->setZoom(points);
}

void MainWindow::addRecent(const QString &path)
{
    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    settings.setValue(QStringLiteral("recentFiles"), recent.mid(0, kMaxRecentFiles));
}

void MainWindow::fillRecentMenu()
{
    m_recentMenu->clear();
    const QStringList recent = QSettings().value(QStringLiteral("recentFiles")).toStringList();
    if (recent.isEmpty()) {
        m_recentMenu->addAction(tr("No Recent Files"))->setEnabled(false);
        return;
    }
    for (const QString &path : recent) {
        QAction *action = m_recentMenu->addAction(QFileInfo(path).fileName(), this,
                                                  [this, path] { openPath(path); });
        action->setToolTip(path);
    }
    m_recentMenu->setToolTipsVisible(true);
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("&Clear Menu"), this,
                            [] { QSettings().remove(QStringLiteral("recentFiles")); });
}

QString MainWindow::displayName() const
{
    return m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
}

QUrl MainWindow::baseUrl() const
{
    const QString dir = m_path.isEmpty() ? QDir::currentPath() : QFileInfo(m_path).absolutePath();
    return QUrl::fromLocalFile(dir + QLatin1Char('/'));
}

void MainWindow::updateTitle()
{
    const QString mark = m_editor->document()->isModified() ? QStringLiteral("• ") : QString();
    setWindowTitle(mark + displayName() + QStringLiteral(" — Textdichter"));
    m_fileLabel->setText(mark + displayName());
    m_fileLabel->setToolTip(m_path);
}

void MainWindow::updateStatus()
{
    const QString mode = m_mode == Mode::Code ? tr("Code") : tr("Preview");
    m_infoLabel->setText(tr("%n word(s)", nullptr, countWords(m_editor->toPlainText()))
                         + QStringLiteral(" · ") + mode);
}

void MainWindow::updateActions()
{
    const bool code = m_mode == Mode::Code;
    m_undoAction->setEnabled(code && m_editor->document()->isUndoAvailable());
    m_redoAction->setEnabled(code && m_editor->document()->isRedoAvailable());
    m_cutAction->setEnabled(code);
    m_pasteAction->setEnabled(code);

    m_previewAction->setChecked(!code);
}

void MainWindow::showCheatSheet()
{
    const QString sheet = tr(
        "**Headings**  \n"
        "`# Heading 1` … `###### Heading 6`\n\n"
        "**Emphasis**  \n"
        "`*italic*` · `**bold**` · `` `code` ``\n\n"
        "**Links and images**  \n"
        "`[text](https://example.com)` · `<https://example.com>` · `![description](image.png)`\n\n"
        "**Lists**  \n"
        "`- item` · `1. item` — indent a line to the item’s text to nest it\n\n"
        "**Quotes**  \n"
        "`> quoted text`\n\n"
        "**Code blocks**  \n"
        "a line of ```` ``` ```` before and after, or an indent of four spaces\n\n"
        "**Thematic break**  \n"
        "`---` on a line of its own\n\n"
        "**Line break**  \n"
        "end the line with `\\` or two spaces\n\n"
        "**Paragraphs**  \n"
        "separate them with an empty line");

    QDialog dialog(this);
    dialog.setWindowTitle(tr("CommonMark Cheat Sheet"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *browser = new QTextBrowser(&dialog);
    browser->setFrameShape(QFrame::NoFrame);
    browser->document()->setDefaultStyleSheet(
        Preview::styleSheet(palette().color(QPalette::Base), palette().color(QPalette::Text)));
    browser->setHtml(markdown::toHtml(sheet));
    layout->addWidget(browser);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(560, 540);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Textdichter"),
                       tr("<p><b>Textdichter</b> %1</p>"
                          "<p>A light, simple and clean Markdown editor. CommonMark only.</p>"
                          "<p>cmark %2 · Qt %3</p>")
                           .arg(QStringLiteral(TEXTDICHTER_VERSION),
                                QString::fromLatin1(cmark_version_string()),
                                QString::fromLatin1(qVersion())));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    QSettings().setValue(QStringLiteral("geometry"), saveGeometry());
    event->accept();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::KeyPress: {
        // Qt falls back to the Latin key for letter shortcuts (Ctrl+S works on a
        // Russian layout), but not for punctuation: there the "/" key types ".",
        // so Ctrl+/ never matches. Match it by physical key as well: xkb keycode
        // 61 (evdev KEY_SLASH + 8), the same on xcb and Wayland.
        if (!watched->isWidgetType() || static_cast<QWidget *>(watched)->window() != this)
            break;
        const auto *key = static_cast<QKeyEvent *>(event);
        constexpr quint32 kSlashKeycode = 61;
        if (key->nativeScanCode() == kSlashKeycode && key->key() != Qt::Key_Slash
            && (key->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier) {
            m_previewAction->trigger();
            return true;
        }
        break;
    }
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop: {
        if (watched != m_editor->viewport() && watched != m_preview->viewport())
            break;
        auto *drop = static_cast<QDropEvent *>(event);
        const QString path = draggedFile(drop->mimeData());
        if (path.isEmpty())
            break; // ordinary text drags keep working in the editor
        drop->acceptProposedAction();
        if (event->type() == QEvent::Drop) {
            // Not from inside the drop handler: confirmDiscard() may open a dialog.
            QTimer::singleShot(0, this, [this, path] { openPath(path); });
        }
        return true;
    }
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
}

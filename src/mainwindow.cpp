#include "mainwindow.h"

#include "backup.h"
#include "banner.h"
#include "bars.h"
#include "findbar.h"
#include "formatbar.h"
#include "formatting.h"
#include "markdown.h"
#include "views.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtMath>

#include <cmark.h>

#include <algorithm>
#include <functional>
#include <memory>

namespace {

constexpr int kMaxRecentFiles = 10;

const QString kMarkdownFilter = QStringLiteral("Markdown (*.md *.markdown)");

// ~/.config/textdichter/textdichter.conf. The organization is named here rather
// than for the whole application, which would also nest the backup folder.
QSettings settings()
{
    return QSettings(QStringLiteral("textdichter"), QStringLiteral("textdichter"));
}

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

QByteArray hashOf(const QString &text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_editor(new Editor(this))
    , m_preview(new Preview(this))
    , m_stack(new QStackedWidget(this))
    , m_findBar(new FindBar(this))
    , m_banner(new Banner(this))
    , m_fileLabel(new QLabel(this))
    , m_infoLabel(new QLabel(this))
    , m_statusTimer(new QTimer(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_diskTimer(new QTimer(this))
    , m_backupTimer(new QTimer(this))
{
    m_stack->addWidget(m_editor);
    m_stack->addWidget(m_preview);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_banner);
    layout->addWidget(m_stack);
    layout->addWidget(m_findBar);
    setCentralWidget(central);

    m_findBar->setView(m_editor);
    connect(m_findBar, &FindBar::closed, this, [this] { m_stack->currentWidget()->setFocus(); });
    connect(new QShortcut(Qt::Key_Escape, this), &QShortcut::activated, this, [this] {
        if (m_findBar->isVisible())
            m_findBar->dismiss();
    });

    createMenus();
    m_editor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_editor, &QWidget::customContextMenuRequested, this, &MainWindow::showEditorMenu);
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

    // Editors that save by renaming replace the file, so its folder is watched
    // too. Saving takes a few steps; look once they are done.
    m_diskTimer->setSingleShot(true);
    m_diskTimer->setInterval(200);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, m_diskTimer, qOverload<>(&QTimer::start));
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_diskTimer, qOverload<>(&QTimer::start));
    connect(m_diskTimer, &QTimer::timeout, this, &MainWindow::checkDisk);

    // A backup a few seconds after a change, not on every keystroke.
    m_backupTimer->setObjectName(QStringLiteral("backupTimer"));
    m_backupTimer->setSingleShot(true);
    m_backupTimer->setInterval(3000);
    connect(m_backupTimer, &QTimer::timeout, this, &MainWindow::writeBackup);
    connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this] {
        if (!m_backupTimer->isActive())
            m_backupTimer->start();
    });
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, [this](bool modified) {
        if (modified)
            return;
        m_backupTimer->stop();
        if (m_backup && !m_recoveryPending)
            m_backup->remove();
    });

    // A dropped file opens in this window instead of being inserted as text.
    m_preview->setAcceptDrops(true);
    m_editor->viewport()->installEventFilter(this);
    m_preview->viewport()->installEventFilter(this);
    qApp->installEventFilter(this); // Ctrl+/ on non-US layouts, see eventFilter()

    QSettings settings = ::settings();
    if (!restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray()))
        // As wide as the text column, so a floating window has no empty
        // sides; three quarters of the screen high.
        resize(m_editor->sizeHint().width(), screen()->availableGeometry().height() * 3 / 4);

    setDocument({}, {});
    updateActions();
}

MainWindow::~MainWindow() = default;

void MainWindow::openFromCommandLine(const QString &path)
{
    openFile(path, true);
}

void MainWindow::recoverUntitled()
{
    if (!m_path.isEmpty() || m_editor->document()->isModified())
        return;
    if (auto backup = Backup::takeUntitled()) {
        m_backup = std::move(backup);
        offerRecovery();
    }
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
    m_copyAction = edit->addAction(tr("&Copy"), QKeySequence::Copy, this, [this] {
        if (m_mode == Mode::Code)
            m_editor->copy();
        else
            m_preview->copy();
    });
    m_pasteAction = edit->addAction(tr("&Paste"), QKeySequence::Paste, m_editor, &QPlainTextEdit::paste);
    edit->addAction(tr("Copy as &HTML"), this, &MainWindow::copyAsHtml);
    edit->addSeparator();
    m_selectAllAction = edit->addAction(tr("Select &All"), QKeySequence::SelectAll, this, [this] {
        if (m_mode == Mode::Code)
            m_editor->selectAll();
        else
            m_preview->selectAll();
    });
    edit->addSeparator();
    edit->addAction(tr("&Find…"), QKeySequence::Find, m_findBar, &FindBar::showFind);
    edit->addAction(tr("Find &Next"), QKeySequence::FindNext, m_findBar, &FindBar::findNext);
    edit->addAction(tr("Find Pre&vious"), QKeySequence::FindPrevious, m_findBar, &FindBar::findPrevious);
    m_replaceAction = edit->addAction(tr("R&eplace…"), QKeySequence(Qt::CTRL | Qt::Key_H), m_findBar,
                                      &FindBar::showReplace);

    QMenu *format = menuBar()->addMenu(tr("F&ormat"));
    const auto addFormat = [this](QMenu *menu, const QString &text, const QKeySequence &key,
                                  std::function<void(QTextCursor &)> command) {
        QAction *action = menu->addAction(text, key, this, [this, command] {
            QTextCursor cursor = m_editor->textCursor();
            command(cursor);
            m_editor->setTextCursor(cursor);
        });
        // For the buttons of the context menu, which have no text of their own.
        if (!key.isEmpty())
            action->setToolTip(QStringLiteral("%1 (%2)").arg(action->iconText(),
                                                             key.toString(QKeySequence::NativeText)));
        m_formatActions.append(action);
        return action;
    };
    // Lucide icons, one set whatever the theme. They show in the context menu
    // only; the menu bar stays text.
    const auto setIcon = [this](QAction *action, const QString &name) {
        action->setIconVisibleInMenu(false);
        m_formatIcons.append({action, QStringLiteral(":/icons/format/%1.svg").arg(name)});
    };
    const auto wrap = [](const QString &marker) {
        return [marker](QTextCursor &cursor) { formatting::toggleInline(cursor, marker); };
    };
    const auto prefix = [](formatting::LinePrefix prefix) {
        return [prefix](QTextCursor &cursor) { formatting::toggleLinePrefix(cursor, prefix); };
    };
    QAction *bold = addFormat(format, tr("&Bold"), QKeySequence::Bold, wrap(QStringLiteral("**")));
    QAction *italic = addFormat(format, tr("&Italic"), QKeySequence::Italic, wrap(QStringLiteral("*")));
    QAction *code = addFormat(format, tr("&Code"), QKeySequence(Qt::CTRL | Qt::Key_E), wrap(QStringLiteral("`")));
    QAction *link = addFormat(format, tr("&Link"), QKeySequence(Qt::CTRL | Qt::Key_K), formatting::insertLink);
    format->addSeparator();
    QMenu *heading = format->addMenu(tr("&Heading"));
    for (int level = 1; level <= 6; ++level) {
        addFormat(heading, tr("Heading &%1").arg(level), QKeySequence(Qt::CTRL | (Qt::Key_0 + level)),
                  [level](QTextCursor &cursor) { formatting::setHeading(cursor, level); });
    }
    heading->addSeparator();
    addFormat(heading, tr("&Normal Text"), QKeySequence(Qt::CTRL | Qt::Key_0),
              [](QTextCursor &cursor) { formatting::setHeading(cursor, 0); });
    format->addSeparator();
    QAction *quote = addFormat(format, tr("&Quote"), {}, prefix(formatting::LinePrefix::Quote));
    QAction *bullets = addFormat(format, tr("B&ulleted List"), {}, prefix(formatting::LinePrefix::Bullet));
    QAction *numbers = addFormat(format, tr("&Numbered List"), {}, prefix(formatting::LinePrefix::Numbered));
    QAction *codeBlock = addFormat(format, tr("Code &Block"), {}, formatting::wrapCodeBlock);

    // Both lists behind one button of the context menu, like the headings.
    auto *lists = new QMenu(tr("&List"), this);
    lists->addActions({bullets, numbers});
    setIcon(bold, QStringLiteral("bold"));
    setIcon(italic, QStringLiteral("italic"));
    setIcon(code, QStringLiteral("code"));
    setIcon(link, QStringLiteral("link"));
    setIcon(heading->menuAction(), QStringLiteral("heading"));
    setIcon(quote, QStringLiteral("quote"));
    setIcon(lists->menuAction(), QStringLiteral("list"));
    setIcon(codeBlock, QStringLiteral("code-block"));
    m_formatButtons = {bold, italic, code, link, heading->menuAction(), quote, lists->menuAction(), codeBlock};

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
        settings().setValue(QStringLiteral("statusBar"), visible);
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
    help->addAction(tr("&About"), this, &MainWindow::showAbout);
}

void MainWindow::showEditorMenu(const QPoint &pos)
{
    // A right click outside the selection moves the cursor there first, so
    // the format applies where the click was.
    const QTextCursor clicked = m_editor->cursorForPosition(pos);
    const QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection() || clicked.position() < cursor.selectionStart()
        || clicked.position() > cursor.selectionEnd())
        m_editor->setTextCursor(clicked);

    // Drawn anew each time, in the colors of the current theme.
    for (const auto &[action, file] : std::as_const(m_formatIcons))
        action->setIcon(formatIcon(file, palette()));

    QMenu menu(this);
    auto *bar = new FormatBar(m_formatButtons, &menu);
    connect(bar, &FormatBar::triggered, &menu, &QMenu::close);
    auto *buttons = new QWidgetAction(&menu);
    buttons->setDefaultWidget(bar);
    menu.addAction(buttons);
    menu.addSeparator();
    menu.addActions({m_undoAction, m_redoAction});
    menu.addSeparator();
    menu.addActions({m_cutAction, m_copyAction, m_pasteAction});
    menu.addSeparator();
    menu.addAction(m_selectAllAction);
    menu.exec(m_editor->viewport()->mapToGlobal(pos));
}

void MainWindow::createStatusBar()
{
    statusBar()->setSizeGripEnabled(false);
    // QStatusBar keeps 2 px before its first item already.
    statusBar()->setContentsMargins(barInset(this) - 2, 0, barInset(this), 0);
    statusBar()->addWidget(m_fileLabel, 1);
    statusBar()->addPermanentWidget(m_infoLabel);

    const bool visible = settings().value(QStringLiteral("statusBar"), true).toBool();
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
    if (box.clickedButton() != dontSave)
        return false;
    if (m_backup && !m_recoveryPending)
        m_backup->remove();
    return true;
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

    m_diskHash = QFileInfo::exists(path) ? hashOf(file.text) : QByteArray();
    watchFile();
    m_banner->hide();
    m_recoveryPending = false;
    m_backup = std::make_unique<Backup>(path);
    if (m_backup->exists())
        offerRecovery();
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
    file.text = m_editor->text();
    QString error;
    if (!file.write(path, &error)) {
        QMessageBox::warning(this, tr("Cannot Save File"),
                             tr("Cannot save “%1”.").arg(QFileInfo(path).fileName())
                                 + QStringLiteral("\n\n") + error);
        return false;
    }
    const QString previous = std::exchange(m_path, QFileInfo(path).absoluteFilePath());
    m_diskHash = hashOf(file.text);
    m_editor->document()->setModified(false); // also removes the backup
    if (m_path != previous)
        m_backup = std::make_unique<Backup>(m_path);
    watchFile();
    if (!m_recoveryPending)
        m_banner->hide(); // a change on disk is overwritten now
    addRecent(m_path);
    updateTitle();
    return true;
}

void MainWindow::replaceText(const QString &text)
{
    const int position = m_editor->textCursor().position();
    const int scroll = m_editor->verticalScrollBar()->value();
    QTextCursor cursor(m_editor->document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.setPosition(std::min(position, m_editor->document()->characterCount() - 1));
    m_editor->setTextCursor(cursor);
    m_editor->verticalScrollBar()->setValue(scroll);
    if (m_mode == Mode::Preview) {
        const int line = m_preview->topLine();
        m_preview->render(text, baseUrl());
        m_preview->scrollToLine(line);
    }
    updateStatus();
}

void MainWindow::watchFile()
{
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    if (m_path.isEmpty())
        return;
    m_watcher->addPath(QFileInfo(m_path).absolutePath());
    if (QFileInfo::exists(m_path))
        m_watcher->addPath(m_path);
}

void MainWindow::checkDisk()
{
    if (m_path.isEmpty())
        return;
    if (!QFileInfo::exists(m_path)) {
        if (!m_diskHash.isEmpty()) {
            m_diskHash.clear();
            m_banner->showMessage(tr("The file has been deleted from disk. Saving creates it again."),
                                  {{tr("OK"), [] {}}});
        }
        return;
    }
    // A file replaced by a rename is no longer watched.
    if (!m_watcher->files().contains(m_path))
        m_watcher->addPath(m_path);

    // Only a change of the text counts: not a touch, not our own save.
    QString error;
    const std::optional<TextFile> file = TextFile::read(m_path, &error);
    if (!file)
        return;
    const QByteArray hash = hashOf(file->text);
    if (hash == m_diskHash)
        return;
    m_banner->showMessage(tr("The file has changed on disk."),
                          {{tr("Reload"), [this] { reload(); }},
                           {tr("Ignore"), [this, hash] { m_diskHash = hash; }}});
}

void MainWindow::reload()
{
    QString error;
    const std::optional<TextFile> file = TextFile::read(m_path, &error);
    if (!file) {
        QMessageBox::warning(this, tr("Cannot Open File"),
                             tr("Cannot open “%1”.").arg(QFileInfo(m_path).fileName())
                                 + QStringLiteral("\n\n") + error);
        return;
    }
    m_file.crlf = file->crlf;
    m_file.bom = file->bom;
    m_diskHash = hashOf(file->text);
    // Undo brings back the text as it was before reloading.
    replaceText(file->text);
    m_editor->document()->setModified(false);
}

void MainWindow::offerRecovery()
{
    const QString text = m_backup->text();
    if (text == m_editor->text()) {
        m_backup->remove();
        return;
    }
    m_recoveryPending = true;
    const QString time = QLocale().toString(m_backup->time(), QLocale::ShortFormat);
    m_banner->showMessage(tr("Unsaved changes from %1 were found.").arg(time),
                          {{tr("Restore"),
                            [this, text] {
                                m_recoveryPending = false;
                                replaceText(text);
                            }},
                           {tr("Delete"), [this] {
                                m_recoveryPending = false;
                                m_backup->remove();
                            }}});
}

void MainWindow::writeBackup()
{
    if (m_backup && !m_recoveryPending && m_editor->document()->isModified())
        m_backup->write(m_editor->text());
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
    const TextFile html{markdown::toStandaloneHtml(m_editor->text(), title)};
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
    QString source = m_editor->text();
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
    Preview::styleDocument(&document, QFontDatabase::systemFont(QFontDatabase::GeneralFont), Qt::white,
                           Qt::black);
    document.setBaseUrl(baseUrl());
    document.setHtml(markdown::toHtml(m_editor->text()));
    document.print(&printer);
}

void MainWindow::setMode(Mode mode)
{
    if (mode != m_mode) {
        if (mode == Mode::Preview) {
            const int line = m_editor->cursorLine();
            m_editorScrollOnEntry = m_editor->verticalScrollBar()->value();
            m_preview->render(m_editor->text(), baseUrl());
            m_stack->setCurrentWidget(m_preview);
            m_preview->scrollToLine(line);
            m_preview->resetScrolledByUser();
            m_preview->setFocus();
            m_findBar->setView(m_preview);
        } else {
            // Without scrolling in the preview, going back must not move anything.
            const int line = m_preview->topLine();
            m_stack->setCurrentWidget(m_editor);
            if (m_preview->scrolledByUser())
                m_editor->scrollToLine(line);
            else
                m_editor->verticalScrollBar()->setValue(m_editorScrollOnEntry);
            m_editor->setFocus();
            m_findBar->setView(m_editor);
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
    QSettings settings = ::settings();
    QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    settings.setValue(QStringLiteral("recentFiles"), recent.mid(0, kMaxRecentFiles));
}

void MainWindow::fillRecentMenu()
{
    m_recentMenu->clear();
    const QStringList recent = settings().value(QStringLiteral("recentFiles")).toStringList();
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
                            [] { settings().remove(QStringLiteral("recentFiles")); });
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
    m_infoLabel->setText(tr("%n word(s)", nullptr, countWords(m_editor->text()))
                         + QStringLiteral(" · ") + mode);
}

void MainWindow::updateActions()
{
    const bool code = m_mode == Mode::Code;
    m_undoAction->setEnabled(code && m_editor->document()->isUndoAvailable());
    m_redoAction->setEnabled(code && m_editor->document()->isRedoAvailable());
    m_cutAction->setEnabled(code);
    m_pasteAction->setEnabled(code);
    m_replaceAction->setEnabled(code);
    for (QAction *action : std::as_const(m_formatActions))
        action->setEnabled(code);

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
        "`[text](https://example.com)` · `<https://example.com>`  \n"
        "`![description](image.png)`\n\n"
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
    Preview::styleDocument(browser->document(), browser->font(), palette().color(QPalette::Base),
                           palette().color(QPalette::Text));
    browser->setHtml(markdown::toHtml(sheet));
    layout->addWidget(browser);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    // Tall enough for the whole sheet if the screen allows: a cheat sheet that
    // has to be scrolled is not one. Measured on a copy: until it is shown, the
    // browser lays its own document out for its default size.
    const int width = 560;
    const QMargins margins = layout->contentsMargins();
    const std::unique_ptr<QTextDocument> measure(browser->document()->clone());
    measure->setTextWidth(width - margins.left() - margins.right());
    const int height = qCeil(measure->size().height()) + margins.top() + margins.bottom()
                       + layout->spacing() + buttons->sizeHint().height();
    dialog.resize(width, std::min(height, screen()->availableGeometry().height() * 9 / 10));
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About"),
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
    settings().setValue(QStringLiteral("geometry"), saveGeometry());
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

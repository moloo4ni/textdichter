#pragma once

#include "textfile.h"

#include <QMainWindow>

#include <memory>

class Backup;
class Banner;
class Editor;
class FindBar;
class Preview;
class QAction;
class QFileSystemWatcher;
class QLabel;
class QMenu;
class QStackedWidget;
class QTimer;

// One document, one window: opening or creating a document replaces the
// current one instead of opening another window.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // A path that does not exist yet starts a new document that will be saved
    // under it, as in `textdichter notes.md`.
    void openFromCommandLine(const QString &path);

    // At startup without a file: offers the changes of an untitled document
    // lost in a crash.
    void recoverUntitled();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Mode { Code, Preview };

    void createMenus();
    void createStatusBar();

    // Everything that replaces the document asks confirmDiscard() first.
    bool confirmDiscard();
    void newDocument();
    void openWithDialog();
    void openPath(const QString &path);
    bool openFile(const QString &path, bool allowNew);
    void setDocument(const QString &path, const TextFile &file);
    bool save();
    bool saveAs();
    bool saveTo(const QString &path);
    // Replaces the text in one undo step, keeping the place in the document.
    void replaceText(const QString &text);

    void watchFile();
    void checkDisk();
    void reload();
    void offerRecovery();
    void writeBackup();

    void exportHtml();
    void copyAsHtml();
    void print();

    void setMode(Mode mode);
    void followLink(const QUrl &url);
    void setZoom(int points);

    void addRecent(const QString &path);
    void fillRecentMenu();

    QString displayName() const;
    QUrl baseUrl() const;
    void updateTitle();
    void updateStatus();
    void updateActions();

    void showCheatSheet();
    void showAbout();

    Editor *m_editor;
    Preview *m_preview;
    QStackedWidget *m_stack;
    FindBar *m_findBar;
    Banner *m_banner;

    QLabel *m_fileLabel;
    QLabel *m_infoLabel;
    QTimer *m_statusTimer;
    QFileSystemWatcher *m_watcher;
    QTimer *m_diskTimer;
    QTimer *m_backupTimer;
    QMenu *m_recentMenu = nullptr;

    QAction *m_previewAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_cutAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_replaceAction = nullptr;
    QAction *m_statusBarAction = nullptr;
    QList<QAction *> m_formatActions; // enabled in the code only

    QString m_path; // empty for an untitled document
    TextFile m_file; // line endings and BOM of the open file; text is in the editor
    QByteArray m_diskHash; // of the text last read or written; empty if there is no file
    std::unique_ptr<Backup> m_backup;
    bool m_recoveryPending = false; // a found backup is offered and must not be overwritten
    Mode m_mode = Mode::Code;
    int m_editorScrollOnEntry = 0;
    int m_zoom = 0;
};

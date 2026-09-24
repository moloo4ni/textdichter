#pragma once

#include "textfile.h"

#include <QMainWindow>

class Editor;
class Preview;
class QAction;
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

    // A path that does not exist yet starts a new document that will be saved
    // under it, as in `textdichter notes.md`.
    void openFromCommandLine(const QString &path);

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

    QLabel *m_fileLabel;
    QLabel *m_infoLabel;
    QTimer *m_statusTimer;
    QMenu *m_recentMenu = nullptr;

    QAction *m_previewAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_cutAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_statusBarAction = nullptr;

    QString m_path; // empty for an untitled document
    TextFile m_file; // line endings and BOM of the open file; text is in the editor
    Mode m_mode = Mode::Code;
    int m_editorScrollOnEntry = 0;
    int m_zoom = 0;
};

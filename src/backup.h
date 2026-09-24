#pragma once

#include <QDateTime>
#include <QString>

#include <memory>

class QLockFile;

// A copy of unsaved changes in $XDG_STATE_HOME/textdichter/, so they survive
// a crash. The user's file is never touched.
//
// A lock file marks the backups of running windows. A backup left without its
// lock was left by a crash and can be restored.
class Backup
{
public:
    // A file's backup is found by the file's path; an untitled document gets
    // a new one.
    explicit Backup(const QString &path);
    ~Backup();

    // Takes over the newest backup of an untitled document left by a crash.
    static std::unique_ptr<Backup> takeUntitled();

    static QString directory();

    // Whether there is a backup that this window may restore.
    bool exists() const;
    QString text() const;
    QDateTime time() const;

    // Does nothing while another window has the same file open.
    void write(const QString &text);
    void remove();

private:
    Backup(const QString &name, bool);
    bool lock();

    QString m_file;
    std::unique_ptr<QLockFile> m_lock;
};

#include "backup.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString nameFor(const QString &path)
{
    if (path.isEmpty())
        return QStringLiteral("untitled-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QByteArray hash = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("file-") + QString::fromLatin1(hash.left(16));
}

} // namespace

Backup::Backup(const QString &path)
    : Backup(nameFor(path), true)
{
}

Backup::Backup(const QString &name, bool)
    : m_file(QDir(directory()).filePath(name + QStringLiteral(".md")))
{
    // A backup that is already there is claimed now, so that two windows never
    // offer to restore the same one.
    if (QFile::exists(m_file))
        lock();
}

Backup::~Backup() = default;

std::unique_ptr<Backup> Backup::takeUntitled()
{
    const QFileInfoList files = QDir(directory()).entryInfoList({QStringLiteral("untitled-*.md")}, QDir::Files,
                                                                QDir::Time);
    for (const QFileInfo &file : files) {
        std::unique_ptr<Backup> backup(new Backup(file.completeBaseName(), true));
        if (backup->exists())
            return backup;
    }
    return nullptr;
}

QString Backup::directory()
{
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation);
}

bool Backup::exists() const
{
    return m_lock && m_lock->isLocked() && QFile::exists(m_file);
}

QString Backup::text() const
{
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

QDateTime Backup::time() const
{
    return QFileInfo(m_file).lastModified();
}

void Backup::write(const QString &text)
{
    if (!lock())
        return;
    QSaveFile file(m_file);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(text.toUtf8());
        file.commit();
    }
}

void Backup::remove()
{
    if (!m_lock || !m_lock->isLocked())
        return; // another window's backup
    QFile::remove(m_file);
    m_lock.reset();
}

bool Backup::lock()
{
    if (m_lock && m_lock->isLocked())
        return true;
    QDir().mkpath(directory());
    m_lock = std::make_unique<QLockFile>(m_file + QStringLiteral(".lock"));
    // Only a crash leaves a lock behind: a window may hold one for days.
    m_lock->setStaleLockTime(0);
    return m_lock->tryLock(0);
}

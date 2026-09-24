#pragma once

#include <QCoreApplication>
#include <QString>

#include <optional>

// A text file as it was on disk. The text always uses '\n'; the original line
// endings and byte order mark are restored on write.
struct TextFile
{
    Q_DECLARE_TR_FUNCTIONS(TextFile)

public:
    QString text;
    bool crlf = false;
    bool bom = false;

    // Only UTF-8 is supported. Anything else is refused rather than silently
    // re-encoded on the next save.
    static std::optional<TextFile> read(const QString &path, QString *error);
    bool write(const QString &path, QString *error) const;
};

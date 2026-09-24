#include "textfile.h"

#include <QFile>
#include <QSaveFile>
#include <QStringDecoder>

namespace {

const QByteArray kBom = QByteArrayLiteral("\xEF\xBB\xBF");

} // namespace

std::optional<TextFile> TextFile::read(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return std::nullopt;
    }
    QByteArray bytes = file.readAll();

    TextFile result;
    result.bom = bytes.startsWith(kBom);
    if (result.bom)
        bytes.remove(0, kBom.size());

    QStringDecoder decoder(QStringDecoder::Utf8);
    result.text = decoder(bytes);
    if (decoder.hasError()) {
        *error = tr("The file is not valid UTF-8.");
        return std::nullopt;
    }

    result.crlf = result.text.contains(QLatin1String("\r\n"));
    if (result.crlf)
        result.text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    return result;
}

bool TextFile::write(const QString &path, QString *error) const
{
    QByteArray bytes = crlf ? QString(text).replace('\n', QLatin1String("\r\n")).toUtf8()
                            : text.toUtf8();
    if (bom)
        bytes.prepend(kBom);

    // QSaveFile writes to a temporary file and renames it: a failed save never
    // leaves a half-written file behind.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

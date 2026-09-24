#pragma once

#include <QList>
#include <QString>

namespace markdown {

// Spec-conformant CommonMark HTML, for export and "Copy as HTML".
QString toHtml(const QString &source);

// A complete HTML page for "Export as HTML".
QString toStandaloneHtml(const QString &source, const QString &title);

// HTML for the preview. Leaf blocks carry <a name="L<line>"> anchors with their
// source line, so the preview and the editor can be scrolled to each other.
// The anchor lines are returned sorted.
QString toPreviewHtml(const QString &source, QList<int> *anchorLines);

} // namespace markdown

#pragma once

#include <QList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

// Styles the Markdown source: headings, emphasis, code and links, with the
// markup itself dimmed. The block structure comes from cmark, so a `#` inside
// a code block is not a heading; inline markup is matched line by line.
class Highlighter : public QSyntaxHighlighter
{
public:
    explicit Highlighter(QTextDocument *document);

    void setPalette(const QPalette &palette);

    // What a source line is, as far as styling goes.
    enum class Kind : quint8 { Text, Heading, HeadingUnderline, Fence, Code, Break };
    struct Line
    {
        Kind kind = Kind::Text;
        quint8 quoteDepth = 0;
        qint16 marker = -1; // column of the list marker starting on this line
        bool operator==(const Line &) const = default;
    };
    // One entry per block of the document.
    static QList<Line> parse(const QString &source);

protected:
    void highlightBlock(const QString &text) override;

private:
    void documentChanged(int position, int charsRemoved, int charsAdded);
    void highlightInlines(const QString &text, int from);
    void merge(int start, int length, const QTextCharFormat &format);

    QList<Line> m_lines;
    QTextCharFormat m_markup;
    QTextCharFormat m_heading;
    QTextCharFormat m_bold;
    QTextCharFormat m_italic;
    QTextCharFormat m_code;
    QTextCharFormat m_linkText;
};

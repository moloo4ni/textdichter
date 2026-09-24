#pragma once

#include <QString>

class QTextCursor;

// What typing does in the source beyond inserting characters. Each function
// returns false when it does not apply, and the key or paste works as usual.
namespace editing {

// Enter in a list item or a quote starts the next one; Enter on an empty item
// ends the list.
bool continueBlock(QTextCursor &cursor);

// Tab nests the selected list items under the item above; Shift+Tab (outdent)
// moves them out to the level of their parent.
bool indentListItems(QTextCursor &cursor, bool outdent);

// A URL pasted over a selection makes it a link: `[selection](url)`.
bool linkSelection(QTextCursor &cursor, const QString &pasted);

} // namespace editing

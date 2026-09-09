#ifndef MD_SHORTCUTS_H
#define MD_SHORTCUTS_H

#include "app.h"

G_BEGIN_DECLS

/**
 * Central registration point for all keyboard shortcuts and app actions.
 * Every keybinding in the app lives in the table in shortcuts.c.
 */
void md_shortcuts_register(MdApp* app);

G_END_DECLS

#endif /* MD_SHORTCUTS_H */
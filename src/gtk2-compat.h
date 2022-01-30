/*
 *
 * License: See COPYING file
 *
 */

#ifndef __GTK2_COMPAT_H
#define __GTK2_COMPAT_H

#if (GTK_MAJOR_VERSION == 2)
#define gtk_menu_shell_get_selected_item(mc) mc->active_menu_item
#endif

#endif /* __GTK2_COMPAT_H */

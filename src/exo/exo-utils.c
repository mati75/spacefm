/*-
 * Copyright (c) 2005 Benedikt Meurer <benny@xfce.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

/* implement exo-utils's inline functions */
#define G_IMPLEMENT_INLINES 1

#include <stdbool.h>

#include <glib.h>
#include <gtk/gtk.h>

/**
 * exo_noop_true:
 *
 * This function has no effect, but simply returns
 * the boolean value %TRUE. It is mostly useful in
 * situations where you just need a function that
 * returns %TRUE, but don't want to perform any
 * other actions.
 *
 * Returns: the boolean value %TRUE.
 *
 * Since: 0.3.1
 **/
bool exo_noop_true(void)
{
    return TRUE;
}

/**
 * exo_noop_false:
 *
 * This function has no effect, but simply returns
 * the boolean value %FALSE. It is mostly useful in
 * situations where you just need a function that
 * returns %FALSE, but don't want to perform any
 * other actions.
 *
 * Returns: the boolean value %FALSE.
 *
 * Since: 0.3.1
 **/
bool exo_noop_false(void)
{
    return FALSE;
}

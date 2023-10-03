/**
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <string>

#include <gtkmm.h>
#include <gdkmm.h>
#include <glibmm.h>

#include <ztd/ztd.hxx>
#include <ztd/ztd_logger.hxx>

#include "compat/gtk4-porting.hxx"

#include "settings.hxx"
#include "utils.hxx"

#include "ptk/ptk-keyboard.hxx"

#include "xset/xset.hxx"

#include "xset/xset-keyboard.hxx"

const std::string
xset_get_keyname(xset_t set, i32 key_val, i32 key_mod)
{
    u32 keyval;
    u32 keymod;
    if (set)
    {
        keyval = set->key;
        keymod = set->keymod;
    }
    else
    {
        keyval = key_val;
        keymod = key_mod;
    }
    if (keyval <= 0)
    {
        return "( none )";
    }

    std::string mod = gdk_keyval_name(keyval);
    if (keymod)
    {
        if (keymod & GdkModifierType::GDK_SUPER_MASK)
        {
            mod = std::format("Super+{}", mod);
        }
        if (keymod & GdkModifierType::GDK_HYPER_MASK)
        {
            mod = std::format("Hyper+{}", mod);
        }
        if (keymod & GdkModifierType::GDK_META_MASK)
        {
            mod = std::format("Meta+{}", mod);
        }
        if (keymod & GdkModifierType::GDK_MOD1_MASK)
        {
            mod = std::format("Alt+{}", mod);
        }
        if (keymod & GdkModifierType::GDK_CONTROL_MASK)
        {
            mod = std::format("Ctrl+{}", mod);
        }
        if (keymod & GdkModifierType::GDK_SHIFT_MASK)
        {
            mod = std::format("Shift+{}", mod);
        }
    }
    return mod;
}

static bool
on_set_key_keypress(GtkWidget* widget, GdkEventKey* event, void* user_data)
{
    (void)user_data;

    i32* newkey = (i32*)g_object_get_data(G_OBJECT(widget), "newkey");
    i32* newkeymod = (i32*)g_object_get_data(G_OBJECT(widget), "newkeymod");
    GtkWidget* btn_set = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "btn_set"));

    xset_t set = xset_get(static_cast<const char*>(g_object_get_data(G_OBJECT(widget), "set")));
    assert(set != nullptr);

    if (!event->keyval)
    {
        *newkey = 0;
        *newkeymod = 0;
        gtk_widget_set_sensitive(btn_set, false);
        return true;
    }

    gtk_widget_set_sensitive(btn_set, true);

    const u32 keymod = ptk_get_keymod(event->state);
    if (*newkey != 0 && keymod == 0)
    {
        if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
        {
            // user pressed Enter after selecting a key, so click Set
            gtk_button_clicked(GTK_BUTTON(btn_set));
            return true;
        }
        else if (event->keyval == GDK_KEY_Escape && *newkey == GDK_KEY_Escape)
        {
            // user pressed Escape twice so click Unset
            GtkWidget* btn_unset = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "btn_unset"));
            gtk_button_clicked(GTK_BUTTON(btn_unset));
            return true;
        }
    }

    *newkey = 0;
    *newkeymod = 0;

    xset_t keyset = nullptr;
    if (set->shared_key)
    {
        keyset = xset_get(set->shared_key.value());
    }

    const std::string keyname = xset_get_keyname(nullptr, event->keyval, keymod);

    for (xset_t set2 : xsets)
    {
        assert(set2 != nullptr);
        if (set != set2 && set2->key > 0 && set2->key == event->keyval && set2->keymod == keymod &&
            set2 != keyset)
        {
            std::string name;
            if (set2->menu_label)
            {
                name = clean_label(set2->menu_label.value(), false, false);
            }
            else
            {
                name = "( no name )";
            }

            gtk_message_dialog_format_secondary_text(
                GTK_MESSAGE_DIALOG(widget),
                std::format("\t{}\n\tKeycode: {:#x}  Modifier: {:#x}\n\n{} is already assigned to "
                            "'{}'.\n\nPress a different key or click Set to replace the current "
                            "key assignment.",
                            keyname,
                            event->keyval,
                            keymod,
                            keyname,
                            name)
                    .data(),
                nullptr);

            *newkey = event->keyval;
            *newkeymod = keymod;

            return true;
        }
    }

    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(widget),
        std::format("\t{}\n\tKeycode: {:#x}  Modifier: {:#x}", keyname, event->keyval, keymod)
            .data(),
        nullptr);
    *newkey = event->keyval;
    *newkeymod = keymod;

    return true;
}

void
xset_set_key(GtkWidget* parent, xset_t set)
{
    xset_t keyset;

    std::string name;
    if (set->menu_label)
    {
        name = clean_label(set->menu_label.value(), false, true);
    }
    else if (set->tool > xset::tool::custom)
    {
        name = xset_get_builtin_toolitem_label(set->tool);
    }
    else if (ztd::startswith(set->name, "open_all_type_"))
    {
        keyset = xset_get(xset::name::open_all);
        name = clean_label(keyset->menu_label.value(), false, true);
        set->shared_key = xset::get_name_from_xsetname(xset::name::open_all);
    }
    else
    {
        name = "( no name )";
    }

    GtkWidget* dialog = gtk_message_dialog_new(
        GTK_WINDOW(parent),
        GtkDialogFlags::GTK_DIALOG_MODAL,
        GtkMessageType::GTK_MESSAGE_QUESTION,
        GtkButtonsType::GTK_BUTTONS_NONE,
        std::format("Press your key combination for item '{}' then click Set. "
                    "To remove the current key assignment, click Unset.",
                    name)
            .data(),
        nullptr);

    GtkWidget* btn_cancel = gtk_button_new();
    gtk_button_set_label(GTK_BUTTON(btn_cancel), "Cancel");
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                                 btn_cancel,
                                 GtkResponseType::GTK_RESPONSE_CANCEL);

    GtkWidget* btn_unset = gtk_button_new();
    gtk_button_set_label(GTK_BUTTON(btn_unset), "Unset");
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), btn_unset, GtkResponseType::GTK_RESPONSE_NO);

    GtkWidget* btn_set = gtk_button_new();
    gtk_button_set_label(GTK_BUTTON(btn_set), "Set");
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), btn_set, GtkResponseType::GTK_RESPONSE_OK);
    gtk_widget_set_sensitive(btn_set, false);

    if (set->shared_key)
    {
        keyset = xset_get(set->shared_key.value());
    }
    else
    {
        keyset = set;
    }
    if (keyset->key <= 0)
    {
        gtk_widget_set_sensitive(btn_unset, false);
    }

    u32 newkey = 0;
    u32 newkeymod = 0;

    g_object_set_data(G_OBJECT(dialog), "set", set->name.data());
    g_object_set_data(G_OBJECT(dialog), "newkey", &newkey);
    g_object_set_data(G_OBJECT(dialog), "newkeymod", &newkeymod);
    g_object_set_data(G_OBJECT(dialog), "btn_set", btn_set);
    g_object_set_data(G_OBJECT(dialog), "btn_unset", btn_unset);
    g_signal_connect(dialog, "key_press_event", G_CALLBACK(on_set_key_keypress), nullptr);
    gtk_widget_show_all(dialog);
    gtk_window_set_title(GTK_WINDOW(dialog), "Set Key");

    const auto response = gtk4_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GtkResponseType::GTK_RESPONSE_OK ||
        response == GtkResponseType::GTK_RESPONSE_NO)
    {
        if (response == GtkResponseType::GTK_RESPONSE_OK && (newkey || newkeymod))
        {
            // clear duplicate key assignments
            for (xset_t set2 : xsets)
            {
                assert(set2 != nullptr);

                if (set2 && set2->key > 0 && set2->key == newkey && set2->keymod == newkeymod)
                {
                    set2->key = 0;
                    set2->keymod = 0;
                }
            }
        }
        else if (response == GtkResponseType::GTK_RESPONSE_NO)
        {
            newkey = 0; // unset
            newkeymod = 0;
        }

        // set new key
        if (set->shared_key)
        {
            keyset = xset_get(set->shared_key.value());
        }
        else
        {
            keyset = set;
        }
        keyset->key = newkey;
        keyset->keymod = newkeymod;
    }
}

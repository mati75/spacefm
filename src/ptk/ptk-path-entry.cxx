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

#include <filesystem>

#include <algorithm>

#include <glibmm.h>

#include <magic_enum.hpp>

#include <ztd/ztd.hxx>
#include <ztd/ztd_logger.hxx>

#include "compat/gtk4-porting.hxx"

#include "xset/xset.hxx"

#include "vfs/vfs-user-dirs.hxx"

#include "settings.hxx"

#include "ptk/ptk-keyboard.hxx"

#include "ptk/ptk-path-entry.hxx"

namespace ptk::path_entry
{
    enum column
    {
        name,
        path,
    };
}

EntryData::EntryData(PtkFileBrowser* file_browser)
{
    this->browser = file_browser;
    this->seek_timer = 0;
}

static const std::filesystem::path
get_cwd(GtkEntry* entry)
{
    const char* entry_txt = gtk_entry_get_text(entry);
    if (!entry_txt)
    {
        return vfs::user_dirs->home_dir();
    }

    const std::filesystem::path path = entry_txt;
    if (path.empty())
    {
        return vfs::user_dirs->home_dir();
    }

    if (path.is_absolute())
    {
        return path.parent_path();
    }
    // else
    // {
    //     ztd::logger::warn("entered path in pathbar is invalid: {}", path);
    // }

    return vfs::user_dirs->home_dir();
}

static bool
seek_path(GtkEntry* entry)
{
    if (!GTK_IS_ENTRY(entry))
    {
        return false;
    }

    EntryData* edata = ENTRY_DATA(g_object_get_data(G_OBJECT(entry), "edata"));
    if (!(edata && edata->browser))
    {
        return false;
    }

    if (edata->seek_timer)
    {
        g_source_remove(edata->seek_timer);
        edata->seek_timer = 0;
    }

    if (!xset_get_b(xset::name::path_seek))
    {
        return false;
    }

    const char* entry_txt = gtk_entry_get_text(entry);
    if (!entry_txt)
    {
        return false;
    }
    const std::filesystem::path path = entry_txt;

    // get dir and name prefix
    const auto cwd = get_cwd(entry);
    if (!std::filesystem::is_directory(cwd))
    {
        return false; // entry does not contain a valid dir
    }

    const auto seek_name = path.filename();
    const auto seek_dir = cwd / seek_name;

    // check if path name is unique or part of another path name
    bool is_unique = true;
    if (std::filesystem::is_directory(seek_dir))
    { // complete dir path is in entry - is it unique?
        u32 count = 0;
        for (const auto& file : std::filesystem::directory_iterator(seek_dir))
        {
            if (count == 2)
            {
                // multiple files start with the name that is being types,
                // do not auto change dir to first match
                is_unique = false;
                break;
            }

            const auto file_name = file.path().filename();
            if (ztd::startswith(file_name.string(), seek_name.string()))
            {
                const auto full_path = seek_dir / file_name;
                if (std::filesystem::is_directory(full_path))
                {
                    count++;
                }
            }
        }
    }

    edata->browser->seek_path(is_unique ? seek_dir : "", seek_name);

    return false;
}

static void
seek_path_delayed(GtkEntry* entry, u32 delay = 250)
{
    EntryData* edata = ENTRY_DATA(g_object_get_data(G_OBJECT(entry), "edata"));
    if (!(edata && edata->browser))
    {
        return;
    }

    // user is still typing - restart timer
    if (edata->seek_timer)
    {
        g_source_remove(edata->seek_timer);
    }
    edata->seek_timer = g_timeout_add(delay, (GSourceFunc)seek_path, entry);
}

static bool
match_func(GtkEntryCompletion* completion, const char* key, GtkTreeIter* it, void* user_data)
{
    (void)user_data;

    char* name = nullptr;
    GtkTreeModel* model = gtk_entry_completion_get_model(completion);

    key = (const char*)g_object_get_data(G_OBJECT(completion), "fn");
    gtk_tree_model_get(model, it, ptk::path_entry::column::name, &name, -1);

    if (!name)
    {
        return false;
    }

    if (*key == 0 || g_ascii_strncasecmp(name, key, std::strlen(key)) == 0)
    {
        std::free(name);
        return true;
    }

    std::free(name);
    return false;
}

static void
update_completion(GtkEntry* entry, GtkEntryCompletion* completion)
{
    const char* text = gtk_entry_get_text(entry);
    if (!text)
    {
        return;
    }

    // dir completion
    const std::string fn = ztd::rpartition(text, "/")[2];
    g_object_set_data_full(G_OBJECT(completion), "fn", ztd::strdup(fn), (GDestroyNotify)std::free);

    const std::string cwd = get_cwd(entry);
    const char* old_dir = (const char*)g_object_get_data(G_OBJECT(completion), "cwd");
    if (old_dir && ztd::same(cwd, old_dir))
    {
        return;
    }

    g_object_set_data_full(G_OBJECT(completion),
                           "cwd",
                           ztd::strdup(cwd),
                           (GDestroyNotify)std::free);

    if (std::filesystem::is_directory(cwd))
    {
        std::vector<std::filesystem::path> name_list;
        for (const auto& file : std::filesystem::directory_iterator(cwd))
        {
            const auto file_name = file.path().filename();
            const auto full_path = cwd / file_name;
            if (std::filesystem::is_directory(full_path))
            {
                name_list.emplace_back(full_path);
            }
        }

        std::ranges::sort(name_list);

        GtkListStore* list = GTK_LIST_STORE(gtk_entry_completion_get_model(completion));
        gtk_list_store_clear(list);

        // add sorted list to liststore
        for (const auto& name : name_list)
        {
            const auto disp_name = name.filename();

            GtkTreeIter it;
            gtk_list_store_append(list, &it);
            gtk_list_store_set(list,
                               &it,
                               ptk::path_entry::column::name,
                               disp_name.c_str(),
                               ptk::path_entry::column::path,
                               name.c_str(),
                               -1);
        }

        gtk_entry_completion_set_match_func(completion,
                                            (GtkEntryCompletionMatchFunc)match_func,
                                            nullptr,
                                            nullptr);
    }
    else
    {
        gtk_entry_completion_set_match_func(completion, nullptr, nullptr, nullptr);
    }
}

static void
on_changed(GtkEntry* entry, void* user_data)
{
    (void)user_data;

    GtkEntryCompletion* completion = gtk_entry_get_completion(entry);
    update_completion(entry, completion);
    gtk_entry_completion_complete(gtk_entry_get_completion(GTK_ENTRY(entry)));
    seek_path_delayed(GTK_ENTRY(entry), 0);
}

static void
insert_complete(GtkEntry* entry)
{
    // find a real completion
    const char* prefix = gtk_entry_get_text(GTK_ENTRY(entry));
    if (!prefix)
    {
        return;
    }

    const auto cwd = get_cwd(entry);
    if (!std::filesystem::is_directory(cwd))
    {
        return;
    }

    // find longest common prefix
    i32 count = 0;
    std::filesystem::path last_path;
    std::string prefix_name;
    std::string long_prefix;

    if (!ztd::endswith(prefix, "/"))
    {
        prefix_name = std::filesystem::path(prefix).filename();
    }

    for (const auto& file : std::filesystem::directory_iterator(cwd))
    {
        const auto file_name = file.path().filename();
        const auto full_path = cwd / file_name;

        if (!std::filesystem::is_directory(full_path))
        {
            continue;
        }

        if (prefix_name.empty())
        { // full match
            last_path = full_path;
            if (++count > 1)
            {
                break;
            }
        }
        else if (ztd::startswith(file_name.string(), prefix_name))
        { // prefix matches
            count++;
            if (long_prefix.empty())
            {
                long_prefix = file_name;
            }
            else
            {
                i32 i = 0;
                while (file_name.string()[i] && file_name.string()[i] == long_prefix[i])
                {
                    i++;
                }

                if (i && long_prefix[i])
                {
                    // shorter prefix found
                    long_prefix = file_name;
                }
            }
        }
    }

    std::filesystem::path new_prefix;
    if (prefix_name.empty() && count == 1)
    {
        new_prefix = last_path;
    }
    else
    {
        new_prefix = cwd / long_prefix;
    }

    g_signal_handlers_block_matched(G_OBJECT(entry),
                                    GSignalMatchType::G_SIGNAL_MATCH_FUNC,
                                    0,
                                    0,
                                    nullptr,
                                    (void*)on_changed,
                                    nullptr);
    gtk_entry_set_text(GTK_ENTRY(entry), new_prefix.c_str());
    gtk_editable_set_position(GTK_EDITABLE(entry), -1);
    g_signal_handlers_unblock_matched(G_OBJECT(entry),
                                      GSignalMatchType::G_SIGNAL_MATCH_FUNC,
                                      0,
                                      0,
                                      nullptr,
                                      (void*)on_changed,
                                      nullptr);
}

static bool
on_key_press(GtkWidget* entry, GdkEventKey* event, EntryData* edata)
{
    (void)edata;

    if (event->keyval == GDK_KEY_Tab)
    {
        const u32 keymod = ptk_get_keymod(event->state);
        if (keymod)
        {
            return false;
        }

        insert_complete(GTK_ENTRY(entry));
        on_changed(GTK_ENTRY(entry), nullptr);
        seek_path_delayed(GTK_ENTRY(entry), 10);
        return true;
    }

    return false;
}

static bool
on_insert_prefix(GtkEntryCompletion* completion, char* prefix, GtkWidget* entry)
{
    (void)completion;
    (void)prefix;
    (void)entry;

    // do not use the default handler because it inserts partial names
    return true;
}

static bool
on_match_selected(GtkEntryCompletion* completion, GtkTreeModel* model, GtkTreeIter* iter,
                  GtkWidget* entry)
{
    (void)completion;

    char* path = nullptr;
    gtk_tree_model_get(model, iter, ptk::path_entry::column::path, &path, -1);
    if (path && path[0])
    {
        g_signal_handlers_block_matched(G_OBJECT(entry),
                                        GSignalMatchType::G_SIGNAL_MATCH_FUNC,
                                        0,
                                        0,
                                        nullptr,
                                        (void*)on_changed,
                                        nullptr);

        gtk_entry_set_text(GTK_ENTRY(entry), path);
        std::free(path);
        gtk_editable_set_position(GTK_EDITABLE(entry), -1);

        g_signal_handlers_unblock_matched(G_OBJECT(entry),
                                          GSignalMatchType::G_SIGNAL_MATCH_FUNC,
                                          0,
                                          0,
                                          nullptr,
                                          (void*)on_changed,
                                          nullptr);

        on_changed(GTK_ENTRY(entry), nullptr);
        seek_path_delayed(GTK_ENTRY(entry), 10);
    }

    return true;
}

static bool
on_focus_in(GtkWidget* entry, GdkEventFocus* evt, void* user_data)
{
    (void)evt;
    (void)user_data;

    GtkEntryCompletion* completion = gtk_entry_completion_new();
    GtkListStore* list = gtk_list_store_new(magic_enum::enum_count<ptk::path_entry::column>(),
                                            G_TYPE_STRING,
                                            G_TYPE_STRING);

    gtk_entry_completion_set_minimum_key_length(completion, 1);
    gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(list));
    g_object_unref(list);

    /* gtk_entry_completion_set_text_column( completion, ptk::path_entry::column::path ); */

    // Following line causes GTK3 to show both columns, so skip this and use
    // custom match-selected handler to insert ptk::path_entry::column::path
    // g_object_set(completion, "text-column", ptk::path_entry::column::path, nullptr);
    GtkCellRenderer* render = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start((GtkCellLayout*)completion, render, true);
    gtk_cell_layout_add_attribute((GtkCellLayout*)completion,
                                  render,
                                  "text",
                                  ptk::path_entry::column::name);

    // gtk_entry_completion_set_inline_completion(completion, true);
    gtk_entry_completion_set_popup_set_width(completion, true);
    gtk_entry_set_completion(GTK_ENTRY(entry), completion);

    g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(on_changed), nullptr);
    g_signal_connect(G_OBJECT(completion), "match-selected", G_CALLBACK(on_match_selected), entry);
    g_signal_connect(G_OBJECT(completion), "insert-prefix", G_CALLBACK(on_insert_prefix), entry);

    g_object_unref(completion);

    return false;
}

static bool
on_focus_out(GtkWidget* entry, GdkEventFocus* evt, void* user_data)
{
    (void)evt;
    (void)user_data;

    g_signal_handlers_disconnect_by_func(entry, (void*)on_changed, nullptr);
    gtk_entry_set_completion(GTK_ENTRY(entry), nullptr);

    return false;
}

static void
on_populate_popup(GtkEntry* entry, GtkMenu* menu, PtkFileBrowser* file_browser)
{
    if (!file_browser)
    {
        return;
    }

    xset_t set;

    GtkAccelGroup* accel_group = gtk_accel_group_new();

    set = xset_get(xset::name::separator);
    xset_add_menuitem(file_browser, GTK_WIDGET(menu), accel_group, set);

    const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
    set->disable = !(text && (std::filesystem::exists(text) || ztd::startswith(text, ":/") ||
                              ztd::startswith(text, "//")));
    xset_add_menuitem(file_browser, GTK_WIDGET(menu), accel_group, set);

    set = xset_get(xset::name::path_seek);
    xset_add_menuitem(file_browser, GTK_WIDGET(menu), accel_group, set);
    gtk_widget_show_all(GTK_WIDGET(menu));
}

static void
entry_data_free(EntryData* edata)
{
    delete edata;
}

GtkWidget*
ptk_path_entry_new(PtkFileBrowser* file_browser)
{
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_has_frame(GTK_ENTRY(entry), true);
    gtk_widget_set_size_request(entry, 50, -1);

    const auto edata = new EntryData(file_browser);

    g_signal_connect(entry, "focus-in-event", G_CALLBACK(on_focus_in), nullptr);
    g_signal_connect(entry, "focus-out-event", G_CALLBACK(on_focus_out), nullptr);

    // used to eat the tab key
    g_signal_connect(entry, "key-press-event", G_CALLBACK(on_key_press), edata);

    g_signal_connect(entry, "populate-popup", G_CALLBACK(on_populate_popup), file_browser);

    g_object_weak_ref(G_OBJECT(entry), (GWeakNotify)entry_data_free, edata);
    g_object_set_data(G_OBJECT(entry), "edata", edata);

    return entry;
}

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
#include <string_view>

#include <format>

#include <filesystem>

#include <span>
#include <vector>

#include <gtkmm.h>

#include <ztd/ztd.hxx>
#include <ztd/ztd_logger.hxx>

#include "compat/gtk4-porting.hxx"

#include "vfs/vfs-file-info.hxx"
#include "vfs/vfs-mime-type.hxx"
#include "vfs/vfs-time.hxx"
#include "vfs/vfs-utils.hxx"

#include "ptk/ptk-file-properties.hxx"

struct properties_dialog_data
{
    std::span<const vfs::file_info> file_list{};
    std::filesystem::path cwd{};

    GtkWidget* total_size_label{nullptr};
    GtkWidget* size_on_disk_label{nullptr};
    GtkWidget* count_label{nullptr};

    u64 total_size{0};
    u64 size_on_disk{0};
    u64 total_count_file{0};
    u64 total_count_dir{0};
    bool cancel{false};
    bool done{false};
    GThread* calc_size_thread{nullptr};
    u32 update_label_timer{0};
};

const std::vector<std::filesystem::path>
find_subdirectories(const std::filesystem::path& directory, properties_dialog_data* data)
{
    std::vector<std::filesystem::path> subdirectories;

    if (data->cancel)
    {
        return subdirectories;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (data->cancel)
        {
            return subdirectories;
        }

        if (entry.is_directory() && !entry.is_symlink())
        {
            const std::filesystem::path& subdirectory = entry.path();

            subdirectories.emplace_back(subdirectory);
            const auto nested_subdirectories = find_subdirectories(subdirectory, data);
            subdirectories.insert(subdirectories.cend(),
                                  nested_subdirectories.cbegin(),
                                  nested_subdirectories.cend());
        }
    }

    return subdirectories;
}

// Recursively count total size of all files in the specified directory.
// If the path specified is a file, the size of the file is directly returned.
// cancel is used to cancel the operation. This function will check the value
// pointed by cancel in every iteration. If cancel is set to true, the
// calculation is cancelled.
static void
calc_total_size_of_files(const std::filesystem::path& path, properties_dialog_data* data)
{
    if (data->cancel)
    {
        return;
    }

    const auto file_stat = ztd::statx(path, ztd::statx::symlink::no_follow);
    if (!file_stat)
    {
        return;
    }

    data->total_size += file_stat.size();
    data->size_on_disk += file_stat.size_on_disk();

    if (!std::filesystem::is_directory(path))
    {
        return;
    }

    // recursion time
    std::vector<std::filesystem::path> subdirectories{path};
    const auto found_subdirectories = find_subdirectories(path, data);
    subdirectories.insert(subdirectories.cend(),
                          found_subdirectories.cbegin(),
                          found_subdirectories.cend());

    for (const auto& directory : subdirectories)
    {
        if (data->cancel)
        {
            return;
        }

        for (const auto& directory_file : std::filesystem::directory_iterator(directory))
        {
            if (data->cancel)
            {
                return;
            }

            const auto directory_file_stat =
                ztd::statx(directory_file, ztd::statx::symlink::no_follow);

            data->total_size += directory_file_stat.size();
            data->size_on_disk += directory_file_stat.size_on_disk();
            ++data->total_count_file;
        }
    }
    return;
}

static void*
calc_size(void* user_data)
{
    const auto data = static_cast<properties_dialog_data*>(user_data);
    for (const vfs::file_info file : data->file_list)
    {
        if (data->cancel)
        {
            break;
        }

        if (file->is_directory())
        {
            ++data->total_count_dir;
        }
        else
        {
            ++data->total_count_file;
        }

        const auto path = data->cwd / file->name();
        calc_total_size_of_files(path, data);
    }
    data->done = true;
    return nullptr;
}

static bool
on_update_labels(properties_dialog_data* data)
{
    // need a better thread model for this. all of the
    // data->cancel checks are needed to avoid segfaults when
    // closing the property dialog while this is still running.
    // To reproduce this problem open and close the dialog very fast.
    if (data->cancel)
    {
        return true;
    }

    const auto size_str =
        std::format("{} ( {:L} bytes )", vfs_file_size_format(data->total_size), data->total_size);
    if (data->cancel)
    {
        return true;
    }
    gtk_label_set_text(GTK_LABEL(data->total_size_label), size_str.data());

    const auto disk_str = std::format("{} ( {:L} bytes )",
                                      vfs_file_size_format(data->size_on_disk),
                                      data->size_on_disk);
    if (data->cancel)
    {
        return true;
    }
    gtk_label_set_text(GTK_LABEL(data->size_on_disk_label), disk_str.data());

    const auto count_str =
        std::format("{:L} files, {:L} directories", data->total_count_file, data->total_count_dir);
    if (data->cancel)
    {
        return true;
    }
    gtk_label_set_text(GTK_LABEL(data->count_label), count_str.data());

    if (data->done)
    {
        data->update_label_timer = 0;
    }
    return !data->done;
}

class PropertiesSection
{
  private:
    GtkWidget* box_{nullptr};
    GtkWidget* content_box_{nullptr};

  public:
    PropertiesSection()
    {
        this->content_box_ = gtk_box_new(GtkOrientation::GTK_ORIENTATION_VERTICAL, 6);

        // clang-format off
        GtkWidget* hbox = gtk_box_new(GtkOrientation::GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_box_pack_start(GTK_BOX(hbox), gtk_box_new(GtkOrientation::GTK_ORIENTATION_HORIZONTAL, 0), false, false, 0);
        gtk_box_pack_start(GTK_BOX(hbox), this->content_box_, true, true, 0);
        // clang-format on

        this->box_ = gtk_box_new(GtkOrientation::GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_pack_start(GTK_BOX(this->box_), hbox, false, false, 0);
    }

    void
    new_split_vboxes(GtkWidget** left_box, GtkWidget** right_box)
    {
        *left_box = gtk_box_new(GtkOrientation::GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_set_homogeneous(GTK_BOX(*left_box), false);

        *right_box = gtk_box_new(GtkOrientation::GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_set_homogeneous(GTK_BOX(*right_box), false);

        GtkWidget* hbox = gtk_box_new(GtkOrientation::GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(GTK_BOX(hbox), *left_box, false, false, 0);
        gtk_box_pack_start(GTK_BOX(hbox), *right_box, false, true, 0);
        gtk_box_pack_start(GTK_BOX(this->content_box_), hbox, true, true, 0);

        gtk_widget_set_hexpand(GTK_WIDGET(*right_box), true);
    }

    GtkWidget*
    box()
    {
        return box_;
    }

    GtkWidget*
    contentbox()
    {
        return content_box_;
    }
};

class PropertiesPage
{
  private:
    GtkWidget* box_ = gtk_box_new(GtkOrientation::GTK_ORIENTATION_VERTICAL, 12);
    PropertiesSection section_;

  public:
    PropertiesPage()
    {
        gtk_box_set_homogeneous(GTK_BOX(this->box_), false);
        gtk_box_set_spacing(GTK_BOX(this->box_), 12);
        gtk_container_set_border_width(GTK_CONTAINER(this->box_), 12);

        this->section_ = PropertiesSection();
        gtk_box_pack_start(GTK_BOX(box_), this->section_.box(), false, false, 0);
    }

    void
    add_row(const std::string_view left_item_name, GtkWidget* right_item = nullptr)
    {
        GtkWidget* left_item = gtk_label_new(left_item_name.data());
        PangoAttrList* attr_list = pango_attr_list_new();
        PangoAttribute* attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        pango_attr_list_insert(attr_list, attr);
        gtk_label_set_attributes(GTK_LABEL(left_item), attr_list);
        gtk_label_set_xalign(GTK_LABEL(left_item), 0.0);
        gtk_label_set_yalign(GTK_LABEL(left_item), 0.5);

        if (right_item == nullptr)
        {
            // clang-format off
            gtk_box_pack_start(GTK_BOX(this->section_.contentbox()), GTK_WIDGET(left_item), true, true, 0);
            // clang-format on
        }
        else
        {
            GtkWidget* left_box;
            GtkWidget* right_box;
            this->section_.new_split_vboxes(&left_box, &right_box);
            gtk_box_pack_start(GTK_BOX(left_box), GTK_WIDGET(left_item), true, true, 0);
            gtk_box_pack_start(GTK_BOX(right_box), GTK_WIDGET(right_item), true, true, 0);
        }
    }

    void
    add_row_widget(GtkWidget* item)
    {
        gtk_box_pack_start(GTK_BOX(this->section_.contentbox()), GTK_WIDGET(item), true, true, 0);
    }

    GtkWidget*
    box()
    {
        return this->box_;
    }
};

GtkEntry*
create_prop_text_box(const std::string_view data)
{
    GtkEntry* entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_text(entry, data.data());
    gtk_editable_set_editable(GTK_EDITABLE(entry), false);
    return entry;
}

GtkEntry*
create_prop_text_box_no_focus(const std::string_view data)
{
    GtkEntry* entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_text(entry, data.data());
    gtk_editable_set_editable(GTK_EDITABLE(entry), false);
    gtk_widget_set_can_focus(GTK_WIDGET(entry), false);
    gtk_widget_set_sensitive(GTK_WIDGET(entry), false);
    return entry;
}

GtkEntry*
create_prop_text_box_date(const std::time_t time)
{
    const auto time_formated = vfs_create_display_date(time);

    GtkEntry* entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_text(entry, time_formated.data());
    gtk_editable_set_editable(GTK_EDITABLE(entry), false);
    return entry;
}

GtkWidget*
init_file_info_tab(properties_dialog_data* data, const std::filesystem::path& cwd,
                   const std::span<const vfs::file_info> selected_files)
{
    // FIXME using spaces to align the right GtkWiget with the label
    // This works but should be fixed with an alignment solution

    auto page = PropertiesPage();

    const auto file = selected_files.front();
    const bool multiple_files = selected_files.size() > 1;

    if (multiple_files)
    {
        page.add_row("File Name:   ",
                     GTK_WIDGET(create_prop_text_box_no_focus("( multiple files )")));
    }
    else
    {
        if (file->is_symlink())
        {
            page.add_row("Link Name:   ", GTK_WIDGET(create_prop_text_box(file->name())));
        }
        else if (file->is_directory())
        {
            page.add_row("Directory:   ", GTK_WIDGET(create_prop_text_box(file->name())));
        }
        else
        {
            page.add_row("File Name:   ", GTK_WIDGET(create_prop_text_box(file->name())));
        }
    }

    page.add_row("Location:    ", GTK_WIDGET(create_prop_text_box(cwd.string())));

    if (file->is_symlink())
    {
        std::string target;
        try
        {
            target = std::filesystem::read_symlink(file->path());
            if (!std::filesystem::exists(target))
            {
                target = "( broken link )";
            }
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            target = "( read link error )";
        }

        page.add_row("Link Target: ", GTK_WIDGET(create_prop_text_box(target)));
    }

    vfs::mime_type type;
    vfs::mime_type type2 = nullptr;
    bool same_type = true;
    bool is_dirs = false;
    for (const vfs::file_info selected_file : selected_files)
    {
        type = selected_file->mime_type();
        if (!type2)
        {
            type2 = selected_file->mime_type();
        }
        if (selected_file->is_directory())
        {
            is_dirs = true;
        }
        if (type != type2)
        {
            same_type = false;
        }
        if (is_dirs && !same_type)
        {
            break;
        }
    }
    if (same_type)
    {
        const auto mime = file->mime_type();
        const auto file_type = std::format("{}\n{}", mime->description(), mime->type());
        GtkWidget* type_label = gtk_label_new(file_type.data());
        gtk_label_set_xalign(GTK_LABEL(type_label), 0.0);
        gtk_label_set_yalign(GTK_LABEL(type_label), 0.5);

        page.add_row("Type:        ", GTK_WIDGET(type_label));
    }
    else
    {
        GtkWidget* type_label = gtk_label_new("( multiple types )");
        gtk_label_set_xalign(GTK_LABEL(type_label), 0.0);
        gtk_label_set_yalign(GTK_LABEL(type_label), 0.5);

        page.add_row("Type:        ", GTK_WIDGET(type_label));
    }

    data->total_size_label = gtk_label_new("Calculating...");
    gtk_label_set_xalign(GTK_LABEL(data->total_size_label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(data->total_size_label), 0.5);
    page.add_row("Total Size:  ", GTK_WIDGET(data->total_size_label));

    data->size_on_disk_label = gtk_label_new("Calculating...");
    gtk_label_set_xalign(GTK_LABEL(data->size_on_disk_label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(data->size_on_disk_label), 0.5);
    page.add_row("Size On Disk:", GTK_WIDGET(data->size_on_disk_label));

    data->count_label = gtk_label_new("Calculating...");
    gtk_label_set_xalign(GTK_LABEL(data->count_label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(data->count_label), 0.5);
    page.add_row("Count:       ", GTK_WIDGET(data->count_label));

    bool need_calc_size = true;
    if (!multiple_files && !file->is_directory())
    {
        need_calc_size = false;

        const std::string size =
            std::format("{}  ( {:L} bytes )", file->display_size(), file->size());
        gtk_label_set_text(GTK_LABEL(data->total_size_label), size.data());

        const std::string on_disk =
            std::format("{}  ( {:L} bytes )", file->display_size_on_disk(), file->size_on_disk());
        gtk_label_set_text(GTK_LABEL(data->size_on_disk_label), on_disk.data());

        gtk_label_set_text(GTK_LABEL(data->count_label), "1 file");
    }
    if (need_calc_size)
    {
        data->calc_size_thread = g_thread_new("calc_size", calc_size, data);
        data->update_label_timer = g_timeout_add(250, (GSourceFunc)on_update_labels, data);
    }

    if (multiple_files)
    {
        page.add_row("Accessed:    ",
                     GTK_WIDGET(create_prop_text_box_no_focus("( multiple timestamps )")));
        page.add_row("Created:     ",
                     GTK_WIDGET(create_prop_text_box_no_focus("( multiple timestamps )")));
        page.add_row("Metadata:    ",
                     GTK_WIDGET(create_prop_text_box_no_focus("( multiple timestamps )")));
        page.add_row("Modified:    ",
                     GTK_WIDGET(create_prop_text_box_no_focus("( multiple timestamps )")));
    }
    else
    {
        page.add_row("Accessed:    ", GTK_WIDGET(create_prop_text_box_date(file->atime())));
        page.add_row("Created:     ", GTK_WIDGET(create_prop_text_box_date(file->btime())));
        page.add_row("Metadata:    ", GTK_WIDGET(create_prop_text_box_date(file->ctime())));
        page.add_row("Modified:    ", GTK_WIDGET(create_prop_text_box_date(file->mtime())));
    }

    return page.box();
}

GtkWidget*
init_attributes_tab(properties_dialog_data* data,
                    const std::span<const vfs::file_info> selected_files)
{
    (void)data;

    auto page = PropertiesPage();

    const bool multiple_files = selected_files.size() > 1;
    const auto selected_file = selected_files.front();

    if (multiple_files)
    {
        bool is_same_value_compressed = true;
        bool is_same_value_immutable = true;
        bool is_same_value_append = true;
        bool is_same_value_nodump = true;
        bool is_same_value_encrypted = true;
        bool is_same_value_verity = true;
        bool is_same_value_dax = true;

        // The first file will get checked against itself
        for (const auto file : selected_files)
        {
            if (is_same_value_compressed)
            {
                is_same_value_compressed = selected_file->is_compressed() == file->is_compressed();
            }
            if (is_same_value_immutable)
            {
                is_same_value_immutable = selected_file->is_immutable() == file->is_immutable();
            }
            if (is_same_value_append)
            {
                is_same_value_append = selected_file->is_append() == file->is_append();
            }
            if (is_same_value_nodump)
            {
                is_same_value_nodump = selected_file->is_nodump() == file->is_nodump();
            }
            if (is_same_value_encrypted)
            {
                is_same_value_encrypted = selected_file->is_encrypted() == file->is_encrypted();
            }
            if (is_same_value_verity)
            {
                is_same_value_verity = selected_file->is_verity() == file->is_verity();
            }
            if (is_same_value_dax)
            {
                is_same_value_dax = selected_file->is_dax() == file->is_dax();
            }
        }

        if (is_same_value_compressed)
        {
            GtkWidget* check_button_compressed =
                gtk_check_button_new_with_label(" ( All Selected Files ) ");
            gtk_widget_set_sensitive(GTK_WIDGET(check_button_compressed), false);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_compressed),
                                         selected_file->is_compressed());
            page.add_row("Compressed: ", GTK_WIDGET(check_button_compressed));
        }
        else
        {
            page.add_row("Compressed: ", gtk_label_new(" ( Multiple Values ) "));
        }

        if (is_same_value_immutable)
        {
            GtkWidget* check_button_immutable =
                gtk_check_button_new_with_label(" ( All Selected Files ) ");
            gtk_widget_set_sensitive(GTK_WIDGET(check_button_immutable), false);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_immutable),
                                         selected_file->is_immutable());
            page.add_row("Immutable:  ", GTK_WIDGET(check_button_immutable));
        }
        else
        {
            page.add_row("Immutable:  ", gtk_label_new(" ( Multiple Values ) "));
        }

        if (is_same_value_append)
        {
            GtkWidget* check_button_append =
                gtk_check_button_new_with_label(" ( All Selected Files ) ");
            gtk_widget_set_sensitive(GTK_WIDGET(check_button_append), false);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_append),
                                         selected_file->is_append());
            page.add_row("Append:     ", GTK_WIDGET(check_button_append));
        }
        else
        {
            page.add_row("Append:     ", gtk_label_new(" ( Multiple Values ) "));
        }

        if (is_same_value_nodump)
        {
            GtkWidget* check_button_nodump =
                gtk_check_button_new_with_label(" ( All Selected Files ) ");
            gtk_widget_set_sensitive(GTK_WIDGET(check_button_nodump), false);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_nodump),
                                         selected_file->is_nodump());
            page.add_row("Nodump:     ", GTK_WIDGET(check_button_nodump));
        }
        else
        {
            page.add_row("Nodump:     ", gtk_label_new(" ( Multiple Values ) "));
        }

        if (is_same_value_encrypted)
        {
            GtkWidget* check_button_encrypted =
                gtk_check_button_new_with_label(" ( All Selected Files ) ");
            gtk_widget_set_sensitive(GTK_WIDGET(check_button_encrypted), false);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_encrypted),
                                         selected_file->is_encrypted());
            page.add_row("Encrypted:  ", GTK_WIDGET(check_button_encrypted));
        }
        else
        {
            page.add_row("Encrypted:  ", gtk_label_new(" ( Multiple Values ) "));
        }

        if (is_same_value_verity)
        {
            GtkWidget* check_button_verity =
                gtk_check_button_new_with_label(" ( All Selected Files ) ");
            gtk_widget_set_sensitive(GTK_WIDGET(check_button_verity), false);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_verity),
                                         selected_file->is_verity());
            page.add_row("Verity:     ", GTK_WIDGET(check_button_verity));
        }
        else
        {
            page.add_row("Verity:     ", gtk_label_new(" ( Multiple Values ) "));
        }

        if (is_same_value_dax)
        {
            GtkWidget* check_button_dax =
                gtk_check_button_new_with_label(" ( All Selected Files ) ");
            gtk_widget_set_sensitive(GTK_WIDGET(check_button_dax), false);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_dax),
                                         selected_file->is_dax());
            page.add_row("Dax:        ", GTK_WIDGET(check_button_dax));
        }
        else
        {
            page.add_row("Dax:        ", gtk_label_new(" ( Multiple Values ) "));
        }
    }
    else
    {
        GtkWidget* check_button_compressed = gtk_check_button_new();
        gtk_widget_set_sensitive(GTK_WIDGET(check_button_compressed), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_compressed),
                                     selected_file->is_compressed());
        page.add_row("Compressed: ", GTK_WIDGET(check_button_compressed));

        GtkWidget* check_button_immutable = gtk_check_button_new();
        gtk_widget_set_sensitive(GTK_WIDGET(check_button_immutable), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_immutable),
                                     selected_file->is_immutable());
        page.add_row("Immutable:  ", GTK_WIDGET(check_button_immutable));

        GtkWidget* check_button_append = gtk_check_button_new();
        gtk_widget_set_sensitive(GTK_WIDGET(check_button_append), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_append),
                                     selected_file->is_append());
        page.add_row("Append:     ", GTK_WIDGET(check_button_append));

        GtkWidget* check_button_nodump = gtk_check_button_new();
        gtk_widget_set_sensitive(GTK_WIDGET(check_button_nodump), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_nodump),
                                     selected_file->is_nodump());
        page.add_row("Nodump:     ", GTK_WIDGET(check_button_nodump));

        GtkWidget* check_button_encrypted = gtk_check_button_new();
        gtk_widget_set_sensitive(GTK_WIDGET(check_button_encrypted), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_encrypted),
                                     selected_file->is_encrypted());
        page.add_row("Encrypted:  ", GTK_WIDGET(check_button_encrypted));

        GtkWidget* check_button_verity = gtk_check_button_new();
        gtk_widget_set_sensitive(GTK_WIDGET(check_button_verity), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_verity),
                                     selected_file->is_verity());
        page.add_row("Verity:     ", GTK_WIDGET(check_button_verity));

        GtkWidget* check_button_dax = gtk_check_button_new();
        gtk_widget_set_sensitive(GTK_WIDGET(check_button_dax), false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_dax), selected_file->is_dax());
        page.add_row("Dax:        ", GTK_WIDGET(check_button_dax));
    }

    return page.box();
}

GtkWidget*
init_permissions_tab(properties_dialog_data* data,
                     const std::span<const vfs::file_info> selected_files)
{
    (void)data;

    auto page = PropertiesPage();

    const auto selected_file = selected_files.front();

    // Owner
    GtkWidget* entry_owner = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry_owner), selected_file->display_owner().data());
    gtk_editable_set_editable(GTK_EDITABLE(entry_owner), false);

    page.add_row("Owner:", GTK_WIDGET(entry_owner));

    // Group
    GtkWidget* entry_group = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry_group), selected_file->display_group().data());
    gtk_editable_set_editable(GTK_EDITABLE(entry_group), false);

    page.add_row("Group:", GTK_WIDGET(entry_group));

    // Permissions

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);

    // Create the labels for the first column
    GtkWidget* label_perm_owner = gtk_label_new("Owner:");
    GtkWidget* label_perm_group = gtk_label_new("Group:");
    GtkWidget* label_perm_other = gtk_label_new("Other:");

    gtk_grid_attach(GTK_GRID(grid), label_perm_owner, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_perm_group, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_perm_other, 0, 2, 1, 1);

    // Create the permissions checked buttons

    // Owner
    GtkWidget* check_button_owner_read = gtk_check_button_new_with_label("Read");
    GtkWidget* check_button_owner_write = gtk_check_button_new_with_label("Write");
    GtkWidget* check_button_owner_exec = gtk_check_button_new_with_label("Execute");
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_owner_read), false);
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_owner_write), false);
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_owner_exec), false);
    gtk_grid_attach(GTK_GRID(grid), check_button_owner_read, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_button_owner_write, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_button_owner_exec, 3, 0, 1, 1);

    // Group
    GtkWidget* check_button_group_read = gtk_check_button_new_with_label("Read");
    GtkWidget* check_button_group_write = gtk_check_button_new_with_label("Write");
    GtkWidget* check_button_group_exec = gtk_check_button_new_with_label("Execute");
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_group_read), false);
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_group_write), false);
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_group_exec), false);
    gtk_grid_attach(GTK_GRID(grid), check_button_group_read, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_button_group_write, 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_button_group_exec, 3, 1, 1, 1);

    // Other
    GtkWidget* check_button_others_read = gtk_check_button_new_with_label("Read");
    GtkWidget* check_button_others_write = gtk_check_button_new_with_label("Write");
    GtkWidget* check_button_others_exec = gtk_check_button_new_with_label("Execute");
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_others_read), false);
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_others_write), false);
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_others_exec), false);
    gtk_grid_attach(GTK_GRID(grid), check_button_others_read, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_button_others_write, 2, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_button_others_exec, 3, 2, 1, 1);

    // Special - added at the end of the above
    GtkWidget* check_button_set_uid = gtk_check_button_new_with_label("Set UID");
    GtkWidget* check_button_set_gid = gtk_check_button_new_with_label("Set GID");
    GtkWidget* check_button_sticky_bit = gtk_check_button_new_with_label("Sticky Bit");
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_set_uid), false);
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_set_gid), false);
    gtk_widget_set_sensitive(GTK_WIDGET(check_button_sticky_bit), false);
    gtk_grid_attach(GTK_GRID(grid), check_button_set_uid, 4, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_button_set_gid, 4, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), check_button_sticky_bit, 4, 2, 1, 1);

    // Set/unset the permission

    const auto& permissions = selected_file->permissions();

    // Owner
    if ((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_owner_read), true);
    }
    if ((permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_owner_write), true);
    }
    if ((permissions & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_owner_exec), true);
    }

    // Group
    if ((permissions & std::filesystem::perms::group_read) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_group_read), true);
    }
    if ((permissions & std::filesystem::perms::group_write) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_group_write), true);
    }
    if ((permissions & std::filesystem::perms::group_exec) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_group_exec), true);
    }

    // Other
    if ((permissions & std::filesystem::perms::others_read) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_others_read), true);
    }
    if ((permissions & std::filesystem::perms::others_write) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_others_write), true);
    }
    if ((permissions & std::filesystem::perms::others_exec) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_others_exec), true);
    }

    // Special
    if ((permissions & std::filesystem::perms::set_uid) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_set_uid), true);
    }
    if ((permissions & std::filesystem::perms::set_gid) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_set_gid), true);
    }
    if ((permissions & std::filesystem::perms::sticky_bit) != std::filesystem::perms::none)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_button_sticky_bit), true);
    }

    page.add_row_widget(GTK_WIDGET(grid));

    return page.box();
}

void
close_dialog(GtkWidget* widget, void* user_data)
{
    (void)user_data;
    GtkWidget* dialog = GTK_WIDGET(gtk_widget_get_ancestor(widget, GTK_TYPE_DIALOG));

    auto data = static_cast<properties_dialog_data*>(g_object_get_data(G_OBJECT(dialog), "data"));

    if (data != nullptr)
    {
        if (data->update_label_timer)
        {
            g_source_remove(data->update_label_timer);
            data->update_label_timer = 0;
        }
        data->cancel = true;

        if (data->calc_size_thread)
        {
            g_thread_join(data->calc_size_thread);
            data->calc_size_thread = nullptr;
        }

        delete data;
    }

    gtk_widget_destroy(dialog);
}

void
show_file_properties_dialog(GtkWindow* parent, const std::filesystem::path& cwd,
                            const std::span<const vfs::file_info> selected_files, i32 page)
{
    GtkWidget* dialog =
        gtk_dialog_new_with_buttons("File Properties",
                                    parent,
                                    GtkDialogFlags(GtkDialogFlags::GTK_DIALOG_MODAL |
                                                   GtkDialogFlags::GTK_DIALOG_DESTROY_WITH_PARENT),
                                    "Close",
                                    GtkResponseType::GTK_RESPONSE_OK,
                                    nullptr);

    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
    gtk_window_set_resizable(GTK_WINDOW(dialog), false);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GdkWindowTypeHint::GDK_WINDOW_TYPE_HINT_DIALOG);

    GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget* notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(content_area), notebook);

    gtk_container_set_border_width(GTK_CONTAINER(notebook), 2);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 2);

    const auto data = new properties_dialog_data;
    data->file_list = selected_files;
    data->cwd = cwd;
    g_object_set_data(G_OBJECT(dialog), "data", data);

    // clang-format off
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), init_file_info_tab(data, cwd , selected_files), gtk_label_new("File Info"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), init_attributes_tab(data, selected_files), gtk_label_new("Attributes"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), init_permissions_tab(data, selected_files), gtk_label_new("Permissions"));
    // clang-format on

    gtk_widget_set_size_request(GTK_WIDGET(dialog), 470, 400);
    // gtk_widget_set_size_request(GTK_WIDGET(dialog), 500, 800);
    // gtk_window_set_resizable(GTK_WINDOW(dialog), true);
    gtk_window_set_resizable(GTK_WINDOW(dialog), false);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GdkWindowTypeHint::GDK_WINDOW_TYPE_HINT_DIALOG);

    g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(close_dialog), data);

    gtk_widget_show_all(dialog);

    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), page);

    gtk4_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void
ptk_show_file_properties(GtkWindow* parent, const std::filesystem::path& cwd,
                         const std::span<const vfs::file_info> selected_files, i32 page)
{
    if (selected_files.empty())
    {
        vfs::file_info file = vfs_file_info_new(cwd);
        const std::vector<vfs::file_info> cwd_selected{file};

        show_file_properties_dialog(parent, cwd.parent_path(), cwd_selected, page);

        vfs_file_info_unref(file);
    }
    else
    {
        show_file_properties_dialog(parent, cwd, selected_files, page);
    }
}

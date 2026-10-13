/**************************************************************************/
/*  pre_patch_settings.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "pre_patch_settings.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "editor/editor_string_names.h"
#include "editor/themes/editor_scale.h"
#include "main/pre_patches.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/tree.h"

void PrePatchSettings::_notification(int p_what) {
	switch (p_what) {
		// Pre-patches are installed outside the editor, so the list is read again whenever it's shown.
		case NOTIFICATION_VISIBILITY_CHANGED:
		case NOTIFICATION_WM_WINDOW_FOCUS_IN:
		case NOTIFICATION_TRANSLATION_CHANGED: {
			if (is_visible_in_tree()) {
				update_pre_patches();
			}
		} break;
	}
}

PackedStringArray PrePatchSettings::_get_enabled() {
	return GLOBAL_GET(PrePatches::ENABLED_SETTING);
}

// Saved to project.godot right away, like the Plugins tab; an empty list removes the setting from it.
void PrePatchSettings::_set_enabled(const PackedStringArray &p_enabled) {
	ProjectSettings::get_singleton()->set_setting(PrePatches::ENABLED_SETTING, p_enabled);
	ProjectSettings::get_singleton()->save();
}

TreeItem *PrePatchSettings::_create_section(const String &p_title) {
	TreeItem *section = pre_patch_list->create_item(pre_patch_list->get_root());
	section->set_text(COLUMN_NAME, p_title);
	section->set_custom_font(COLUMN_NAME, get_theme_font(SNAME("bold"), EditorStringName(EditorFonts)));
	for (int column = 0; column < COLUMN_MAX; column++) {
		section->set_selectable(column, false);
	}
	return section;
}

void PrePatchSettings::update_pre_patches() {
	updating = true;
	const PrePatches::ScanResult installed = PrePatches::scan();
	const PackedStringArray enabled = _get_enabled();
	const Color disabled_color = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const Color warning_color = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
	const Color error_color = get_theme_color(SNAME("error_color"), EditorStringName(Editor));

	const String directory = PrePatches::get_directory();
	directory_label->set_text(vformat(TTR("Installed in: %s"), directory));
	directory_label->set_tooltip_text(directory);

	pre_patch_list->clear();
	pre_patch_list->create_item();

	TreeItem *installed_section = _create_section(vformat(TTR("Installed (%d)"), installed.pre_patches.size()));
	if (installed.pre_patches.is_empty()) {
		TreeItem *item = pre_patch_list->create_item(installed_section);
		item->set_text(COLUMN_NAME, vformat(TTR("None. A pre-patch is a folder in the folder above with a %s next to its .dll."), PrePatches::MANIFEST_FILE_NAME));
		item->set_custom_color(COLUMN_NAME, disabled_color);
		item->set_selectable(COLUMN_NAME, false);
	}
	for (const PrePatches::PrePatch &pre_patch : installed.pre_patches) {
		const bool is_enabled = enabled.has(pre_patch.id);
		TreeItem *item = pre_patch_list->create_item(installed_section);
		item->set_cell_mode(COLUMN_STATUS, TreeItem::CELL_MODE_CHECK);
		item->set_text(COLUMN_STATUS, TTRC("On"));
		item->set_checked(COLUMN_STATUS, is_enabled);
		item->set_editable(COLUMN_STATUS, true);
		item->set_text(COLUMN_NAME, pre_patch.name);
		item->set_metadata(COLUMN_NAME, pre_patch.id);
		if (!is_enabled) {
			item->set_custom_color(COLUMN_NAME, disabled_color);
		}
		String tooltip = vformat(TTR("Name: %s\nId: %s\nAssembly: %s\nManifest: %s"), pre_patch.name, pre_patch.id, pre_patch.assembly_path, pre_patch.manifest_path);
		if (!pre_patch.description.is_empty()) {
			tooltip += "\n\n" + pre_patch.description;
		}
		for (int column = COLUMN_STATUS; column < COLUMN_PADDING_RIGHT; column++) {
			item->set_tooltip_text(column, tooltip);
		}
		item->set_text(COLUMN_VERSION, pre_patch.version);
		item->set_custom_font(COLUMN_VERSION, get_theme_font(SNAME("source"), EditorStringName(EditorFonts)));
		item->set_text(COLUMN_AUTHOR, pre_patch.author);
	}

	// Enabled ids without a usable pre-patch: not installed (any more), or their manifest has a problem.
	Vector<String> missing;
	for (const String &entry : enabled) {
		const String id = entry.strip_edges();
		if (!id.is_empty() && !installed.find(id) && !missing.has(id)) {
			missing.push_back(id);
		}
	}
	if (!missing.is_empty()) {
		TreeItem *missing_section = _create_section(vformat(TTR("Enabled but not found (%d)"), missing.size()));
		missing_section->set_custom_color(COLUMN_NAME, warning_color);
		for (const String &id : missing) {
			TreeItem *item = pre_patch_list->create_item(missing_section);
			const bool conflicting = installed.conflicting_ids.has(id);
			item->set_text(COLUMN_NAME, conflicting ? vformat(TTR("%s (several pre-patches use this id, see Problems)"), id) : id);
			item->set_tooltip_text(COLUMN_NAME, vformat(TTR("The project enables the pre-patch \"%s\", but no usable pre-patch with this id is installed in:\n%s"), id, directory));
			item->set_metadata(COLUMN_NAME, id);
			item->add_button(COLUMN_NAME, get_editor_theme_icon(SNAME("Remove")), BUTTON_REMOVE_ENABLED, false, TTRC("Remove this id from the project's enabled pre-patches."));
		}
	}

	if (!installed.problems.is_empty()) {
		TreeItem *problem_section = _create_section(vformat(TTR("Problems (%d), these pre-patches can't be used"), installed.problems.size()));
		problem_section->set_custom_color(COLUMN_NAME, error_color);
		for (const PrePatches::Problem &problem : installed.problems) {
			TreeItem *item = pre_patch_list->create_item(problem_section);
			// The folder's name, which tells pre-patches apart; the full path is in the tooltip.
			item->set_text(COLUMN_NAME, vformat("%s: %s", problem.manifest_path.get_base_dir().get_file(), problem.message));
			item->set_autowrap_mode(COLUMN_NAME, TextServer::AUTOWRAP_WORD_SMART);
			item->set_tooltip_text(COLUMN_NAME, vformat("%s\n\n%s", problem.manifest_path, problem.message));
		}
	}

	updating = false;
}

void PrePatchSettings::_pre_patch_toggled() {
	if (updating) {
		return;
	}
	TreeItem *item = pre_patch_list->get_edited();
	ERR_FAIL_NULL(item);
	const String id = item->get_metadata(COLUMN_NAME);
	const bool checked = item->is_checked(COLUMN_STATUS);

	PackedStringArray enabled = _get_enabled();
	if (checked) {
		if (!enabled.has(id)) {
			enabled.push_back(id); // Newly enabled pre-patches load last.
		}
	} else {
		while (enabled.has(id)) {
			enabled.erase(id);
		}
	}
	_set_enabled(enabled);

	if (checked) {
		item->clear_custom_color(COLUMN_NAME);
	} else {
		item->set_custom_color(COLUMN_NAME, get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)));
	}
}

void PrePatchSettings::_button_clicked(Object *p_item, int p_column, int p_id, MouseButton p_button) {
	if (p_button != MouseButton::LEFT || p_id != BUTTON_REMOVE_ENABLED) {
		return;
	}
	TreeItem *item = Object::cast_to<TreeItem>(p_item);
	ERR_FAIL_NULL(item);
	_remove_enabled(item->get_metadata(COLUMN_NAME));
}

void PrePatchSettings::_remove_enabled(const String &p_id) {
	PackedStringArray enabled = _get_enabled();
	for (int i = enabled.size() - 1; i >= 0; i--) {
		if (enabled[i].strip_edges() == p_id) {
			enabled.remove_at(i);
		}
	}
	_set_enabled(enabled);
	callable_mp(this, &PrePatchSettings::update_pre_patches).call_deferred(); // Not while the Tree handles the click.
}

void PrePatchSettings::_open_directory() {
	const String directory = PrePatches::get_directory();
	DirAccess::make_dir_recursive_absolute(directory); // So it can be opened before any pre-patch is installed.
	OS::get_singleton()->shell_show_in_file_manager(directory, true);
}

PrePatchSettings::PrePatchSettings() {
	// Edited here only, so it isn't shown in the General tab too.
	ProjectSettings::get_singleton()->add_hidden_prefix(PrePatches::ENABLED_SETTING);

	HBoxContainer *title_hb = memnew(HBoxContainer);
	Label *title = memnew(Label(TTRC("Pre-Patches:")));
	title->set_theme_type_variation("HeaderSmall");
	title_hb->add_child(title);
	// One line, cut off when the dialog is narrow: a wrapping label would raise the dialog's minimum height.
	directory_label = memnew(Label);
	directory_label->set_h_size_flags(SIZE_EXPAND_FILL);
	directory_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	directory_label->set_mouse_filter(MOUSE_FILTER_PASS);
	title_hb->add_child(directory_label);
	Button *rescan_button = memnew(Button(TTRC("Rescan")));
	rescan_button->connect(SceneStringName(pressed), callable_mp(this, &PrePatchSettings::update_pre_patches));
	title_hb->add_child(rescan_button);
	Button *open_button = memnew(Button(TTRC("Open Folder")));
	open_button->connect(SceneStringName(pressed), callable_mp(this, &PrePatchSettings::_open_directory));
	title_hb->add_child(open_button);
	add_child(title_hb);

	Label *info = memnew(Label(TTRC("Run before the game starts when it's run from the editor, in the order they were enabled. Saved in project.godot, so everyone working on the project uses the same ones.")));
	info->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	info->set_tooltip_text(info->get_text());
	info->set_mouse_filter(MOUSE_FILTER_PASS);
	add_child(info);

	pre_patch_list = memnew(Tree);
	pre_patch_list->set_v_size_flags(SIZE_EXPAND_FILL);
	pre_patch_list->set_columns(COLUMN_MAX);
	pre_patch_list->set_column_titles_visible(true);
	pre_patch_list->set_column_title(COLUMN_STATUS, TTRC("Enabled"));
	pre_patch_list->set_column_title(COLUMN_NAME, TTRC("Name"));
	pre_patch_list->set_column_title(COLUMN_VERSION, TTRC("Version"));
	pre_patch_list->set_column_title(COLUMN_AUTHOR, TTRC("Author"));
	for (int column = COLUMN_STATUS; column < COLUMN_PADDING_RIGHT; column++) {
		pre_patch_list->set_column_title_alignment(column, HORIZONTAL_ALIGNMENT_LEFT);
		pre_patch_list->set_column_clip_content(column, true);
	}
	pre_patch_list->set_column_expand(COLUMN_PADDING_LEFT, false);
	pre_patch_list->set_column_expand(COLUMN_STATUS, false);
	pre_patch_list->set_column_expand(COLUMN_NAME, true);
	pre_patch_list->set_column_expand(COLUMN_VERSION, false);
	pre_patch_list->set_column_expand(COLUMN_AUTHOR, false);
	pre_patch_list->set_column_expand(COLUMN_PADDING_RIGHT, false);
	pre_patch_list->set_column_custom_minimum_width(COLUMN_PADDING_LEFT, 10 * EDSCALE);
	pre_patch_list->set_column_custom_minimum_width(COLUMN_STATUS, 80 * EDSCALE);
	pre_patch_list->set_column_custom_minimum_width(COLUMN_VERSION, 100 * EDSCALE);
	pre_patch_list->set_column_custom_minimum_width(COLUMN_AUTHOR, 200 * EDSCALE);
	pre_patch_list->set_column_custom_minimum_width(COLUMN_PADDING_RIGHT, 10 * EDSCALE);
	pre_patch_list->set_hide_root(true);
	pre_patch_list->connect("item_edited", callable_mp(this, &PrePatchSettings::_pre_patch_toggled), CONNECT_DEFERRED);
	pre_patch_list->connect("button_clicked", callable_mp(this, &PrePatchSettings::_button_clicked));
	add_child(pre_patch_list);
}

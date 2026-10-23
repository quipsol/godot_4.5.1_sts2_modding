/**************************************************************************/
/*  mod_pack_dialog.cpp                                                   */
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

#include "mod_pack_dialog.h"

#include "core/config/project_settings.h"
#include "editor/editor_node.h"
#include "editor/export/mod_packers.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_toaster.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"

void ModPackDialog::popup_pack() {
	const ModPackers::ScanResult installed = ModPackers::scan();
	const String selected_id = GLOBAL_GET(ModPackers::PACKER_SETTING);

	packer_list->clear();
	for (const ModPackers::Packer &packer : installed.packers) {
		packer_list->add_item(packer.name);
		const int index = packer_list->get_item_count() - 1;
		packer_list->set_item_metadata(index, packer.id);
		String tooltip = vformat(TTR("Id: %s\nScript: %s"), packer.id, packer.script_path);
		if (!packer.version.is_empty() || !packer.author.is_empty()) {
			tooltip += vformat(TTR("\nVersion: %s, author: %s"), packer.version, packer.author);
		}
		if (!packer.description.is_empty()) {
			tooltip += "\n\n" + packer.description;
		}
		packer_list->set_item_tooltip(index, tooltip);
		if (packer.id == selected_id) {
			packer_list->select(index);
		}
	}
	if (installed.packers.is_empty()) {
		packer_list->add_item(TTR("No mod packers installed"));
		packer_list->set_item_disabled(0, true);
	}
	_packer_selected(packer_list->get_selected());

	// One line, with the details in the tooltip: a dialog grows with its labels.
	problems_label->set_visible(!installed.problems.is_empty());
	if (!installed.problems.is_empty()) {
		problems_label->set_text(vformat(TTR("%d mod packers in \"%s\" can't be used (hover for details)."), installed.problems.size(), ModPackers::get_directory()));
		String details;
		for (const ModPackers::Problem &problem : installed.problems) {
			details += vformat("%s\n    %s\n", problem.manifest_path, problem.message);
		}
		problems_label->set_tooltip_text(details.strip_edges());
	}

	output_path->set_text(GLOBAL_GET(ModPackers::OUTPUT_PATH_SETTING));
	_update_ok_button();
	popup_centered(Size2(600, 0) * EDSCALE);
}

void ModPackDialog::_packer_selected(int p_index) {
	if (p_index < 0 || packer_list->is_item_disabled(p_index)) {
		packer_info->set_text(vformat(TTR("Install mod packers as folders in \"%s\", each with a %s."), ModPackers::get_directory(), ModPackers::MANIFEST_FILE_NAME));
	} else {
		packer_info->set_text(vformat(TTR("Packs the mod \"%s\". Only the .pck: the mod's .dll and .json come from building the C# project."), ModPackers::get_mod_name()));
	}
	_update_ok_button();
}

void ModPackDialog::_browse() {
	const String path = output_path->get_text().strip_edges();
	if (!path.is_empty()) {
		file_dialog->set_current_path(path);
	} else {
		file_dialog->set_current_file(ModPackers::get_mod_name() + ".pck");
	}
	file_dialog->popup_file_dialog();
}

void ModPackDialog::_file_selected(const String &p_path) {
	output_path->set_text(p_path);
	_update_ok_button();
}

void ModPackDialog::_update_ok_button() {
	const int selected = packer_list->get_selected();
	get_ok_button()->set_disabled(selected < 0 || packer_list->is_item_disabled(selected) || output_path->get_text().strip_edges().is_empty());
}

void ModPackDialog::ok_pressed() {
	const String id = packer_list->get_item_metadata(packer_list->get_selected());
	const String path = output_path->get_text().strip_edges().replace_char('\\', '/');

	// Remembered for next time; the output path goes to override.cfg (see ProjectSettings::set_machine_specific()).
	ProjectSettings::get_singleton()->set_setting(ModPackers::PACKER_SETTING, id);
	ProjectSettings::get_singleton()->set_setting(ModPackers::OUTPUT_PATH_SETTING, path);
	ProjectSettings::get_singleton()->save();

	// Like running the project: what's packed is what's on disk.
	EditorNode::get_singleton()->save_all_scenes();

	String error;
	const String script = ModPackers::find_script(id, error);
	const Error err = script.is_empty() ? ERR_FILE_NOT_FOUND : ModPackers::pack(script, path, Vector<String>(), error);
	if (err != OK) {
		ERR_PRINT(error);
		EditorNode::get_singleton()->show_warning(error);
		return; // Stays open, e.g. to choose another path.
	}
	hide();
	EditorToaster::get_singleton()->popup_str(vformat(TTR("Packed the mod into \"%s\"."), path));
}

ModPackDialog::ModPackDialog() {
	set_title(TTRC("Pack Mod"));
	set_ok_button_text(TTRC("Pack"));
	set_hide_on_ok(false); // Hidden when packing worked.

	VBoxContainer *vbox = memnew(VBoxContainer);
	add_child(vbox);

	Label *packer_title = memnew(Label(TTRC("Mod Packer:")));
	vbox->add_child(packer_title);
	packer_list = memnew(OptionButton);
	packer_list->connect(SceneStringName(item_selected), callable_mp(this, &ModPackDialog::_packer_selected));
	vbox->add_child(packer_list);

	packer_info = memnew(Label);
	packer_info->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	packer_info->set_custom_minimum_size(Size2(560, 0) * EDSCALE); // Wrapping needs a width; see the size in popup_pack().
	vbox->add_child(packer_info);

	problems_label = memnew(Label);
	problems_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	problems_label->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	problems_label->hide();
	vbox->add_child(problems_label);

	Label *path_title = memnew(Label(TTRC("Output Path (.pck):")));
	vbox->add_child(path_title);
	HBoxContainer *path_hb = memnew(HBoxContainer);
	vbox->add_child(path_hb);
	output_path = memnew(LineEdit);
	output_path->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	output_path->set_tooltip_text(TTRC("Saved in override.cfg, since it's machine-specific. Relative paths are relative to the project folder."));
	output_path->connect(SceneStringName(text_changed), callable_mp(this, &ModPackDialog::_update_ok_button).unbind(1));
	path_hb->add_child(output_path);
	Button *browse_button = memnew(Button(TTRC("Browse...")));
	browse_button->connect(SceneStringName(pressed), callable_mp(this, &ModPackDialog::_browse));
	path_hb->add_child(browse_button);

	file_dialog = memnew(EditorFileDialog);
	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	file_dialog->add_filter("*.pck", TTRC("Godot Pack"));
	file_dialog->connect("file_selected", callable_mp(this, &ModPackDialog::_file_selected));
	add_child(file_dialog);
}

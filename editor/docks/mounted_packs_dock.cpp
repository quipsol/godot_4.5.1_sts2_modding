/**************************************************************************/
/*  mounted_packs_dock.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
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

#include "mounted_packs_dock.h"

#include "core/io/file_access_pack.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "editor/docks/inspector_dock.h"
#include "editor/editor_node.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/inspector/editor_resource_preview.h"
#include "editor/settings/editor_settings.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/tree.h"
#include "scene/main/timer.h"
#include "servers/display_server.h"

int MountedPacksDock::_add_entry(const String &p_name, const String &p_path, int p_parent, Entry::Kind p_kind) {
	const int index = entries.size();
	Entry entry;
	entry.name = p_name;
	entry.path = p_path;
	entry.parent = p_parent;
	entry.kind = p_kind;
	entries.push_back(entry);
	if (p_parent != -1) {
		entries[p_parent].children.push_back(index);
	}
	return index;
}

void MountedPacksDock::_sort_children(int p_entry) {
	struct SortKey {
		String key;
		int index = -1;
		bool operator<(const SortKey &p_other) const { return key < p_other.key; }
	};
	Vector<SortKey> keys;
	for (int child : entries[p_entry].children) {
		// Folders before files, then by name.
		keys.push_back({ (entries[child].kind == Entry::KIND_FOLDER ? "0" : "1") + entries[child].name.to_lower(), child });
	}
	keys.sort();
	for (int i = 0; i < keys.size(); i++) {
		entries[p_entry].children[i] = keys[i].index;
	}
}

void MountedPacksDock::_rebuild_model() {
	entries.clear();
	pack_entries.clear();
	file_entries.clear();

	PackedData *packed_data = PackedData::get_singleton();
	if (!packed_data) {
		return;
	}
	const HashMap<String, Vector<String>> pack_files = packed_data->get_read_only_file_paths();

	for (const String &pack : packed_data->get_mounted_read_only_packs()) {
		const int pack_entry = _add_entry(pack.get_file().get_basename(), pack, -1, Entry::KIND_PACK); // The icon marks it as a pack.
		pack_entries.push_back(pack_entry);

		const Vector<String> *files = pack_files.getptr(pack);
		if (!files) {
			continue;
		}
		HashMap<String, int> folders; // Folder path (relative) -> entry.
		HashSet<String> added;
		for (String path : *files) {
			// Show files the way the project uses them: "x.png" for "x.png.import", "x.tscn" for "x.tscn.remap".
			// The packs' internals (.godot/, compiled scripts, .uid files) are left out.
			if (path.begins_with(".godot/") || path.ends_with(".uid") || path.ends_with(".gdc")) {
				continue;
			}
			if (path.ends_with(".import")) {
				path = path.trim_suffix(".import");
			} else if (path.ends_with(".remap")) {
				path = path.trim_suffix(".remap");
			}
			if (added.has(path)) {
				continue;
			}
			added.insert(path);

			int parent = pack_entry;
			const String dir = path.get_base_dir();
			if (!dir.is_empty()) {
				String folder_path;
				for (const String &part : dir.split("/")) {
					folder_path = folder_path.path_join(part);
					const int *folder = folders.getptr(folder_path);
					if (folder) {
						parent = *folder;
					} else {
						parent = _add_entry(part, "res://" + folder_path, parent, Entry::KIND_FOLDER);
						folders[folder_path] = parent;
					}
				}
			}
			file_entries.push_back(_add_entry(path.get_file(), "res://" + path, parent, Entry::KIND_FILE));
		}
	}

	for (uint32_t i = 0; i < entries.size(); i++) {
		if (entries[i].children.size() > 1) {
			_sort_children(i);
		}
	}
}

TreeItem *MountedPacksDock::_create_item(TreeItem *p_parent, int p_entry, bool p_lazy) {
	const Entry &entry = entries[p_entry];
	TreeItem *item = tree->create_item(p_parent);
	ERR_FAIL_NULL_V(item, nullptr);
	item->set_text(0, entry.name);
	item->set_metadata(0, p_entry);

	switch (entry.kind) {
		case Entry::KIND_PACK: {
			item->set_icon(0, get_editor_theme_icon(SNAME("PackedDataContainer")));
			item->set_tooltip_text(0, vformat(TTR("%s\nMounted read-only."), entry.path));
		} break;
		case Entry::KIND_FOLDER: {
			item->set_icon(0, get_editor_theme_icon(SNAME("Folder")));
			item->set_icon_modulate(0, get_theme_color(SNAME("folder_icon_color"), SNAME("FileDialog")));
			item->set_tooltip_text(0, entry.path);
		} break;
		case Entry::KIND_FILE: {
			// Only reads the file's header (or its .import file); files get items only when their folder is shown.
			const String type = ResourceLoader::get_resource_type(entry.path);
			item->set_icon(0, type.is_empty() ? get_editor_theme_icon(SNAME("File")) : EditorNode::get_singleton()->get_class_icon(type, "File"));
			item->set_tooltip_text(0, type.is_empty() ? entry.path : vformat("%s\n%s", entry.path, type));
			// Like the FileSystem dock: a small preview (e.g. of an image) replaces the icon once it's generated.
			EditorResourcePreview::get_singleton()->queue_resource_preview(entry.path, this, "_thumbnail_done", uint64_t(item->get_instance_id()));
		} break;
	}

	if (p_lazy && entry.kind != Entry::KIND_FILE && !entry.children.is_empty()) {
		tree->create_item(item); // Placeholder without metadata, replaced by the children when expanded.
		item->set_collapsed(true);
	}
	return item;
}

void MountedPacksDock::_populate(TreeItem *p_item) {
	TreeItem *first = p_item->get_first_child();
	if (!first || first->get_metadata(0).get_type() != Variant::NIL) {
		return; // Already populated.
	}
	memdelete(first);
	const int entry = p_item->get_metadata(0);
	for (int child : entries[entry].children) {
		_create_item(p_item, child);
	}
}

void MountedPacksDock::_populate_deferred(ObjectID p_item) {
	TreeItem *item = ObjectDB::get_instance<TreeItem>(p_item);
	if (item) {
		_populate(item); // The item may be gone if the tree was rebuilt in the meantime.
	}
}

void MountedPacksDock::_on_item_collapsed(TreeItem *p_item) {
	if (!p_item->is_collapsed()) {
		// Expanding by clicking emits this while the Tree handles the click, and the Tree doesn't allow creating items then.
		callable_mp(this, &MountedPacksDock::_populate_deferred).call_deferred(p_item->get_instance_id());
	}
}

void MountedPacksDock::_update_tree() {
	tree->clear();
	const bool has_packs = !pack_entries.is_empty();
	tree->set_visible(has_packs);
	empty_label->set_visible(!has_packs);
	if (!has_packs) {
		return;
	}
	TreeItem *root = tree->create_item();

	const String text = filter->get_text().strip_edges();
	if (text.is_empty()) {
		for (int pack : pack_entries) {
			TreeItem *item = _create_item(root, pack);
			if (!item) {
				return;
			}
			_populate(item);
			item->set_collapsed(false);
		}
		return;
	}

	// Filtered: the matching files with their folders, expanded.
	// "path|entry", so sorting orders by path and the same path from two packs shows up under each.
	Vector<String> matches;
	for (int file : file_entries) {
		if (entries[file].path.containsn(text)) {
			matches.push_back(entries[file].path + "|" + itos(file));
		}
	}
	matches.sort();

	HashMap<int, TreeItem *> created;
	const int shown = MIN(matches.size(), MAX_FILTER_RESULTS);
	for (int i = 0; i < shown; i++) {
		const int file = matches[i].get_slicec('|', 1).to_int();
		LocalVector<int> chain;
		for (int e = file; e != -1; e = entries[e].parent) {
			chain.push_back(e);
		}
		TreeItem *parent = root;
		for (int j = chain.size() - 1; j >= 0; j--) {
			TreeItem **existing = created.getptr(chain[j]);
			if (existing) {
				parent = *existing;
				continue;
			}
			parent = _create_item(parent, chain[j], false);
			if (!parent) {
				return;
			}
			parent->set_collapsed(false);
			created[chain[j]] = parent;
		}
	}

	if (matches.is_empty()) {
		TreeItem *item = tree->create_item(root);
		item->set_text(0, TTR("No matching files."));
		item->set_selectable(0, false);
	} else if (matches.size() > shown) {
		TreeItem *item = tree->create_item(root);
		item->set_text(0, vformat(TTR("%d more matches; refine the filter to see them."), matches.size() - shown));
		item->set_selectable(0, false);
	}
}

void MountedPacksDock::update_packs() {
	_rebuild_model();
	_update_tree();
}

Vector<String> MountedPacksDock::_get_selected_files() const {
	Vector<String> files;
	for (TreeItem *item = tree->get_next_selected(nullptr); item; item = tree->get_next_selected(item)) {
		const Variant entry = item->get_metadata(0);
		if (entry.get_type() == Variant::INT && entries[(int)entry].kind == Entry::KIND_FILE) {
			files.push_back(entries[(int)entry].path);
		}
	}
	return files;
}

void MountedPacksDock::_open_file(const String &p_path) {
	// Like the FileSystem dock: scenes open as scenes, everything else in the Inspector (or its editor).
	if (ResourceLoader::get_resource_type(p_path) == "PackedScene") {
		EditorNode::get_singleton()->load_scene_or_resource(p_path);
	} else {
		EditorNode::get_singleton()->load_resource(p_path);
	}
}

void MountedPacksDock::_on_item_activated() {
	TreeItem *item = tree->get_selected();
	if (!item || item->get_metadata(0).get_type() != Variant::INT) {
		return;
	}
	const Entry &entry = entries[(int)item->get_metadata(0)];
	if (entry.kind == Entry::KIND_FILE) {
		_open_file(entry.path);
	} else {
		item->set_collapsed(!item->is_collapsed());
	}
}

void MountedPacksDock::_on_item_mouse_selected(const Vector2 &p_position, MouseButton p_button) {
	if (p_button != MouseButton::RIGHT) {
		return;
	}
	const Vector<String> files = _get_selected_files();
	if (files.is_empty()) {
		return;
	}
	context_menu->clear();
	if (files.size() == 1) {
		context_menu->add_icon_item(get_editor_theme_icon(SNAME("Load")), TTRC("Open"), CONTEXT_OPEN);
		context_menu->add_separator();
	}
	context_menu->add_icon_item(get_editor_theme_icon(SNAME("ActionCopy")), TTRC("Copy Path"), CONTEXT_COPY_PATH);
	context_menu->add_icon_item(get_editor_theme_icon(SNAME("Instance")), TTRC("Copy UID"), CONTEXT_COPY_UID);
	if (files.size() == 1) {
		context_menu->add_icon_item(get_editor_theme_icon(SNAME("ActionCopy")), TTRC("Copy Resource"), CONTEXT_COPY_RESOURCE);
		context_menu->set_item_tooltip(context_menu->get_item_count() - 1, TTRC("For pasting into a resource property in the Inspector (Paste Value, or Paste in its dropdown)."));
	}
	context_menu->set_position(tree->get_screen_position() + p_position);
	context_menu->reset_size();
	context_menu->popup();
}

void MountedPacksDock::_on_context_menu_id_pressed(int p_option) {
	const Vector<String> files = _get_selected_files();
	switch (p_option) {
		case CONTEXT_OPEN: {
			if (!files.is_empty()) {
				_open_file(files[0]);
			}
		} break;
		case CONTEXT_COPY_PATH: {
			DisplayServer::get_singleton()->clipboard_set(String("\n").join(files));
		} break;
		case CONTEXT_COPY_UID: {
			PackedStringArray uids;
			for (const String &file : files) {
				const ResourceUID::ID uid = ResourceLoader::get_resource_uid(file);
				uids.push_back(uid == ResourceUID::INVALID_ID ? file : ResourceUID::get_singleton()->id_to_text(uid));
			}
			DisplayServer::get_singleton()->clipboard_set(String("\n").join(uids));
		} break;
		case CONTEXT_COPY_RESOURCE: {
			// The Inspector has two clipboards of its own (neither is the system clipboard): the property menu's
			// Paste Value (Ctrl+V on a property) and the resource dropdown's Paste. Fill both.
			if (!files.is_empty()) {
				const Ref<Resource> resource = ResourceLoader::load(files[0]);
				if (resource.is_valid()) {
					InspectorDock::get_inspector_singleton()->set_property_clipboard(resource);
					EditorSettings::get_singleton()->set_resource_clipboard(resource);
				}
			}
		} break;
	}
}

Variant MountedPacksDock::_get_drag_data_fw(const Point2 &p_point, Control *p_from) {
	// The same drag data as the FileSystem dock, so the files can be dropped wherever project files can.
	const Vector<String> files = _get_selected_files();
	if (files.is_empty()) {
		return Variant();
	}
	return EditorNode::get_singleton()->drag_files_and_dirs(files, p_from);
}

void MountedPacksDock::_thumbnail_done(const String &p_path, const Ref<Texture2D> &p_preview, const Ref<Texture2D> &p_small_preview, const Variant &p_item) {
	TreeItem *item = ObjectDB::get_instance<TreeItem>(ObjectID(uint64_t(p_item)));
	if (item && p_small_preview.is_valid()) {
		item->set_icon(0, p_small_preview); // The item may be gone if the tree was rebuilt meanwhile.
	}
}

void MountedPacksDock::_bind_methods() {
	// Called by name by EditorResourcePreview.
	ClassDB::bind_method(D_METHOD("_thumbnail_done", "path", "preview", "small_preview", "item"), &MountedPacksDock::_thumbnail_done);
}

void MountedPacksDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			update_packs();
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			filter->set_right_icon(get_editor_theme_icon(SNAME("Search")));
			remount_button->set_button_icon(get_editor_theme_icon(SNAME("Reload")));
			if (is_ready()) {
				_update_tree(); // Item icons come from the theme.
			}
		} break;
	}
}

MountedPacksDock::MountedPacksDock() {
	singleton = this;
	set_name("Mounted Packs");

	HBoxContainer *toolbar = memnew(HBoxContainer);
	add_child(toolbar);

	filter = memnew(LineEdit);
	filter->set_placeholder(TTRC("Filter Files"));
	filter->set_accessibility_name(TTRC("Filter Files"));
	filter->set_clear_button_enabled(true);
	filter->set_h_size_flags(SIZE_EXPAND_FILL);
	toolbar->add_child(filter);

	remount_button = memnew(Button);
	remount_button->set_flat(true);
	remount_button->set_tooltip_text(TTRC("Remount Read-Only Packs\nMounts the packs again, e.g. after a pack changed on disk."));
	remount_button->set_accessibility_name(TTRC("Remount Read-Only Packs"));
	toolbar->add_child(remount_button);
	remount_button->connect(SceneStringName(pressed), callable_mp(EditorNode::get_singleton(), &EditorNode::remount_read_only_packs));

	tree = memnew(Tree);
	tree->set_hide_root(true);
	tree->set_select_mode(Tree::SELECT_MULTI);
	tree->set_allow_rmb_select(true);
	tree->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	tree->set_v_size_flags(SIZE_EXPAND_FILL);
	tree->set_accessibility_name(TTRC("Mounted Packs"));
	add_child(tree);
	tree->connect("item_collapsed", callable_mp(this, &MountedPacksDock::_on_item_collapsed));
	tree->connect("item_activated", callable_mp(this, &MountedPacksDock::_on_item_activated));
	tree->connect("item_mouse_selected", callable_mp(this, &MountedPacksDock::_on_item_mouse_selected));
	tree->set_drag_forwarding(callable_mp(this, &MountedPacksDock::_get_drag_data_fw).bind(tree), Callable(), Callable());

	empty_label = memnew(Label);
	empty_label->set_text(TTRC("No packs are mounted.\n\nMount the game's pack and other mods' packs read-only with the project setting editor/mounted_packs/paths (e.g. in override.cfg), or start the editor with --mount-pack <file>."));
	empty_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	empty_label->set_vertical_alignment(VERTICAL_ALIGNMENT_TOP);
	empty_label->set_v_size_flags(SIZE_EXPAND_FILL);
	empty_label->hide();
	add_child(empty_label);

	context_menu = memnew(PopupMenu);
	add_child(context_menu);
	context_menu->connect(SceneStringName(id_pressed), callable_mp(this, &MountedPacksDock::_on_context_menu_id_pressed));

	// Filtering walks every file, so wait until typing pauses.
	filter_timer = memnew(Timer);
	filter_timer->set_one_shot(true);
	filter_timer->set_wait_time(0.25);
	add_child(filter_timer);
	filter_timer->connect("timeout", callable_mp(this, &MountedPacksDock::_update_tree));
	filter->connect(SceneStringName(text_changed), callable_mp(filter_timer, &Timer::start).bind(-1).unbind(1));
}

MountedPacksDock::~MountedPacksDock() {
	singleton = nullptr;
}

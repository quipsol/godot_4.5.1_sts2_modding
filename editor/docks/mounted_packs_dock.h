/**************************************************************************/
/*  mounted_packs_dock.h                                                  */
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

#pragma once

#include "scene/gui/box_container.h"

class Button;
class Label;
class LineEdit;
class PopupMenu;
class Timer;
class Tree;
class TreeItem;

// Browses the packs mounted read-only on top of the project (editor/mounted_packs/paths, --mount-pack).
// Their files are not on disk, so the FileSystem dock never shows them; here they can be found, opened and dragged.
class MountedPacksDock : public VBoxContainer {
	GDCLASS(MountedPacksDock, VBoxContainer);

	static inline MountedPacksDock *singleton = nullptr;

	// The packs' contents as a tree of entries; TreeItems store their entry's index as metadata.
	// Folders get their TreeItems only when expanded (a game's pack has thousands of files).
	struct Entry {
		String name;
		String path; // res:// path of a file or folder; the pack's file path for a pack.
		int parent = -1;
		LocalVector<int> children; // Folders first, then files, each sorted by name.
		enum Kind {
			KIND_PACK,
			KIND_FOLDER,
			KIND_FILE,
		} kind = KIND_FILE;
	};
	LocalVector<Entry> entries;
	LocalVector<int> pack_entries;
	LocalVector<int> file_entries; // For filtering.

	static constexpr int MAX_FILTER_RESULTS = 500;

	LineEdit *filter = nullptr;
	Button *remount_button = nullptr;
	Tree *tree = nullptr;
	Label *empty_label = nullptr;
	PopupMenu *context_menu = nullptr;
	Timer *filter_timer = nullptr;

	enum ContextMenuOption {
		CONTEXT_OPEN,
		CONTEXT_COPY_PATH,
		CONTEXT_COPY_UID,
		CONTEXT_COPY_RESOURCE,
	};

	void _rebuild_model();
	int _add_entry(const String &p_name, const String &p_path, int p_parent, Entry::Kind p_kind);
	void _sort_children(int p_entry);

	void _update_tree();
	TreeItem *_create_item(TreeItem *p_parent, int p_entry, bool p_lazy = true);
	void _populate(TreeItem *p_item);
	void _populate_deferred(ObjectID p_item);
	void _on_item_collapsed(TreeItem *p_item);

	Vector<String> _get_selected_files() const;
	void _open_file(const String &p_path);
	void _on_item_activated();
	void _on_item_mouse_selected(const Vector2 &p_position, MouseButton p_button);
	void _on_context_menu_id_pressed(int p_option);
	Variant _get_drag_data_fw(const Point2 &p_point, Control *p_from);
	void _thumbnail_done(const String &p_path, const Ref<Texture2D> &p_preview, const Ref<Texture2D> &p_small_preview, const Variant &p_item);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static MountedPacksDock *get_singleton() { return singleton; }

	void update_packs(); // After the read-only packs were (re)mounted.

	MountedPacksDock();
	~MountedPacksDock();
};

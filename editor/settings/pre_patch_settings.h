/**************************************************************************/
/*  pre_patch_settings.h                                                  */
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

#pragma once

#include "scene/gui/box_container.h"

class Label;
class Tree;
class TreeItem;

// Project Settings > Pre-Patches: the pre-patches installed with the editor (see PrePatches), and which of them the
// project enables (editor/pre_patches/enabled). Built like the Plugins tab.
class PrePatchSettings : public VBoxContainer {
	GDCLASS(PrePatchSettings, VBoxContainer);

	enum {
		COLUMN_PADDING_LEFT,
		COLUMN_STATUS,
		COLUMN_NAME,
		COLUMN_VERSION,
		COLUMN_AUTHOR,
		COLUMN_PADDING_RIGHT,
		COLUMN_MAX,
	};

	enum {
		BUTTON_REMOVE_ENABLED,
	};

	// Everything goes in one Tree, which scrolls: labels stacked above it would set the dialog's minimum height.
	Label *directory_label = nullptr;
	Tree *pre_patch_list = nullptr;
	bool updating = false;

	static PackedStringArray _get_enabled();
	void _set_enabled(const PackedStringArray &p_enabled);
	TreeItem *_create_section(const String &p_title);

	void _pre_patch_toggled();
	void _button_clicked(Object *p_item, int p_column, int p_id, MouseButton p_button);
	void _remove_enabled(const String &p_id);
	void _open_directory();

protected:
	void _notification(int p_what);

public:
	void update_pre_patches();

	PrePatchSettings();
};

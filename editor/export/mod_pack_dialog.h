/**************************************************************************/
/*  mod_pack_dialog.h                                                     */
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

#include "scene/gui/dialogs.h"

class EditorFileDialog;
class Label;
class LineEdit;
class OptionButton;

// Project > Pack Mod...: packs the project's mod into a .pck with one of the mod packers installed with the editor
// (see ModPackers). The packer and the output path are remembered (editor/mod_packer/packer in project.godot,
// editor/mod_packer/output_path in override.cfg, since it's machine-specific).
class ModPackDialog : public ConfirmationDialog {
	GDCLASS(ModPackDialog, ConfirmationDialog);

	OptionButton *packer_list = nullptr;
	Label *packer_info = nullptr;
	Label *problems_label = nullptr;
	LineEdit *output_path = nullptr;
	EditorFileDialog *file_dialog = nullptr;

	void _packer_selected(int p_index);
	void _browse();
	void _file_selected(const String &p_path);
	void _update_ok_button();

protected:
	virtual void ok_pressed() override;

public:
	void popup_pack();

	ModPackDialog();
};

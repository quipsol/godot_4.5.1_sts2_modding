/**************************************************************************/
/*  mounted_pack_addons.h                                                 */
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

#include "core/io/config_file.h"
#include "core/templates/hash_set.h"

// The editor plugins of addons that come from read-only mounted packs (editor/mounted_packs/addons), e.g. a game's addons.
// An exported pack doesn't contain the addons' plugin.cfg files, so the editor provides one in their place: the plugin
// script is the addon folder's one tool script that extends EditorPlugin, the name is the folder's. A plugin.cfg on disk
// always wins. Enabling works like for any plugin, through editor_plugins/enabled (see EditorNode::set_addon_plugin_enabled()).
class MountedPackAddons {
public:
	struct Plugin {
		String config_path; // res://addons/<name>/plugin.cfg, also where no such file exists.
		String pack; // The mounted pack the addon comes from.
		Ref<ConfigFile> config; // The plugin.cfg provided in place of the missing file; null if the plugin can't be used.
		String error; // Why the plugin can't be used, if it can't.
	};

	enum Result {
		NOT_FROM_A_PACK, // Not a mounted pack addon's plugin.cfg, or the project has the plugin.cfg: the usual rules apply.
		NOT_LISTED, // A mounted pack has the addon, but editor/mounted_packs/addons doesn't list it (r_plugin.error says so).
		NO_PLUGIN, // The addon has no editor plugin, e.g. only a GDExtension.
		FOUND, // r_plugin.config, or r_plugin.error if the plugin can't be used here.
	};

	// The addon folders in editor/mounted_packs/addons that come from mounted packs, as res://addons/<name>.
	static Vector<String> get_listed_addons();
	// The plugin for p_config_path (res://addons/<name>/plugin.cfg).
	static Result get_plugin(const String &p_config_path, Plugin &r_plugin);
	// The plugins of all listed addons that don't have a plugin.cfg on disk.
	static Vector<Plugin> get_plugins();

	// Plugins in editor_plugins/enabled that can't be used right now (e.g. their script doesn't compile without the editor
	// build of their GDExtension). They stay in the list, so they work once that's fixed.
	static void set_unavailable_enabled(const String &p_config_path, bool p_unavailable);
	static bool is_unavailable_enabled(const String &p_config_path);
	static const HashSet<String> &get_unavailable_enabled();

private:
	static inline HashSet<String> unavailable_enabled;
};

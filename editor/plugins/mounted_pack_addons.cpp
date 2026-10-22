/**************************************************************************/
/*  mounted_pack_addons.cpp                                               */
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

#include "mounted_pack_addons.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/os.h"

static const char *ADDONS_SETTING = "editor/mounted_packs/addons";

// As in the setting: res://addons/<name>, or addons/<name>, with or without a trailing slash.
static String _normalize_addon(const String &p_entry) {
	String addon = p_entry.strip_edges().replace_char('\\', '/').trim_suffix("/");
	if (!addon.is_empty() && !addon.begins_with("res://")) {
		addon = "res://" + addon;
	}
	return addon;
}

static bool _is_listed(const String &p_addon) {
	if (!ProjectSettings::get_singleton()->has_setting(ADDONS_SETTING)) {
		return false;
	}
	const PackedStringArray entries = GLOBAL_GET(ADDONS_SETTING);
	for (const String &entry : entries) {
		if (_normalize_addon(entry) == p_addon) {
			return true;
		}
	}
	return false;
}

// The mounted pack that has the addon p_addon (res://addons/<name>), or an empty string; and the files directly in its folder.
static String _find_addon_pack(const HashMap<String, Vector<String>> &p_pack_files, const String &p_addon, Vector<String> *r_root_files = nullptr) {
	const String prefix = p_addon.trim_prefix("res://") + "/";
	String found;
	for (const KeyValue<String, Vector<String>> &E : p_pack_files) {
		for (const String &file : E.value) {
			if (!file.begins_with(prefix)) {
				continue;
			}
			if (found.is_empty()) {
				found = E.key;
			}
			if (r_root_files && !file.substr(prefix.length()).contains_char('/')) {
				r_root_files->push_back("res://" + file);
			}
		}
	}
	return found;
}

Vector<String> MountedPackAddons::get_listed_addons() {
	Vector<String> addons;
	PackedData *packed_data = PackedData::get_singleton();
	if (!packed_data || !packed_data->has_read_only_mounts() || !ProjectSettings::get_singleton()->has_setting(ADDONS_SETTING)) {
		return addons;
	}
	const HashMap<String, Vector<String>> pack_files = packed_data->get_read_only_file_paths();
	const PackedStringArray entries = GLOBAL_GET(ADDONS_SETTING);
	for (const String &entry : entries) {
		const String addon = _normalize_addon(entry);
		if (!addon.is_empty() && !addons.has(addon) && !_find_addon_pack(pack_files, addon).is_empty()) {
			addons.push_back(addon);
		}
	}
	return addons;
}

MountedPackAddons::Result MountedPackAddons::get_plugin(const String &p_config_path, Plugin &r_plugin) {
	r_plugin = Plugin();
	r_plugin.config_path = p_config_path;
	PackedData *packed_data = PackedData::get_singleton();
	if (!packed_data || !packed_data->has_read_only_mounts() || !p_config_path.begins_with("res://addons/") || p_config_path.get_file() != "plugin.cfg") {
		return NOT_FROM_A_PACK;
	}
	if (!packed_data->is_read_only_path(p_config_path) && FileAccess::exists(p_config_path)) {
		return NOT_FROM_A_PACK; // The project's own plugin.cfg.
	}

	const String addon = p_config_path.get_base_dir();
	Vector<String> root_files;
	const String pack = _find_addon_pack(packed_data->get_read_only_file_paths(), addon, &root_files);
	if (pack.is_empty()) {
		return NOT_FROM_A_PACK;
	}
	r_plugin.pack = pack;
	if (!_is_listed(addon)) {
		// Line breaks: tooltips and the warning dialog don't wrap.
		r_plugin.error = vformat(TTR("The addon \"%s\" is in the mounted pack \"%s\",\nbut editor/mounted_packs/addons doesn't list it, so its plugin isn't loaded.\nAdd \"%s\" to that setting and restart the editor to use it."), addon, pack.get_file(), addon);
		return NOT_LISTED;
	}

	// A pack that does contain the plugin.cfg (unusual for an exported pack).
	if (packed_data->is_read_only_path(p_config_path)) {
		Ref<ConfigFile> config;
		config.instantiate();
		if (config->load(p_config_path) == OK) {
			r_plugin.config = config;
		} else {
			r_plugin.error = vformat(TTR("The plugin.cfg of the addon \"%s\" in the mounted pack \"%s\" can't be read."), addon, pack.get_file());
		}
		return FOUND;
	}

	// The plugin script: a tool script directly in the addon's folder that extends EditorPlugin.
	Vector<String> scripts;
	for (const String &file : root_files) {
		const String script = file.trim_suffix(".remap"); // Exported packs often only have the .remap of a script.
		if (script.get_extension() == "gd" && !scripts.has(script)) {
			scripts.push_back(script);
		}
	}
	scripts.sort();

	// Classes the editor left out because they don't compile (see EditorFileSystem::remove_uncompilable_mounted_script_classes()).
	HashMap<String, String> uncompilable_bases; // Script path -> base class.
	for (const Variant &entry : packed_data->get_read_only_uncompilable_global_classes()) {
		const Dictionary script_class = entry;
		uncompilable_bases[script_class.get("path", String())] = script_class.get("base", String());
	}

	Vector<String> plugin_scripts;
	Vector<String> uncompilable_plugin_scripts;
	const bool printing_errors = Engine::get_singleton()->is_printing_error_messages();
	Engine::get_singleton()->set_print_error_messages(false); // Scripts that don't compile are reported below, once.
	for (const String &script : scripts) {
		if (uncompilable_bases.has(script)) {
			if (ClassDB::is_parent_class(uncompilable_bases[script], "EditorPlugin")) {
				uncompilable_plugin_scripts.push_back(script);
			}
			continue;
		}
		const Ref<Script> scr = ResourceLoader::load(script, "Script");
		if (scr.is_valid() && scr->is_valid() && scr->is_tool() && ClassDB::is_parent_class(scr->get_instance_base_type(), "EditorPlugin")) {
			plugin_scripts.push_back(script);
		}
	}
	Engine::get_singleton()->set_print_error_messages(printing_errors);

	if (plugin_scripts.size() == 1) {
		Ref<ConfigFile> config;
		config.instantiate();
		config->set_value("plugin", "name", addon.get_file());
		config->set_value("plugin", "description", vformat(TTR("From the read-only mounted pack \"%s\",\nwhich doesn't contain the addon's plugin.cfg; the editor found its plugin script."), pack.get_file()));
		config->set_value("plugin", "author", String());
		config->set_value("plugin", "version", String());
		config->set_value("plugin", "script", plugin_scripts[0].get_file());
		r_plugin.config = config;
		return FOUND;
	}
	if (plugin_scripts.size() > 1) {
		r_plugin.error = vformat(TTR("The addon \"%s\" of the mounted pack \"%s\" has several editor plugin scripts:\n%s\nso it's unclear which is the plugin. A plugin.cfg in the project's \"%s\" folder can name it."), addon, pack.get_file(), String(", ").join(plugin_scripts), addon);
		return FOUND;
	}
	if (!uncompilable_plugin_scripts.is_empty()) {
		const String libraries = OS::get_singleton()->get_executable_path().get_base_dir().path_join("gdextension_libraries");
		r_plugin.error = vformat(TTR("The editor plugin of the addon \"%s\" (%s, from the mounted pack \"%s\")\ncan't be compiled in the editor. It probably needs the editor build of the addon's GDExtension,\nwhich a game doesn't ship. Put that build's library files into this folder and restart the editor:\n%s"), addon, uncompilable_plugin_scripts[0].get_file(), pack.get_file(), libraries);
		return FOUND;
	}
	return NO_PLUGIN;
}

Vector<MountedPackAddons::Plugin> MountedPackAddons::get_plugins() {
	Vector<Plugin> plugins;
	for (const String &addon : get_listed_addons()) {
		Plugin plugin;
		if (get_plugin(addon.path_join("plugin.cfg"), plugin) == FOUND) {
			plugins.push_back(plugin);
		}
	}
	return plugins;
}

void MountedPackAddons::set_unavailable_enabled(const String &p_config_path, bool p_unavailable) {
	if (p_unavailable) {
		unavailable_enabled.insert(p_config_path);
	} else {
		unavailable_enabled.erase(p_config_path);
	}
}

bool MountedPackAddons::is_unavailable_enabled(const String &p_config_path) {
	return unavailable_enabled.has(p_config_path);
}

const HashSet<String> &MountedPackAddons::get_unavailable_enabled() {
	return unavailable_enabled;
}

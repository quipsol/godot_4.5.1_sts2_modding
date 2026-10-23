/**************************************************************************/
/*  mod_packers.cpp                                                       */
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

#include "mod_packers.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_method_pointer.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"

static bool _is_valid_id(const String &p_id) {
	if (p_id.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_id.length(); i++) {
		const char32_t c = p_id[i];
		if (!is_ascii_alphanumeric_char(c) && c != '.' && c != '_' && c != '-') {
			return false;
		}
	}
	return true;
}

// Reads one manifest. Returns false and sets r_error if it can't be used.
static bool _read_manifest(const String &p_manifest_path, ModPackers::Packer &r_packer, String &r_error) {
	String text = FileAccess::get_file_as_string(p_manifest_path);
	if (text.begins_with(String::chr(0xFEFF))) {
		text = text.substr(1); // Byte order mark, e.g. from Visual Studio.
	}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(text) != OK) {
		r_error = vformat("Invalid JSON at line %d: %s", json->get_error_line() + 1, json->get_error_message());
		return false;
	}
	if (json->get_data().get_type() != Variant::DICTIONARY) {
		r_error = "The manifest must be a JSON object: { ... }.";
		return false;
	}
	const Dictionary data = json->get_data();
	const auto get_string = [&data](const String &p_key) -> String {
		const Variant value = data.get(p_key, Variant());
		return value.get_type() == Variant::NIL ? String() : String(value).strip_edges();
	};

	r_packer.id = get_string("id");
	if (r_packer.id.is_empty()) {
		r_error = "\"id\" is missing.";
		return false;
	}
	if (!_is_valid_id(r_packer.id)) {
		r_error = vformat("\"id\" \"%s\" may only contain letters, digits, '.', '_' and '-'.", r_packer.id);
		return false;
	}

	const String script = get_string("script");
	if (script.is_empty()) {
		r_error = "\"script\" is missing. It's the file name of the packer script, e.g. \"pack_mod.gd\".";
		return false;
	}
	if (!script.is_valid_filename() || script == "." || script == "..") {
		r_error = vformat("\"script\" \"%s\" must be the name of a script in the manifest's folder, not a path.", script);
		return false;
	}
	r_packer.script_path = p_manifest_path.get_base_dir().path_join(script);
	if (!FileAccess::exists(r_packer.script_path)) {
		r_error = vformat("The script \"%s\" doesn't exist.", r_packer.script_path);
		return false;
	}

	r_packer.name = get_string("name");
	if (r_packer.name.is_empty()) {
		r_packer.name = r_packer.id;
	}
	r_packer.author = get_string("author");
	r_packer.version = get_string("version");
	r_packer.description = get_string("description");
	r_packer.manifest_path = p_manifest_path;
	return true;
}

struct _PackerNameComparator {
	bool operator()(const ModPackers::Packer &p_a, const ModPackers::Packer &p_b) const {
		const int order = p_a.name.naturalnocasecmp_to(p_b.name);
		return order != 0 ? order < 0 : p_a.id < p_b.id;
	}
};

const ModPackers::Packer *ModPackers::ScanResult::find(const String &p_id) const {
	for (const Packer &packer : packers) {
		if (packer.id == p_id) {
			return &packer;
		}
	}
	return nullptr;
}

String ModPackers::get_directory() {
	return OS::get_singleton()->get_executable_path().get_base_dir().path_join("mod_packers");
}

// Like the pre-patches: only the direct subfolders of mod_packers/ are searched, and an id used by several packers loads
// none of them, so what is used never depends on the folders' order.
ModPackers::ScanResult ModPackers::scan() {
	ScanResult result;
	const String directory = get_directory();
	Ref<DirAccess> dir = DirAccess::open(directory);
	if (dir.is_null()) {
		return result; // No packers installed.
	}

	Vector<String> folders;
	dir->list_dir_begin();
	for (String folder = dir->get_next(); !folder.is_empty(); folder = dir->get_next()) {
		if (dir->current_is_dir() && folder != "." && folder != "..") {
			folders.push_back(folder);
		}
	}
	dir->list_dir_end();
	folders.sort();

	Vector<Packer> found;
	for (const String &folder : folders) {
		const String manifest_path = directory.path_join(folder).path_join(MANIFEST_FILE_NAME);
		if (!FileAccess::exists(manifest_path)) {
			continue; // Not a packer.
		}
		Packer packer;
		String error;
		if (_read_manifest(manifest_path, packer, error)) {
			found.push_back(packer);
		} else {
			result.problems.push_back({ manifest_path, error });
		}
	}

	HashMap<String, Vector<String>> manifests_by_id;
	for (const Packer &packer : found) {
		manifests_by_id[packer.id].push_back(packer.manifest_path);
	}
	for (const Packer &packer : found) {
		const Vector<String> &manifests = manifests_by_id[packer.id];
		if (manifests.size() == 1) {
			result.packers.push_back(packer);
			continue;
		}
		Vector<String> others;
		for (const String &manifest : manifests) {
			if (manifest != packer.manifest_path) {
				others.push_back("\"" + manifest + "\"");
			}
		}
		result.problems.push_back({ packer.manifest_path, vformat("The id \"%s\" is also used by %s. None of them is used.", packer.id, String(", ").join(others)) });
	}

	result.packers.sort_custom<_PackerNameComparator>();
	return result;
}

static String _globalize(const String &p_path) {
	String path = ProjectSettings::get_singleton()->globalize_path(p_path.strip_edges().replace_char('\\', '/'));
	if (path.is_relative_path()) {
		path = ProjectSettings::get_singleton()->get_resource_path().path_join(path);
	}
	return path.simplify_path();
}

String ModPackers::find_script(const String &p_packer, String &r_error) {
	const String packer = p_packer.strip_edges();
	if (packer.get_extension() == "gd" || packer.contains_char('/') || packer.contains_char('\\')) {
		// A script by path, which needs no manifest.
		const String path = _globalize(packer);
		if (!FileAccess::exists(path)) {
			r_error = vformat("The packer script \"%s\" doesn't exist.", path);
			return String();
		}
		return path;
	}

	const ScanResult installed = scan();
	const Packer *found = installed.find(packer);
	if (found) {
		return found->script_path;
	}
	r_error = vformat("No mod packer with the id \"%s\" is installed in \"%s\".", packer, get_directory());
	for (const Problem &problem : installed.problems) {
		r_error += vformat("\n    %s: %s", problem.manifest_path, problem.message);
	}
	return String();
}

String ModPackers::get_mod_name() {
	const String assembly_name = GLOBAL_GET("dotnet/project/assembly_name");
	return assembly_name.is_empty() ? String(GLOBAL_GET("application/config/name")) : assembly_name;
}

static bool _is_mounted(const String &p_path) {
	PackedData *packed_data = PackedData::get_singleton();
	if (!packed_data || !packed_data->has_read_only_mounts()) {
		return false;
	}
	return packed_data->is_read_only_path(p_path) || packed_data->is_read_only_path(p_path + ".import") || packed_data->is_read_only_path(p_path + ".remap");
}

Error ModPackers::pack(const String &p_script_path, const String &p_output_path, const Vector<String> &p_args, String &r_error) {
	const String output_path = _globalize(p_output_path);
	if (p_output_path.strip_edges().is_empty() || output_path.get_extension().to_lower() != "pck") {
		r_error = vformat("The output path must be a .pck file, not \"%s\".", p_output_path);
		return ERR_INVALID_PARAMETER;
	}

	const Ref<Script> scr = ResourceLoader::load(p_script_path, "Script");
	if (scr.is_null()) {
		r_error = vformat("The packer script \"%s\" can't be loaded.", p_script_path);
		return ERR_CANT_OPEN;
	}
	if (!scr->is_valid()) {
		r_error = vformat("The packer script \"%s\" has errors (see the output above).", p_script_path);
		return ERR_PARSE_ERROR;
	}
	// In the editor, only tool scripts run.
	if (!scr->is_tool()) {
		r_error = vformat("The packer script \"%s\" must be a @tool script.", p_script_path);
		return ERR_INVALID_DATA;
	}
	const StringName base = scr->get_instance_base_type();
	if (!ClassDB::is_parent_class(base, "RefCounted")) {
		r_error = vformat("The packer script \"%s\" must extend RefCounted, not %s.", p_script_path, base);
		return ERR_INVALID_DATA;
	}

	Ref<RefCounted> packer = Object::cast_to<RefCounted>(ClassDB::instantiate(base));
	ERR_FAIL_COND_V(packer.is_null(), ERR_CANT_CREATE);
	packer->set_script(scr);
	if (!packer->has_method("pack")) {
		r_error = vformat("The packer script \"%s\" has no pack(context: Dictionary) -> Error function.", p_script_path);
		return ERR_INVALID_DATA;
	}

	const Error dir_error = DirAccess::make_dir_recursive_absolute(output_path.get_base_dir());
	if (dir_error != OK) {
		r_error = vformat("Couldn't create the folder \"%s\" (%s).", output_path.get_base_dir(), error_names[dir_error]);
		return dir_error;
	}

	PackedStringArray args;
	for (const String &arg : p_args) {
		args.push_back(arg);
	}
	Dictionary context;
	context["output_path"] = output_path;
	context["mod_name"] = get_mod_name();
	context["project_path"] = ProjectSettings::get_singleton()->get_resource_path();
	context["args"] = args;
	context["is_mounted"] = callable_mp_static(&_is_mounted);

	print_line(vformat("Packing mod \"%s\" into \"%s\" with \"%s\"...", get_mod_name(), output_path, p_script_path));
	const Variant arg = context;
	const Variant *argptr = &arg;
	Callable::CallError call_error;
	const Variant result = packer->callp("pack", &argptr, 1, call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		r_error = vformat("The packer script's pack() can't be called with the context: %s.", Variant::get_callable_error_text(Callable(packer.ptr(), "pack"), &argptr, 1, call_error));
		return ERR_INVALID_DATA;
	}
	const Error err = result.get_type() == Variant::INT ? Error(int(result)) : (result.get_type() == Variant::NIL ? OK : FAILED);
	if (err != OK) {
		r_error = vformat("The packer script \"%s\" failed: %s.", p_script_path, error_names[err]);
	}
	return err;
}

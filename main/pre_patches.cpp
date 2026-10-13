/**************************************************************************/
/*  pre_patches.cpp                                                       */
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

#include "pre_patches.h"

#ifdef TOOLS_ENABLED

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
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
static bool _read_manifest(const String &p_manifest_path, PrePatches::PrePatch &r_pre_patch, String &r_error) {
	String text = FileAccess::get_file_as_string(p_manifest_path);
	if (text.begins_with(String::chr(0xFEFF))) {
		text = text.substr(1); // Byte order mark, e.g. from Visual Studio.
	}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(text) != OK) {
		r_error = vformat("Invalid JSON at line %d: %s", json->get_error_line(), json->get_error_message());
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

	r_pre_patch.id = get_string("id");
	if (r_pre_patch.id.is_empty()) {
		r_error = "\"id\" is missing.";
		return false;
	}
	if (!_is_valid_id(r_pre_patch.id)) {
		r_error = vformat("\"id\" \"%s\" may only contain letters, digits, '.', '_' and '-'.", r_pre_patch.id);
		return false;
	}

	const String assembly = get_string("assembly");
	if (assembly.is_empty()) {
		r_error = "\"assembly\" is missing. It's the file name of the pre-patch's .dll, without \".dll\".";
		return false;
	}
	if (assembly.to_lower().ends_with(".dll")) {
		r_error = vformat("\"assembly\" is the file name without \".dll\": \"%s\" instead of \"%s\".", assembly.get_basename(), assembly);
		return false;
	}
	if (!assembly.is_valid_filename() || assembly == "." || assembly == "..") {
		r_error = vformat("\"assembly\" \"%s\" must be the name of a .dll in the manifest's folder, not a path.", assembly);
		return false;
	}
	r_pre_patch.assembly_path = p_manifest_path.get_base_dir().path_join(assembly + ".dll");
	if (!FileAccess::exists(r_pre_patch.assembly_path)) {
		r_error = vformat("The assembly \"%s\" doesn't exist.", r_pre_patch.assembly_path);
		return false;
	}

	r_pre_patch.name = get_string("name");
	if (r_pre_patch.name.is_empty()) {
		r_pre_patch.name = r_pre_patch.id;
	}
	r_pre_patch.author = get_string("author");
	r_pre_patch.version = get_string("version");
	r_pre_patch.description = get_string("description");
	r_pre_patch.manifest_path = p_manifest_path;
	return true;
}

struct _PrePatchNameComparator {
	bool operator()(const PrePatches::PrePatch &p_a, const PrePatches::PrePatch &p_b) const {
		const int order = p_a.name.naturalnocasecmp_to(p_b.name);
		return order != 0 ? order < 0 : p_a.id < p_b.id;
	}
};

const PrePatches::PrePatch *PrePatches::ScanResult::find(const String &p_id) const {
	for (const PrePatch &pre_patch : pre_patches) {
		if (pre_patch.id == p_id) {
			return &pre_patch;
		}
	}
	return nullptr;
}

String PrePatches::get_directory() {
	return OS::get_singleton()->get_executable_path().get_base_dir().path_join("pre_patches");
}

// Only the direct subfolders of pre_patches/ are searched, so a pre-patch can organize its own folder freely.
PrePatches::ScanResult PrePatches::scan() {
	ScanResult result;
	const String directory = get_directory();
	Ref<DirAccess> dir = DirAccess::open(directory);
	if (dir.is_null()) {
		return result; // No pre-patches installed.
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

	Vector<PrePatch> found;
	for (const String &folder : folders) {
		const String manifest_path = directory.path_join(folder).path_join(MANIFEST_FILE_NAME);
		if (!FileAccess::exists(manifest_path)) {
			continue; // Not a pre-patch.
		}
		PrePatch pre_patch;
		String error;
		if (_read_manifest(manifest_path, pre_patch, error)) {
			found.push_back(pre_patch);
		} else {
			result.problems.push_back({ manifest_path, error });
		}
	}

	// An id used by several pre-patches loads none of them, so what is loaded never depends on the folders' order.
	HashMap<String, Vector<String>> manifests_by_id;
	for (const PrePatch &pre_patch : found) {
		manifests_by_id[pre_patch.id].push_back(pre_patch.manifest_path);
	}
	for (const PrePatch &pre_patch : found) {
		const Vector<String> &manifests = manifests_by_id[pre_patch.id];
		if (manifests.size() == 1) {
			result.pre_patches.push_back(pre_patch);
			continue;
		}
		Vector<String> others;
		for (const String &manifest : manifests) {
			if (manifest != pre_patch.manifest_path) {
				others.push_back("\"" + manifest + "\"");
			}
		}
		result.problems.push_back({ pre_patch.manifest_path, vformat("The id \"%s\" is also used by %s. None of them is loaded.", pre_patch.id, String(", ").join(others)) });
		if (!result.conflicting_ids.has(pre_patch.id)) {
			result.conflicting_ids.push_back(pre_patch.id);
		}
	}

	result.pre_patches.sort_custom<_PrePatchNameComparator>();
	return result;
}

#endif // TOOLS_ENABLED

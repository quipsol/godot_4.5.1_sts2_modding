/**************************************************************************/
/*  mod_packers.h                                                         */
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

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Mod packers installed with the editor: scripts that pack a project's mod into a .pck, for games that load mods as packs.
// Unlike Godot's export, a packer decides exactly what goes in (an export always adds project.binary and the project's
// caches, which would replace the game's own files when the mod's pack is loaded). Each one is a folder in
// <editor folder>/mod_packers/ (the folder's name doesn't matter) with a mod_packer_manifest.json:
//
// {
//   "id": "modot.default",      required, unique; letters, digits, '.', '_' and '-'
//   "script": "pack_mod.gd",    required, the packer script, in the manifest's folder
//   "name": "Default",          optional, shown in the editor (default: the id)
//   "author": "...", "version": "v1.0.0", "description": "..."   optional, shown in the editor
// }
//
// A packer script is a @tool script that extends RefCounted (it runs in the editor) and has
//
//   func pack(context: Dictionary) -> Error
//
// with these context entries:
//   output_path   String, absolute path of the .pck to write; its folder exists.
//   mod_name      String, the project's .NET assembly name, or its name if it has none.
//   project_path  String, the project's folder, absolute.
//   args          PackedStringArray, the command line arguments after "--" (empty when packing from the editor's menu).
//   is_mounted    Callable(path: String) -> bool, whether a res:// path comes from a read-only mounted pack (see
//                 editor/mounted_packs/paths); such files belong to the game, not the mod.
//
// Projects pack with Project > Pack Mod..., or with --pack-mod <id or script path> <output path> (after the first file
// scan and import, like --export-pack).
class ModPackers {
public:
	static constexpr const char *MANIFEST_FILE_NAME = "mod_packer_manifest.json";
	static constexpr const char *PACKER_SETTING = "editor/mod_packer/packer"; // The packer's id, in project.godot.
	static constexpr const char *OUTPUT_PATH_SETTING = "editor/mod_packer/output_path"; // Machine-specific (override.cfg).

	struct Packer {
		String id;
		String name;
		String script_path; // Absolute path to the script.
		String author;
		String version;
		String description;
		String manifest_path;
	};

	struct Problem {
		String manifest_path;
		String message;
	};

	struct ScanResult {
		Vector<Packer> packers; // Valid and with a unique id, sorted by name.
		Vector<Problem> problems; // Manifests that can't be used, e.g. invalid JSON or an id used twice.

		const Packer *find(const String &p_id) const;
	};

	static String get_directory(); // <editor folder>/mod_packers
	static ScanResult scan();

	// The script of p_packer, a packer's id or the path to a packer script (relative paths are relative to the project).
	// Returns an empty string and sets r_error if there's no such packer.
	static String find_script(const String &p_packer, String &r_error);
	static String get_mod_name();
	// Runs the packer script. p_output_path may be relative to the project. Sets r_error if it fails.
	static Error pack(const String &p_script_path, const String &p_output_path, const Vector<String> &p_args, String &r_error);
};

/**************************************************************************/
/*  pre_patches.h                                                         */
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

#ifdef TOOLS_ENABLED

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Pre-patches installed with the editor: .NET assemblies whose module initializers run before the project run from the
// editor starts (e.g. to patch a game's assembly with Harmony). Each one is a folder in <editor folder>/pre_patches/
// (the folder's name doesn't matter) with a pre_patch_manifest.json, which names the assembly in that folder:
//
// {
//   "id": "moddot.native.printlog",       required, unique; letters, digits, '.', '_' and '-'
//   "assembly": "moddot.native.printlog", required, the .dll's file name without ".dll"
//   "name": "PrintLog",                   optional, shown in the editor (default: the id)
//   "author": "...", "version": "v1.0.0", "description": "..."   optional, shown in the editor
// }
//
// Projects enable pre-patches by id in editor/pre_patches/enabled. See Main::get_pre_patch_paths() and GDMono.
class PrePatches {
public:
	static constexpr const char *MANIFEST_FILE_NAME = "pre_patch_manifest.json";
	static constexpr const char *ENABLED_SETTING = "editor/pre_patches/enabled";

	struct PrePatch {
		String id;
		String name;
		String assembly_path; // Absolute path to the .dll.
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
		Vector<PrePatch> pre_patches; // Valid and with a unique id, sorted by name.
		Vector<Problem> problems; // Manifests that can't be used, e.g. invalid JSON or an id used twice.
		Vector<String> conflicting_ids; // Ids used by several manifests; none of them is loaded.

		const PrePatch *find(const String &p_id) const;
	};

	static String get_directory(); // <editor folder>/pre_patches
	static ScanResult scan();
};

#endif // TOOLS_ENABLED

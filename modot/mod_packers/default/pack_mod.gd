@tool
extends RefCounted

## MoDot's default mod packer (see editor/export/mod_packers.h in MoDot for how packers are called).
##
## Packs:
## - res://<mod name>/, with the imported data of its imported files instead of their sources;
## - the C# scripts of res://<mod name>Code/ with their .uid files (the game runs the mod's .dll; the scripts help other
##   people's editors, e.g. when looking at the mod's nodes in the remote scene tree);
## - the project files these depend on, e.g. a shader outside the mod folder that a mod scene uses;
##   never files from read-only mounted packs (the game's), which the game has itself.
## Arguments after "--" on the command line are extra folders to pack, e.g. "addons/my_addon".


func pack(context: Dictionary) -> Error:
	var mod_name: String = context.mod_name
	var is_mounted: Callable = context.is_mounted

	var folders: PackedStringArray = ["res://" + mod_name]
	for arg: String in context.args:
		folders.append(arg if arg.begins_with("res://") else "res://" + arg.trim_prefix("/"))

	var files := {} # res:// path -> true, the files to pack.
	for folder in folders:
		if not DirAccess.dir_exists_absolute(folder):
			push_error("Mod packer: the folder %s doesn't exist." % folder)
			return ERR_FILE_NOT_FOUND
		_add_folder(folder, files, is_mounted)
	_add_csharp_scripts("res://" + mod_name + "Code", files)
	_add_dependencies(files, is_mounted)

	var packer := PCKPacker.new()
	var err := packer.pck_start(context.output_path, 16)
	if err != OK:
		push_error("Mod packer: couldn't start the pack %s (%s)." % [context.output_path, error_string(err)])
		return err
	var paths: Array = files.keys()
	paths.sort()
	for path: String in paths:
		err = packer.add_file(path, path)
		if err != OK:
			push_error("Mod packer: couldn't add %s (%s)." % [path, error_string(err)])
			return err
	err = packer.flush(true)
	if err == OK:
		print("Mod packer: packed %d files into %s." % [paths.size(), context.output_path])
	return err


# Every file in p_folder and its subfolders, as _add_file() decides.
func _add_folder(p_folder: String, r_files: Dictionary, p_is_mounted: Callable) -> void:
	var dir := DirAccess.open(p_folder)
	if dir == null:
		return
	for subfolder in dir.get_directories():
		if not subfolder.begins_with("."):
			_add_folder(p_folder.path_join(subfolder), r_files, p_is_mounted)
	for file in dir.get_files():
		var path := p_folder.path_join(file)
		if file.ends_with(".import"):
			continue # Added with the file it belongs to.
		_add_file(path, r_files, p_is_mounted)


# A file and what it needs in the pack: an imported file goes in as its .import and imported data (which the game
# loads), not as its source.
func _add_file(p_path: String, r_files: Dictionary, p_is_mounted: Callable) -> void:
	if r_files.has(p_path) or p_is_mounted.call(p_path):
		return
	if FileAccess.file_exists(p_path + ".import"):
		var import_file := ConfigFile.new()
		if import_file.load(p_path + ".import") == OK and import_file.get_value("remap", "importer", "") != "keep":
			r_files[p_path + ".import"] = true
			# The imported data: "path", or one "path.<feature>" per platform's texture format.
			var keys := import_file.get_section_keys("remap") if import_file.has_section("remap") else PackedStringArray()
			for key in keys:
				if key == "path" or key.begins_with("path."):
					var imported: String = import_file.get_value("remap", key)
					if FileAccess.file_exists(imported):
						r_files[imported] = true
			return
	r_files[p_path] = true
	if FileAccess.file_exists(p_path + ".uid"):
		r_files[p_path + ".uid"] = true # E.g. a GDScript's UID.


# The C# scripts and their .uid files, so that the game resolves the uid:// references of the mod's scenes to them.
func _add_csharp_scripts(p_folder: String, r_files: Dictionary) -> void:
	var dir := DirAccess.open(p_folder)
	if dir == null:
		return
	for subfolder in dir.get_directories():
		if not subfolder.begins_with("."):
			_add_csharp_scripts(p_folder.path_join(subfolder), r_files)
	for file in dir.get_files():
		if file.ends_with(".cs") or file.ends_with(".cs.uid"):
			r_files[p_folder.path_join(file)] = true


# What the packed resources depend on outside the packed folders, recursively; never the game's files.
func _add_dependencies(r_files: Dictionary, p_is_mounted: Callable) -> void:
	var queue: Array = r_files.keys()
	while not queue.is_empty():
		var path: String = queue.pop_back()
		if path.begins_with("res://.godot/") or path.get_extension() in ["import", "uid"]:
			continue
		for dependency in ResourceLoader.get_dependencies(path):
			# "uid://...::Type::res://path", or just the path.
			var dependency_path: String = dependency.get_slice("::", 2) if dependency.contains("::") else dependency
			if dependency_path.is_empty() or not dependency_path.begins_with("res://") or r_files.has(dependency_path):
				continue
			if p_is_mounted.call(dependency_path) or not FileAccess.file_exists(dependency_path):
				continue # The game's, or missing.
			var before := r_files.size()
			_add_file(dependency_path, r_files, p_is_mounted)
			if r_files.size() > before:
				queue.push_back(dependency_path)

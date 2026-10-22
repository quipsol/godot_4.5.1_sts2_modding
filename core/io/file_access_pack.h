/**************************************************************************/
/*  file_access_pack.h                                                    */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/mutex.h"
#include "core/os/rw_lock.h"
#include "core/os/thread.h"
#include "core/string/print_string.h"
#include "core/templates/safe_refcount.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"

// Godot's packed file magic header ("GDPC" in ASCII).
#define PACK_HEADER_MAGIC 0x43504447

#define PACK_FORMAT_VERSION_V2 2
#define PACK_FORMAT_VERSION_V3 3

// The current packed file format version number.
#define PACK_FORMAT_VERSION PACK_FORMAT_VERSION_V3

enum PackFlags {
	PACK_DIR_ENCRYPTED = 1 << 0,
	PACK_REL_FILEBASE = 1 << 1,
	PACK_SPARSE_BUNDLE = 1 << 2,
};

enum PackFileFlags {
	PACK_FILE_ENCRYPTED = 1 << 0,
	PACK_FILE_REMOVAL = 1 << 1,
};

class PackSource;

class PackedData {
	friend class FileAccessPack;
	friend class DirAccessPack;
	friend class PackSource;

public:
	struct PackedFile {
		String pack;
		uint64_t offset; //if offset is ZERO, the file was ERASED
		uint64_t size;
		uint8_t md5[16];
		PackSource *src = nullptr;
		bool encrypted;
		bool bundle;
		bool read_only = false; // Added by mount_read_only_pack(); files on disk take priority over it.
	};

private:
	struct PackedDir {
		PackedDir *parent = nullptr;
		String name;
		HashMap<String, PackedDir *> subdirs;
		HashSet<String> files;
	};

	struct PathMD5 {
		uint64_t a = 0;
		uint64_t b = 0;

		bool operator==(const PathMD5 &p_val) const {
			return (a == p_val.a) && (b == p_val.b);
		}
		static uint32_t hash(const PathMD5 &p_val) {
			uint32_t h = hash_murmur3_one_32(p_val.a);
			return hash_fmix32(hash_murmur3_one_32(p_val.b, h));
		}

		PathMD5() {}

		explicit PathMD5(const Vector<uint8_t> &p_buf) {
			a = *((uint64_t *)&p_buf[0]);
			b = *((uint64_t *)&p_buf[8]);
		}
	};

	HashMap<PathMD5, PackedFile, PathMD5> files;

	Vector<PackSource *> sources;

	PackedDir *root = nullptr;

	static inline PackedData *singleton = nullptr;
	bool disabled = false;

	// Read-only mounts (see mount_read_only_pack()).
	bool mounting_read_only = false; // True while mount_read_only_pack() is parsing a pack.
	int read_only_mount_count = 0;
	Vector<String> mounted_read_only_packs; // In mount order.
	Vector<String> command_line_read_only_packs; // From --mount-pack.
	Vector<String> read_only_disk_collisions; // Files skipped during the current mount because they exist on disk.
	int read_only_pack_collisions = 0; // Files skipped during the current mount because an earlier pack provided them.
	PackedFile read_only_uid_cache; // The current mount's .godot/uid_cache.bin, read after parsing.
	bool has_read_only_uid_cache = false;
	PackedFile read_only_class_cache; // The current mount's .godot/global_script_class_cache.cfg, read after parsing.
	bool has_read_only_class_cache = false;
	Array read_only_global_classes; // See get_read_only_global_classes().
	Array read_only_uncompilable_global_classes; // See remove_read_only_global_classes().

	// Read-only mounts can be remounted while other threads (file system scans, previews, threaded loads) look files up,
	// so once they exist, the index is guarded. The thread that is mounting doesn't lock again: opening the .pck
	// goes through FileAccess::open(), which looks the path up here.
	mutable RWLock read_only_lock;
	bool read_only_lock_enabled = false;
	SafeNumeric<uint64_t> read_only_lock_writer;

	class MountsReadLock {
		const PackedData *packed_data;
		bool locked;

	public:
		_FORCE_INLINE_ MountsReadLock(const PackedData *p_packed_data) :
				packed_data(p_packed_data),
				locked(p_packed_data->read_only_lock_enabled && p_packed_data->read_only_lock_writer.get() != Thread::get_caller_id()) {
			if (locked) {
				packed_data->read_only_lock.read_lock();
			}
		}
		_FORCE_INLINE_ ~MountsReadLock() {
			if (locked) {
				packed_data->read_only_lock.read_unlock();
			}
		}
	};
	void _lock_mounts_for_writing();
	void _unlock_mounts_for_writing();

	Mutex shadow_warning_mutex;
	HashSet<String> shadow_warnings; // Mounted paths already reported as replaced by a project file.

	int _register_read_only_uids(PackedFile p_uid_cache, const String &p_pack_path);
	int _register_read_only_global_classes(PackedFile p_class_cache, const String &p_pack_path);
	bool _remove_read_only_files(PackedDir *p_dir, const String &p_dir_path); // Returns true if anything was removed.
	void _get_read_only_file_paths(const PackedDir *p_dir, const String &p_dir_path, HashMap<String, Vector<String>> &r_paths) const;
	void _clear_read_only_state();
	bool _yields_to_disk(const PackedFile &p_file, const String &p_path); // Read-only entry shadowed by a project file (warns once).

	void _free_packed_dirs(PackedDir *p_dir);
	void _get_file_paths(PackedDir *p_dir, const String &p_parent_dir, HashSet<String> &r_paths) const;

	static bool _is_excluded_from_read_only_mount(const String &p_simplified_path);
	static bool _file_exists_on_disk(const String &p_path);
	static bool _is_shadowed_by_disk(const String &p_path); // The file, or the file a sidecar (.remap, .import, .uid) belongs to, exists on disk.
	static bool _dir_exists_on_disk(const String &p_path);

public:
	void add_pack_source(PackSource *p_source);
	void add_path(const String &p_pkg_path, const String &p_path, uint64_t p_ofs, uint64_t p_size, const uint8_t *p_md5, PackSource *p_src, bool p_replace_files, bool p_encrypted = false, bool p_bundle = false); // for PackSource
	void remove_path(const String &p_path);
	uint8_t *get_file_hash(const String &p_path);
	HashSet<String> get_file_paths() const;

	void set_disabled(bool p_disabled) { disabled = p_disabled; }
	_FORCE_INLINE_ bool is_disabled() const { return disabled; }

	static PackedData *get_singleton() { return singleton; }
	Error add_pack(const String &p_path, bool p_replace_files, uint64_t p_offset);

	// Read-only mounts: packs mounted on top of the project without taking it over. Unlike ProjectSettings::load_resource_pack(),
	// res:// is not snapshotted and directory access stays on disk. A file that exists in the project always wins over a
	// pack's file at the same path, also when it is created later (it "shadows" the pack's file, with a warning).
	// The packs' UIDs are registered with ResourceUID, but never saved to the project's cache.
	Error mount_read_only_pack(const String &p_path);
	void add_command_line_read_only_pack(const String &p_path); // From --mount-pack (absolute path).
	Vector<String> get_configured_read_only_packs() const; // --mount-pack packs first, then editor/mounted_packs/paths.
	void mount_configured_read_only_packs();
	void unmount_read_only_packs(); // Removes only the read-only mounts and their UIDs.
	void remount_read_only_packs(); // Unmounts, then mounts the configured packs again (e.g. after a pack changed on disk).
	const Vector<String> &get_mounted_read_only_packs() const { return mounted_read_only_packs; }
	// The files of the read-only mounted packs by pack, as stored in them (relative to res://, sidecars and .godot/ included).
	HashMap<String, Vector<String>> get_read_only_file_paths() const;
	// The global script classes (class_name) of the read-only mounted packs, from their .godot/global_script_class_cache.cfg,
	// in the same format. Only the classes whose scripts a pack provides itself; on a name collision the earlier pack wins.
	// ProjectSettings adds them to the project's global classes, but never saves them to the project's cache.
	Array get_read_only_global_classes() const;
	// Takes classes that can't be compiled here out of get_read_only_global_classes(), until remounting.
	// They are kept, in the same format, in get_read_only_uncompilable_global_classes(), e.g. to explain why.
	void remove_read_only_global_classes(const HashSet<String> &p_classes);
	Array get_read_only_uncompilable_global_classes() const;
	bool has_read_only_mounts() const { return read_only_mount_count > 0; }
	_FORCE_INLINE_ bool is_read_only_path(const String &p_path); // Served from a read-only mount (not shadowed by the disk).
	// Same for a script, which an exported pack often only contains as its .remap (e.g. GDScript compiled to .gdc).
	bool is_read_only_script(const String &p_path) { return is_read_only_path(p_path) || is_read_only_path(p_path + ".remap"); }
	// The read-only mounted pack whose file a write to p_path would shadow, or an empty string.
	// Paths under .godot/ never count: that is the project's own cache (e.g. imported data of a project file).
	String get_pack_shadowed_by_writing(const String &p_path);
	void warn_shadowed(const String &p_path, const String &p_pack); // Reports once per path that the project now replaces a pack's file.

	void clear();

	_FORCE_INLINE_ Ref<FileAccess> try_open_path(const String &p_path);
	_FORCE_INLINE_ bool has_path(const String &p_path);

	_FORCE_INLINE_ int64_t get_size(const String &p_path);

	_FORCE_INLINE_ Ref<DirAccess> try_open_directory(const String &p_path);
	_FORCE_INLINE_ bool has_directory(const String &p_path);

	PackedData();
	~PackedData();
};

class PackSource {
public:
	virtual bool try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset) = 0;
	virtual Ref<FileAccess> get_file(const String &p_path, PackedData::PackedFile *p_file) = 0;
	virtual ~PackSource() {}
};

class PackedSourcePCK : public PackSource {
public:
	virtual bool try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset) override;
	virtual Ref<FileAccess> get_file(const String &p_path, PackedData::PackedFile *p_file) override;
};

class PackedSourceDirectory : public PackSource {
	void add_directory(const String &p_path, bool p_replace_files);

public:
	virtual bool try_open_pack(const String &p_path, bool p_replace_files, uint64_t p_offset) override;
	virtual Ref<FileAccess> get_file(const String &p_path, PackedData::PackedFile *p_file) override;
};

class FileAccessPack : public FileAccess {
	GDSOFTCLASS(FileAccessPack, FileAccess);
	PackedData::PackedFile pf;

	mutable uint64_t pos;
	mutable bool eof;
	uint64_t off;

	Ref<FileAccess> f;
	virtual Error open_internal(const String &p_path, int p_mode_flags) override;
	virtual uint64_t _get_modified_time(const String &p_file) override { return 0; }
	virtual uint64_t _get_access_time(const String &p_file) override { return 0; }
	virtual int64_t _get_size(const String &p_file) override { return -1; }
	virtual BitField<FileAccess::UnixPermissionFlags> _get_unix_permissions(const String &p_file) override { return 0; }
	virtual Error _set_unix_permissions(const String &p_file, BitField<FileAccess::UnixPermissionFlags> p_permissions) override { return FAILED; }

	virtual bool _get_hidden_attribute(const String &p_file) override { return false; }
	virtual Error _set_hidden_attribute(const String &p_file, bool p_hidden) override { return ERR_UNAVAILABLE; }
	virtual bool _get_read_only_attribute(const String &p_file) override { return false; }
	virtual Error _set_read_only_attribute(const String &p_file, bool p_ro) override { return ERR_UNAVAILABLE; }

public:
	virtual bool is_open() const override;

	virtual void seek(uint64_t p_position) override;
	virtual void seek_end(int64_t p_position = 0) override;
	virtual uint64_t get_position() const override;
	virtual uint64_t get_length() const override;

	virtual bool eof_reached() const override;

	virtual uint64_t get_buffer(uint8_t *p_dst, uint64_t p_length) const override;

	virtual void set_big_endian(bool p_big_endian) override;

	virtual Error get_error() const override;

	virtual Error resize(int64_t p_length) override { return ERR_UNAVAILABLE; }
	virtual void flush() override;
	virtual bool store_buffer(const uint8_t *p_src, uint64_t p_length) override;

	virtual bool file_exists(const String &p_name) override;

	virtual void close() override;

	FileAccessPack(const String &p_path, const PackedData::PackedFile &p_file);
};

int64_t PackedData::get_size(const String &p_path) {
	String simplified_path = p_path.simplify_path();
	PathMD5 pmd5(simplified_path.md5_buffer());
	MountsReadLock lock(this);
	HashMap<PathMD5, PackedFile, PathMD5>::Iterator E = files.find(pmd5);
	if (!E) {
		return -1; // File not found.
	}
	if (E->value.offset == 0) {
		return -1; // File was erased.
	}
	return E->value.size;
}

Ref<FileAccess> PackedData::try_open_path(const String &p_path) {
	String simplified_path = p_path.simplify_path().trim_prefix("res://");
	PathMD5 pmd5(simplified_path.md5_buffer());
	PackedFile file;
	{
		MountsReadLock lock(this);
		HashMap<PathMD5, PackedFile, PathMD5>::Iterator E = files.find(pmd5);
		if (!E) {
			return nullptr; // Not found.
		}
		if (E->value.read_only && _yields_to_disk(E->value, p_path)) {
			return nullptr; // The project has this file now; it wins.
		}
		file = E->value; // Copied, so the file is opened without holding the lock.
	}

	return file.src->get_file(p_path, &file);
}

bool PackedData::has_path(const String &p_path) {
	const PathMD5 pmd5(p_path.simplify_path().trim_prefix("res://").md5_buffer());
	MountsReadLock lock(this);
	HashMap<PathMD5, PackedFile, PathMD5>::Iterator E = files.find(pmd5);
	if (!E) {
		return false;
	}
	if (E->value.read_only && _yields_to_disk(E->value, p_path)) {
		return false; // The project has this file now; it wins.
	}
	return true;
}

bool PackedData::is_read_only_path(const String &p_path) {
	if (read_only_mount_count == 0) {
		return false;
	}
	const PathMD5 pmd5(p_path.simplify_path().trim_prefix("res://").md5_buffer());
	MountsReadLock lock(this);
	HashMap<PathMD5, PackedFile, PathMD5>::Iterator E = files.find(pmd5);
	return E && E->value.read_only && !_yields_to_disk(E->value, p_path);
}

bool PackedData::has_directory(const String &p_path) {
	MountsReadLock lock(this);
	if (read_only_mount_count > 0 && _dir_exists_on_disk(p_path)) {
		// Folders that exist in the project and in a mounted pack (e.g. res://scenes) are the project's,
		// so modified times and permissions come from the disk.
		return false;
	}
	Ref<DirAccess> da = try_open_directory(p_path);
	if (da.is_valid()) {
		return true;
	} else {
		return false;
	}
}

class DirAccessPack : public DirAccess {
	GDSOFTCLASS(DirAccessPack, DirAccess);
	PackedData::PackedDir *current;

	List<String> list_dirs;
	List<String> list_files;
	bool cdir = false;

	PackedData::PackedDir *_find_dir(const String &p_dir);

public:
	virtual Error list_dir_begin() override;
	virtual String get_next() override;
	virtual bool current_is_dir() const override;
	virtual bool current_is_hidden() const override;
	virtual void list_dir_end() override;

	virtual int get_drive_count() override;
	virtual String get_drive(int p_drive) override;

	virtual Error change_dir(String p_dir) override;
	virtual String get_current_dir(bool p_include_drive = true) const override;

	virtual bool file_exists(String p_file) override;
	virtual bool dir_exists(String p_dir) override;

	virtual Error make_dir(String p_dir) override;

	virtual Error rename(String p_from, String p_to) override;
	virtual Error remove(String p_name) override;

	uint64_t get_space_left() override;

	virtual bool is_link(String p_file) override { return false; }
	virtual String read_link(String p_file) override { return p_file; }
	virtual Error create_link(String p_source, String p_target) override { return FAILED; }

	virtual String get_filesystem_type() const override;

	DirAccessPack();
};

Ref<DirAccess> PackedData::try_open_directory(const String &p_path) {
	Ref<DirAccess> da = memnew(DirAccessPack());
	if (da->change_dir(p_path) != OK) {
		da = Ref<DirAccess>();
	}
	return da;
}

# gdextension_libraries

This folder holds the **editor builds of the GDExtensions that come with the game**. MoDot can't ship them, so you add them yourself. By storing them here you do not need to keep them in each of your mod projects.

## Why this folder exists

Some of the game's addons are GDExtensions; native libraries (`.dll`), not scripts. The game's pack only
contains their `.gdextension` files, which name the library to load; the libraries themselves sit on disk
next to the game. A game only ships the **release** builds of these libraries.

The MoDot editor looks for the **editor** build first. It searches:

1. The path the `.gdextension` names (inside your project)
2. This folder
3. Next to the game's pack
4. Next to the MoDot executable

If it finds no editor build, it falls back to the debug build, then to the release build next to the game.
With the release build the game runs in the editor-player, but **editor-only parts are missing**, for
example importers, previews and editor plugins of that addon. To get them, put the editor builds here.

Libraries that a library depends on (for example FMOD's own DLLs) go into this folder too, next to it.

## Example: the game's addons

The file names must match the editor entries in the addon's `.gdextension` file. You can view it in the
editor's FileSystem dock under `res://addons/<addon>/`.

**Use the same version of each addon that the game uses.** A different version can fail to load or crash.

Beneath are the proper sources, however [GDRE Tools](https://github.com/GDRETools/gdsdecomp) may provide the libraries as well if you choose to decompile the `Slay The Spire 2` project.

### Spine (`res://addons/spine`)

| File | Source |
|---|---|
| `libspine_godot.windows.editor.x86_64.dll` | The spine-godot GDExtension release from Esoteric Software ([spine-runtimes](https://github.com/EsotericSoftware/spine-runtimes)), matching the game's Spine version |

Without it, Spine files (`.spskel`, `.spatlas`) can't be imported or previewed in the editor.

### FMOD (`res://addons/fmod`)

| File | Source |
|---|---|
| `libGodotFmod.windows.editor.x86_64.dll` | The fmod-gdextension release ([utopia-rise/fmod-gdextension](https://github.com/utopia-rise/fmod-gdextension)), matching the game's version |
| `fmodL.dll` | FMOD Engine (logging build), from the fmod-gdextension release or the [FMOD Engine download](https://www.fmod.com/download) |
| `fmodstudioL.dll` | FMOD Studio API (logging build), same source as `fmodL.dll` |

The editor build of the FMOD addon uses FMOD's logging libraries (the `L` suffix), so they have to be here as well.

## Resulting folder

```
gdextension_libraries/
├─ README.md
├─ libspine_godot.windows.editor.x86_64.dll
├─ libGodotFmod.windows.editor.x86_64.dll
├─ fmodL.dll
└─ fmodstudioL.dll
```

If a library can't be found, the editor's Output panel names the `.gdextension` and the folders it searched.

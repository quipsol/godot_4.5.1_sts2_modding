###### This ReadMe is still work in progress

# MoDot

**MoDot is an unofficial fork of [Godot Engine](https://godotengine.org) 4.5.1 for making mods for
*Slay the Spire 2*.** It lets you open a mod project in the editor with the game's own scenes, scripts and
assets available read-only, run the game with your mod straight from the editor, and pack the finished mod.

> [!IMPORTANT]
> MoDot is not affiliated with, endorsed by, or supported by the Godot Foundation or the Godot Engine
> project, nor by Mega Crit. Please **do not report MoDot issues to the Godot issue tracker**. Report them
> here instead.
>
> MoDot does not contain any game files. You need your own copy of *Slay the Spire 2*. MoDot reads the
> game's files from your installation.

## Table of Content

- [Features](#features)
- [Download](#download)
- [Getting started](#getting-started)
- [Building from Source](#building-from-source)
- [Relationship to Godot](#relationship-to-godot)
- [License](#license)

## Features

<!-- TODO: link each feature to its documentation page once written. -->

- **Read-only game packs:** mount the game's `.pck` into `res://` (`editor/mounted_packs/paths`,
  `--mount-pack`). Game scenes, resources and scripts can be opened, used and inherited, but not overwritten.
  Opening a game scene offers to create an inherited scene instead; tick *Don't ask again* to always open
  them directly (Editor Settings > FileSystem > Mounted Packs). Play and *Save All* skip changed game scenes
  instead of asking where to save them.
- **Addons from packs:** enable editor plugins and GDExtensions that ship inside a mounted pack, such as
  the game's Spine and FMOD integration (`editor/mounted_packs/addons`).
- **Mounted assemblies:** load the game's C# assembly (`sts2.dll`) before your project's
  (`editor/mounted_assemblies/paths`, `--mount-assembly`), so the game's C# scripts work in the editor and
  your mod can reference them without copying them. Assemblies the game loads itself at runtime, such as a mod
  library like BaseLib from the game's mods folder, can be mounted in the editor only
  (`editor/mounted_assemblies/editor_only_paths`, `--mount-assembly-editor`), so the editor-player doesn't
  load them a second time.
- **Pre-patches:** run small Harmony patch assemblies before the game starts in the editor-player
  (`editor/pre_patches/paths`, `editor/pre_patches/enabled`, `--pre-patch`). For example,
  `MoDot.Native.SeparateProjectMods` makes each project load only its own mods folder.
- **Decompiled game scripts:** opening one of the game's C# scripts (the pack only holds an empty
  placeholder) shows its class decompiled from the mounted assemblies, read-only, with the members Godot's
  source generators add hidden (Editor Settings > Dotnet > Editor). Scripts from mounted packs always open
  read-only in the built-in script editor.
- **Mod packers:** pack a mod with **Project > Pack Mod...** or `--pack-mod <packer> <output.pck>`. The
  default packer is `modot.default`; you can add your own in `mod_packers/` next to the executable.
- **Publishing from the editor:** the publish menu next to the C# build button runs your project's
  `dotnet publish` (Debug for the editor-player, or ExportRelease), like an IDE's "Publish to folder".
  Optionally, Play publishes (Debug) first whenever the project's files changed since the last publish.
- **Machine-specific paths** go into `override.cfg`, so `project.godot` can be shared.
- **IDE hot reload:** when the game runs, the project assembly is loaded from its file, so Rider's and
  Visual Studio's hot reload (Edit and Continue) work while debugging.
- Some basic project templates will be available soon. <!-- TODO: project templates -->

## Download

Prebuilt editors are on the [Releases](https://github.com/quipsol/godot_4.5.1_sts2_modding/releases) page.
Download the zip for your platform and extract it anywhere.

| Platform | File | Status |
|---|---|---|
| Windows x86_64 | `MoDot_<version>_windows_x86_64.zip` | Tested |
| Linux x86_64 | `MoDot_<version>_linux_x86_64.zip` | Built automatically, not tested yet |
| macOS (Apple Silicon and Intel) | `MoDot_<version>_macos.zip` | Built automatically, not tested yet |

MoDot keeps its editor settings next to the executable (self-contained mode), so it doesn't share settings
with a regular Godot installation.

On macOS, MoDot isn't signed by an Apple developer account. After extracting, remove the download quarantine
once, in a terminal in the extracted folder:

```
xattr -dr com.apple.quarantine "MoDot.app"
```

Pre-patches are a separate download on the same Releases page.

## Getting started

TODO
<!-- TODO: Finish templates and make them public. -->
<!-- TODO: Move these steps into documentation once written and link to it. -->
<!-- TODO: short walkthrough or link to the docs/wiki:
     1. Download / build MoDot.
     2. Create a mod project (template).
     3. Point override.cfg at your game installation.
     4. Open the editor, press Play.
     5. Pack the mod. -->



## Building from source

MoDot builds like Godot with C# support. Follow Godot's
[Compiling](https://docs.godotengine.org/en/4.5/contributing/development/compiling/index.html) guide for
your platform, including the
[C# (.NET) build steps](https://docs.godotengine.org/en/4.5/contributing/development/compiling/compiling_with_dotnet.html),
using this repository and the `sts2-modding` branch.

Windows, for example:

```
scons platform=windows target=editor arch=x86_64 module_mono_enabled=yes d3d12=yes
python modules/mono/build_scripts/build_assemblies.py --godot-output-dir=./bin --godot-platform=windows
```

On Linux and macOS, use `platform=linuxbsd` or `platform=macos` and leave out `d3d12=yes`. *These platforms
are not tested yet.*

After building, copy `modot/mod_packers/` next to the executable (`bin/mod_packers/`).

## Relationship to Godot

- MoDot is based on Godot **4.5.1-stable**. Changes are kept on the `sts2-modding` branch.
  <!-- TODO: optional: list where the MoDot-specific code lives (main/pre_patches.cpp, modot/, GodotPlugins, ...). -->
- Projects made with MoDot are regular Godot 4.5 projects plus a few MoDot-only project settings.
  Stock Godot ignores those settings, so it can still open the project, without the game's files. Though any outside references are lost.
- For general engine documentation, use the [Godot documentation](https://docs.godotengine.org/en/4.5/).

## License

MoDot is distributed under the [MIT license](LICENSE.txt), like the Godot Engine.

- Godot Engine is Copyright (c) 2014-present Godot Engine contributors, and Copyright (c) 2007-2014 Juan
  Linietsky, Ariel Manzur. See [AUTHORS.md](AUTHORS.md).
- Third-party components and their licenses are listed in [COPYRIGHT.txt](COPYRIGHT.txt).
- MoDot's own changes are Copyright (c) 2026-present Quipsol and also MIT-licensed.

"Godot" and the Godot logo are trademarks of the Godot Foundation. MoDot uses the name only to describe
what it is based on. *Slay the Spire 2* is a trademark of Mega Crit.

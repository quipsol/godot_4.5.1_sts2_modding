#!/usr/bin/env python3
"""Puts a MoDot editor build (bin/, with GodotSharp/) together in the layout of a release, ready to be zipped.

    python modot/ci/package_release.py --platform windows --version 4.5.1-m.2

creates "dist/MoDot 4.5.1-m.2/" with:
  - the editor: on Windows MoDot_v4.5.1-stable_mono_win64.exe and .console.exe (and D3D12Core.dll), on Linux
    MoDot_v4.5.1-stable_mono_linux.x86_64, on macOS MoDot.app (with GodotSharp inside, as Godot ships it);
  - GodotSharp/ next to the executable (Windows, Linux);
  - mod_packers/ and gdextension_libraries/ (only its README) from modot/, next to the executable or, on macOS, next
    to MoDot.app (where MoDot looks for them, see core/os/modot_install_dir.h);
  - ._sc_ (self-contained mode: editor settings next to the executable), LICENSE.txt and COPYRIGHT.txt.

Pre-patches aren't part of a release package; they are released separately.
"""

import argparse
import os
import shutil
import stat
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def engine_version() -> str:
    values = {}
    with open(os.path.join(REPO, "version.py"), encoding="utf-8") as f:
        exec(f.read(), values)
    return f"{values['major']}.{values['minor']}.{values['patch']}-{values['status']}"


def make_executable(path: str) -> None:
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def copy_file(source: str, target: str) -> None:
    if not os.path.isfile(source):
        sys.exit(f"Missing build output: {source}")
    os.makedirs(os.path.dirname(target), exist_ok=True)
    shutil.copy2(source, target)


def copy_dir(source: str, target: str) -> None:
    if not os.path.isdir(source):
        sys.exit(f"Missing folder: {source}")
    shutil.copytree(source, target, symlinks=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", required=True, choices=["windows", "linux", "macos"])
    parser.add_argument("--version", required=True, help="MoDot version, e.g. 4.5.1-m.2")
    parser.add_argument("--bin", default=os.path.join(REPO, "bin"), help="Build output folder (default: bin/)")
    parser.add_argument("--out", default=os.path.join(REPO, "dist"), help="Output folder (default: dist/)")
    args = parser.parse_args()

    engine = engine_version()
    root = os.path.join(args.out, f"MoDot {args.version}")
    if os.path.exists(root):
        shutil.rmtree(root)
    os.makedirs(root)
    godot_sharp = os.path.join(args.bin, "GodotSharp")

    if args.platform == "windows":
        name = f"MoDot_v{engine}_mono_win64"
        copy_file(os.path.join(args.bin, "godot.windows.editor.x86_64.mono.exe"), os.path.join(root, f"{name}.exe"))
        copy_file(os.path.join(args.bin, "godot.windows.editor.x86_64.mono.console.exe"), os.path.join(root, f"{name}.console.exe"))
        # Direct3D 12 Agility SDK runtime, which the build copies next to the editor. (Not its debug layer.)
        copy_file(os.path.join(args.bin, "D3D12Core.dll"), os.path.join(root, "D3D12Core.dll"))
        copy_dir(godot_sharp, os.path.join(root, "GodotSharp"))
    elif args.platform == "linux":
        executable = os.path.join(root, f"MoDot_v{engine}_mono_linux.x86_64")
        copy_file(os.path.join(args.bin, "godot.linuxbsd.editor.x86_64.mono"), executable)
        make_executable(executable)
        copy_dir(godot_sharp, os.path.join(root, "GodotSharp"))
    else:
        app = os.path.join(root, "MoDot.app")
        copy_dir(os.path.join(REPO, "misc", "dist", "macos_tools.app"), app)
        # The template's Info.plist names the executable "Godot".
        executable = os.path.join(app, "Contents", "MacOS", "Godot")
        copy_file(os.path.join(args.bin, "godot.macos.editor.universal.mono"), executable)
        make_executable(executable)
        copy_dir(godot_sharp, os.path.join(app, "Contents", "Resources", "GodotSharp"))

    copy_dir(os.path.join(REPO, "modot", "mod_packers"), os.path.join(root, "mod_packers"))
    copy_file(os.path.join(REPO, "modot", "gdextension_libraries", "README.md"), os.path.join(root, "gdextension_libraries", "README.md"))
    copy_file(os.path.join(REPO, "LICENSE.txt"), os.path.join(root, "LICENSE.txt"))
    copy_file(os.path.join(REPO, "COPYRIGHT.txt"), os.path.join(root, "COPYRIGHT.txt"))
    open(os.path.join(root, "._sc_"), "w").close()

    print(f"Packaged MoDot {args.version} ({args.platform}) into {root}")


if __name__ == "__main__":
    main()

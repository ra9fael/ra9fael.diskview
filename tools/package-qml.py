#!/usr/bin/env python3
"""Package the QML edition as dist/<id>-<version>.wsplugin.

Usage:
  python tools/package-qml.py [--out dist] [--qt <Qt SDK root>]

The native edition has its own packaging script (tools/package.py). This one
is the QML edition's, and it is deliberately small.

The plugin runs on WinShun's own Qt: the manifest declares
`runtime.qt: "host"`, the host guarantees Core/Gui/Qml/Quick plus Controls
and Layouts, and the DLL imports nothing the host lacks. What the host does
*not* have is the top-level `QtQuick/Controls` module -- its `qml` tree only
carries the style subfolders, the module itself lives in
Qt6QuickControls2.dll's resources and is reached through a qmldir that is not
on disk. So the package ships exactly those three files (about 42 KB) and
nothing else Qt-related.

The archive holds: plugin.json, the program, the app DLL, the icon, and the
three Controls files. Well under WinShun's 256 MB limit.

Standard library only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "qmlapp"
FIXED_TIME = (2020, 1, 1, 0, 0, 0)
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
MAX_BYTES = 256 << 20  # WinShun's archive limit (src/plugin/Install.h)

# The top-level Controls module the host's qml tree lacks. The styles and
# Templates/Layouts are the host's.
CONTROLS_FILES = ["qmldir", "qtquickcontrols2plugin.dll", "plugins.qmltypes"]


def fail(msg: str) -> int:
    print(f"error: {msg}", file=sys.stderr)
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "dist"))
    ap.add_argument("--qt", default=r"D:\tools\Qt\6.12.0\msvc2022_64",
                    help="Qt SDK the DLL was built against (its Controls module is copied)")
    ap.add_argument("--build", default=str(ROOT / "build-plugin" / "plugin"),
                    help="folder with diskview-qml.exe and DiskViewQmlApp.dll")
    args = ap.parse_args()

    manifest = json.loads((APP / "plugin.json").read_text(encoding="utf-8"))
    pid, version = manifest.get("id", ""), manifest.get("version", "")
    if not VERSION_RE.fullmatch(version):
        return fail(f"plugin.json version {version!r} must be three numbers like 1.2.3")
    runtime = manifest.get("runtime", {})
    if runtime.get("qt") != "host":
        return fail('runtime.qt must be "host": this package runs on WinShun\'s Qt')
    program = str(runtime.get("program", ""))
    if not program.lower().endswith(".exe"):
        return fail(f"runtime.program {program!r} is not an exe")

    build = Path(args.build)
    for needed in (program, "DiskViewQmlApp.dll"):
        if not (build / needed).is_file():
            return fail(f"{needed} not found in {build}; build with -DDISKVIEW_BUILD_PLUGIN=ON first")
    controls = Path(args.qt) / "qml" / "QtQuick" / "Controls"
    for name in CONTROLS_FILES:
        if not (controls / name).is_file():
            return fail(f"{name} not found under {controls}: is this the Qt the DLL was built against?")

    with tempfile.TemporaryDirectory(prefix="diskview-qml-pkg-") as tmp:
        staging = Path(tmp) / "stage"
        staging.mkdir()
        shutil.copy2(build / program, staging / program)
        shutil.copy2(build / "DiskViewQmlApp.dll", staging / "DiskViewQmlApp.dll")
        for name in ("plugin.json", "app.ico"):
            source = APP / name if (APP / name).is_file() else ROOT / name
            if source.is_file():
                shutil.copy2(source, staging / name)
        target_dir = staging / "qml" / "QtQuick" / "Controls"
        target_dir.mkdir(parents=True)
        for name in CONTROLS_FILES:
            shutil.copy2(controls / name, target_dir / name)

        files = sorted(p.relative_to(staging).as_posix() for p in staging.rglob("*") if p.is_file())
        total = sum((staging / f).stat().st_size for f in files)
        if total > MAX_BYTES:
            return fail(f"package would be {total >> 20} MB, over WinShun's {MAX_BYTES >> 20} MB limit")

        out = Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        target = out / f"{pid}-{version}.wsplugin"
        with zipfile.ZipFile(target, "w", zipfile.ZIP_STORED) as z:
            for name in files:
                info = zipfile.ZipInfo(name, FIXED_TIME)
                info.compress_type = zipfile.ZIP_STORED
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                z.writestr(info, (staging / name).read_bytes())

    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    target.with_suffix(".wsplugin.sha256").write_text(f"{digest}  {target.name}\n", encoding="utf-8")
    print(f"{target} ({total / 1024:.0f} KB, {len(files)} files)")
    print(f"sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

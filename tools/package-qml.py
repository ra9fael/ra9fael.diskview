#!/usr/bin/env python3
"""Package the QML edition as dist/<id>-<version>.wsplugin.

Usage:
  python tools/package-qml.py [--out dist] [--qt <Qt SDK root>]

The native edition has its own packaging script (tools/package.py); this one
is the QML edition's, which is a different shape:

  * its manifest and program live in qmlapp/ (plugin.json names
    diskview-qml.exe, the no-Qt starter);
  * the plugin runs on its own Qt: WinShun's Qt does not ship
    QtQuick.Controls, which this window uses, so the unpacked Qt runtime goes
    into the archive (manifest leaves `runtime.qt` out, which is what
    "bring your own Qt" means);
  * the WinShun archive limit is 256 MB, so the Qt deployment is pruned to the
    modules the window actually imports.

The zip is deterministic (sorted names, fixed timestamps, stored), so the same
inputs give the same sha256. Standard library only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "qmlapp"
FIXED_TIME = (2020, 1, 1, 0, 0, 0)
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
MAX_BYTES = 256 << 20  # WinShun's archive limit (src/plugin/Install.h)

# Everything the window and the protocol reach for. windeployqt adds what the
# DLLs import; this list is what the QML side imports on top.
QML_MODULES = ["QtQuick", "QtQuick.Controls", "QtQuick.Layouts", "QtQuick.Templates",
               "QtQuick.Window", "QtQuick.Controls.Basic", "QtQml", "QtQml.Models",
               "QtQml.WorkerScript"]
PLUGINS = ["platforms/qwindows.dll", "styles/qmodernwindowsstyle.dll"]


def fail(msg: str) -> int:
    print(f"error: {msg}", file=sys.stderr)
    return 1


def qml_imports() -> set[str]:
    """The `import X.Y` lines of every QML file, as module names."""
    found = set()
    for path in (APP / "qml").glob("*.qml"):
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line.startswith("import ") and not line.startswith("import QtQuick 2"):
                parts = line.split()
                if len(parts) >= 2 and parts[1][0].isupper():
                    found.add(parts[1])
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "dist"))
    ap.add_argument("--qt", default=r"D:\tools\Qt\6.12.0\msvc2022_64",
                    help="Qt SDK the DLL was built against (default: the one this tree uses)")
    ap.add_argument("--build", default=str(APP / "build-plugin" / "plugin"),
                    help="folder with diskview-qml.exe and DiskViewQmlApp.dll")
    args = ap.parse_args()

    manifest = json.loads((APP / "plugin.json").read_text(encoding="utf-8"))
    pid, version = manifest.get("id", ""), manifest.get("version", "")
    if not VERSION_RE.fullmatch(version):
        return fail(f"plugin.json version {version!r} must be three numbers like 1.2.3")
    runtime = manifest.get("runtime", {})
    if "qt" in runtime or "qtVersion" in runtime:
        return fail("the QML edition ships its own Qt: runtime.qt and runtime.qtVersion must be left out")
    program = str(runtime.get("program", ""))
    if not program.lower().endswith(".exe"):
        return fail(f"runtime.program {program!r} is not an exe")

    build = Path(args.build)
    for needed in (program, "DiskViewQmlApp.dll"):
        if not (build / needed).is_file():
            return fail(f"{needed} not found in {build}; build with -DDISKVIEW_BUILD_PLUGIN=ON first")

    imported = qml_imports()
    print("qml imports:", ", ".join(sorted(imported)))

    with tempfile.TemporaryDirectory(prefix="diskview-qml-pkg-") as tmp:
        staging = Path(tmp) / "stage"
        staging.mkdir()
        # The program and the Qt DLL; qt.conf makes the loader look beside them.
        shutil.copy2(build / program, staging / program)
        shutil.copy2(build / "DiskViewQmlApp.dll", staging / "DiskViewQmlApp.dll")
        for name in ("app.ico", "plugin.json"):
            source = APP / name if (APP / name).is_file() else ROOT / name
            if source.is_file():
                shutil.copy2(source, staging / name)
        (staging / "qt.conf").write_text("[Paths]\nPrefix = .\nPlugins = .\nQml = qml\n", encoding="utf-8")

        # The DLL's imports first, then the QML modules it imports.
        windeployqt = Path(args.qt) / "bin" / "windeployqt.exe"
        if not windeployqt.is_file():
            return fail(f"windeployqt not found under {args.qt}")
        run = [str(windeployqt), "--release", "--no-translations", "--no-system-d3d-compiler",
               "--no-opengl-sw", "--no-compiler-runtime", "--qmldir", str(APP / "qml"),
               str(staging / "DiskViewQmlApp.dll")]
        result = subprocess.run(run, capture_output=True, text=True)
        if result.returncode != 0:
            return fail(f"windeployqt failed: {result.stderr.strip() or result.stdout.strip()}")

        # Prune what the window never imports: the full deployment carries
        # every Qt module, far past the archive limit. Everything the window
        # needs is `imported` plus the QtQuick core it builds on.
        keep_modules = {
            "QtQuick", "QtQuick/Controls", "QtQuick/Templates", "QtQuick/Window",
            "QtQuick/Layouts", "QtQml", "QtQml/Models", "QtQml/WorkerScript",
            "QtQml/Base", "QML",
        }
        pruned = 0
        qml_root = staging / "qml"
        for module_dir in sorted(p for p in qml_root.iterdir() if p.is_dir()):
            for sub in sorted(p for p in module_dir.iterdir() if p.is_dir()):
                module = f"{module_dir.name}/{sub.name}"
                if module in keep_modules:
                    continue
                # QtQml's own submodules sit under the same folder.
                if module_dir.name == "QtQml" and sub.name in ("Models", "WorkerScript", "Base"):
                    continue
                shutil.rmtree(sub, ignore_errors=True)
                pruned += 1
        print(f"pruned {pruned} QML modules")

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
    (target.with_suffix(".wsplugin.sha256")).write_text(f"{digest}  {target.name}\n", encoding="utf-8")
    print(f"{target} ({target.stat().st_size >> 20} MB, {len(files)} files)")
    print(f"sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

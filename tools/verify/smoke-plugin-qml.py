#!/usr/bin/env python3
"""Exercise the QML edition over its real JSON-lines plugin protocol (Windows).

The native edition has its own check (smoke-plugin.py): it looks for a class
name the Win32 viewer uses and for diskview.json. This one is the QML
edition's: a Qt Quick window with no class name of its own and its own
settings file, so it asserts on the protocol, the windows and that file.

The working data lives beside the executable, not in %TEMP%: some machines
refuse writes from a child process under the temp tree, which the plugin hits
while saving its settings.
"""
import ctypes
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import threading
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)

VIEWER_TITLE = "Disk View"
SETTINGS_TITLE = "磁盘透视设置"


def windows_of(pid):
    """The visible top-level windows of one process, as (handle, title)."""
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            length = user32.GetWindowTextLengthW(hwnd)
            buffer = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buffer, length + 1)
            found.append((hwnd, buffer.value))
        return True

    user32.EnumWindows(visit, 0)
    return found


def wait_window(pid, title, timeout=25.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for hwnd, text in windows_of(pid):
            if text == title:
                return hwnd
        time.sleep(0.1)
    return None


def find_app_dir(exe):
    """The folder the stub loads Qt from: WinShun's own, as WS_APP_DIR names it.

    A self-contained package carries Qt beside the executable; otherwise the
    Qt the plugin was built against is the one on PATH.
    """
    candidates = []
    if os.environ.get("WS_APP_DIR"):
        candidates.append(Path(os.environ["WS_APP_DIR"]))
    candidates.append(exe.parent)
    for folder in os.environ.get("PATH", "").split(os.pathsep):
        if folder:
            candidates.append(Path(folder))
    for candidate in candidates:
        try:
            if (candidate / "Qt6Core.dll").is_file():
                return candidate
        except OSError:
            continue
    return None


def main():
    exe = Path(sys.argv[1]).resolve()
    app_dir = find_app_dir(exe)
    assert app_dir, "no Qt6Core.dll found: set WS_APP_DIR to WinShun's folder"
    base = exe.parent / "verify-data"
    shutil.rmtree(base, ignore_errors=True)
    base.mkdir(parents=True)
    data = base / "data"
    data.mkdir()
    scan = base / "测试 folder"
    scan.mkdir()
    (scan / "test.bin").write_bytes(b"diskview" * 1024)
    # A second folder for the result menu: a space and Chinese characters in
    # the path are what the host hands over, and what the scan has to receive.
    menu = base / "另一个 文件夹"
    menu.mkdir()
    (menu / "menu.bin").write_bytes(b"diskview" * 2048)
    config = data / "diskview-qml.json"

    log = (base / "stderr.log").open("w", encoding="utf-8")
    # The settings window presses its own 应用 button once it is up: an
    # unattended session cannot click it, and the hook runs the same handler.
    environment = dict(os.environ, WS_APP_DIR=str(app_dir), WS_SETTINGS_APPLY="1")
    process = subprocess.Popen([str(exe)], cwd=exe.parent, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=log, text=True,
                               encoding="utf-8", env=environment,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    replies = queue.Queue()
    notices = []

    def read_stdout():
        for line in process.stdout:
            replies.put(line)
        replies.put(None)

    threading.Thread(target=read_stdout, daemon=True).start()
    serial = 0

    def next_message(timeout=30):
        line = replies.get(timeout=timeout)
        assert line is not None, "plugin exited before replying"
        return json.loads(line)

    def call(method, params=None, notify=False):
        nonlocal serial
        serial += 1
        message = {"jsonrpc": "2.0", "method": method, "params": params or {}}
        if not notify:
            message["id"] = serial
        process.stdin.write(json.dumps(message, ensure_ascii=False) + "\n")
        process.stdin.flush()
        if notify:
            return None
        # Notifications (winshun/settings/update, winshun/notify) arrive
        # between answers: keep them and wait for the answer to this id.
        while True:
            reply = next_message()
            if "id" not in reply:
                notices.append(reply)
                continue
            assert reply.get("id") == serial and "error" not in reply, reply
            return reply["result"]

    def wait_notice(method, timeout=15.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for notice in notices:
                if notice.get("method") == method:
                    return notice["params"]
            try:
                reply = next_message(timeout=0.2)
            except queue.Empty:
                continue
            if "id" not in reply:
                notices.append(reply)
        return None

    def saved():
        return json.loads(config.read_text(encoding="utf-8"))

    try:
        initialized = call("initialize", {
            "locale": "en", "theme": "light", "pluginDir": str(exe.parent),
            "dataDir": str(data),
            "settings": {"defaultView": "treemap", "scanMode": "auto",
                         "folderMenuEnabled": True}})
        assert initialized["name"] == "ra9fael.diskview.qml", initialized

        items = call("query", {"command": "disk", "text": "", "queryId": 1})["items"]
        ids = [item["id"] for item in items]
        assert "disk.open" in ids, ids
        assert "disk.settings" in ids, ids
        assert any(i.startswith("disk.drive.") for i in ids), ids
        opener = next(item for item in items if item["id"] == "disk.open")
        assert opener["data"]["path"] == ""
        # A query must not touch anything.
        assert not config.exists(), "the query wrote the settings file"

        result = call("action", {"command": "disk", "action": "open",
                                 "text": "help", "item": opener})
        notices_text = [e.get("text", "") for e in result["effects"] if e["type"] == "notify"]
        assert any("Disk view opened" in n for n in notices_text), notices_text
        assert wait_window(process.pid, VIEWER_TITLE), "the viewer window did not appear"

        # Open with a path: the window scans it and remembers it.
        path_items = call("query", {"command": "disk", "text": str(scan), "queryId": 2})["items"]
        assert path_items[0]["data"]["path"] == str(scan)
        call("action", {"command": "disk", "action": "open",
                        "text": str(scan), "item": path_items[0]})
        deadline = time.monotonic() + 30.0
        while not config.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
        assert config.exists(), "the window did not remember the scanned path"
        assert saved().get("path") == str(scan), saved()

        # The launcher's empty query now carries the last target and the
        # settings entry, and neither of them scans by itself.
        items = call("query", {"command": "disk", "text": "", "queryId": 3})["items"]
        ids = [item["id"] for item in items]
        assert "disk.settings" in ids, ids
        recent = next((item for item in items if item["id"] == "disk.recent"), None)
        assert recent and recent["data"]["path"] == str(scan), items

        # The host's card button: a settings window of its own, and no scan.
        call("showSettings")
        settings_window = wait_window(process.pid, SETTINGS_TITLE)
        assert settings_window, "the settings window did not appear: %s" % windows_of(process.pid)

        # The window applies its settings on open (WS_SETTINGS_APPLY): the
        # host's keys go back to it, and nothing scans.
        update = wait_notice("winshun/settings/update")
        assert update, "applying the settings did not reach the host: %s" % notices
        values = update["settings"]
        assert values.get("defaultView") == "treemap", values
        assert values.get("scanMode") == "auto", values
        assert values.get("folderMenuEnabled") is True, values
        assert saved().get("path") == str(scan), "applying the settings started a scan"
        assert saved().get("defaultView") == 0, saved()
        assert saved().get("folderMenuEnabled") is True, saved()

        # A folder from the result menu: the host hands over the exact path,
        # and the scan receives it unchanged.
        call("action", {"command": "disk", "action": "files", "trigger": "files",
                        "files": [str(menu)], "item": {}, "text": ""})
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            if config.exists() and saved().get("path") == str(menu):
                break
            time.sleep(0.1)
        assert saved().get("path") == str(menu), saved()

        # The switch hides the entry; a host that sends it anyway must not
        # start a scan.
        call("settingsChanged", {"settings": {"defaultView": "sunburst",
                                              "scanMode": "auto",
                                              "folderMenuEnabled": False}}, notify=True)
        time.sleep(0.5)
        call("action", {"command": "disk", "action": "files", "trigger": "files",
                        "files": [str(scan)], "item": {}, "text": ""})
        time.sleep(1.0)
        assert saved().get("path") == str(menu), "the folder menu acted while switched off"

        call("appearance", {"theme": "dark", "locale": "en"}, notify=True)
        call("shutdown")
        assert process.wait(timeout=30) == 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        try:
            process.stdin.close()
        except Exception:
            pass
        log.close()
        shutil.rmtree(base, ignore_errors=True)
    print("plugin protocol (qml edition): query, open, scan, settings window, "
          "settings write-back, folder menu and shutdown passed")


if __name__ == "__main__":
    main()

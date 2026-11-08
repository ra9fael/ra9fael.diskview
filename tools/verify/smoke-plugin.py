#!/usr/bin/env python3
"""Exercise the built plugin over its real JSON-lines protocol (Windows)."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time


user32 = ctypes.WinDLL("user32", use_last_error=True)
callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowExW.restype = wintypes.HWND
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.SendMessageW.restype = ctypes.c_ssize_t
user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user32.IsWindowVisible.argtypes = [wintypes.HWND]


def window_for(pid):
    found = []

    @callback_type
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, name, len(name))
        if owner.value == pid and name.value == "WinShunDiskViewWindow":
            found.append(hwnd)
            return False
        return True

    user32.EnumWindows(visit, 0)
    return found[0] if found else None


def wait_until(predicate, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(0.02)
    raise AssertionError("timed out waiting for plugin state")


def main():
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="diskview_protocol_") as tmp:
        base = Path(tmp)
        data = base / "data"
        data.mkdir()
        scan = base / "测试 folder"
        scan.mkdir()
        (scan / "test.bin").write_bytes(b"diskview" * 1024)
        config = data / "diskview.json"
        with (base / "stderr.log").open("w", encoding="utf-8") as log:
            process = subprocess.Popen([str(exe)], cwd=exe.parent, stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=log, text=True,
                                       encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
            replies = queue.Queue()

            def read_stdout():
                for line in process.stdout:
                    replies.put(line)
                replies.put(None)

            reader = threading.Thread(target=read_stdout, daemon=True)
            reader.start()
            serial = 0

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
                line = replies.get(timeout=10)
                assert line is not None, "plugin exited before replying"
                reply = json.loads(line)
                assert reply.get("id") == serial and "error" not in reply, reply
                return reply["result"]

            def open_item(item, text=""):
                result = call("action", {"command": "disk", "action": "open",
                                         "text": text, "item": item})
                notices = [e.get("text", "") for e in result["effects"] if e["type"] == "notify"]
                assert any("Disk view opened" in notice for notice in notices), notices
                hwnd = wait_until(lambda: window_for(process.pid))
                assert user32.IsWindowVisible(hwnd), "viewer reported success but stayed hidden"
                user32.ShowWindow(hwnd, 0)  # keep the automated check out of the way
                return hwnd

            def edit_text(hwnd):
                edit = user32.FindWindowExW(hwnd, None, "Edit", None)
                assert edit, "path edit is missing"
                text = ctypes.create_unicode_buffer(32768)
                user32.SendMessageW(edit, 0x000D, len(text), ctypes.addressof(text))  # WM_GETTEXT
                return text.value

            try:
                initialized = call("initialize", {"locale": "en", "theme": "light",
                    "pluginDir": str(exe.parent), "dataDir": str(data),
                    "settings": {"defaultView": "treemap"}})
                assert initialized["name"] == "ra9fael.diskview"
                items = call("query", {"command": "disk", "text": "", "queryId": 1})["items"]
                opener = next(item for item in items if item["id"] == "disk.open")
                assert opener["data"]["path"] == ""
                assert not window_for(process.pid) and not config.exists(), "query started a scan"
                # The help query's explicit empty data path must beat its text.
                hwnd = open_item(opener, "help")
                assert edit_text(hwnd) == "", "help was interpreted as a scan path"
                assert not config.exists()
                path_items = call("query", {"command": "disk", "text": str(scan), "queryId": 2})["items"]
                hwnd = open_item(path_items[0], str(scan))
                wait_until(lambda: config.exists() and config.stat().st_size > 0)
                saved = json.loads(config.read_text(encoding="utf-8"))
                assert saved["path"] == str(scan), saved
                assert edit_text(hwnd) == str(scan)
                call("appearance", {"theme": "dark", "locale": "en"}, notify=True)
                call("settingsChanged", {"settings": {"defaultView": "sunburst"}}, notify=True)
                # Reopen repeatedly: exercise GUI-thread teardown and startup.
                for _ in range(3):
                    hwnd = open_item(opener, "help")
                    assert edit_text(hwnd) == str(scan)
                call("shutdown")
                assert process.wait(timeout=5) == 0
                assert not window_for(process.pid)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
                process.stdin.close()
                reader.join(timeout=2)
                process.stdout.close()
        print("plugin protocol: query, open, Unicode scan, theme, reopen and shutdown passed")
        # Standalone preview must retain Unicode command-line paths and exit
        # cleanly when its window closes, without a host or persisted config.
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0  # hide the console, not the explicit viewer UI
        preview = subprocess.Popen([str(exe), "--view", str(scan)], cwd=base, startupinfo=startup,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            hwnd = wait_until(lambda: window_for(preview.pid))
            wait_until(lambda: user32.IsWindowVisible(hwnd))
            user32.ShowWindow(hwnd, 0)
            wait_until(lambda: edit_text(hwnd) == str(scan))
            user32.SendMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
            assert preview.wait(timeout=5) == 0
            assert not (base / "diskview.json").exists()
        finally:
            if preview.poll() is None:
                preview.kill()
                preview.wait(timeout=5)
        print("standalone preview: Unicode path and clean window shutdown passed")


if __name__ == "__main__":
    main()

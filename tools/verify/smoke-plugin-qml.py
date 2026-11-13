#!/usr/bin/env python3
"""Exercise the QML edition over its real JSON-lines plugin protocol (Windows).

The native edition has its own check (smoke-plugin.py): it looks for a class
name the Win32 viewer uses and for diskview.json. This one is the QML
edition's: a Qt Quick window with no class name of its own and its own
settings file, so it asserts on the protocol, the window and that file.

The working data lives beside the executable, not in %TEMP%: some machines
refuse writes from a child process under the temp tree, which the plugin hits
while saving its settings.
"""
import json
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import threading
import time


def main():
    exe = Path(sys.argv[1]).resolve()
    base = exe.parent / "verify-data"
    shutil.rmtree(base, ignore_errors=True)
    base.mkdir(parents=True)
    data = base / "data"
    data.mkdir()
    scan = base / "测试 folder"
    scan.mkdir()
    (scan / "test.bin").write_bytes(b"diskview" * 1024)
    config = data / "diskview-qml.json"

    log = (base / "stderr.log").open("w", encoding="utf-8")
    process = subprocess.Popen([str(exe)], cwd=exe.parent, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=log, text=True,
                               encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
    replies = queue.Queue()

    def read_stdout():
        for line in process.stdout:
            replies.put(line)
        replies.put(None)

    threading.Thread(target=read_stdout, daemon=True).start()
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
        line = replies.get(timeout=30)
        assert line is not None, "plugin exited before replying"
        reply = json.loads(line)
        assert reply.get("id") == serial and "error" not in reply, reply
        return reply["result"]

    try:
        initialized = call("initialize", {
            "locale": "en", "theme": "light", "pluginDir": str(exe.parent),
            "dataDir": str(data), "settings": {"defaultView": "treemap"}})
        assert initialized["name"] == "ra9fael.diskview.qml", initialized

        items = call("query", {"command": "disk", "text": "", "queryId": 1})["items"]
        ids = [item["id"] for item in items]
        assert "disk.open" in ids, ids
        assert any(i.startswith("disk.drive.") for i in ids), ids
        opener = next(item for item in items if item["id"] == "disk.open")
        assert opener["data"]["path"] == ""
        # A query must not touch anything.
        assert not config.exists(), "the query wrote the settings file"

        result = call("action", {"command": "disk", "action": "open",
                                 "text": "help", "item": opener})
        notices = [e.get("text", "") for e in result["effects"] if e["type"] == "notify"]
        assert any("Disk view opened" in n for n in notices), notices

        # Open with a path: the window scans it and remembers it.
        path_items = call("query", {"command": "disk", "text": str(scan), "queryId": 2})["items"]
        assert path_items[0]["data"]["path"] == str(scan)
        call("action", {"command": "disk", "action": "open",
                        "text": str(scan), "item": path_items[0]})
        deadline = time.monotonic() + 30.0
        while not config.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
        assert config.exists(), "the window did not remember the scanned path"
        saved = json.loads(config.read_text(encoding="utf-8"))
        assert saved.get("path") == str(scan), saved

        call("appearance", {"theme": "dark", "locale": "en"}, notify=True)
        call("settingsChanged", {"settings": {"defaultView": "sunburst"}}, notify=True)
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
    print("plugin protocol (qml edition): query, open, scan, theme and shutdown passed")


if __name__ == "__main__":
    main()

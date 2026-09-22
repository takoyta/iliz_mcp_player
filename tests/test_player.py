#!/usr/bin/env python3
import json
import os
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "bin" / "iliz_mcp_player.exe"

SPECIAL = [
    r"C:\iliz_test\love.mp3",
    r"C:\iliz_test\love_song.mp3",
    r"C:\iliz_test\ёлка.mp3",
    r"C:\iliz_test\other.mp3",
    r"C:\iliz_test\LoveFolder\zzz.mp3",
    r"C:\iliz_test\meteor.mp3",
    r"C:\iliz_test\oxwave.mp3",
]
PADS = 52


def titles(paths):
    return [Path(p).name for p in paths]


class Player:
    def __init__(self):
        self.port = 0
        self.proc = None
        self.tmp = None

    @property
    def base(self):
        return f"http://127.0.0.1:{self.port}"

    def get(self, path, q=None):
        url = self.base + path
        if q is not None:
            url += "?" + urllib.parse.urlencode({"q": q}, encoding="utf-8")
        with urllib.request.urlopen(url, timeout=3) as r:
            return r.status, json.loads(r.read().decode("utf-8"))

    def mcp(self, body, method="POST"):
        data = json.dumps(body).encode("utf-8") if body is not None else None
        req = urllib.request.Request(
            self.base + "/mcp",
            data=data,
            method=method,
            headers={"Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(req, timeout=3) as r:
                raw = r.read()
                return r.status, json.loads(raw.decode("utf-8")) if raw else None
        except urllib.error.HTTPError as e:
            raw = e.read()
            if not raw:
                return e.code, None
            try:
                return e.code, json.loads(raw.decode("utf-8"))
            except json.JSONDecodeError:
                return e.code, None

    def start(self):
        if not EXE.is_file():
            raise FileNotFoundError(f"build first: {EXE}")
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        self.port = s.getsockname()[1]
        s.close()
        self.tmp = tempfile.TemporaryDirectory(prefix="iliz_test_")
        app = Path(self.tmp.name)
        pl = app / "iliz" / "iliz_mcp_player"
        pl.mkdir(parents=True)
        con = sqlite3.connect(pl / "library.db")
        con.execute(
            "CREATE TABLE tracks ("
            " id INTEGER PRIMARY KEY,"
            " path TEXT NOT NULL UNIQUE,"
            " title TEXT NOT NULL DEFAULT '',"
            " artist TEXT NOT NULL DEFAULT '',"
            " album TEXT NOT NULL DEFAULT '',"
            " file TEXT NOT NULL DEFAULT '',"
            " secs INTEGER NOT NULL DEFAULT -1,"
            " liked INTEGER NOT NULL DEFAULT 0,"
            " plays INTEGER NOT NULL DEFAULT 0,"
            " pos INTEGER)"
        )
        paths = list(SPECIAL) + [rf"C:\iliz_test\pad_{i:02d}.mp3" for i in range(PADS)]
        for i, p in enumerate(paths):
            name = Path(p).name
            con.execute(
                "INSERT INTO tracks(path,title,artist,album,file,pos) VALUES (?,?,?,?,?,?)",
                (p, name, "", "", name, i),
            )
        con.commit()
        con.close()
        env = os.environ.copy()
        env["APPDATA"] = str(app)
        self.proc = subprocess.Popen(
            [str(EXE), "--port", str(self.port)],
            cwd=str(EXE.parent),
            env=env,
        )
        deadline = time.time() + 8
        last = None
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"player exited {self.proc.returncode}")
            try:
                st, body = self.get("/search", "")
                if st == 200 and body.get("ok"):
                    return
            except OSError as e:
                last = e
            time.sleep(0.05)
        raise TimeoutError(f"http not up: {last}")

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(timeout=5)
        if self.tmp:
            self.tmp.cleanup()


P = Player()


def setUpModule():
    P.start()


def tearDownModule():
    P.stop()


class Search(unittest.TestCase):
    def titles(self, q):
        st, body = P.get("/search", q)
        self.assertEqual(st, 200)
        self.assertTrue(body["ok"])
        return [t["title"] for t in body["tracks"]], body

    def test_table(self):
        cases = [
            ("love", ["love.mp3", "love_song.mp3"]),
            ("LOVE", ["love.mp3", "love_song.mp3"]),
            ("ёлка", ["ёлка.mp3"]),
            ("Ёлка", ["ёлка.mp3"]),
            ("елка", ["ёлка.mp3"]),
            ("elka", ["ёлка.mp3"]),
            ("zzz", ["zzz.mp3"]),
            ("pad_00", ["pad_00.mp3"]),
            ("meteor", ["meteor.mp3"]),
            ("метеор", ["meteor.mp3"]),
            ("mrteor", ["meteor.mp3"]),
            ("oxw", ["oxwave.mp3"]),
            ("wave", ["oxwave.mp3"]),
            ("оксвэйв", ["oxwave.mp3"]),
            ("оксвейв", ["oxwave.mp3"]),
            ("оксвэй", ["oxwave.mp3"]),
            ("nope", []),
        ]
        for q, want in cases:
            with self.subTest(q=q):
                got, body = self.titles(q)
                self.assertEqual(got, want)
                self.assertEqual(body["count"], len(want))
                self.assertEqual(body["returned"], len(want))
                self.assertEqual(body["query"], q)

    def test_empty_cap(self):
        got, body = self.titles("")
        self.assertEqual(body["count"], len(SPECIAL) + PADS)
        self.assertEqual(body["returned"], 50)
        self.assertEqual(len(got), 50)
        self.assertEqual(got[: len(SPECIAL)], titles(SPECIAL))

    def test_filename_not_path(self):
        got, _ = self.titles("iliz_test")
        self.assertEqual(got, [])

    def test_ids_are_db_id(self):
        _, body = self.titles("love_song")
        self.assertEqual(body["tracks"][0]["id"], 2)

    def test_plays_default_zero(self):
        _, body = self.titles("love_song")
        self.assertEqual(body["tracks"][0]["plays"], 0)

    def test_like_toggle(self):
        st, body = P.get("/like?id=1")
        self.assertEqual(st, 200)
        self.assertTrue(body["ok"])
        self.assertEqual(body["liked"], 1)
        _, body = P.get("/like?id=1")
        self.assertEqual(body["liked"], 0)

    def test_like_set(self):
        _, body = P.get("/like?id=1&liked=1")
        self.assertEqual(body["liked"], 1)
        _, body = P.get("/like?id=1&liked=1")
        self.assertEqual(body["liked"], 1)
        _, body = P.get("/like?id=1&liked=0")
        self.assertEqual(body["liked"], 0)

    def test_now_stopped(self):
        st, body = P.get("/now")
        self.assertEqual(st, 200)
        self.assertTrue(body["ok"])
        self.assertEqual(body["state"], "stopped")
        self.assertEqual(body["shuffle"], 0)
        self.assertNotIn("id", body)

    def test_like_without_id_idle(self):
        st, body = P.get("/like")
        self.assertEqual(st, 200)
        self.assertFalse(body["ok"])
        self.assertEqual(body["error"], "nothing playing")

    def test_skip_bad_dir(self):
        st, body = P.get("/skip?dir=up")
        self.assertEqual(st, 200)
        self.assertFalse(body["ok"])
        self.assertEqual(body["error"], "bad dir")

    def test_skip_default_cannot_play(self):
        st, body = P.get("/skip")
        self.assertEqual(st, 200)
        self.assertFalse(body["ok"])
        self.assertEqual(body["error"], "cannot play")

    def test_404(self):
        with self.assertRaises(urllib.error.HTTPError) as e:
            P.get("/nope")
        self.assertEqual(e.exception.code, 404)


class Mcp(unittest.TestCase):
    def inner(self, name, args=None):
        st, body = P.mcp(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "tools/call",
                "params": {"name": name, "arguments": args or {}},
            }
        )
        self.assertEqual(st, 200)
        text = body["result"]["content"][0]["text"]
        return json.loads(text), body["result"].get("isError")

    def test_initialize(self):
        st, body = P.mcp(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "t"}},
            }
        )
        self.assertEqual(st, 200)
        info = body["result"]["serverInfo"]
        self.assertEqual(info["name"], "iliz-mcp-player")
        self.assertTrue(info["version"])

    def test_tools_list(self):
        st, body = P.mcp({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        self.assertEqual(st, 200)
        names = [t["name"] for t in body["result"]["tools"]]
        self.assertEqual(names, ["search", "now", "play", "skip", "like", "stop"])
        skip = next(t for t in body["result"]["tools"] if t["name"] == "skip")
        self.assertEqual(skip["inputSchema"]["properties"]["dir"]["enum"], ["next", "prev"])

    def test_search_matches_rest(self):
        inner, err = self.inner("search", {"query": "love"})
        self.assertFalse(err)
        st, rest = P.get("/search", "love")
        self.assertEqual(st, 200)
        self.assertEqual([t["id"] for t in inner["tracks"]], [t["id"] for t in rest["tracks"]])
        self.assertEqual([t["title"] for t in inner["tracks"]], [t["title"] for t in rest["tracks"]])
        self.assertNotIn("path", inner["tracks"][0])

    def test_now_matches_rest(self):
        inner, err = self.inner("now")
        self.assertFalse(err)
        st, rest = P.get("/now")
        self.assertEqual(st, 200)
        self.assertEqual(inner["state"], rest["state"])
        self.assertNotIn("path", inner)

    def test_like_set_mcp(self):
        inner, err = self.inner("like", {"id": 2, "liked": 1})
        self.assertFalse(err)
        self.assertEqual(inner["liked"], 1)
        inner, err = self.inner("like", {"id": 2, "liked": 0})
        self.assertFalse(err)
        self.assertEqual(inner["liked"], 0)

    def test_skip_bad_dir_mcp(self):
        inner, err = self.inner("skip", {"dir": "up"})
        self.assertTrue(err)
        self.assertEqual(inner["error"], "bad dir")

    def test_skip_prev_mcp(self):
        inner, err = self.inner("skip", {"dir": "prev"})
        self.assertTrue(err)
        self.assertEqual(inner["error"], "cannot play")

    def test_unknown_tool(self):
        inner, err = self.inner("nope")
        self.assertTrue(err)
        self.assertEqual(inner["error"], "unknown endpoint")

    def test_get_not_allowed(self):
        st, _ = P.mcp(None, method="GET")
        self.assertEqual(st, 405)


if __name__ == "__main__":
    sys.exit(unittest.main(verbosity=2))

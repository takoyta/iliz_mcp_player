English | [Русский](HTTP.ru.md)

# HTTP control protocol

## MCP (Streamable HTTP)

While the player is running:

`http://127.0.0.1:17321/mcp`

JSON-RPC over Streamable HTTP (same loopback server as REST). Cursor / Claude / Inspector:

```json
{
  "mcpServers": {
    "iliz-player": {
      "url": "http://127.0.0.1:17321/mcp"
    }
  }
}
```

Tools match REST. `play` prefers `id`. Tool errors use `isError` and the same `error` strings as REST.

| Tool | Arguments | Description |
| --- | --- | --- |
| `search` | `query` | Find tracks by title, artist, album, or file name. Empty query lists the queue (max 50). Use id with play. |
| `now` | — | Current song: state (playing, paused, stopped), title, artist, position. Does not change playback. |
| `play` | `id`, `query` | Start a track. id preferred; query plays the first hit. Not pause. |
| `skip` | `dir` | Next or previous queue track. dir next (default) or prev. Shuffle: next is random. Prev restarts if over 3s in. |
| `like` | `id`, `liked` | Like or unlike. Omit id for the loaded track. liked 1 or 0 sets; omit to toggle. |
| `stop` | — | Stop and clear the current track. Not pause. |

---

Local JSON API for agents. Available while the player is running.

- Base URL: `http://127.0.0.1:17321`
- Bind: loopback only
- `--port N` — listen on that port and skip the single-instance check (for tests)
- Methods: `GET` or `POST` (query string is what matters)
- Response: `Content-Type: application/json; charset=utf-8`

Search looks at **title, artist, album, and file name**. Prefix and substring (`oxw` / `oxwave`), FTS5 ranking, letter translit (`meteor` / `метеор`), heard-as-Russian (`оксвэйв` / `oxwave`), and typos (`mrteor`, edit distance 1–2) are applied. It does not change the window search box.

Track `id` is the database id (stable while the row exists).

Queue lives in `%APPDATA%\iliz\iliz_mcp_player\library.db`. Like and play count are fields on the track. Removing a liked track from the queue keeps the row.

## Endpoints

### `GET /search?q=`

Find tracks. Empty `q` returns the playlist (capped).

```
GET /search?q=love
```

```json
{
  "ok": true,
  "query": "love",
  "count": 2,
  "returned": 2,
  "tracks": [
    {
      "id": 4,
      "title": "love.mp3",
      "path": "C:\\Music\\love.mp3",
      "artist": "",
      "album": "",
      "liked": 0,
      "plays": 0,
      "duration": 183
    }
  ]
}
```

- `count` — matches
- `returned` — items in `tracks` (max 50)
- `duration` — seconds, or `0` if not probed yet
- `liked` — 0 or 1
- `plays` — how many times the track was started

### `GET /play?id=`

Play a track by `id` from search (or from the playlist).

```
GET /play?id=4
```

```json
{
  "ok": true,
  "id": 4,
  "title": "love.mp3",
  "liked": 0,
  "plays": 1,
  "path": "C:\\Music\\love.mp3"
}
```

Or play the first search hit:

```
GET /play?q=love
```

If both `id` and `q` are set, `id` wins.

### `GET /now`

Current playback. `state` is `playing`, `paused`, or `stopped`. No track fields when nothing is loaded.

```
GET /now
```

```json
{
  "ok": true,
  "state": "playing",
  "shuffle": 0,
  "position": 12,
  "id": 4,
  "title": "love.mp3",
  "artist": "",
  "album": "",
  "liked": 0,
  "plays": 1,
  "duration": 183,
  "path": "C:\\Music\\love.mp3"
}
```

- `position` — seconds into the track
- `shuffle` — 1 if random next is on (same as the window toggle)

### `GET /skip`

Next or previous track (same as the player buttons). `dir` is `next` or `prev`; omit for next. With shuffle on, next is a random other queue track, not `i+1`. Without shuffle, next wraps the queue in order. Previous restarts the current track if more than 3 seconds in.

```
GET /skip
GET /skip?dir=next
GET /skip?dir=prev
```

```json
{
  "ok": true,
  "dir": "next",
  "shuffle": 1,
  "id": 5,
  "title": "love_song.mp3",
  "liked": 0
}
```

### `GET /like?id=`

Toggle like. Omit `id` to use the current track. `liked=0` or `liked=1` sets instead of toggling.

```
GET /like?id=4
GET /like?id=4&liked=1
GET /like
```

```json
{ "ok": true, "id": 4, "liked": 1, "title": "love.mp3" }
```

### `GET /stop`

Stop playback (not pause). The current track is cleared.

```
GET /stop
```

```json
{ "ok": true }
```

## Errors

Unknown path → HTTP 404. Other failures → HTTP 200 with `"ok": false`.

```json
{ "ok": false, "error": "no match" }
```

| `error` | When |
| --- | --- |
| `unknown endpoint` | path is not `/search`, `/play`, `/now`, `/skip`, `/like`, `/stop` |
| `id or q required` | `/play` without `id` or `q` |
| `no match` | `/play?q=` found nothing, or `/like?id=` unknown |
| `nothing playing` | `/like` without `id` and nothing loaded |
| `bad dir` | `/skip?dir=` is not `next` or `prev` |
| `cannot play` | file missing or format not supported |
| `player not ready` | window busy or shutting down |

## Examples

```bat
curl "http://127.0.0.1:17321/search?q=love"
curl "http://127.0.0.1:17321/play?id=4"
curl "http://127.0.0.1:17321/now"
curl "http://127.0.0.1:17321/like?id=4&liked=1"
curl "http://127.0.0.1:17321/play?q=love"
curl "http://127.0.0.1:17321/skip?dir=next"
curl "http://127.0.0.1:17321/stop"
```

UTF-8 in `q` must be URL-encoded.

Typical agent flow: `/now` or `/search` → pick an `id` → `/play?id=` → `/skip?dir=next` → `/like?liked=1` → `/stop` when done.

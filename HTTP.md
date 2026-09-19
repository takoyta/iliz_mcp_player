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

Tools: `search` `{query}`, `play` `{id?, query?}`, `stop` `{}`. Semantics match REST. `play` prefers `id`. Empty `search` query returns the playlist (cap 50). Tool errors use `isError` and the same `error` strings as REST.

---

Local JSON API for agents. Available while the player is running.

- Base URL: `http://127.0.0.1:17321`
- Bind: loopback only
- `--port N` — listen on that port and skip the single-instance check (for tests)
- Methods: `GET` or `POST` (query string is what matters)
- Response: `Content-Type: application/json; charset=utf-8`

Search looks at **file names** in the current playlist (case-insensitive substring). It does not change the window search box.

Track `id` is the playlist index (stable until a track is removed).

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
      "duration": 183
    }
  ]
}
```

- `count` — matches in the playlist
- `returned` — items in `tracks` (max 50)
- `duration` — seconds, or `0` if not probed yet

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
  "path": "C:\\Music\\love.mp3"
}
```

Or play the first search hit:

```
GET /play?q=love
```

If both `id` and `q` are set, `id` wins.

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
| `unknown endpoint` | path is not `/search`, `/play`, `/stop` |
| `id or q required` | `/play` without `id` or `q` |
| `no match` | `/play?q=` found nothing |
| `cannot play` | file missing or format not supported |
| `player not ready` | window busy or shutting down |

## Examples

```bat
curl "http://127.0.0.1:17321/search?q=love"
curl "http://127.0.0.1:17321/play?id=4"
curl "http://127.0.0.1:17321/play?q=love"
curl "http://127.0.0.1:17321/stop"
```

UTF-8 in `q` must be URL-encoded.

Typical agent flow: `/search` → pick an `id` → `/play?id=` → `/stop` when done.

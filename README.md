# iliz MCP player

A compact playlist audio player for Windows. One window: transport on top, playlist below. Drop files or folders to queue tracks.

## System

- Windows 10 / 11 (x64)
- No installer: unpack and run

## Usage

- Drop files or folders onto the window.
- Space — pause / resume. Enter — play selected. Delete — remove selected.
- Click the bar to seek. Drag the volume pill.
- Esc or the close button — quit.

Playback via [BASS](https://www.un4seen.com/): MP3, WAV, OGG, AIFF, and other built-in formats.

While the player is running:

- REST: `http://127.0.0.1:17321/search|play|stop` — [HTTP.md](HTTP.md)
- MCP Streamable HTTP: `http://127.0.0.1:17321/mcp`

## Build

Requires Visual Studio Build Tools 2022 (MSVC x64) and `bass.dll` in `bin\`.

```bat
build.bat
```

## Tests

Player must be built. Python 3:

```bat
python tests\test_player.py
```

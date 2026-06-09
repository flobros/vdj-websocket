# VDJ WebSocket Plugin

A VirtualDJ 8 plugin that exposes a subscription-based WebSocket server for real-time deck data. Connect any web frontend or local app to read live deck state — title, artist, BPM, position, EQ, time, crossfader — at 100 ms intervals.

## Requirements

- VirtualDJ 8 or later (64-bit)
- **Windows**: Visual Studio 2019+ with **Desktop development with C++** workload (to build from source)
- **macOS**: Xcode Command Line Tools — `xcode-select --install` (to build from source)

## Installation

### Pre-built (recommended)

Download the files for your platform from the [latest release](https://github.com/flobros/vdj-websocket/releases/latest) and copy them to your VirtualDJ Plugins folder:

**Windows**
```
%LOCALAPPDATA%\VirtualDJ\Plugins64\Generics\
```
Files: `NowPlaying.dll` + `NowPlaying.ini`

**macOS**
```
~/Library/Application Support/VirtualDJ/Plugins64/Generics/
```
Files: `NowPlaying.bundle` + `NowPlaying.ini`

Open `NowPlaying.ini` and set `AuthToken` to a random secret (see [Configuration](#configuration)), then restart VirtualDJ.

### Build from source

**Windows**
```bat
git clone https://github.com/flobros/vdj-websocket
cd vdj-websocket
build.bat
```
`build.bat` auto-detects Visual Studio via `vswhere.exe` and installs directly to the VDJ Plugins folder. Pass a custom path as the first argument if VDJ is installed elsewhere:
```bat
build.bat "C:\custom\path\VirtualDJ\Plugins64\Generics"
```

**macOS**
```sh
git clone https://github.com/flobros/vdj-websocket
cd vdj-websocket
make install
```

## Configuration

Edit `NowPlaying.ini` in the same folder as the DLL:

```ini
[NowPlaying]

; WebSocket port (default: 9001)
Port=9001

; Comma-separated allowed Origin headers, or * to allow any.
; For public use, restrict to your frontend domain:
;   AllowedOrigins=https://mysite.com
AllowedOrigins=*

; Auth token — clients must include this in their subscribe message.
; Change this to a random secret before exposing the plugin publicly.
AuthToken=audioforward-local

; Comma-separated VDJ verb stems clients are allowed to subscribe to.
; "deck N " prefix is stripped before matching, so "get_title" covers
; both "deck 1 get_title" and "deck 2 get_title".
; Remove any verbs you don't want exposed.
AllowedVerbs=get_title,get_artist,get_remix_after_title,get_key,get_bpm,get_time "elapsed",get_time "remain",get_level,get_volume,eq_high,eq_mid,eq_low,filter,play,crossfader,get_pos
```

**For public / internet-facing use**, always set `AuthToken` to a strong random secret and restrict `AllowedOrigins` to your frontend domain.

## Wire Protocol

### Connect

Open a WebSocket connection to `ws://localhost:9001` (or whichever port you configured).

### Subscribe

Send one JSON message after connecting:

```json
{
  "subscribe": {
    "numeric": [
      "deck 1 get_pos",
      "deck 1 play",
      "deck 1 get_bpm",
      "deck 1 get_time \"elapsed\"",
      "deck 1 get_time \"remain\"",
      "deck 1 get_level",
      "deck 1 get_volume",
      "deck 1 eq_high",
      "deck 1 eq_mid",
      "deck 1 eq_low",
      "deck 1 filter",
      "deck 2 get_pos",
      "deck 2 play",
      "deck 2 get_bpm",
      "deck 2 get_time \"elapsed\"",
      "deck 2 get_time \"remain\"",
      "deck 2 get_level",
      "deck 2 get_volume",
      "deck 2 eq_high",
      "deck 2 eq_mid",
      "deck 2 eq_low",
      "deck 2 filter",
      "crossfader"
    ],
    "string": [
      "deck 1 get_title",
      "deck 1 get_artist",
      "deck 1 get_remix_after_title",
      "deck 1 get_key",
      "deck 2 get_title",
      "deck 2 get_artist",
      "deck 2 get_remix_after_title",
      "deck 2 get_key"
    ],
    "token": "your-auth-token"
  }
}
```

- `numeric` — verbs whose values are returned as JSON numbers
- `string` — verbs whose values are returned as JSON strings
- `token` — must match `AuthToken` in the INI (omit if auth is disabled)

Any verb not in the server's `AllowedVerbs` whitelist is silently dropped. If no subscribed verbs remain after filtering, the connection is closed.

### Receive

The plugin sends a full snapshot of all subscribed values every 100 ms:

```json
{
  "deck 1 get_pos": 0.4231,
  "deck 1 play": 1.0,
  "deck 1 get_bpm": 128.0,
  "deck 1 get_time \"elapsed\"": 95400,
  "deck 1 get_time \"remain\"": 186200,
  "deck 1 get_title": "Track Name",
  "deck 1 get_artist": "Artist",
  "crossfader": 0.5
}
```

Time values (`get_time "elapsed"` / `get_time "remain"`) are in **milliseconds**, pitch-adjusted. Their sum equals the actual pitch-adjusted track duration.

### Supported VDJ verbs

Any VDJ verb that works with `GetInfo` (numeric) or `GetStringInfo` (string) in the [VDJScript reference](https://www.virtualdj.com/wiki/VDJscript.html) can be subscribed to, subject to the `AllowedVerbs` whitelist.

Common useful verbs:

| Verb | Type | Description |
|------|------|-------------|
| `get_title` | string | Track title |
| `get_artist` | string | Track artist |
| `get_remix_after_title` | string | Remix tag from title |
| `get_key` | string | Key (e.g. `8B`) |
| `get_bpm` | numeric | Current BPM (pitch-adjusted) |
| `get_pos` | numeric | Playhead position 0–1 |
| `get_time "elapsed"` | numeric | Elapsed time in ms |
| `get_time "remain"` | numeric | Remaining time in ms |
| `get_level` | numeric | Audio level 0–1 |
| `get_volume` | numeric | Deck volume 0–1 |
| `eq_high` / `eq_mid` / `eq_low` | numeric | EQ band 0–1 |
| `filter` | numeric | Filter knob 0–1 |
| `play` | numeric | 1.0 = playing, 0.0 = paused |
| `crossfader` | numeric | Crossfader position 0–1 |

## Security

The plugin listens on `localhost` only — it is **not** accessible from the network unless you port-forward. For remote access:

- Always set a strong `AuthToken`
- Restrict `AllowedOrigins` to your specific frontend domain
- Limit `AllowedVerbs` to only what your client needs
- Avoid exposing verbs that reveal file paths (`get_filepath`, `get_vdj_folder`, etc.)

## License

MIT

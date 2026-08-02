# lukwm

A minimal, floating X11 window manager written in C, inspired by [sowm](https://github.com/dylanaraps/sowm) and the suckless philosophy.

- ~500 lines of C
- ~28 KB compiled binary
- ~2.7 MB RAM at runtime
- No runtime config parsing — everything is set in a header file and compiled in
- No external dependencies beyond Xlib

lukwm is a **floating** window manager (windows cascade on open, no automatic tiling layout). It aims to stay light and hackable while still covering real day-to-day usage: workspaces, EWMH basics for status bars, mouse-driven move/resize, and a self-restart mechanism so you can tweak your config without restarting your session.

## Features

- Floating window management with cascading window placement
- 9 workspaces (tags), with the ability to move windows between them
- Focus-follows-mouse
- Border color changes to indicate the focused window
- Move windows with `Super + Left click`, resize with `Super + Right click`
- Fullscreen toggle and a "maximize with gaps" mode (respects a configurable bar height and gap, so it won't cover a status bar like polybar)
- Alt-Tab window cycling (scoped to the current workspace)
- Graceful window closing via the ICCCM `WM_DELETE_WINDOW` protocol, with a fallback to `XKillClient` for clients that don't support it
- Partial EWMH support: `_NET_SUPPORTED`, `_NET_WM_NAME`, `_NET_SUPPORTING_WM_CHECK`, `_NET_NUMBER_OF_DESKTOPS`, `_NET_CURRENT_DESKTOP`, `_NET_ACTIVE_WINDOW` — enough for status bars like polybar to show workspaces and the active window
- Recognizes `_NET_WM_WINDOW_TYPE_DOCK` (status bars) and `_NET_WM_WINDOW_TYPE_DESKTOP` (wallpaper managers / desktop layers) and handles them appropriately instead of treating them as regular windows
- Self-restart (`Super + Shift + R`) — recompile your config and reload without killing your X session
- Configurable autostart command list
- Media key support (volume up/down/mute)
- Config split into `config.def.h` (versioned template) and `config.h` (your personal, uncommitted config) — suckless-style: no config file parsing, you edit a header and recompile

## Known limitations

Being upfront about what lukwm does **not** do, so you know what you're getting into:

- **No tiling layout.** Windows cascade; there's no master-stack, no automatic arrangement. Use the mouse or your own workflow.
- **Self-restart forgets pre-existing windows.** `Super + Shift + R` uses `execvp` to reload the binary in-place. This resets all in-memory state, so windows that were already open before the restart become untracked by the internal window list (they stay on screen, but drop out of Alt-Tab, workspace switching, and EWMH's client list) until you close and reopen them.
- **Single monitor only.** No multi-monitor awareness yet.
- **Config requires recompiling.** There's no way to change keybindings or commands without editing `config.h` and running `make`. This is intentional (suckless philosophy), not a bug.

## Dependencies

**Build:**
- A C compiler (`gcc` or `clang`)
- `libX11` development headers (Xlib, Xatom, XKB)

**Runtime (defaults used in `config.def.h` — all swappable):**
- A terminal emulator (default: `kitty`)
- `rofi` — application launcher
- A browser (default: `qutebrowser`)
- `wpctl` (PipeWire's WirePlumber CLI) — volume control
- `nitrogen`, `polybar`, `picom` — optional autostart entries for wallpaper, status bar, and compositing

None of the runtime tools are hard dependencies of lukwm itself — they're just what the default `config.h` spawns. Edit `config.h` to point at whatever tools you actually use.

## Installation

```sh
git clone https://github.com/simeulinuxkaliaiwr/lukwm.git
cd lukwm
make
sudo make install
```

This compiles `lukwm`, installs the binary to `/usr/local/bin/lukwm`, and installs `lukwm.desktop` to `/usr/share/xsessions/` so display managers (SDDM, LightDM, etc.) can offer lukwm as a session option.

**Do not delete the cloned directory after installing.** Your configuration (`config.h`) lives there, not in `/usr/local/bin`. lukwm's config is compiled into the binary — see [Configuration](#configuration) below.

### Uninstall

```sh
sudo make uninstall
```

## Usage

### From a display manager

Select "lukwm" from your DM's session list at login.

### From `startx` / a TTY

```sh
startx lukwm -- :1 vt$(fgconsole)
```

The `vt$(fgconsole)` part matters: `startx` needs to launch Xorg on the VT you're actually logged into, or Xorg may fail to acquire it.

## Configuration

lukwm has no runtime config file. Configuration lives in `config.h`, which is `#include`d directly into `lukwm.c` at compile time.

- **`config.def.h`** — the versioned template shipped in the repo. This is what defines the *defaults*. Don't edit this for personal tweaks; it's meant to represent sane defaults for anyone cloning the repo.
- **`config.h`** — generated automatically from `config.def.h` on first `make` (and never overwritten afterward, and never tracked by git). This is where you make your actual changes.

To customize lukwm:

```sh
vim config.h 
make
sudo make install
```

Or, if lukwm is already running, use the self-restart keybind (`Super + Shift + R`) instead of logging out — it recompiles-and-reloads in place (run `make` first in a terminal, then hit the keybind).

### What you can configure

- `MOD` / `ALTMOD` — modifier keys (default: Super / Alt)
- `NUM_WS` — number of workspaces
- `BORDER_FOCUS` / `BORDER_UNFOCUS` — border colors (hex strings)
- `BORDER_WIDTH` — border thickness in pixels
- `BAR_HEIGHT` / `GAP` — spacing used by the "maximize" mode
- `term_cmd`, `menu_launcher`, `browser`, `vol_up`, `vol_down`, `vol_mute` — commands spawned by their respective keybinds
- `autostart[]` — a list of commands run once at startup (each command is its own `NULL`-terminated array, so any number of arguments is supported)

## Keybindings

| Keybind | Action |
|---|---|
| `Super + Return` | Open terminal |
| `Super + A` | Open app launcher |
| `Super + X` | Open browser |
| `Super + Q` | Close focused window |
| `Super + F` | Toggle fullscreen |
| `Super + M` | Toggle maximize (fullscreen with gaps, respects bar height) |
| `Super + 1..9` | Switch to workspace 1–9 |
| `Super + Shift + 1..9` | Move focused window to workspace 1–9 |
| `Alt + Tab` | Cycle focus between windows (current workspace) |
| `Super + Shift + R` | Restart lukwm in place |
| `Super + Left click + drag` | Move window |
| `Super + Right click + drag` | Resize window |
| `XF86AudioRaiseVolume` | Volume up |
| `XF86AudioLowerVolume` | Volume down |
| `XF86AudioMute` | Toggle mute |

All of these are defined in `keypress()`/`input_grab()` in `lukwm.c` and reference commands from `config.h` — edit both if you want to add new bindings.

## Philosophy

lukwm follows the suckless approach: source-level configuration over config files, a single small `.c` file over a plugin system, and doing one thing (window management) without trying to also be a status bar, launcher, or wallpaper manager. If you want those, run separate tools alongside it — that's the point.

## License

MIT — see [LICENSE](LICENSE).

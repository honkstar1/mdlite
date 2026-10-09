# mdlite

Markdown lister plugin for Total Commander, without a browser engine. Also builds a standalone viewer, `mdlite.exe`.

`.md` -> [md4c](https://github.com/mity/md4c) (HTML) -> [litehtml](https://github.com/litehtml/litehtml) (layout) -> GDI+ (drawing).
Opening a file costs a parse and a layout (about 40 ms for a 50 KB file) instead of starting WebView2.

## Quick start (Windows, 64-bit Total Commander)

### 1. Prerequisites

- **Visual Studio 2022 or newer** with the "Desktop development with C++" workload (includes cmake and ninja).
- **Git** on your PATH.

### 2. Build

Open PowerShell in the repo folder:

```powershell
.\setup.ps1      # downloads md4c and litehtml into third_party\ (one time)
.\build.ps1      # builds build\mdlite.wlx64 and build\mdlite.exe
```

If PowerShell refuses to run the scripts, use
`powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1` (same for `setup.ps1`).

### 3. Install in Total Commander

1. Copy `build\mdlite.wlx64` somewhere permanent, e.g. `C:\Tools\mdlite\mdlite.wlx64`.
2. In Total Commander: **Configuration -> Options -> Plugins -> Lister plugins (WLX) -> Configure**.
3. Click **Add**, pick `mdlite.wlx64`. Total Commander reads the detect string from the plugin, so `.md`, `.markdown`,
   `.mkd` and `.mkdn` are claimed automatically.
4. Make sure mdlite is **above** any other markdown plugin in the list (use the arrows), then **OK**.

Use it: select a `.md` file and press **F3** (or **Ctrl+Q** for the quick-view panel).

Uninstall: remove it from the same plugin list and delete the file.

## Keys

| Key | Action |
| --- | --- |
| Arrows, PgUp/PgDn, Space, Home/End, wheel | Scroll |
| Ctrl+wheel, Ctrl+`+` / Ctrl+`-` | Zoom in / out (50%-400%, kept for the session) |
| Ctrl+0 | Reset zoom |
| Ctrl+F | Find (case-insensitive, highlights as you type) |
| Enter / F3, Shift+Enter / Shift+F3 | Next / previous match |
| Esc | Close the find bar; otherwise close the viewer |

In Total Commander, Lister's own **Find** (F7) and **Find next** also go to the find bar, and `n`/`p` still move
between files.

## Standalone viewer

```powershell
build\mdlite.exe some.md                # or run it without an argument to pick a file
```

`mdlite.exe` is the same view in its own window and needs only itself (static runtime). Drop a file on the window to
open it. To make it the default app for `.md`, use "Open with" -> "Choose another app" in Explorer.

## Testing without Total Commander

```powershell
pwsh tools\smoke.ps1 -File some.md      # hosts the DLL in a window, prints load time, saves build\smoke.png
build\mdlite_bench.exe some.md 5        # headless timing: md4c / parse / layout
```

## Limits

- x64 only; follows the Windows light/dark app theme.
- No syntax highlighting (code blocks are plain monospace), no copy, no print.
- Zoom scales text and spacing; images keep their size.
- `#anchor` links and relative links to other files do nothing (md4c emits no heading ids). `http(s)` and `mailto` open externally.
- Local images work; remote images are skipped on purpose so opening never waits for the network.
- `src/container/` is litehtml's Windows container adapted to its float `pixel_t`; the shipped version does not compile
  against litehtml HEAD.

## License

MIT, see [LICENSE](LICENSE). Third-party notices (litehtml BSD-3, md4c MIT) are in [NOTICE](NOTICE).

# mdlite

Markdown lister plugin for Total Commander, without a browser engine.

`.md` -> [md4c](https://github.com/mity/md4c) (HTML) -> [litehtml](https://github.com/litehtml/litehtml) (layout) -> GDI+ (drawing).
Opening a file costs a parse and a layout (about 40 ms for a 50 KB file) instead of starting WebView2.

## Build

```
.\setup.ps1      # clone pinned md4c + litehtml into third_party/
.\build.ps1      # x64 Release -> build\mdlite.wlx64, build\mdlite_bench.exe
```

Needs Visual Studio 2022+ C++ tools (the script imports `vcvars64` and uses VS's bundled cmake/ninja).

## Try it

```
pwsh tools\smoke.ps1 -File some.md      # hosts the DLL in a window, prints load time, saves build\smoke.png
build\mdlite_bench.exe some.md 5        # headless timing: md4c / parse / layout
```

Install: Total Commander -> Configuration -> Options -> Plugins -> Lister plugins (WLX) -> Add `mdlite.wlx64`.

## Prototype limits

- x64 only; follows the Windows light/dark app theme.
- No syntax highlighting (code blocks are plain monospace), no find-in-page, no copy, no print.
- `#anchor` links and relative links to other files do nothing (md4c emits no heading ids). `http(s)` and `mailto` open externally.
- Local images work; remote images are skipped on purpose so opening never waits for the network.
- `src/container/` is litehtml's Windows container adapted to its float `pixel_t`; the shipped version does not compile
  against litehtml HEAD.

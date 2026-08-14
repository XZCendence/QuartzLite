# Quartz

quartz is a native log viewer for UEFN but you can really use it for anything. It was born out of my frustration with UE's shitty log viewer that takes several seconds to open and is incredibly slow to scroll. Don't even get me started on the search functionality.

Built on ImGui v1.92.9 docking branch, along with the dx11 backend impl.

## Building

Windows, Visual Studio 2022+ with the C++ workload:

```
build.bat
```

finds VS via vswhere and builds `build/Quartz.exe` with cmake and ninja.

## Keybinds

| Key | Action |
| --- | --- |
| Ctrl+O | Open a log file |
| Ctrl+F | Find in the focused view |
| Enter / Shift+Enter, F3 | Next / previous match |
| Ctrl+A / Ctrl+C | Select all / copy selection |
| Ctrl+L | Clear the focused view |
| Esc | Clear selection / close find |

## Third party

- [Dear ImGui](https://github.com/ocornut/imgui) (docking, with small local patches
  for the glass backdrop and accent-outlined controls) — MIT
- [JetBrains Mono](https://www.jetbrains.com/lp/mono/) — SIL OFL 1.1

## License

MIT — see [LICENSE](LICENSE).

## Slop Notice
Application contains a fair bit of LLM code but based on imgui which is pretty non sloppy so it's cool ig

# ChessAssist

A Windows/Linux/macOS desktop app that watches a live chess.com or Lichess game in an app-managed
Chrome window, feeds the position to a local Stockfish, and shows its analysis - depth, score,
principal variation, an on-board best-move arrow, an eval bar, and a chess.com-style accuracy
metric - alongside a live visual board.

It can also drive the engine's moves onto the board itself (autoplay). **This is built for
testing your own engine/bot against chess.com/Lichess bots or other engines, not for playing
against a live human opponent** - autoplay takes the player out of the loop entirely, which
would be assisted cheating in a real game.

## Features

- **Live game tracking**: connects to an already-open chess.com or Lichess tab via the Chrome
  DevTools Protocol, reads the move list out of the page DOM, and mirrors it as a real board
  (no screenshotting/OCR).
- **Engine analysis**: depth, score (cp or mate-in-N, always shown from White's perspective
  regardless of whose turn is being searched), nodes/nps, and the principal variation, via a
  bundled Stockfish over UCI.
- **Board overlay**: best-move arrow (turns red and is paired with an on-board "mate in N"
  banner when a forced mate is found), source/destination square highlights, a vertical eval
  bar that flips with board orientation, and a highlight on whichever king is currently in
  check.
- **Autoplay**: drags the engine's suggested move onto the board itself, via trusted synthetic
  input events (not scripted DOM mutation) so sites that reject `isTrusted: false` events still
  accept the move.
- **Elo-based strength presets**: one slider drives both the engine's own `UCI_Elo`/
  `UCI_LimitStrength` and a matching search time/depth preset, instead of separate manual
  controls.
- **Blitz mode**: caps every search to a short, fixed think time so autoplay can keep pace with
  fast time controls.
- **Premoves (experimental)**: while autoplay is on, reacts to the opponent's move without
  waiting out the full search time whenever it either matches what the last search's principal
  variation already predicted (an instant, fully-verified reply), or falls back to a short
  capped-time search rather than an unverified guess. See the comment on
  `GameSession::SetPremoveEnabled` for the full scheme.
- **Accuracy metric**: per-move centipawn-loss based, using the same exponential-decay curve
  chess.com's own accuracy score uses.
- **Promotion handling**: recognizes and clicks the site's live promotion picker for the
  requested piece.
- **Modern dark theme** with a bundled Roboto Medium UI font, dockable panels (Controls,
  Tracked Board + engine info, Log).

## Repository layout

```
Source/
  App.h/.cpp          Owns the window, panels, engine, and session; runs the main loop.
  main.cpp             Entry point.
  Chess/                Board types + a SAN-to-UCI move translator (ChessRules) - not a full
                        legal-move engine; Stockfish is the source of truth for legality.
  Engine/               UCI client/protocol and process management for talking to Stockfish.
  Process/              Cross-platform child-process spawning (Win32/POSIX).
  Browser/              Chrome launching, a CDP client, and the chess.com/Lichess DOM-reading
                        and move-playing scripts (ChessSiteAdapter).
  Game/                 GameSession (live-game orchestration) and GameTracker (move history/
                        board state).
  UI/                   ImGui panels: Controls, BoardStatePanel (board + eval bar + arrow),
                        EngineInfoPanel, LogPanel, AppWindow.
  Logging/              spdlog setup + an ImGui log sink.
Tests/                  GoogleTest suite, including fixture-driven tests that launch a real
                        Chrome against local HTML fixtures (Tests/Fixtures/).
Vendor/                 Stockfish, built from source as part of the CMake build.
Assets/                 Fonts and chessboard/piece images, copied next to the built exe.
```

## Building

### Prerequisites

- CMake 4.0+ and Ninja (or Visual Studio 2022 for the MSVC preset).
- A C++23 compiler: LLVM/Clang (paths in `CMakePresets.json` point at
  `C:/Program Files/LLVM` on Windows, `/usr/bin/clang-22`/`clang++-22` on Linux, and
  `/usr/bin/clang`/`clang++` - i.e. the Xcode Command Line Tools - on macOS) or MSVC via
  Visual Studio 2022 on Windows.
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set - dependencies are
  resolved via manifest mode (`vcpkg.json`), so no manual install step is needed beyond that.
- **MSYS2 with a MinGW-w64 toolchain** on Windows (expected at `C:/msys64`) - Stockfish itself
  is built from its own Makefile via `Vendor/CMakeLists.txt`, not vcpkg. On Linux and macOS, a
  plain `make`/system compiler toolchain is used instead (Xcode Command Line Tools provide both
  on macOS).
- Google Chrome installed (the app launches and drives its own dedicated Chrome instance; a
  handful of tests do the same against local fixtures).

### Configure and build

Using the provided CMake presets:

```sh
cmake --preset windows-clang-debug
cmake --build --preset windows-clang-debug
```

(swap in `windows-clang-release`, `windows-msvc-debug`/`-release`, `linux-clang-debug`/
`-release`, or `macos-clang-debug`/`-release` as needed). This builds Stockfish from
`Vendor/stockfish` as part of the build, and
copies both the resulting engine binary and `Assets/` next to the built executable.

### Running

```sh
out/build/windows-clang-debug/Source/ChessAssist.exe
```

The app launches its own Chrome instance pointed at chess.com/Lichess; use the Controls panel
to connect once a game is open, then configure Elo/Blitz/Premove/Autoplay as desired.

### Tests

```sh
cmake --build --preset windows-clang-debug --target ChessAssistTests
out/build/windows-clang-debug/Tests/ChessAssistTests.exe
```

Most tests are pure unit tests (UCI protocol parsing, chess rules, move-list diffing), but
`BrowserPipelineTests` launches a real Chrome against local HTML fixtures under
`Tests/Fixtures/` to exercise the DOM-extraction and click-target scripts end-to-end - Chrome
must be installed wherever these run.

## License

GPLv3 - see [LICENSE](LICENSE).

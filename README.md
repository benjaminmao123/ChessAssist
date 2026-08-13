# ChessAssist

[![CI](https://github.com/benjaminmao123/ChessAssist/actions/workflows/ci.yml/badge.svg)](https://github.com/benjaminmao123/ChessAssist/actions/workflows/ci.yml)

A desktop app for Windows, Linux, and macOS that watches a live chess.com or Lichess game and
overlays real-time engine analysis on top of it.

It launches its own Chrome window, reads the live position straight from the page, and feeds it
to a bundled Stockfish. You get depth, score, the principal variation, an eval bar, and a
best-move arrow drawn right on the board - plus a chess.com-style accuracy score for the game
so far.

> **Not for playing against a human.** ChessAssist can also play the engine's moves for you
> (autoplay). That's meant for testing your own engine/bot against chess.com/Lichess bots or
> other engines. Using it against a real opponent takes the player out of the loop entirely,
> which is assisted cheating.

## Features

- **Live game tracking** - connects to an already-open chess.com or Lichess tab over the Chrome
  DevTools Protocol and reads the move list straight out of the page. No screenshotting or OCR.
- **Engine analysis** - depth, score, nodes/nps, and the principal variation from a bundled
  Stockfish, always shown from White's perspective regardless of whose turn is being searched.
- **Board overlay** - a best-move arrow (turns red with an on-board "mate in N" banner when one's
  found), source/destination highlights, an eval bar that flips with board orientation, and a
  highlight on whichever king is in check.
- **Analysis Board** - a free-standing position-analysis tool, independent of the live game: play
  out or paste (via FEN) any position, step backward/forward, flip orientation, and get live
  engine analysis.
- **Sandbox lines** - explore hypothetical "what if" continuations from the live position without
  disturbing the actual game tracking, complete with a lookahead arrow for the predicted reply.
- **Autoplay** - plays the engine's move on the board itself using trusted synthetic input
  events, so sites that reject scripted (`isTrusted: false`) moves still accept it.
- **Elo-based strength presets** - one slider sets both the engine's `UCI_Elo`/
  `UCI_LimitStrength` and a matching search time/depth, instead of tuning them separately.
- **Blitz mode** - caps every search to a short, fixed think time so autoplay can keep up with
  fast time controls.
- **Premoves** *(experimental)* - while autoplay is on, replies instantly if the opponent plays
  what the last search already predicted, or falls back to a short capped-time search rather
  than guessing. See `GameSession::SetPremoveEnabled` for the full scheme.
- **Accuracy metric** - per-move centipawn loss, using the same exponential-decay curve
  chess.com's own accuracy score uses.
- **Promotion handling** - recognizes and clicks the site's live promotion picker automatically.
- **Modern dark theme** with a bundled Roboto Medium font and dockable panels (Controls, Tracked
  Board + engine info, Log).

## Repository layout

```
Source/
  App.h/.cpp    Owns the window, panels, engine, and session; runs the main loop.
  main.cpp      Entry point.
  Chess/        Board types + a SAN-to-UCI move translator (ChessRules) - not a full legal-move
                engine; Stockfish is the source of truth for legality.
  Engine/       UCI client/protocol and process management for talking to Stockfish.
  Process/      Cross-platform child-process spawning (Win32/POSIX).
  Browser/      Chrome launching, a CDP client, and the chess.com/Lichess DOM-reading and
                move-playing scripts (ChessSiteAdapter).
  Game/         GameSession (live-game orchestration), GameTracker (move history/board state),
                SandboxSession/AnalysisBoardSession (hypothetical lines), PremoveTracker, and
                AccuracyTracker.
  UI/           ImGui panels: Controls, BoardStatePanel (live board + eval bar + arrow),
                AnalysisBoardPanel (free-standing analysis), EngineInfoPanel, LogPanel, AppWindow,
                and ChessBoardWidget (shared board/piece-drag rendering).
  Logging/      spdlog setup + an ImGui log sink.
Tests/          GoogleTest suite, including fixture-driven tests that launch a real Chrome
                against local HTML fixtures (Tests/Fixtures/).
Vendor/         Stockfish, built from source as part of the CMake build.
Assets/         Fonts and chessboard/piece images, copied next to the built executable.
```

## Building

### Prerequisites

Everywhere:

- CMake 4.0+ and Ninja (or Visual Studio 2022, if using the MSVC preset)
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set. Dependencies resolve via
  manifest mode (`vcpkg.json`), so there's no manual install step beyond that.
- Google Chrome. The app launches and drives its own dedicated Chrome instance, and a handful of
  tests do the same against local fixtures.

Per platform - Stockfish is built from its own Makefile (`Vendor/CMakeLists.txt`), not vcpkg, so
each platform needs its own compiler/`make` toolchain available too:

- **Windows** - LLVM/Clang (`CMakePresets.json` expects it at `C:/Program Files/LLVM`) or MSVC
  via Visual Studio 2022. Either way you need **Visual Studio 2022 Build Tools installed and on
  PATH** (e.g. run from a "Developer PowerShell for VS 2022"): `clang.exe` on Windows targets the
  MSVC ABI, so even the Clang preset needs `link.exe`/the Windows SDK, not just an MSVC compiler.
  Also needs **MSYS2 with a MinGW-w64 toolchain** at `C:/msys64`, which is what actually builds
  Stockfish.
- **Linux** - `clang-22`/`clang++-22`, plus a plain `make`/GCC toolchain. vcpkg builds several
  dependencies from source, which pulls in system packages beyond the compiler itself:
  ```sh
  sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
      libxext-dev libgl1-mesa-dev autoconf autoconf-archive automake libtool libltdl-dev
  ```
- **macOS** - Xcode Command Line Tools, which provide both `clang` and `make`.

### Configure and build

Using the provided CMake presets:

```sh
cmake --preset windows-clang-debug
cmake --build --preset windows-clang-debug
```

Swap in whichever preset matches your platform: `windows-clang-release`,
`windows-msvc-debug`/`-release`, `linux-clang-debug`/`-release`, or
`macos-clang-debug`/`-release`.

This builds Stockfish from `Vendor/stockfish` as part of the build, and copies both the
resulting engine binary and `Assets/` next to the built executable.

### Running

```sh
out/build/windows-clang-debug/Source/ChessAssist.exe
```

The app launches its own Chrome instance pointed at chess.com/Lichess. Open a game, hit Connect
in the Controls panel, then configure Elo/Blitz/Premove/Autoplay as you like.

### Tests

```sh
cmake --build --preset windows-clang-debug --target ChessAssistTests
out/build/windows-clang-debug/Tests/ChessAssistTests.exe
```

Most tests are pure unit tests (UCI protocol parsing, chess rules, move-list diffing). The
exception is `BrowserPipelineTests`, which launches a real Chrome against local HTML fixtures
under `Tests/Fixtures/` to exercise the DOM-extraction and click-target scripts end-to-end -
Chrome must be installed wherever these run.

## License

GPLv3 - see [LICENSE](LICENSE).

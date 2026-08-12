#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class EngineErrorCode
{
    ExecutableNotFound,
    ProcessLaunchFailed,
    ProcessNotRunning,
    HandshakeFailed,
    WriteFailed,
    UnexpectedEof,
    SearchAlreadyInProgress,
    InvalidArgument,
};

struct EngineError
{
    EngineErrorCode Code;
    std::string Message;
};

struct SearchLimits
{
    std::optional<int> Depth;
    std::optional<int> MoveTimeMs;
    std::optional<std::int64_t> Nodes;
    bool Infinite = false;
    std::optional<int> WhiteTimeMs;
    std::optional<int> BlackTimeMs;
    std::optional<int> WhiteIncMs;
    std::optional<int> BlackIncMs;
};

struct SearchInfo
{
    int Depth = 0;
    std::optional<int> SelDepth;
    std::optional<int> ScoreCp;
    std::optional<int> ScoreMate;
    std::optional<std::int64_t> Nodes;
    std::optional<std::int64_t> Nps;
    std::optional<int> TimeMs;
    std::vector<std::string> Pv;

    // 1-based UCI "multipv N" line index - which of the engine's N requested candidate lines
    // (see the "MultiPV" UCI option) this info line is reporting on. Defaults to 1 (the
    // engine's own default, and what every line reports when MultiPV is left at its default of
    // 1, in which case Stockfish omits the token entirely rather than sending "multipv 1").
    int MultiPvIndex = 1;
};

struct BestMoveResult
{
    std::string BestMove;
    std::optional<std::string> PonderMove;
};

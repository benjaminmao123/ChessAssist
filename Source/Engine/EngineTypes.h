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

    // 1-based UCI "multipv N" index this info line reports on. Defaults to 1, since Stockfish
    // omits the token entirely when the "MultiPV" option is left at its default of 1.
    int MultiPvIndex = 1;
};

struct BestMoveResult
{
    std::string BestMove;
    std::optional<std::string> PonderMove;
};

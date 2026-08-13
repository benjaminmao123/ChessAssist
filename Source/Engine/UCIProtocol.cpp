#include "UCIProtocol.h"

#include <charconv>
#include <sstream>

namespace UCIProtocol
{
std::string BuildPositionCommand(std::string_view fen, std::span<const std::string> moves)
{
    std::string command = "position fen ";
    command += fen;

    if (!moves.empty()) {
        command += " moves";
        for (const std::string& move : moves) {
            command += ' ';
            command += move;
        }
    }

    return command;
}

std::string BuildGoCommand(const SearchLimits& limits)
{
    std::string command = "go";

    if (limits.Infinite) {
        command += " infinite";
        return command;
    }

    if (limits.Depth)
        command += " depth " + std::to_string(*limits.Depth);

    if (limits.MoveTimeMs)
        command += " movetime " + std::to_string(*limits.MoveTimeMs);

    if (limits.Nodes)
        command += " nodes " + std::to_string(*limits.Nodes);

    if (limits.WhiteTimeMs)
        command += " wtime " + std::to_string(*limits.WhiteTimeMs);

    if (limits.BlackTimeMs)
        command += " btime " + std::to_string(*limits.BlackTimeMs);

    if (limits.WhiteIncMs)
        command += " winc " + std::to_string(*limits.WhiteIncMs);

    if (limits.BlackIncMs)
        command += " binc " + std::to_string(*limits.BlackIncMs);

    return command;
}

std::optional<SearchInfo> ParseInfoLine(std::string_view line)
{
    if (!line.starts_with("info"))
        return std::nullopt;

    std::istringstream stream{std::string(line)};
    std::string token;
    stream >> token;

    SearchInfo info;
    bool sawSearchField = false;

    while (stream >> token) {
        if (token == "depth") {
            stream >> info.Depth;
            sawSearchField = true;
        }
        else if (token == "seldepth") {
            int selDepth = 0;
            stream >> selDepth;
            info.SelDepth = selDepth;
        }
        else if (token == "score") {
            std::string scoreType;
            stream >> scoreType;

            if (scoreType == "cp") {
                int cp = 0;
                stream >> cp;
                info.ScoreCp = cp;
            }
            else if (scoreType == "mate") {
                int mate = 0;
                stream >> mate;
                info.ScoreMate = mate;
            }

            sawSearchField = true;
        }
        else if (token == "nodes") {
            std::int64_t nodes = 0;
            stream >> nodes;
            info.Nodes = nodes;
        }
        else if (token == "nps") {
            std::int64_t nps = 0;
            stream >> nps;
            info.Nps = nps;
        }
        else if (token == "time") {
            int timeMs = 0;
            stream >> timeMs;
            info.TimeMs = timeMs;
        }
        else if (token == "multipv") {
            stream >> info.MultiPvIndex;
        }
        else if (token == "pv") {
            std::string move;
            while (stream >> move)
                info.Pv.push_back(move);
        }
        // Other fields (hashfull, tbhits, currmove, currmovenumber, string, ...) are ignored -
        // not needed for best-move extraction.
    }

    if (!sawSearchField)
        return std::nullopt;

    return info;
}

std::optional<BestMoveResult> ParseBestMoveLine(std::string_view line)
{
    if (!line.starts_with("bestmove"))
        return std::nullopt;

    std::istringstream stream{std::string(line)};
    std::string token;
    stream >> token;

    BestMoveResult result;
    if (!(stream >> result.BestMove))
        return std::nullopt;

    std::string ponder;
    if (stream >> ponder && ponder == "ponder" && stream >> ponder)
        result.PonderMove = ponder;

    return result;
}
}  // namespace UCIProtocol

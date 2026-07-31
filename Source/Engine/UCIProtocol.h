#pragma once

#include "EngineTypes.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace UCIProtocol
{
std::string BuildPositionCommand(std::string_view fen, std::span<const std::string> moves);
std::string BuildGoCommand(const SearchLimits& limits);

std::optional<SearchInfo> ParseInfoLine(std::string_view line);
std::optional<BestMoveResult> ParseBestMoveLine(std::string_view line);
}  // namespace UCIProtocol

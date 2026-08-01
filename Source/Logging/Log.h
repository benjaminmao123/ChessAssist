#pragma once

// Every log call in the codebase goes through these macros instead of calling spdlog
// directly, so the concrete logging backend is swappable in one place (this header) rather
// than at every call site. LogPanel/ImGuiLogSink's spdlog sink registration in main.cpp is
// backend-wiring, not a call site, so it's left talking to spdlog directly.
#include <spdlog/spdlog.h>

#define LOG_TRACE(...) spdlog::trace(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)

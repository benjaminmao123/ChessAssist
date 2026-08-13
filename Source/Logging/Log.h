#pragma once

// All logging goes through these macros instead of calling spdlog directly, so the backend
// stays swappable in one place. Sink registration (LogPanel/ImGuiLogSink) is backend-wiring,
// not a call site, so it talks to spdlog directly.
#include <spdlog/spdlog.h>

#define LOG_TRACE(...) spdlog::trace(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)

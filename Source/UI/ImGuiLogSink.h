#pragma once

#include "LogPanel.h"

#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>

#include <mutex>
#include <string_view>

// Forwards formatted log messages into a LogPanel for in-app display, in addition to
// whatever other sinks (e.g. console) are already attached to the logger. Mutex is the
// same base_sink locking parameter every other spdlog sink takes - use ImGuiLogSinkMt
// below rather than instantiating this directly.
template <typename Mutex>
class ImGuiLogSink : public spdlog::sinks::base_sink<Mutex>
{
public:
    explicit ImGuiLogSink(LogPanel& panel)
        : m_Panel(panel)
    {
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);
        m_Panel.AddLine(msg.level, std::string_view(formatted.data(), formatted.size()));
    }

    void flush_() override
    {
    }

private:
    LogPanel& m_Panel;
};

using ImGuiLogSinkMt = ImGuiLogSink<std::mutex>;

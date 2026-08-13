#pragma once

#include "LogPanel.h"

#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>

#include <mutex>
#include <string_view>

// Forwards formatted log messages to a LogPanel for in-app display, alongside whatever other
// sinks (e.g. console) are attached. Use ImGuiLogSinkMt below rather than instantiating this
// template directly.
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

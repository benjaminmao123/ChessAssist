#include "CdpClient.h"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXWebSocket.h>

#include <nlohmann/json.hpp>

#include <condition_variable>
#include <mutex>

struct CdpClient::Impl
{
    ix::WebSocket WebSocket;
    std::atomic<std::uint64_t> NextRequestId{1};

    std::mutex ConnectMutex;
    std::condition_variable ConnectCv;
    bool ConnectOpened = false;
    bool ConnectFailed = false;

    std::mutex PendingMutex;
    std::condition_variable PendingCv;
    std::uint64_t PendingRequestId = 0;
    bool HasPending = false;
    bool ConnectionLost = false;
    std::optional<nlohmann::json> PendingResponse;

    void HandleMessage(const ix::WebSocketMessagePtr& message)
    {
        if (message->type == ix::WebSocketMessageType::Open)
        {
            std::scoped_lock lock(ConnectMutex);
            ConnectOpened = true;
            ConnectCv.notify_all();
            return;
        }

        if (message->type == ix::WebSocketMessageType::Error || message->type == ix::WebSocketMessageType::Close)
        {
            {
                std::scoped_lock lock(ConnectMutex);
                ConnectFailed = true;
                ConnectCv.notify_all();
            }
            {
                std::scoped_lock lock(PendingMutex);
                ConnectionLost = true;
                PendingCv.notify_all();
            }
            return;
        }

        if (message->type != ix::WebSocketMessageType::Message)
            return;

        nlohmann::json parsed;
        try
        {
            parsed = nlohmann::json::parse(message->str);
        }
        catch (const nlohmann::json::exception&)
        {
            return;  // malformed frame, ignore
        }

        if (!parsed.contains("id"))
            return;  // unsolicited CDP event - no subscriptions used, ignore

        std::scoped_lock lock(PendingMutex);
        if (HasPending && parsed["id"].get<std::uint64_t>() == PendingRequestId)
        {
            PendingResponse = std::move(parsed);
            PendingCv.notify_all();
        }
    }
};

CdpClient::CdpClient()
    : m_Impl(std::make_unique<Impl>())
{
}

CdpClient::~CdpClient()
{
    Disconnect();
}

std::optional<std::string> CdpClient::FindPageWebSocketUrl(std::uint16_t port, std::string_view urlSubstring)
{
    const std::string url = "http://localhost:" + std::to_string(port) + "/json";

    ix::HttpClient httpClient;
    const ix::HttpRequestArgsPtr args = httpClient.createRequest(url, ix::HttpClient::kGet);
    const ix::HttpResponsePtr response = httpClient.get(url, args);

    if (!response || response->statusCode != 200)
        return std::nullopt;

    nlohmann::json pages;
    try
    {
        pages = nlohmann::json::parse(response->body);
    }
    catch (const nlohmann::json::exception&)
    {
        return std::nullopt;
    }

    if (!pages.is_array())
        return std::nullopt;

    for (const nlohmann::json& page : pages)
    {
        if (!page.contains("url") || !page.contains("webSocketDebuggerUrl"))
            continue;

        const std::string pageUrl = page["url"].get<std::string>();
        if (pageUrl.find(urlSubstring) != std::string::npos)
            return page["webSocketDebuggerUrl"].get<std::string>();
    }

    return std::nullopt;
}

std::expected<void, CdpError> CdpClient::Connect(const std::string& webSocketDebuggerUrl, std::chrono::milliseconds timeout)
{
    m_Impl->ConnectOpened = false;
    m_Impl->ConnectFailed = false;

    m_Impl->WebSocket.setUrl(webSocketDebuggerUrl);
    m_Impl->WebSocket.disableAutomaticReconnection();
    m_Impl->WebSocket.setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) { m_Impl->HandleMessage(message); });
    m_Impl->WebSocket.start();

    std::unique_lock lock(m_Impl->ConnectMutex);
    const bool signaled = m_Impl->ConnectCv.wait_for(lock, timeout, [this] { return m_Impl->ConnectOpened || m_Impl->ConnectFailed; });

    if (!signaled || m_Impl->ConnectFailed || !m_Impl->ConnectOpened)
    {
        lock.unlock();
        m_Impl->WebSocket.stop();
        return std::unexpected(CdpError{"Failed to connect to CDP WebSocket within timeout"});
    }

    return {};
}

void CdpClient::Disconnect()
{
    m_Impl->WebSocket.stop();
}

bool CdpClient::IsConnected() const
{
    return m_Impl->WebSocket.getReadyState() == ix::ReadyState::Open;
}

std::expected<std::string, CdpError> CdpClient::EvaluateJs(std::string_view js, std::chrono::milliseconds timeout)
{
    if (!IsConnected())
        return std::unexpected(CdpError{"Not connected"});

    const std::uint64_t requestId = m_Impl->NextRequestId.fetch_add(1);

    const nlohmann::json request = {
        {"id", requestId},
        {"method", "Runtime.evaluate"},
        {"params", {{"expression", std::string(js)}, {"returnByValue", true}, {"awaitPromise", false}}},
    };

    {
        std::scoped_lock lock(m_Impl->PendingMutex);
        m_Impl->PendingRequestId = requestId;
        m_Impl->HasPending = true;
        m_Impl->PendingResponse.reset();
    }

    m_Impl->WebSocket.send(request.dump());

    std::unique_lock lock(m_Impl->PendingMutex);
    const bool signaled = m_Impl->PendingCv.wait_for(lock, timeout, [this] { return m_Impl->PendingResponse.has_value() || m_Impl->ConnectionLost; });
    m_Impl->HasPending = false;

    if (!signaled)
        return std::unexpected(CdpError{"Timed out waiting for CDP response"});

    if (m_Impl->ConnectionLost)
        return std::unexpected(CdpError{"CDP connection lost while waiting for response"});

    if (!m_Impl->PendingResponse)
        return std::unexpected(CdpError{"No CDP response received"});

    const nlohmann::json response = std::move(*m_Impl->PendingResponse);
    m_Impl->PendingResponse.reset();
    lock.unlock();

    if (response.contains("error"))
        return std::unexpected(CdpError{"CDP error: " + response["error"].dump()});

    if (!response.contains("result") || !response["result"].is_object())
        return std::unexpected(CdpError{"Malformed CDP response: missing result"});

    const nlohmann::json& evaluateResult = response["result"];

    if (evaluateResult.contains("exceptionDetails"))
        return std::unexpected(CdpError{"JS exception: " + evaluateResult["exceptionDetails"].dump()});

    if (!evaluateResult.contains("result"))
        return std::unexpected(CdpError{"Malformed CDP response: missing result.result"});

    const nlohmann::json& remoteObject = evaluateResult["result"];
    if (remoteObject.contains("value"))
        return remoteObject["value"].dump();

    return std::string("null");
}

#include "infra/ws/connection_registry.h"

#include <drogon/WebSocketConnection.h>

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chatserver::infra::ws {
namespace {

struct ConnectionEntry
{
    std::string userId;
    std::string deviceId;
    std::weak_ptr<drogon::WebSocketConnection> connection;
};

std::mutex g_registryMutex;
std::unordered_map<std::string, ConnectionEntry> g_connectionsByDeviceSessionId;
std::unordered_map<std::string, std::unordered_set<std::string>>
    g_deviceSessionsByUserId;

void eraseDeviceSessionNoLock(const std::string &deviceSessionId,
                              const std::string &userId)
{
    g_connectionsByDeviceSessionId.erase(deviceSessionId);

    auto userIt = g_deviceSessionsByUserId.find(userId);
    if (userIt == g_deviceSessionsByUserId.end())
    {
        return;
    }

    userIt->second.erase(deviceSessionId);
    if (userIt->second.empty())
    {
        g_deviceSessionsByUserId.erase(userIt);
    }
}

}  // namespace

void ConnectionRegistry::registerConnection(ConnectionBinding binding)
{
    drogon::WebSocketConnectionPtr previousConnection;

    {
        std::lock_guard<std::mutex> lock(g_registryMutex);

        auto existingIt =
            g_connectionsByDeviceSessionId.find(binding.deviceSessionId);
        if (existingIt != g_connectionsByDeviceSessionId.end())
        {
            previousConnection = existingIt->second.connection.lock();
            if (existingIt->second.userId != binding.userId)
            {
                eraseDeviceSessionNoLock(binding.deviceSessionId,
                                         existingIt->second.userId);
            }
        }

        ConnectionEntry entry;
        entry.userId = binding.userId;
        entry.deviceId = binding.deviceId;
        entry.connection = binding.connection;
        g_connectionsByDeviceSessionId[binding.deviceSessionId] = std::move(entry);
        g_deviceSessionsByUserId[binding.userId].insert(binding.deviceSessionId);
    }

    if (previousConnection != nullptr &&
        previousConnection.get() != binding.connection.get() &&
        previousConnection->connected())
    {
        previousConnection->shutdown(drogon::CloseCode::kNormalClosure,
                                     "replaced by newer websocket connection");
    }
}

void ConnectionRegistry::unregisterConnection(
    const std::string &deviceSessionId,
    const drogon::WebSocketConnectionPtr &connection)
{
    std::lock_guard<std::mutex> lock(g_registryMutex);

    auto existingIt = g_connectionsByDeviceSessionId.find(deviceSessionId);
    if (existingIt == g_connectionsByDeviceSessionId.end())
    {
        return;
    }

    auto existingConnection = existingIt->second.connection.lock();
    if (existingConnection != nullptr && connection != nullptr &&
        existingConnection.get() != connection.get())
    {
        return;
    }

    eraseDeviceSessionNoLock(deviceSessionId, existingIt->second.userId);
}

drogon::WebSocketConnectionPtr
ConnectionRegistry::findConnectionByDeviceSessionId(
    const std::string &deviceSessionId)
{
    std::lock_guard<std::mutex> lock(g_registryMutex);

    auto entryIt = g_connectionsByDeviceSessionId.find(deviceSessionId);
    if (entryIt == g_connectionsByDeviceSessionId.end())
    {
        return nullptr;
    }

    auto connection = entryIt->second.connection.lock();
    if (connection == nullptr || !connection->connected())
    {
        eraseDeviceSessionNoLock(deviceSessionId, entryIt->second.userId);
        return nullptr;
    }

    return connection;
}

std::vector<drogon::WebSocketConnectionPtr>
ConnectionRegistry::findConnectionsByUserId(const std::string &userId)
{
    std::vector<drogon::WebSocketConnectionPtr> connections;
    std::vector<std::string> staleDeviceSessionIds;

    std::lock_guard<std::mutex> lock(g_registryMutex);

    auto userIt = g_deviceSessionsByUserId.find(userId);
    if (userIt == g_deviceSessionsByUserId.end())
    {
        return connections;
    }

    for (const auto &deviceSessionId : userIt->second)
    {
        auto entryIt = g_connectionsByDeviceSessionId.find(deviceSessionId);
        if (entryIt == g_connectionsByDeviceSessionId.end())
        {
            staleDeviceSessionIds.push_back(deviceSessionId);
            continue;
        }

        auto connection = entryIt->second.connection.lock();
        if (connection == nullptr || !connection->connected())
        {
            staleDeviceSessionIds.push_back(deviceSessionId);
            continue;
        }

        connections.push_back(std::move(connection));
    }

    for (const auto &deviceSessionId : staleDeviceSessionIds)
    {
        userIt->second.erase(deviceSessionId);
        g_connectionsByDeviceSessionId.erase(deviceSessionId);
    }

    if (userIt->second.empty())
    {
        g_deviceSessionsByUserId.erase(userIt);
    }

    return connections;
}

}  // namespace chatserver::infra::ws

#include "service/ws_session_service.h"

#include "infra/log/app_logger.h"
#include "infra/security/token_provider.h"
#include "infra/ws/connection_registry.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>

// WsSessionService 负责 WebSocket 连接的身份确认和会话绑定。
// 它处在“协议层”和“业务层”之间：
// - 连接对象来自 WebSocket controller
// - 身份校验依赖 access token + device_session
// - 认证成功后把连接注册到 ConnectionRegistry
namespace chatserver::service {
namespace {

constexpr auto kWsAuthLogTag = "ws.auth";

std::int64_t nowEpochSec()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

void WsSessionService::authenticateConnection(
    protocol::dto::ws::WsAuthRequest request,
    const drogon::WebSocketConnectionPtr &connection,
    AuthenticateSuccess &&onSuccess,
    AuthenticateFailure &&onFailure) const
{
    if (connection == nullptr)
    {
        onFailure(ServiceError{
            protocol::error::ErrorCode::kInternalError,
            "websocket connection is not available",
        });
        return;
    }

    infra::security::TokenProvider tokenProvider;
    infra::security::AccessTokenClaims claims;
    if (!tokenProvider.verifyAccessToken(request.accessToken, &claims))
    {
        CHATSERVER_LOG_WARN(kWsAuthLogTag)
            << "ws.auth 被拒绝，原因：访问令牌无效";
        onFailure(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    if (claims.expiresAtSec <= nowEpochSec())
    {
        CHATSERVER_LOG_WARN(kWsAuthLogTag)
            << "ws.auth 被拒绝，原因：访问令牌已过期";
        onFailure(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    if (claims.deviceSessionId != request.deviceSessionId)
    {
        CHATSERVER_LOG_WARN(kWsAuthLogTag)
            << "ws.auth 被拒绝，原因：device_session_id 与访问令牌不匹配";
        onFailure(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    auto sharedSuccess =
        std::make_shared<AuthenticateSuccess>(std::move(onSuccess));
    auto sharedFailure =
        std::make_shared<AuthenticateFailure>(std::move(onFailure));

    repository::FindActiveDeviceSessionParams params;
    params.userId = claims.userId;
    params.deviceSessionId = request.deviceSessionId;
    params.deviceId = request.deviceId;

    deviceSessionRepository_.findActiveSession(
        std::move(params),
        [connection, sharedSuccess, sharedFailure](
            std::optional<repository::ActiveDeviceSessionRecord> sessionRecord) mutable {
            if (!sessionRecord.has_value())
            {
                CHATSERVER_LOG_WARN(kWsAuthLogTag)
                    << "ws.auth 被拒绝，原因：设备会话不存在或已失效";
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kInvalidAccessToken,
                    "invalid access token",
                });
                return;
            }

            WsConnectionContext context;
            context.userId = sessionRecord->userId;
            context.deviceSessionId = sessionRecord->deviceSessionId;
            context.deviceId = sessionRecord->deviceId;

            connection->setContext(std::make_shared<WsConnectionContext>(context));
            infra::ws::ConnectionRegistry::registerConnection(
                infra::ws::ConnectionBinding{
                    context.userId,
                    context.deviceSessionId,
                    context.deviceId,
                    connection,
                });

            (*sharedSuccess)(std::move(context));
        },
        [sharedFailure](std::string errorMessage) mutable {
            CHATSERVER_LOG_ERROR(kWsAuthLogTag)
                << "ws.auth 失败，查询激活设备会话时发生错误："
                << errorMessage;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to validate device session",
            });
        });
}

void WsSessionService::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr &connection) const
{
    if (connection == nullptr || !connection->hasContext())
    {
        return;
    }

    const auto context = connection->getContext<WsConnectionContext>();
    if (!context)
    {
        return;
    }

    infra::ws::ConnectionRegistry::unregisterConnection(context->deviceSessionId,
                                                        connection);
    connection->clearContext();
}

bool WsSessionService::isAuthenticated(
    const drogon::WebSocketConnectionPtr &connection) const
{
    return connection != nullptr && connection->hasContext() &&
           connection->getContext<WsConnectionContext>() != nullptr;
}

std::optional<WsConnectionContext> WsSessionService::connectionContext(
    const drogon::WebSocketConnectionPtr &connection) const
{
    if (!isAuthenticated(connection))
    {
        return std::nullopt;
    }

    return *connection->getContext<WsConnectionContext>();
}

}  // namespace chatserver::service

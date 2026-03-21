#include "service/friend_service.h"

#include "infra/id/id_generator.h"
#include "infra/log/app_logger.h"
#include "infra/security/token_provider.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <string_view>
#include <utility>

// FriendService 处理好友域业务：
// - 发好友申请
// - 同意 / 拒绝
// - 查询收件箱 / 发件箱 / 正式好友列表
//
// 同时它也是好友 HTTP 成功后触发 WS 实时通知的入口。
namespace chatserver::service {
namespace {

constexpr auto kFriendRequestLogTag = "friend.request";

std::string trimCopy(const std::string_view input)
{
    std::size_t begin = 0;
    std::size_t end = input.size();

    while (begin < end &&
           std::isspace(static_cast<unsigned char>(input[begin])) != 0)
    {
        ++begin;
    }

    while (end > begin &&
           std::isspace(static_cast<unsigned char>(input[end - 1])) != 0)
    {
        --end;
    }

    return std::string(input.substr(begin, end - begin));
}

protocol::dto::friendship::FriendPeerUserView toPeerUserView(
    const repository::UserProfileRecord &user)
{
    protocol::dto::friendship::FriendPeerUserView peer;
    peer.userId = user.userId;
    peer.account = user.account;
    peer.nickname = user.nickname;
    peer.avatarUrl = user.avatarUrl;
    return peer;
}

protocol::dto::friendship::FriendRequestItemView toFriendRequestItemView(
    const repository::FriendRequestListItemRecord &record)
{
    protocol::dto::friendship::FriendRequestItemView item;
    item.requestId = record.request.requestId;
    item.peerUser.userId = record.peerUser.userId;
    item.peerUser.account = record.peerUser.account;
    item.peerUser.nickname = record.peerUser.nickname;
    item.peerUser.avatarUrl = record.peerUser.avatarUrl;
    item.requestMessage = record.request.requestMessage;
    item.status = record.request.status;
    item.createdAtMs = record.request.createdAtMs;
    item.handledAtMs = record.request.handledAtMs;
    return item;
}

protocol::dto::friendship::FriendRequestItemView toFriendRequestItemView(
    const repository::FriendRequestRecord &request,
    const repository::UserProfileRecord &peerUser)
{
    protocol::dto::friendship::FriendRequestItemView item;
    item.requestId = request.requestId;
    item.peerUser = toPeerUserView(peerUser);
    item.requestMessage = request.requestMessage;
    item.status = request.status;
    item.createdAtMs = request.createdAtMs;
    item.handledAtMs = request.handledAtMs;
    return item;
}

protocol::dto::friendship::FriendListItemView toFriendListItemView(
    const repository::FriendListItemRecord &record)
{
    protocol::dto::friendship::FriendListItemView item;
    item.user.userId = record.user.userId;
    item.user.account = record.user.account;
    item.user.nickname = record.user.nickname;
    item.user.avatarUrl = record.user.avatarUrl;
    item.createdAtMs = record.createdAtMs;
    return item;
}

bool isUserIdCharacterAllowed(const char ch)
{
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

bool resolveCurrentUserClaims(const std::string &accessToken,
                              infra::security::AccessTokenClaims &claims)
{
    infra::security::TokenProvider tokenProvider;
    return tokenProvider.verifyAccessToken(accessToken, &claims);
}

std::int64_t currentEpochMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

void FriendService::sendFriendRequest(
    protocol::dto::friendship::SendFriendRequest request,
    std::string accessToken,
    SearchResultSuccess &&onSuccess,
    Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<SearchResultSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    // 第一阶段：同步校验输入和登录态。
    // 这里先挡住空 target_user_id、非法 request_message 以及无效 token，
    // 避免把明显错误带进后面的异步数据库链路。
    ServiceError validationError;
    if (!validateSendFriendRequest(request, validationError))
    {
        (*sharedFailure)(std::move(validationError));
        return;
    }

    accessToken = trimCopy(accessToken);
    infra::security::AccessTokenClaims claims;
    if (accessToken.empty() || !resolveCurrentUserClaims(accessToken, claims))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    if (request.targetUserId == claims.userId)
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "cannot add yourself as friend",
        });
        return;
    }

    const std::string targetUserId = request.targetUserId;
    // 第二阶段：异步串起“查目标用户 -> 查是否已是好友 -> 创建申请”。
    // 这样成功时就已经拿到了足够完整的 peer 用户资料，可以直接拼 FriendRequestItemView。
    userRepository_.findUserById(
        targetUserId,
        [request = std::move(request),
         claims,
         sharedSuccess,
         sharedFailure,
         this](std::optional<repository::UserProfileRecord> targetUser) mutable {
            if (!targetUser.has_value())
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "target user not found",
                });
                return;
            }

            friendRepository_.hasFriendship(
                claims.userId,
                targetUser->userId,
                [request = std::move(request),
                 claims,
                 targetUser = std::move(targetUser),
                 sharedSuccess,
                 sharedFailure,
                 this](bool exists) mutable {
                    if (exists)
                    {
                        (*sharedFailure)(ServiceError{
                            protocol::error::ErrorCode::kFriendAlreadyExists,
                            "friend already exists",
                        });
                        return;
                    }

                    infra::id::IdGenerator idGenerator;
                    repository::CreateFriendRequestParams params;
                    params.requestId = idGenerator.nextFriendRequestId();
                    params.requesterId = claims.userId;
                    params.targetId = targetUser->userId;
                    params.requestMessage = request.requestMessage;

                    // 申请创建成功后，HTTP 响应和 WS 推送复用同一份 item 视图。
                    // 这样客户端无论是通过接口返回还是实时事件接收，字段口径都一致。
                    friendRepository_.createFriendRequest(
                        std::move(params),
                        [targetUser = std::move(targetUser),
                         sharedSuccess,
                         this](
                            repository::FriendRequestRecord record) mutable {
                            auto item =
                                toFriendRequestItemView(record, *targetUser);
                            CHATSERVER_LOG_INFO(kFriendRequestLogTag)
                                << "好友申请创建成功，request_id="
                                << record.requestId;
                            (*sharedSuccess)(item);

                            // 目标用户在线时再做最佳努力推送；不在线不影响申请创建本身。
                            Json::Value payload(Json::objectValue);
                            payload["request"] =
                                protocol::dto::friendship::toJson(item);
                            realtimePushService_.pushNewToUser(
                                targetUser->userId,
                                "friend.request.new",
                                std::move(payload));
                        },
                        [sharedFailure](
                            repository::CreateFriendRequestError error) mutable {
                            if (error.kind ==
                                repository::CreateFriendRequestErrorKind::
                                    kPendingRequestAlreadyExists)
                            {
                                (*sharedFailure)(ServiceError{
                                    protocol::error::ErrorCode::
                                        kFriendRequestAlreadyPending,
                                    "friend request already pending",
                                });
                                return;
                            }

                            (*sharedFailure)(ServiceError{
                                protocol::error::ErrorCode::kInternalError,
                                "failed to create friend request",
                            });
                        });
                },
                [sharedFailure](std::string message) mutable {
                    CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                        << "查询好友关系失败：" << message;
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kInternalError,
                        "failed to query friendship",
                    });
                });
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                << "查询目标用户失败：" << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query target user",
            });
        });
}

void FriendService::acceptFriendRequest(std::string requestId,
                                        std::string accessToken,
                                        SearchResultSuccess &&onSuccess,
                                        Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<SearchResultSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    requestId = trimCopy(requestId);
    accessToken = trimCopy(accessToken);
    if (requestId.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "request_id is required",
        });
        return;
    }

    infra::security::AccessTokenClaims claims;
    if (accessToken.empty() || !resolveCurrentUserClaims(accessToken, claims))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    const std::string lookupRequestId = requestId;
    // 同意申请的关键顺序是：
    // 1. 先查申请是否存在、是否属于当前用户、是否仍是 pending；
    // 2. 再查申请发起人的展示资料；
    // 3. 最后执行数据库里的 accept 操作。
    //
    // 这样成功后能立即返回一条完整的 FriendRequestItemView，并顺手给发起人做 WS 通知。
    friendRepository_.findFriendRequestById(
        lookupRequestId,
        [claims,
         sharedSuccess,
         sharedFailure,
         this](std::optional<repository::FriendRequestRecord> request) mutable {
            if (!request.has_value())
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "friend request not found",
                });
                return;
            }

            if (request->targetId != claims.userId)
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kForbidden,
                    "forbidden to handle this friend request",
                });
                return;
            }

            if (request->status != "pending")
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kFriendRequestAlreadyHandled,
                    "friend request already handled",
                });
                return;
            }

            const std::string requesterId = request->requesterId;
            userRepository_.findUserById(
                requesterId,
                [request = std::move(request),
                 claims,
                 sharedSuccess,
                 sharedFailure,
                 this](std::optional<repository::UserProfileRecord> peerUser) mutable {
                    if (!peerUser.has_value())
                    {
                        (*sharedFailure)(ServiceError{
                            protocol::error::ErrorCode::kNotFound,
                            "requester user not found",
                        });
                        return;
                    }

                    repository::HandleFriendRequestParams params;
                    params.requestId = request->requestId;
                    params.targetUserId = claims.userId;

                    friendRepository_.acceptFriendRequest(
                        std::move(params),
                        [request = std::move(request),
                         peerUser = std::move(peerUser),
                         sharedSuccess,
                         sharedFailure,
                         this](
                            repository::UpdateFriendRequestResult result) mutable {
                            if (result.status ==
                                repository::UpdateFriendRequestStatus::
                                    kRequestNotPending)
                            {
                                (*sharedFailure)(ServiceError{
                                    protocol::error::ErrorCode::
                                        kFriendRequestAlreadyHandled,
                                    "friend request already handled",
                                });
                                return;
                            }

                            request->status = "accepted";
                            request->handledAtMs = currentEpochMs();
                            auto item =
                                toFriendRequestItemView(*request, *peerUser);
                            (*sharedSuccess)(item);

                            // 申请通过后，发起人如果在线，会立刻收到状态更新。
                            Json::Value payload(Json::objectValue);
                            payload["request"] =
                                protocol::dto::friendship::toJson(item);
                            realtimePushService_.pushNewToUser(
                                request->requesterId,
                                "friend.request.accepted",
                                std::move(payload));
                        },
                        [sharedFailure](std::string message) mutable {
                            CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                                << "同意好友申请失败：" << message;
                            (*sharedFailure)(ServiceError{
                                protocol::error::ErrorCode::kInternalError,
                                "failed to accept friend request",
                            });
                        });
                },
                [sharedFailure](std::string message) mutable {
                    CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                        << "查询申请发起人失败：" << message;
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kInternalError,
                        "failed to query requester user",
                    });
                });
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                << "查询好友申请失败：" << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query friend request",
            });
        });
}

void FriendService::rejectFriendRequest(std::string requestId,
                                        std::string accessToken,
                                        SearchResultSuccess &&onSuccess,
                                        Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<SearchResultSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    requestId = trimCopy(requestId);
    accessToken = trimCopy(accessToken);
    if (requestId.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "request_id is required",
        });
        return;
    }

    infra::security::AccessTokenClaims claims;
    if (accessToken.empty() || !resolveCurrentUserClaims(accessToken, claims))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    const std::string lookupRequestId = requestId;
    // 拒绝申请的结构与 accept 基本一致，差别只在最终数据库动作和状态值。
    // 保持这两条链路形状相同，后续阅读和维护会更稳定。
    friendRepository_.findFriendRequestById(
        lookupRequestId,
        [claims,
         sharedSuccess,
         sharedFailure,
         this](std::optional<repository::FriendRequestRecord> request) mutable {
            if (!request.has_value())
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "friend request not found",
                });
                return;
            }

            if (request->targetId != claims.userId)
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kForbidden,
                    "forbidden to handle this friend request",
                });
                return;
            }

            if (request->status != "pending")
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kFriendRequestAlreadyHandled,
                    "friend request already handled",
                });
                return;
            }

            const std::string requesterId = request->requesterId;
            userRepository_.findUserById(
                requesterId,
                [request = std::move(request),
                 claims,
                 sharedSuccess,
                 sharedFailure,
                 this](std::optional<repository::UserProfileRecord> peerUser) mutable {
                    if (!peerUser.has_value())
                    {
                        (*sharedFailure)(ServiceError{
                            protocol::error::ErrorCode::kNotFound,
                            "requester user not found",
                        });
                        return;
                    }

                    repository::HandleFriendRequestParams params;
                    params.requestId = request->requestId;
                    params.targetUserId = claims.userId;

                    friendRepository_.rejectFriendRequest(
                        std::move(params),
                        [request = std::move(request),
                         peerUser = std::move(peerUser),
                         sharedSuccess,
                         sharedFailure,
                         this](
                            repository::UpdateFriendRequestResult result) mutable {
                            if (result.status ==
                                repository::UpdateFriendRequestStatus::
                                    kRequestNotPending)
                            {
                                (*sharedFailure)(ServiceError{
                                    protocol::error::ErrorCode::
                                        kFriendRequestAlreadyHandled,
                                    "friend request already handled",
                                });
                                return;
                            }

                            request->status = "rejected";
                            request->handledAtMs = currentEpochMs();
                            auto item =
                                toFriendRequestItemView(*request, *peerUser);
                            (*sharedSuccess)(item);

                            // 被拒绝也走实时通知，方便发起人端的申请列表立即更新。
                            Json::Value payload(Json::objectValue);
                            payload["request"] =
                                protocol::dto::friendship::toJson(item);
                            realtimePushService_.pushNewToUser(
                                request->requesterId,
                                "friend.request.rejected",
                                std::move(payload));
                        },
                        [sharedFailure](std::string message) mutable {
                            CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                                << "拒绝好友申请失败：" << message;
                            (*sharedFailure)(ServiceError{
                                protocol::error::ErrorCode::kInternalError,
                                "failed to reject friend request",
                            });
                        });
                },
                [sharedFailure](std::string message) mutable {
                    CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                        << "查询申请发起人失败：" << message;
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kInternalError,
                        "failed to query requester user",
                    });
                });
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                << "查询好友申请失败：" << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query friend request",
            });
        });
}

void FriendService::listIncomingFriendRequests(std::string accessToken,
                                               ListRequestsSuccess &&onSuccess,
                                               Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<ListRequestsSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    accessToken = trimCopy(accessToken);
    infra::security::AccessTokenClaims claims;
    if (accessToken.empty() || !resolveCurrentUserClaims(accessToken, claims))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    friendRepository_.listIncomingFriendRequests(
        claims.userId,
        [sharedSuccess](std::vector<repository::FriendRequestListItemRecord> rows) mutable {
            // 列表查询服务层主要做“仓储记录 -> 对外视图”的映射，保持 controller 简单。
            std::vector<protocol::dto::friendship::FriendRequestItemView> items;
            items.reserve(rows.size());
            for (const auto &row : rows)
            {
                items.push_back(toFriendRequestItemView(row));
            }
            (*sharedSuccess)(std::move(items));
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                << "查询收到的好友申请失败：" << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to list incoming friend requests",
            });
        });
}

void FriendService::listOutgoingFriendRequests(std::string accessToken,
                                               ListRequestsSuccess &&onSuccess,
                                               Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<ListRequestsSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    accessToken = trimCopy(accessToken);
    infra::security::AccessTokenClaims claims;
    if (accessToken.empty() || !resolveCurrentUserClaims(accessToken, claims))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    friendRepository_.listOutgoingFriendRequests(
        claims.userId,
        [sharedSuccess](std::vector<repository::FriendRequestListItemRecord> rows) mutable {
            // 发件箱和收件箱都复用同一份 FriendRequestItemView，
            // 这样客户端列表和实时推送的数据形状可以保持一致。
            std::vector<protocol::dto::friendship::FriendRequestItemView> items;
            items.reserve(rows.size());
            for (const auto &row : rows)
            {
                items.push_back(toFriendRequestItemView(row));
            }
            (*sharedSuccess)(std::move(items));
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                << "查询已发送的好友申请失败：" << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to list outgoing friend requests",
            });
        });
}

void FriendService::listFriends(std::string accessToken,
                                ListFriendsSuccess &&onSuccess,
                                Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<ListFriendsSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    accessToken = trimCopy(accessToken);
    infra::security::AccessTokenClaims claims;
    if (accessToken.empty() || !resolveCurrentUserClaims(accessToken, claims))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return;
    }

    friendRepository_.listFriends(
        claims.userId,
        [sharedSuccess](std::vector<repository::FriendListItemRecord> rows) mutable {
            // 好友列表返回的是稳定关系视图，不混入好友申请状态。
            std::vector<protocol::dto::friendship::FriendListItemView> items;
            items.reserve(rows.size());
            for (const auto &row : rows)
            {
                items.push_back(toFriendListItemView(row));
            }
            (*sharedSuccess)(std::move(items));
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kFriendRequestLogTag)
                << "查询好友列表失败：" << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to list friends",
            });
        });
}

bool FriendService::validateSendFriendRequest(
    protocol::dto::friendship::SendFriendRequest &request,
    ServiceError &error) const
{
    request.targetUserId = trimCopy(request.targetUserId);
    if (request.targetUserId.empty() || request.targetUserId.size() > 32)
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "target_user_id is invalid";
        return false;
    }

    if (!std::all_of(request.targetUserId.begin(),
                     request.targetUserId.end(),
                     isUserIdCharacterAllowed))
    {
        error.code = protocol::error::ErrorCode::kInvalidArgument;
        error.message = "target_user_id is invalid";
        return false;
    }

    if (request.requestMessage.has_value())
    {
        request.requestMessage = trimCopy(*request.requestMessage);
        if (request.requestMessage->empty())
        {
            request.requestMessage.reset();
        }
        else if (request.requestMessage->size() > 200)
        {
            error.code = protocol::error::ErrorCode::kInvalidArgument;
            error.message = "request_message length must not exceed 200";
            return false;
        }
    }

    return true;
}

}  // namespace chatserver::service

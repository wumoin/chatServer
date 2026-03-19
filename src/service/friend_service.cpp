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

                    friendRepository_.createFriendRequest(
                        std::move(params),
                        [targetUser = std::move(targetUser),
                         sharedSuccess](
                            repository::FriendRequestRecord record) mutable {
                            CHATSERVER_LOG_INFO(kFriendRequestLogTag)
                                << "好友申请创建成功，request_id="
                                << record.requestId;
                            (*sharedSuccess)(
                                toFriendRequestItemView(record, *targetUser));
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
                         sharedFailure](
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
                            (*sharedSuccess)(
                                toFriendRequestItemView(*request, *peerUser));
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
                         sharedFailure](
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
                            (*sharedSuccess)(
                                toFriendRequestItemView(*request, *peerUser));
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

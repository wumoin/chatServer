#include "service/conversation_service.h"

#include "infra/id/id_generator.h"
#include "infra/log/app_logger.h"
#include "infra/security/token_provider.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string_view>
#include <utility>

namespace chatserver::service {
namespace {

constexpr auto kConversationLogTag = "conversation";

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

bool isUserIdCharacterAllowed(const char ch)
{
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

std::string buildDirectPairKey(const std::string &leftUserId,
                               const std::string &rightUserId)
{
    if (leftUserId < rightUserId)
    {
        return leftUserId + ":" + rightUserId;
    }
    return rightUserId + ":" + leftUserId;
}

}  // namespace

void ConversationService::createOrFindPrivateConversation(
    protocol::dto::conversation::CreatePrivateConversationRequest request,
    std::string accessToken,
    ConversationItemSuccess &&onSuccess,
    Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<ConversationItemSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    request.peerUserId = trimCopy(request.peerUserId);
    if (request.peerUserId.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "peer_user_id is required",
        });
        return;
    }

    if (!std::all_of(request.peerUserId.begin(),
                     request.peerUserId.end(),
                     isUserIdCharacterAllowed))
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "peer_user_id contains invalid characters",
        });
        return;
    }

    infra::security::AccessTokenClaims claims;
    if (!resolveCurrentUserClaims(accessToken, claims, *sharedFailure))
    {
        return;
    }

    if (request.peerUserId == claims.userId)
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "cannot create conversation with yourself",
        });
        return;
    }

    const std::string peerUserId = request.peerUserId;
    userRepository_.findUserById(
        peerUserId,
        [claims, peerUserId, sharedSuccess, sharedFailure, this](
            std::optional<repository::UserProfileRecord> peerUser) mutable {
            if (!peerUser.has_value())
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "peer user not found",
                });
                return;
            }

            friendRepository_.hasFriendship(
                claims.userId,
                peerUserId,
                [claims, peerUserId, sharedSuccess, sharedFailure, this](
                    bool areFriends) mutable {
                    if (!areFriends)
                    {
                        (*sharedFailure)(ServiceError{
                            protocol::error::ErrorCode::kForbidden,
                            "peer user is not your friend",
                        });
                        return;
                    }

                    infra::id::IdGenerator idGenerator;
                    repository::CreateOrFindDirectConversationParams params;
                    params.conversationId =
                        idGenerator.nextConversationId();
                    params.currentUserId = claims.userId;
                    params.peerUserId = peerUserId;
                    params.directPairKey =
                        buildDirectPairKey(claims.userId, peerUserId);

                    conversationRepository_.createOrFindDirectConversation(
                        std::move(params),
                        [claims, sharedSuccess, sharedFailure, this](
                            repository::CreateOrFindDirectConversationResult result) mutable {
                            conversationRepository_.findConversationItem(
                                claims.userId,
                                result.conversationId,
                                [sharedSuccess, sharedFailure, this](
                                    std::optional<repository::ConversationListItemRecord> item) mutable {
                                    if (!item.has_value())
                                    {
                                        (*sharedFailure)(ServiceError{
                                            protocol::error::ErrorCode::kInternalError,
                                            "failed to load created conversation",
                                        });
                                        return;
                                    }
                                    (*sharedSuccess)(toConversationView(*item));
                                },
                                [sharedFailure](std::string message) mutable {
                                    CHATSERVER_LOG_ERROR(kConversationLogTag)
                                        << "failed to load direct conversation: "
                                        << message;
                                    (*sharedFailure)(ServiceError{
                                        protocol::error::ErrorCode::kInternalError,
                                        "failed to load direct conversation",
                                    });
                                });
                        },
                        [sharedFailure](std::string message) mutable {
                            CHATSERVER_LOG_ERROR(kConversationLogTag)
                                << "failed to create or find direct conversation: "
                                << message;
                            (*sharedFailure)(ServiceError{
                                protocol::error::ErrorCode::kInternalError,
                                "failed to create or find conversation",
                            });
                        });
                },
                [sharedFailure](std::string message) mutable {
                    CHATSERVER_LOG_ERROR(kConversationLogTag)
                        << "failed to query friendship for direct conversation: "
                        << message;
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kInternalError,
                        "failed to query friendship",
                    });
                });
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kConversationLogTag)
                << "failed to query peer user: " << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query peer user",
            });
        });
}

void ConversationService::listConversations(
    std::string accessToken,
    ListConversationsSuccess &&onSuccess,
    Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<ListConversationsSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    infra::security::AccessTokenClaims claims;
    if (!resolveCurrentUserClaims(accessToken, claims, *sharedFailure))
    {
        return;
    }

    conversationRepository_.listConversations(
        claims.userId,
        [sharedSuccess, this](
            std::vector<repository::ConversationListItemRecord> items) mutable {
            std::vector<protocol::dto::conversation::ConversationListItemView> views;
            views.reserve(items.size());
            for (const auto &item : items)
            {
                views.push_back(toConversationView(item));
            }
            (*sharedSuccess)(std::move(views));
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kConversationLogTag)
                << "failed to list conversations: " << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to list conversations",
            });
        });
}

void ConversationService::getConversationDetail(
    std::string conversationId,
    std::string accessToken,
    ConversationDetailSuccess &&onSuccess,
    Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<ConversationDetailSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    conversationId = trimCopy(conversationId);
    if (conversationId.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "conversation_id is required",
        });
        return;
    }

    infra::security::AccessTokenClaims claims;
    if (!resolveCurrentUserClaims(accessToken, claims, *sharedFailure))
    {
        return;
    }

    conversationRepository_.findConversationDetail(
        claims.userId,
        conversationId,
        [sharedSuccess, sharedFailure, this](
            std::optional<repository::ConversationDetailRecord> item) mutable {
            if (!item.has_value())
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "conversation not found",
                });
                return;
            }

            (*sharedSuccess)(toConversationDetailView(*item));
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kConversationLogTag)
                << "failed to query conversation detail: " << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query conversation detail",
            });
        });
}

void ConversationService::listMessages(
    std::string conversationId,
    protocol::dto::conversation::ListConversationMessagesRequest request,
    std::string accessToken,
    ListMessagesSuccess &&onSuccess,
    Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<ListMessagesSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    conversationId = trimCopy(conversationId);
    if (conversationId.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "conversation_id is required",
        });
        return;
    }

    if (request.beforeSeq.has_value() && request.afterSeq.has_value())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "before_seq and after_seq cannot be used together",
        });
        return;
    }

    if (request.limit == 0 || request.limit > 100)
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "limit must be between 1 and 100",
        });
        return;
    }

    infra::security::AccessTokenClaims claims;
    if (!resolveCurrentUserClaims(accessToken, claims, *sharedFailure))
    {
        return;
    }

    conversationRepository_.findConversationItem(
        claims.userId,
        conversationId,
        [conversationId,
         request,
         sharedSuccess,
         sharedFailure,
         this](std::optional<repository::ConversationListItemRecord> item) mutable {
            if (!item.has_value())
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "conversation not found",
                });
                return;
            }

            repository::ListMessagesParams params;
            params.conversationId = conversationId;
            params.limit = request.limit;
            params.beforeSeq = request.beforeSeq;
            params.afterSeq = request.afterSeq;

            conversationRepository_.listMessages(
                std::move(params),
                [sharedSuccess, this](
                    repository::ListMessagesResult result) mutable {
                    protocol::dto::conversation::ListConversationMessagesResult view;
                    view.hasMore = result.hasMore;
                    view.nextBeforeSeq = result.nextBeforeSeq;
                    view.nextAfterSeq = result.nextAfterSeq;
                    view.items.reserve(result.items.size());
                    for (const auto &item : result.items)
                    {
                        view.items.push_back(toMessageView(item));
                    }
                    (*sharedSuccess)(std::move(view));
                },
                [sharedFailure](std::string message) mutable {
                    CHATSERVER_LOG_ERROR(kConversationLogTag)
                        << "failed to list messages: " << message;
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kInternalError,
                        "failed to list messages",
                    });
                });
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kConversationLogTag)
                << "failed to query conversation membership: " << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query conversation membership",
            });
        });
}

void ConversationService::sendTextMessage(
    std::string conversationId,
    protocol::dto::conversation::SendTextMessageRequest request,
    std::string accessToken,
    MessageSuccess &&onSuccess,
    Failure &&onFailure) const
{
    auto sharedSuccess =
        std::make_shared<MessageSuccess>(std::move(onSuccess));
    auto sharedFailure = std::make_shared<Failure>(std::move(onFailure));

    conversationId = trimCopy(conversationId);
    request.text = trimCopy(request.text);
    if (conversationId.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "conversation_id is required",
        });
        return;
    }

    if (request.text.empty())
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "text must not be empty",
        });
        return;
    }

    if (request.text.size() > 4000)
    {
        (*sharedFailure)(ServiceError{
            protocol::error::ErrorCode::kInvalidArgument,
            "text is too long",
        });
        return;
    }

    if (request.clientMessageId.has_value())
    {
        *request.clientMessageId = trimCopy(*request.clientMessageId);
        if (request.clientMessageId->empty())
        {
            request.clientMessageId.reset();
        }
    }

    infra::security::AccessTokenClaims claims;
    if (!resolveCurrentUserClaims(accessToken, claims, *sharedFailure))
    {
        return;
    }

    conversationRepository_.findConversationItem(
        claims.userId,
        conversationId,
        [conversationId,
         request = std::move(request),
         claims,
         sharedSuccess,
         sharedFailure,
         this](std::optional<repository::ConversationListItemRecord> item) mutable {
            if (!item.has_value())
            {
                (*sharedFailure)(ServiceError{
                    protocol::error::ErrorCode::kNotFound,
                    "conversation not found",
                });
                return;
            }

            infra::id::IdGenerator idGenerator;
            repository::CreateTextMessageParams params;
            params.messageId = idGenerator.nextMessageId();
            params.conversationId = conversationId;
            params.senderId = claims.userId;
            params.clientMessageId = request.clientMessageId;
            params.text = request.text;

            conversationRepository_.createTextMessage(
                std::move(params),
                [sharedSuccess, this](
                    repository::ConversationMessageRecord item) mutable {
                    (*sharedSuccess)(toMessageView(item));
                },
                [sharedFailure](std::string message) mutable {
                    CHATSERVER_LOG_ERROR(kConversationLogTag)
                        << "failed to create text message: " << message;
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kInternalError,
                        "failed to create message",
                    });
                });
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kConversationLogTag)
                << "failed to query conversation before send message: "
                << message;
            (*sharedFailure)(ServiceError{
                protocol::error::ErrorCode::kInternalError,
                "failed to query conversation",
            });
        });
}

bool ConversationService::resolveCurrentUserClaims(
    const std::string &accessToken,
    infra::security::AccessTokenClaims &claims,
    const Failure &onFailure) const
{
    const std::string normalizedToken = trimCopy(accessToken);
    infra::security::TokenProvider tokenProvider;
    if (normalizedToken.empty() ||
        !tokenProvider.verifyAccessToken(normalizedToken, &claims))
    {
        onFailure(ServiceError{
            protocol::error::ErrorCode::kInvalidAccessToken,
            "invalid access token",
        });
        return false;
    }
    return true;
}

protocol::dto::conversation::ConversationListItemView
ConversationService::toConversationView(
    const repository::ConversationListItemRecord &item) const
{
    protocol::dto::conversation::ConversationListItemView view;
    view.conversationId = item.conversationId;
    view.conversationType = item.conversationType;
    view.peerUser.userId = item.peerUser.userId;
    view.peerUser.account = item.peerUser.account;
    view.peerUser.nickname = item.peerUser.nickname;
    view.peerUser.avatarUrl = item.peerUser.avatarUrl;
    view.lastMessageSeq = item.lastMessageSeq;
    view.lastReadSeq = item.lastReadSeq;
    view.unreadCount = item.unreadCount;
    view.lastMessagePreview = item.lastMessagePreview;
    view.lastMessageAtMs = item.lastMessageAtMs;
    view.createdAtMs = item.createdAtMs;
    return view;
}

protocol::dto::conversation::ConversationDetailView
ConversationService::toConversationDetailView(
    const repository::ConversationDetailRecord &item) const
{
    protocol::dto::conversation::ConversationDetailView view;
    view.conversationId = item.conversationId;
    view.conversationType = item.conversationType;
    view.peerUser.userId = item.peerUser.userId;
    view.peerUser.account = item.peerUser.account;
    view.peerUser.nickname = item.peerUser.nickname;
    view.peerUser.avatarUrl = item.peerUser.avatarUrl;
    view.myMember.userId = item.myMember.userId;
    view.myMember.memberRole = item.myMember.memberRole;
    view.myMember.joinedAtMs = item.myMember.joinedAtMs;
    view.myMember.lastReadSeq = item.myMember.lastReadSeq;
    view.myMember.lastReadAtMs = item.myMember.lastReadAtMs;
    view.lastMessageSeq = item.lastMessageSeq;
    view.unreadCount = item.unreadCount;
    view.lastMessagePreview = item.lastMessagePreview;
    view.lastMessageAtMs = item.lastMessageAtMs;
    view.createdAtMs = item.createdAtMs;
    return view;
}

protocol::dto::conversation::ConversationMessageView
ConversationService::toMessageView(
    const repository::ConversationMessageRecord &item) const
{
    protocol::dto::conversation::ConversationMessageView view;
    view.messageId = item.messageId;
    view.conversationId = item.conversationId;
    view.seq = item.seq;
    view.senderId = item.senderId;
    view.clientMessageId = item.clientMessageId;
    view.messageType = item.messageType;
    view.content = item.contentJson;
    view.createdAtMs = item.createdAtMs;
    return view;
}

}  // namespace chatserver::service

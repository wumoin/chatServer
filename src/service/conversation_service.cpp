#include "service/conversation_service.h"

#include "infra/id/id_generator.h"
#include "infra/log/app_logger.h"
#include "infra/security/token_provider.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string_view>
#include <utility>

// ConversationService 是会话域的业务编排中心。
// 它连接了好友关系、会话创建、历史消息查询、文本消息 HTTP 发送以及
// 部分实时推送触发点。Repository 负责 SQL，而这里负责把业务规则串起来。
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

    // 第一阶段：做最小输入规范化和参数校验。
    // 这里先挡住空 peer_user_id、非法字符和“给自己建私聊”这类明显错误，
    // 后续异步链路就只处理真正可能成功的请求。
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
    // 第二阶段：异步串起“查对端用户 -> 校验好友关系 -> 创建或复用私聊 -> 回读会话详情”。
    // 这里故意在最终 createOrFind 成功后再回读一次会话项，而不是直接用 repository
    // 的 create 结果拼响应，这样 HTTP 返回和后续 ws.new 推送能共用同一份 ConversationView。
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

                    // 第三阶段：数据库里真正“创建或复用”私聊后，统一回读可展示视图。
                    // 这样无论是第一次创建还是命中已有私聊，后续行为都完全一致。
                    conversationRepository_.createOrFindDirectConversation(
                        std::move(params),
                        [claims, peerUserId, sharedSuccess, sharedFailure, this](
                            repository::CreateOrFindDirectConversationResult result) mutable {
                            // findConversationItem 返回的 peer_user 是相对第一个参数 user_id 的视角。
                            // 因而同一条 conversation，如果按 Alice 查和按 Bob 查，peer_user 会不同。
                            conversationRepository_.findConversationItem(
                                claims.userId,
                                result.conversationId,
                                [peerUserId, sharedSuccess, sharedFailure, this](
                                    std::optional<repository::ConversationListItemRecord> item) mutable {
                                    if (!item.has_value())
                                    {
                                        (*sharedFailure)(ServiceError{
                                            protocol::error::ErrorCode::kInternalError,
                                            "failed to load created conversation",
                                        });
                                        return;
                                    }
                                    auto view = toConversationView(*item);
                                    (*sharedSuccess)(view);

                                    // HTTP 主流程成功后，再做最佳努力实时通知。
                                    // 即使对端当前不在线，私聊创建本身也已经成立，不应该回滚。
                                    //
                                    // conversation list item 是“视角敏感 DTO”：
                                    // - 对 Alice 来说 peer_user 应该是 Bob；
                                    // - 对 Bob 来说 peer_user 应该是 Alice；
                                    // - unread / last_read 也同样依赖接收方身份。
                                    //
                                    // 因此这里不能直接把“创建者视角”的 view 原样推给对端，
                                    // 而是需要按接收方 user_id 再回读一遍会话摘要，然后再推送。
                                    conversationRepository_.findConversationItem(
                                        peerUserId,
                                        view.conversationId,
                                        [peerUserId, this](
                                            std::optional<repository::ConversationListItemRecord>
                                                recipientItem) mutable {
                                            if (!recipientItem.has_value())
                                            {
                                                CHATSERVER_LOG_WARN(
                                                    kConversationLogTag)
                                                    << "实时推送会话创建事件前，未能按接收方视角回读会话摘要，user_id="
                                                    << peerUserId;
                                                return;
                                            }

                                            Json::Value payload(
                                                Json::objectValue);
                                            payload["conversation"] =
                                                protocol::dto::conversation::
                                                    toJson(toConversationView(
                                                        *recipientItem));
                                            realtimePushService_.pushNewToUser(
                                                peerUserId,
                                                "conversation.created",
                                                std::move(payload));
                                        },
                                        [peerUserId](
                                            std::string message) mutable {
                                            CHATSERVER_LOG_WARN(
                                                kConversationLogTag)
                                                << "按接收方视角组装 conversation.created 失败，user_id="
                                                << peerUserId
                                                << " message="
                                                << message;
                                        });
                                },
                                [sharedFailure](std::string message) mutable {
                                    CHATSERVER_LOG_ERROR(kConversationLogTag)
                                        << "加载私聊会话详情失败："
                                        << message;
                                    (*sharedFailure)(ServiceError{
                                        protocol::error::ErrorCode::kInternalError,
                                        "failed to load direct conversation",
                                    });
                                });
                        },
                        [sharedFailure](std::string message) mutable {
                            CHATSERVER_LOG_ERROR(kConversationLogTag)
                                << "创建或复用私聊会话失败："
                                << message;
                            (*sharedFailure)(ServiceError{
                                protocol::error::ErrorCode::kInternalError,
                                "failed to create or find conversation",
                            });
                        });
                },
                [sharedFailure](std::string message) mutable {
                    CHATSERVER_LOG_ERROR(kConversationLogTag)
                        << "查询私聊好友关系失败："
                        << message;
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kInternalError,
                        "failed to query friendship",
                    });
                });
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kConversationLogTag)
                << "查询会话对端用户失败：" << message;
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
                << "查询会话列表失败：" << message;
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
                << "查询会话详情失败：" << message;
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

    // 查询历史消息前先校验“当前用户是否属于这个会话”。
    // 这样后面的 listMessages SQL 可以专注做分页，不需要再重复做权限判断。
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

            // 权限校验通过后，再真正执行历史消息分页查询。
            // before_seq / after_seq 的组合约束已经在函数前半段统一验证过。
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
                        << "查询历史消息失败：" << message;
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kInternalError,
                        "failed to list messages",
                    });
                });
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kConversationLogTag)
                << "查询会话成员关系失败：" << message;
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

    // 发送文本消息的 HTTP 路径仍然遵循与 WS 类似的校验顺序：
    // 1. 先确认当前用户是会话成员；
    // 2. 再创建消息；
    // 3. 最后把仓储记录映射成统一 MessageView 返回给客户端。
    //
    // 但它当前只负责“同步写入 + HTTP 响应”，不会像 WS 路径那样再广播
    // message.created；因此两条发送路径暂时还不是完全等价的实时语义。
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

            // 文本消息入库后，仓储会同时推进会话的 last_message_seq / last_message_at。
            // service 层只负责把最终记录转成对外 DTO。
            conversationRepository_.createTextMessage(
                std::move(params),
                [sharedSuccess, this](
                    repository::ConversationMessageRecord item) mutable {
                    (*sharedSuccess)(toMessageView(item));
                },
                [sharedFailure](std::string message) mutable {
                    CHATSERVER_LOG_ERROR(kConversationLogTag)
                        << "发送文本消息失败：" << message;
                    (*sharedFailure)(ServiceError{
                        protocol::error::ErrorCode::kInternalError,
                        "failed to create message",
                    });
                });
        },
        [sharedFailure](std::string message) mutable {
            CHATSERVER_LOG_ERROR(kConversationLogTag)
                << "发送消息前查询会话信息失败："
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
    // 这里当前只验证 access token 本身，不回查 active device_session。
    // 这让大多数 HTTP 入口保持轻量，但也意味着它们的失效语义比 WS 更宽松。
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

#pragma once

#include <json/value.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chatserver::protocol::dto::conversation {

/**
 * @brief 发起一对一私聊会话请求。
 */
struct CreatePrivateConversationRequest
{
    std::string peerUserId;
};

/**
 * @brief 查询历史消息时的游标参数。
 */
struct ListConversationMessagesRequest
{
    std::optional<std::int64_t> beforeSeq;
    std::optional<std::int64_t> afterSeq;
    std::size_t limit{50};
};

/**
 * @brief 发送文本消息请求。
 */
struct SendTextMessageRequest
{
    std::optional<std::string> clientMessageId;
    std::string text;
};

/**
 * @brief 会话中展示的对端用户信息。
 */
struct ConversationPeerUserView
{
    std::string userId;
    std::string account;
    std::string nickname;
    std::optional<std::string> avatarUrl;
};

/**
 * @brief 当前登录用户在会话中的成员状态视图。
 */
struct ConversationMemberView
{
    std::string userId;
    std::string memberRole;
    std::int64_t joinedAtMs{0};
    std::int64_t lastReadSeq{0};
    std::optional<std::int64_t> lastReadAtMs;
};

/**
 * @brief 会话列表项视图。
 */
struct ConversationListItemView
{
    std::string conversationId;
    std::string conversationType;
    ConversationPeerUserView peerUser;
    std::int64_t lastMessageSeq{0};
    std::int64_t lastReadSeq{0};
    std::int64_t unreadCount{0};
    std::optional<std::string> lastMessagePreview;
    std::optional<std::int64_t> lastMessageAtMs;
    std::int64_t createdAtMs{0};
};

/**
 * @brief 单条会话详情视图。
 */
struct ConversationDetailView
{
    std::string conversationId;
    std::string conversationType;
    ConversationPeerUserView peerUser;
    ConversationMemberView myMember;
    std::int64_t lastMessageSeq{0};
    std::int64_t unreadCount{0};
    std::optional<std::string> lastMessagePreview;
    std::optional<std::int64_t> lastMessageAtMs;
    std::int64_t createdAtMs{0};
};

/**
 * @brief 消息视图。
 */
struct ConversationMessageView
{
    std::string messageId;
    std::string conversationId;
    std::int64_t seq{0};
    std::string senderId;
    std::optional<std::string> clientMessageId;
    std::string messageType;
    Json::Value content{Json::objectValue};
    std::int64_t createdAtMs{0};
};

/**
 * @brief 历史消息分页结果。
 */
struct ListConversationMessagesResult
{
    std::vector<ConversationMessageView> items;
    bool hasMore{false};
    std::optional<std::int64_t> nextBeforeSeq;
    std::optional<std::int64_t> nextAfterSeq;
};

/**
 * @brief 解析发起私聊请求。
 * @param json 请求体 JSON。
 * @param out 解析成功后写入的 DTO。
 * @param errorMessage 解析失败时写入的错误消息。
 * @return true 表示解析成功；false 表示失败。
 */
inline bool parseCreatePrivateConversationRequest(const Json::Value &json,
                                                  CreatePrivateConversationRequest &out,
                                                  std::string &errorMessage)
{
    if (!json.isObject())
    {
        errorMessage = "Request body must be a JSON object";
        return false;
    }

    if (!json.isMember("peer_user_id") || !json["peer_user_id"].isString())
    {
        errorMessage = "Field 'peer_user_id' must be a string";
        return false;
    }

    out.peerUserId = json["peer_user_id"].asString();
    return true;
}

/**
 * @brief 解析发送文本消息请求。
 * @param json 请求体 JSON。
 * @param out 解析成功后写入的 DTO。
 * @param errorMessage 解析失败时写入的错误消息。
 * @return true 表示解析成功；false 表示失败。
 */
inline bool parseSendTextMessageRequest(const Json::Value &json,
                                        SendTextMessageRequest &out,
                                        std::string &errorMessage)
{
    if (!json.isObject())
    {
        errorMessage = "Request body must be a JSON object";
        return false;
    }

    if (!json.isMember("text") || !json["text"].isString())
    {
        errorMessage = "Field 'text' must be a string";
        return false;
    }
    out.text = json["text"].asString();

    if (json.isMember("client_message_id"))
    {
        if (!json["client_message_id"].isNull() &&
            !json["client_message_id"].isString())
        {
            errorMessage =
                "Field 'client_message_id' must be a string when provided";
            return false;
        }

        if (json["client_message_id"].isString())
        {
            out.clientMessageId = json["client_message_id"].asString();
        }
    }

    return true;
}

/**
 * @brief 把会话对端用户转换成 JSON。
 * @param user 会话对端用户视图。
 * @return JSON 对象。
 */
inline Json::Value toJson(const ConversationPeerUserView &user)
{
    Json::Value json(Json::objectValue);
    json["user_id"] = user.userId;
    json["account"] = user.account;
    json["nickname"] = user.nickname;
    if (user.avatarUrl.has_value())
    {
        json["avatar_url"] = *user.avatarUrl;
    }
    return json;
}

/**
 * @brief 把当前用户会话成员状态转换成 JSON。
 * @param member 当前用户会话成员状态视图。
 * @return JSON 对象。
 */
inline Json::Value toJson(const ConversationMemberView &member)
{
    Json::Value json(Json::objectValue);
    json["user_id"] = member.userId;
    json["member_role"] = member.memberRole;
    json["joined_at_ms"] = Json::Int64(member.joinedAtMs);
    json["last_read_seq"] = Json::Int64(member.lastReadSeq);
    if (member.lastReadAtMs.has_value())
    {
        json["last_read_at_ms"] = Json::Int64(*member.lastReadAtMs);
    }
    return json;
}

/**
 * @brief 把会话列表项转换成 JSON。
 * @param item 会话列表项视图。
 * @return JSON 对象。
 */
inline Json::Value toJson(const ConversationListItemView &item)
{
    Json::Value json(Json::objectValue);
    json["conversation_id"] = item.conversationId;
    json["conversation_type"] = item.conversationType;
    json["peer_user"] = toJson(item.peerUser);
    json["last_message_seq"] = Json::Int64(item.lastMessageSeq);
    json["last_read_seq"] = Json::Int64(item.lastReadSeq);
    json["unread_count"] = Json::Int64(item.unreadCount);
    if (item.lastMessagePreview.has_value())
    {
        json["last_message_preview"] = *item.lastMessagePreview;
    }
    if (item.lastMessageAtMs.has_value())
    {
        json["last_message_at_ms"] = Json::Int64(*item.lastMessageAtMs);
    }
    json["created_at_ms"] = Json::Int64(item.createdAtMs);
    return json;
}

/**
 * @brief 把会话详情转换成 JSON。
 * @param item 会话详情视图。
 * @return JSON 对象。
 */
inline Json::Value toJson(const ConversationDetailView &item)
{
    Json::Value json(Json::objectValue);
    json["conversation_id"] = item.conversationId;
    json["conversation_type"] = item.conversationType;
    json["peer_user"] = toJson(item.peerUser);
    json["my_member"] = toJson(item.myMember);
    json["last_message_seq"] = Json::Int64(item.lastMessageSeq);
    json["unread_count"] = Json::Int64(item.unreadCount);
    if (item.lastMessagePreview.has_value())
    {
        json["last_message_preview"] = *item.lastMessagePreview;
    }
    if (item.lastMessageAtMs.has_value())
    {
        json["last_message_at_ms"] = Json::Int64(*item.lastMessageAtMs);
    }
    json["created_at_ms"] = Json::Int64(item.createdAtMs);
    return json;
}

/**
 * @brief 把会话列表转换成 JSON 数组。
 * @param items 会话列表视图集合。
 * @return JSON 数组。
 */
inline Json::Value toJson(const std::vector<ConversationListItemView> &items)
{
    Json::Value json(Json::arrayValue);
    for (const auto &item : items)
    {
        json.append(toJson(item));
    }
    return json;
}

/**
 * @brief 把消息视图转换成 JSON。
 * @param item 消息视图。
 * @return JSON 对象。
 */
inline Json::Value toJson(const ConversationMessageView &item)
{
    Json::Value json(Json::objectValue);
    json["message_id"] = item.messageId;
    json["conversation_id"] = item.conversationId;
    json["seq"] = Json::Int64(item.seq);
    json["sender_id"] = item.senderId;
    if (item.clientMessageId.has_value())
    {
        json["client_message_id"] = *item.clientMessageId;
    }
    json["type"] = item.messageType;
    json["content"] = item.content;
    json["created_at_ms"] = Json::Int64(item.createdAtMs);
    return json;
}

/**
 * @brief 把消息列表转换成 JSON 数组。
 * @param items 消息视图集合。
 * @return JSON 数组。
 */
inline Json::Value toJson(const std::vector<ConversationMessageView> &items)
{
    Json::Value json(Json::arrayValue);
    for (const auto &item : items)
    {
        json.append(toJson(item));
    }
    return json;
}

/**
 * @brief 把历史消息分页结果转换成 JSON。
 * @param result 分页结果。
 * @return JSON 对象。
 */
inline Json::Value toJson(const ListConversationMessagesResult &result)
{
    Json::Value json(Json::objectValue);
    json["items"] = toJson(result.items);
    json["has_more"] = result.hasMore;
    if (result.nextBeforeSeq.has_value())
    {
        json["next_before_seq"] = Json::Int64(*result.nextBeforeSeq);
    }
    if (result.nextAfterSeq.has_value())
    {
        json["next_after_seq"] = Json::Int64(*result.nextAfterSeq);
    }
    return json;
}

}  // namespace chatserver::protocol::dto::conversation

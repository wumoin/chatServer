#pragma once

#include <json/value.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chatserver::protocol::dto::friendship {

/**
 * @brief 发送好友申请请求。
 */
struct SendFriendRequest
{
    std::string targetUserId;
    std::optional<std::string> requestMessage;
};

/**
 * @brief 好友申请中涉及的对端用户视图。
 */
struct FriendPeerUserView
{
    std::string userId;
    std::string account;
    std::string nickname;
    std::optional<std::string> avatarUrl;
};

/**
 * @brief 好友申请列表项视图。
 */
struct FriendRequestItemView
{
    std::string requestId;
    FriendPeerUserView peerUser;
    std::optional<std::string> requestMessage;
    std::string status;
    std::int64_t createdAtMs{0};
    std::optional<std::int64_t> handledAtMs;
};

/**
 * @brief 好友列表项视图。
 */
struct FriendListItemView
{
    FriendPeerUserView user;
    std::int64_t createdAtMs{0};
};

/**
 * @brief 解析发送好友申请请求。
 * @param json 请求体 JSON。
 * @param out 解析成功后写入的请求 DTO。
 * @param errorMessage 解析失败时写入的错误消息。
 * @return true 表示解析成功；false 表示结构不合法。
 */
inline bool parseSendFriendRequest(const Json::Value &json,
                                   SendFriendRequest &out,
                                   std::string &errorMessage)
{
    if (!json.isObject())
    {
        errorMessage = "Request body must be a JSON object";
        return false;
    }

    if (!json.isMember("target_user_id") || !json["target_user_id"].isString())
    {
        errorMessage = "Field 'target_user_id' must be a string";
        return false;
    }
    out.targetUserId = json["target_user_id"].asString();

    if (json.isMember("request_message"))
    {
        if (!json["request_message"].isNull() &&
            !json["request_message"].isString())
        {
            errorMessage = "Field 'request_message' must be a string when provided";
            return false;
        }

        if (json["request_message"].isString())
        {
            out.requestMessage = json["request_message"].asString();
        }
    }

    return true;
}

/**
 * @brief 把对端用户视图转换成 JSON。
 * @param user 对端用户视图。
 * @return JSON 对象。
 */
inline Json::Value toJson(const FriendPeerUserView &user)
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
 * @brief 把好友申请列表项转换成 JSON。
 * @param item 好友申请列表项视图。
 * @return JSON 对象。
 */
inline Json::Value toJson(const FriendRequestItemView &item)
{
    Json::Value json(Json::objectValue);
    json["request_id"] = item.requestId;
    json["peer_user"] = toJson(item.peerUser);
    if (item.requestMessage.has_value())
    {
        json["request_message"] = *item.requestMessage;
    }
    json["status"] = item.status;
    json["created_at_ms"] = Json::Int64(item.createdAtMs);
    if (item.handledAtMs.has_value())
    {
        json["handled_at_ms"] = Json::Int64(*item.handledAtMs);
    }
    return json;
}

/**
 * @brief 把好友申请列表转换成 JSON 数组。
 * @param items 好友申请列表。
 * @return JSON 数组。
 */
inline Json::Value toJson(const std::vector<FriendRequestItemView> &items)
{
    Json::Value json(Json::arrayValue);
    for (const auto &item : items)
    {
        json.append(toJson(item));
    }
    return json;
}

/**
 * @brief 把好友列表项转换成 JSON。
 * @param item 好友列表项视图。
 * @return JSON 对象。
 */
inline Json::Value toJson(const FriendListItemView &item)
{
    Json::Value json(Json::objectValue);
    json["user"] = toJson(item.user);
    json["created_at_ms"] = Json::Int64(item.createdAtMs);
    return json;
}

/**
 * @brief 把好友列表转换成 JSON 数组。
 * @param items 好友列表。
 * @return JSON 数组。
 */
inline Json::Value toJson(const std::vector<FriendListItemView> &items)
{
    Json::Value json(Json::arrayValue);
    for (const auto &item : items)
    {
        json.append(toJson(item));
    }
    return json;
}

}  // namespace chatserver::protocol::dto::friendship

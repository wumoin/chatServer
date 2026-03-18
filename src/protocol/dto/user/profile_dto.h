#pragma once

#include <json/value.h>

#include <optional>
#include <string>

namespace chatserver::protocol::dto::user {

/**
 * @brief 用户资料视图。
 */
struct UserProfileView
{
    std::string userId;
    std::string account;
    std::string nickname;
    std::optional<std::string> avatarUrl;
};

/**
 * @brief 更新用户资料请求。
 *
 * 这里使用显式的 `hasXxx` 标志位区分：
 * 1) 客户端没有提交这个字段；
 * 2) 客户端提交了这个字段。
 */
struct UpdateUserProfileRequest
{
    bool hasNickname{false};
    std::optional<std::string> nickname;
    bool hasAvatarUploadKey{false};
    std::optional<std::string> avatarUploadKey;
};

/**
 * @brief 解析更新用户资料请求。
 * @param json 请求体 JSON。
 * @param out 解析成功后写入的请求 DTO。
 * @param errorMessage 解析失败时写入的错误消息。
 * @return true 表示解析成功；false 表示请求体结构不合法。
 */
inline bool parseUpdateUserProfileRequest(const Json::Value &json,
                                          UpdateUserProfileRequest &out,
                                          std::string &errorMessage)
{
    if (!json.isObject())
    {
        errorMessage = "Request body must be a JSON object";
        return false;
    }

    if (json.isMember("nickname"))
    {
        out.hasNickname = true;
        if (!json["nickname"].isNull() && !json["nickname"].isString())
        {
            errorMessage = "Field 'nickname' must be a string when provided";
            return false;
        }
        if (json["nickname"].isString())
        {
            out.nickname = json["nickname"].asString();
        }
    }

    if (json.isMember("avatar_upload_key"))
    {
        out.hasAvatarUploadKey = true;
        if (!json["avatar_upload_key"].isNull() &&
            !json["avatar_upload_key"].isString())
        {
            errorMessage =
                "Field 'avatar_upload_key' must be a string when provided";
            return false;
        }
        if (json["avatar_upload_key"].isString())
        {
            out.avatarUploadKey = json["avatar_upload_key"].asString();
        }
    }

    return true;
}

/**
 * @brief 把用户资料视图转换成 JSON 对象。
 * @param user 用户资料视图。
 * @return 可直接放入 HTTP 响应 `data.user` 的 JSON 对象。
 */
inline Json::Value toJson(const UserProfileView &user)
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

}  // namespace chatserver::protocol::dto::user

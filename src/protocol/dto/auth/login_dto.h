#pragma once

#include <json/value.h>

#include <cstdint>
#include <optional>
#include <string>

namespace chatserver::protocol::dto::auth {

/**
 * @brief 登录请求 DTO。
 *
 * 当前登录除了账号密码，还必须携带最小设备信息。
 * 原因是登录成功后服务端需要立刻创建一条 `device_session`，
 * 后续 refresh、WebSocket 鉴权和设备管理都围绕这条会话展开。
 */
struct LoginRequest
{
    std::string account;
    std::string password;
    std::string deviceId;
    std::string devicePlatform;
    std::optional<std::string> deviceName;
    std::optional<std::string> clientVersion;
};

/**
 * @brief 登录成功后返回给客户端的最小用户视图。
 */
struct LoginUserView
{
    std::string userId;
    std::string nickname;
    std::optional<std::string> avatarUrl;
};

/**
 * @brief 登录成功响应视图。
 */
struct LoginResultView
{
    LoginUserView user;
    std::string deviceSessionId;
    std::string accessToken;
    std::string refreshToken;
    std::int64_t expiresInSec{0};
};

/**
 * @brief 解析登录接口的 JSON 请求体。
 * @param json Drogon 解析得到的 JSON 对象。
 * @param out 解析成功后写入的登录请求 DTO。
 * @param errorMessage 解析失败时写入的错误消息。
 * @return true 表示字段存在且 JSON 类型正确；false 表示请求体结构不合法。
 */
inline bool parseLoginRequest(const Json::Value &json,
                              LoginRequest &out,
                              std::string &errorMessage)
{
    if (!json.isObject())
    {
        errorMessage = "Request body must be a JSON object";
        return false;
    }

    if (!json.isMember("account") || !json["account"].isString())
    {
        errorMessage = "Field 'account' must be a string";
        return false;
    }

    if (!json.isMember("password") || !json["password"].isString())
    {
        errorMessage = "Field 'password' must be a string";
        return false;
    }

    if (!json.isMember("device_id") || !json["device_id"].isString())
    {
        errorMessage = "Field 'device_id' must be a string";
        return false;
    }

    if (!json.isMember("device_platform") ||
        !json["device_platform"].isString())
    {
        errorMessage = "Field 'device_platform' must be a string";
        return false;
    }

    if (json.isMember("device_name") && !json["device_name"].isNull() &&
        !json["device_name"].isString())
    {
        errorMessage = "Field 'device_name' must be a string when provided";
        return false;
    }

    if (json.isMember("client_version") &&
        !json["client_version"].isNull() &&
        !json["client_version"].isString())
    {
        errorMessage = "Field 'client_version' must be a string when provided";
        return false;
    }

    out.account = json["account"].asString();
    out.password = json["password"].asString();
    out.deviceId = json["device_id"].asString();
    out.devicePlatform = json["device_platform"].asString();

    if (json.isMember("device_name") && json["device_name"].isString())
    {
        out.deviceName = json["device_name"].asString();
    }
    else
    {
        out.deviceName.reset();
    }

    if (json.isMember("client_version") && json["client_version"].isString())
    {
        out.clientVersion = json["client_version"].asString();
    }
    else
    {
        out.clientVersion.reset();
    }

    return true;
}

/**
 * @brief 把登录成功结果转换为 JSON 对象。
 * @param result 已组装好的登录成功视图。
 * @return 可直接放入 HTTP 响应 `data` 的 JSON 对象。
 */
inline Json::Value toJson(const LoginResultView &result)
{
    Json::Value data(Json::objectValue);

    Json::Value user(Json::objectValue);
    user["user_id"] = result.user.userId;
    user["nickname"] = result.user.nickname;
    if (result.user.avatarUrl.has_value())
    {
        user["avatar_url"] = *result.user.avatarUrl;
    }

    data["user"] = std::move(user);
    data["device_session_id"] = result.deviceSessionId;
    data["access_token"] = result.accessToken;
    data["refresh_token"] = result.refreshToken;
    data["expires_in_sec"] = Json::Int64(result.expiresInSec);
    return data;
}

}  // namespace chatserver::protocol::dto::auth

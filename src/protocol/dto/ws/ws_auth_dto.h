#pragma once

#include <json/value.h>

#include <optional>
#include <string>

namespace chatserver::protocol::dto::ws {

/**
 * @brief `ws.auth` 请求载荷。
 */
struct WsAuthRequest
{
    std::string accessToken;
    std::string deviceId;
    std::string deviceSessionId;
    std::optional<std::string> clientVersion;
};

/**
 * @brief `ws.auth.ok` 成功载荷。
 */
struct WsAuthOkPayload
{
    std::string userId;
    std::string deviceSessionId;
};

/**
 * @brief 解析 `ws.auth` 载荷。
 * @param json 统一信封中的 `payload` 对象。
 * @param out 解析成功后写入的认证请求。
 * @param errorMessage 解析失败时写入的错误说明。
 * @return true 表示结构合法；false 表示字段缺失或类型错误。
 */
inline bool parseWsAuthRequest(const Json::Value &json,
                               WsAuthRequest &out,
                               std::string &errorMessage)
{
    if (!json.isObject())
    {
        errorMessage = "ws.auth payload must be an object";
        return false;
    }

    if (!json.isMember("access_token") || !json["access_token"].isString())
    {
        errorMessage = "field 'access_token' must be a string";
        return false;
    }

    if (!json.isMember("device_id") || !json["device_id"].isString())
    {
        errorMessage = "field 'device_id' must be a string";
        return false;
    }

    if (!json.isMember("device_session_id") ||
        !json["device_session_id"].isString())
    {
        errorMessage = "field 'device_session_id' must be a string";
        return false;
    }

    if (json.isMember("client_version") && !json["client_version"].isNull() &&
        !json["client_version"].isString())
    {
        errorMessage = "field 'client_version' must be a string when provided";
        return false;
    }

    out.accessToken = json["access_token"].asString();
    out.deviceId = json["device_id"].asString();
    out.deviceSessionId = json["device_session_id"].asString();
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
 * @brief 把 `ws.auth.ok` 载荷转换为 JSON。
 * @param payload 已组装好的认证成功返回。
 * @return 可直接写入统一信封 `payload` 的 JSON 对象。
 */
inline Json::Value toJson(const WsAuthOkPayload &payload)
{
    Json::Value json(Json::objectValue);
    json["user_id"] = payload.userId;
    json["device_session_id"] = payload.deviceSessionId;
    return json;
}

}  // namespace chatserver::protocol::dto::ws

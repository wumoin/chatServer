#pragma once

#include <json/value.h>

#include <string>

namespace chatserver::protocol::dto::ws {

/**
 * @brief `ws.send` 业务载荷。
 */
struct WsSendPayload
{
    std::string route;
    Json::Value data{Json::objectValue};
};

/**
 * @brief `ws.ack` 业务载荷。
 */
struct WsAckPayload
{
    std::string route;
    bool ok{false};
    int code{0};
    std::string message;
    Json::Value data{Json::objectValue};
};

/**
 * @brief `ws.new` 业务载荷。
 */
struct WsNewPayload
{
    std::string route;
    Json::Value data{Json::objectValue};
};

/**
 * @brief 解析 `ws.send` 载荷。
 * @param json 统一信封中的 `payload` 对象。
 * @param out 解析成功后写入的业务请求。
 * @param errorMessage 解析失败时写入的错误说明。
 * @return true 表示结构合法；false 表示字段缺失或类型错误。
 */
inline bool parseWsSendPayload(const Json::Value &json,
                               WsSendPayload &out,
                               std::string &errorMessage)
{
    if (!json.isObject())
    {
        errorMessage = "ws.send payload must be an object";
        return false;
    }

    if (!json.isMember("route") || !json["route"].isString())
    {
        errorMessage = "field 'route' must be a string";
        return false;
    }

    if (!json.isMember("data") || !json["data"].isObject())
    {
        errorMessage = "field 'data' must be an object";
        return false;
    }

    out.route = json["route"].asString();
    out.data = json["data"];
    return true;
}

/**
 * @brief 把 `ws.ack` 载荷转换为 JSON。
 * @param payload 已组装好的业务确认消息。
 * @return 可直接写入统一信封 `payload` 的 JSON 对象。
 */
inline Json::Value toJson(const WsAckPayload &payload)
{
    Json::Value json(Json::objectValue);
    json["route"] = payload.route;
    json["ok"] = payload.ok;
    json["code"] = payload.code;
    json["message"] = payload.message;
    json["data"] =
        payload.data.isObject() ? payload.data : Json::Value(Json::objectValue);
    return json;
}

/**
 * @brief 把 `ws.new` 载荷转换为 JSON。
 * @param payload 已组装好的业务推送消息。
 * @return 可直接写入统一信封 `payload` 的 JSON 对象。
 */
inline Json::Value toJson(const WsNewPayload &payload)
{
    Json::Value json(Json::objectValue);
    json["route"] = payload.route;
    json["data"] =
        payload.data.isObject() ? payload.data : Json::Value(Json::objectValue);
    return json;
}

}  // namespace chatserver::protocol::dto::ws

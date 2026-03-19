#pragma once

#include <json/value.h>

#include <cstdint>
#include <string>

namespace chatserver::protocol::dto::ws {

/**
 * @brief WebSocket 统一信封。
 *
 * 所有客户端主动发送和服务端主动回包的 JSON 文本消息，
 * 第一版都统一包成这一层结构，便于：
 * 1) 先按 `type` 分发；
 * 2) 统一携带 `request_id`；
 * 3) 后续扩展更多实时事件时不改外层结构。
 */
struct WsEnvelope
{
    std::int64_t version{1};
    std::string type;
    std::string requestId;
    std::int64_t tsMs{0};
    Json::Value payload{Json::objectValue};
};

/**
 * @brief WebSocket 统一错误载荷。
 */
struct WsErrorPayload
{
    int code{0};
    std::string message;
};

/**
 * @brief 解析 WebSocket 统一信封。
 * @param json 收到的 JSON 对象。
 * @param out 解析成功后写入的统一信封。
 * @param errorMessage 解析失败时写入的错误说明。
 * @return true 表示结构合法；false 表示结构不合法。
 */
inline bool parseWsEnvelope(const Json::Value &json,
                            WsEnvelope &out,
                            std::string &errorMessage)
{
    if (!json.isObject())
    {
        errorMessage = "websocket message must be a json object";
        return false;
    }

    if (!json.isMember("version") || !json["version"].isInt64())
    {
        errorMessage = "field 'version' must be an integer";
        return false;
    }

    if (!json.isMember("type") || !json["type"].isString())
    {
        errorMessage = "field 'type' must be a string";
        return false;
    }

    if (!json.isMember("request_id") || !json["request_id"].isString())
    {
        errorMessage = "field 'request_id' must be a string";
        return false;
    }

    if (!json.isMember("ts_ms") || !json["ts_ms"].isInt64())
    {
        errorMessage = "field 'ts_ms' must be an integer";
        return false;
    }

    if (!json.isMember("payload") || !json["payload"].isObject())
    {
        errorMessage = "field 'payload' must be an object";
        return false;
    }

    out.version = json["version"].asInt64();
    out.type = json["type"].asString();
    out.requestId = json["request_id"].asString();
    out.tsMs = json["ts_ms"].asInt64();
    out.payload = json["payload"];
    return true;
}

/**
 * @brief 把统一信封转换为 JSON。
 * @param envelope 已组装完成的统一信封。
 * @return 可直接发送给客户端的 JSON 对象。
 */
inline Json::Value toJson(const WsEnvelope &envelope)
{
    Json::Value json(Json::objectValue);
    json["version"] = Json::Int64(envelope.version);
    json["type"] = envelope.type;
    json["request_id"] = envelope.requestId;
    json["ts_ms"] = Json::Int64(envelope.tsMs);
    json["payload"] =
        envelope.payload.isObject() ? envelope.payload : Json::Value(Json::objectValue);
    return json;
}

/**
 * @brief 把统一错误载荷转换为 JSON。
 * @param payload 错误码和错误消息。
 * @return 可直接写入统一信封 `payload` 的 JSON 对象。
 */
inline Json::Value toJson(const WsErrorPayload &payload)
{
    Json::Value json(Json::objectValue);
    json["code"] = payload.code;
    json["message"] = payload.message;
    return json;
}

}  // namespace chatserver::protocol::dto::ws

#pragma once

#include <json/value.h>

#include <cstdint>
#include <optional>
#include <string>

namespace chatserver::protocol::dto::auth {

// RegisterRequest 对应 HTTP 注册接口的 JSON 请求体。
// 当前只保留最小闭环需要的字段：
// - account
// - password
// - nickname
// - avatar_url(可选)
struct RegisterRequest {
    // 登录账号原值。
    // 这一层只做数据承载，不在这里解释账号格式是否合法。
    std::string account;
    // 明文密码。
    // 后续会由 service 层转成 bcrypt 哈希，不会直接传入 repository。
    std::string password;
    // 展示昵称原值。
    // service 层会继续负责 trim 和长度校验。
    std::string nickname;
    // 头像地址，可选。
    // 当前为空时不会强行写成空字符串，而是直接用 std::optional 表达“未提供”。
    std::optional<std::string> avatarUrl;
};

// RegisterUserView 对应注册成功后返回给客户端的用户基础信息。
// 它是“接口返回视图”，不是数据库行对象，因此只保留对外真的需要暴露的字段。
struct RegisterUserView {
    // 新用户的业务主键，例如 u_xxx。
    std::string userId;
    // 注册成功后的账号原值。
    std::string account;
    // 注册成功后的昵称。
    std::string nickname;
    // 头像地址；如果注册时没提供，则保持为空。
    std::optional<std::string> avatarUrl;
    // 创建时间毫秒值，便于前端直接排序或显示。
    std::int64_t createdAtMs{0};
};

/**
 * @brief 解析注册接口的 JSON 请求体。
 * @param json Drogon 解析得到的 JSON 对象。
 * @param out 解析成功后写入的注册请求 DTO。
 * @param errorMessage 解析失败时写入的错误消息。
 * @return true 表示字段存在且 JSON 类型正确；false 表示请求体结构不合法。
 */
inline bool parseRegisterRequest(const Json::Value &json,
                                 RegisterRequest &out,
                                 std::string &errorMessage)
{
    // 第 1 步：校验最外层结构。
    // 注册接口只接受 JSON object，不接受数组、字符串或其它 JSON 类型。
    if (!json.isObject())
    {
        errorMessage = "Request body must be a JSON object";
        return false;
    }

    // 第 2 步：校验必填字段 account。
    // 这里先只判断“字段存在 + 类型正确”，不在 DTO 层做更重的业务规则判断。
    if (!json.isMember("account") || !json["account"].isString())
    {
        errorMessage = "Field 'account' must be a string";
        return false;
    }

    // 第 3 步：校验必填字段 password。
    if (!json.isMember("password") || !json["password"].isString())
    {
        errorMessage = "Field 'password' must be a string";
        return false;
    }

    // 第 4 步：校验必填字段 nickname。
    if (!json.isMember("nickname") || !json["nickname"].isString())
    {
        errorMessage = "Field 'nickname' must be a string";
        return false;
    }

    // 第 5 步：校验可选字段 avatar_url。
    // 当前允许它缺失，也允许显式传 null；但如果传了具体值，就必须是字符串。
    if (json.isMember("avatar_url") && !json["avatar_url"].isNull() &&
        !json["avatar_url"].isString())
    {
        errorMessage = "Field 'avatar_url' must be a string when provided";
        return false;
    }

    // 第 6 步：把 JSON 字段搬运到 DTO。
    // 注意这里不会做 trim，避免协议层擅自改变用户原始输入。
    out.account = json["account"].asString();
    out.password = json["password"].asString();
    out.nickname = json["nickname"].asString();

    if (json.isMember("avatar_url") && json["avatar_url"].isString())
    {
        out.avatarUrl = json["avatar_url"].asString();
    }
    else
    {
        out.avatarUrl.reset();
    }

    return true;
}

/**
 * @brief 把注册成功返回视图转换为 JSON 对象。
 * @param user 已组装好的注册成功用户视图。
 * @return 可直接放入 HTTP 响应 `data.user` 的 JSON 对象。
 */
inline Json::Value toJson(const RegisterUserView &user)
{
    Json::Value json(Json::objectValue);

    // 当前注册成功后返回的 user 对象和协议文档保持一致：
    // - 始终返回 user_id / account / nickname / created_at_ms；
    // - avatar_url 只有在值存在时才回传。
    json["user_id"] = user.userId;
    json["account"] = user.account;
    json["nickname"] = user.nickname;
    json["created_at_ms"] = Json::Int64(user.createdAtMs);
    if (user.avatarUrl.has_value())
    {
        json["avatar_url"] = *user.avatarUrl;
    }
    return json;
}

}  // namespace chatserver::protocol::dto::auth

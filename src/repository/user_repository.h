#pragma once

#include <drogon/orm/DbClient.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace chatserver::repository {

// CreateUserParams 是 repository 层真正需要落库的字段集合。
// 它已经不再关心 HTTP 请求长什么样，只表达“要插入 users 表的业务数据”。
struct CreateUserParams {
    // users.user_id，对外稳定暴露的业务主键。
    std::string userId;
    // users.account，登录账号原值。
    std::string account;
    // users.nickname，展示昵称。
    std::string nickname;
    // users.avatar_url，可为空。
    std::optional<std::string> avatarUrl;
    // users.password_hash。
    std::string passwordHash;
    // users.password_algo，当前固定为 bcrypt。
    std::string passwordAlgo;
};

// CreatedUserRecord 是 INSERT 成功后回给上层的最小数据。
// 这里不把 password_hash 等内部字段再往上抬，避免误传到接口层。
struct CreatedUserRecord {
    // 新用户主键。
    std::string userId;
    // 新用户账号。
    std::string account;
    // 新用户昵称。
    std::string nickname;
    // 新用户头像地址。
    std::optional<std::string> avatarUrl;
    // 数据库 created_at 对应的毫秒时间戳。
    std::int64_t createdAtMs{0};
};

// AuthUserRecord 是登录校验真正需要的最小账号记录。
// 它既包含对外会回给客户端的基础资料，也包含 service 校验密码必须读取的内部字段。
struct AuthUserRecord {
    std::string userId;
    std::string account;
    std::string nickname;
    std::optional<std::string> avatarUrl;
    std::string passwordHash;
    std::string passwordAlgo;
    std::string accountStatus;
};

enum class CreateUserErrorKind {
    // account 唯一约束冲突。
    kAccountAlreadyExists,
    // 除唯一约束外的其它数据库错误。
    kDatabaseError,
};

struct CreateUserError {
    // 失败类型，供 service 层做错误映射。
    CreateUserErrorKind kind;
    // 数据库或本地异常消息，主要用于服务端日志。
    std::string message;
};

class UserRepository {
  public:
    using CreateUserSuccess = std::function<void(CreatedUserRecord)>;
    using CreateUserFailure = std::function<void(CreateUserError)>;
    using FindUserByAccountSuccess =
        std::function<void(std::optional<AuthUserRecord>)>;
    using RepositoryFailure = std::function<void(std::string)>;

    /**
     * @brief 向 users 表插入一条新的用户账号记录。
     * @param params 待写入数据库的用户字段集合。
     * @param onSuccess 写入成功后的回调，参数为新创建用户的最小结果记录。
     * @param onFailure 写入失败后的回调，参数为归类后的数据库错误信息。
     */
    void createUser(CreateUserParams params,
                    CreateUserSuccess &&onSuccess,
                    CreateUserFailure &&onFailure) const;

    /**
     * @brief 按登录账号查询登录校验所需的最小用户记录。
     * @param account 登录账号原值。
     * @param onSuccess 查询成功后的回调；若账号不存在则回调空值。
     * @param onFailure 查询失败后的回调，参数为数据库错误文本。
     */
    void findUserByAccount(std::string account,
                           FindUserByAccountSuccess &&onSuccess,
                           RepositoryFailure &&onFailure) const;

  private:
    /**
     * @brief 获取默认 PostgreSQL 客户端。
     * @return 当前服务统一使用的 Drogon 数据库客户端指针。
     */
    drogon::orm::DbClientPtr dbClient() const;
};

}  // namespace chatserver::repository

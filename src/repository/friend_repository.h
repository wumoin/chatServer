#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

namespace chatserver::repository {

/**
 * @brief 发送好友申请时需要落库的最小字段集合。
 */
struct CreateFriendRequestParams
{
    std::string requestId;
    std::string requesterId;
    std::string targetId;
    std::optional<std::string> requestMessage;
};

/**
 * @brief 好友申请记录。
 */
struct FriendRequestRecord
{
    std::string requestId;
    std::string requesterId;
    std::string targetId;
    std::optional<std::string> requestMessage;
    std::string status;
    std::int64_t createdAtMs{0};
    std::optional<std::int64_t> handledAtMs;
};

/**
 * @brief 好友申请列表项里的对端用户记录。
 */
struct FriendPeerUserRecord
{
    std::string userId;
    std::string account;
    std::string nickname;
    std::optional<std::string> avatarUrl;
};

/**
 * @brief 好友申请列表项记录。
 */
struct FriendRequestListItemRecord
{
    FriendRequestRecord request;
    FriendPeerUserRecord peerUser;
};

enum class CreateFriendRequestErrorKind
{
    kPendingRequestAlreadyExists,
    kDatabaseError,
};

/**
 * @brief 创建好友申请时的错误结果。
 */
struct CreateFriendRequestError
{
    CreateFriendRequestErrorKind kind;
    std::string message;
};

enum class UpdateFriendRequestStatus
{
    kUpdated,
    kRequestNotPending,
};

/**
 * @brief 更新好友申请状态后的结果。
 */
struct UpdateFriendRequestResult
{
    UpdateFriendRequestStatus status{UpdateFriendRequestStatus::kUpdated};
};

/**
 * @brief 处理好友申请时需要的最小字段集合。
 */
struct HandleFriendRequestParams
{
    std::string requestId;
    std::string targetUserId;
};

class FriendRepository
{
  public:
    using CheckFriendshipSuccess = std::function<void(bool)>;
    using CreateFriendRequestSuccess =
        std::function<void(FriendRequestRecord)>;
    using CreateFriendRequestFailure =
        std::function<void(CreateFriendRequestError)>;
    using FindFriendRequestSuccess =
        std::function<void(std::optional<FriendRequestRecord>)>;
    using ListFriendRequestsSuccess =
        std::function<void(std::vector<FriendRequestListItemRecord>)>;
    using HandleFriendRequestSuccess =
        std::function<void(UpdateFriendRequestResult)>;
    using RepositoryFailure = std::function<void(std::string)>;

    /**
     * @brief 判断两名用户是否已经是正式好友。
     * @param userId 当前用户 ID。
     * @param friendUserId 目标好友用户 ID。
     * @param onSuccess 查询成功后的回调。
     * @param onFailure 查询失败后的回调。
     */
    void hasFriendship(std::string userId,
                       std::string friendUserId,
                       CheckFriendshipSuccess &&onSuccess,
                       RepositoryFailure &&onFailure) const;

    /**
     * @brief 创建一条新的好友申请。
     * @param params 待写入的好友申请字段。
     * @param onSuccess 创建成功后的回调。
     * @param onFailure 创建失败后的回调。
     */
    void createFriendRequest(CreateFriendRequestParams params,
                             CreateFriendRequestSuccess &&onSuccess,
                             CreateFriendRequestFailure &&onFailure) const;

    /**
     * @brief 按申请 ID 查询好友申请。
     * @param requestId 申请 ID。
     * @param onSuccess 查询成功后的回调。
     * @param onFailure 查询失败后的回调。
     */
    void findFriendRequestById(std::string requestId,
                               FindFriendRequestSuccess &&onSuccess,
                               RepositoryFailure &&onFailure) const;

    /**
     * @brief 查询当前用户收到的好友申请列表。
     * @param targetUserId 当前登录用户 ID。
     * @param onSuccess 查询成功后的回调。
     * @param onFailure 查询失败后的回调。
     */
    void listIncomingFriendRequests(
        std::string targetUserId,
        ListFriendRequestsSuccess &&onSuccess,
        RepositoryFailure &&onFailure) const;

    /**
     * @brief 查询当前用户发出的好友申请列表。
     * @param requesterUserId 当前登录用户 ID。
     * @param onSuccess 查询成功后的回调。
     * @param onFailure 查询失败后的回调。
     */
    void listOutgoingFriendRequests(
        std::string requesterUserId,
        ListFriendRequestsSuccess &&onSuccess,
        RepositoryFailure &&onFailure) const;

    /**
     * @brief 接受好友申请，并同时建立双向好友关系。
     * @param params 处理申请所需的最小字段。
     * @param onSuccess 处理成功后的回调。
     * @param onFailure 处理失败后的回调。
     */
    void acceptFriendRequest(HandleFriendRequestParams params,
                             HandleFriendRequestSuccess &&onSuccess,
                             RepositoryFailure &&onFailure) const;

    /**
     * @brief 拒绝好友申请。
     * @param params 处理申请所需的最小字段。
     * @param onSuccess 处理成功后的回调。
     * @param onFailure 处理失败后的回调。
     */
    void rejectFriendRequest(HandleFriendRequestParams params,
                             HandleFriendRequestSuccess &&onSuccess,
                             RepositoryFailure &&onFailure) const;

  private:
    /**
     * @brief 获取默认 PostgreSQL 客户端。
     * @return 当前服务统一使用的 Drogon 数据库客户端指针。
     */
    drogon::orm::DbClientPtr dbClient() const;
};

}  // namespace chatserver::repository

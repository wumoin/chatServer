#pragma once

#include "protocol/dto/friend/friend_dto.h"
#include "repository/friend_repository.h"
#include "repository/user_repository.h"
#include "service/realtime_push_service.h"
#include "service/service_error.h"

#include <functional>
#include <string>
#include <vector>

namespace chatserver::service {

class FriendService
{
  public:
    using SearchResultSuccess =
        std::function<void(protocol::dto::friendship::FriendRequestItemView)>;
    using ListRequestsSuccess =
        std::function<void(std::vector<protocol::dto::friendship::FriendRequestItemView>)>;
    using ListFriendsSuccess =
        std::function<void(std::vector<protocol::dto::friendship::FriendListItemView>)>;
    using Failure = std::function<void(ServiceError)>;

    /**
     * @brief 发送好友申请。
     * @param request 发送好友申请请求 DTO。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 发送成功回调。
     * @param onFailure 发送失败回调。
     */
    void sendFriendRequest(protocol::dto::friendship::SendFriendRequest request,
                           std::string accessToken,
                           SearchResultSuccess &&onSuccess,
                           Failure &&onFailure) const;

    /**
     * @brief 同意好友申请。
     * @param requestId 好友申请 ID。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 处理成功回调。
     * @param onFailure 处理失败回调。
     */
    void acceptFriendRequest(std::string requestId,
                             std::string accessToken,
                             SearchResultSuccess &&onSuccess,
                             Failure &&onFailure) const;

    /**
     * @brief 拒绝好友申请。
     * @param requestId 好友申请 ID。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 处理成功回调。
     * @param onFailure 处理失败回调。
     */
    void rejectFriendRequest(std::string requestId,
                             std::string accessToken,
                             SearchResultSuccess &&onSuccess,
                             Failure &&onFailure) const;

    /**
     * @brief 查询当前用户收到的好友申请列表。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 查询成功回调。
     * @param onFailure 查询失败回调。
     */
    void listIncomingFriendRequests(std::string accessToken,
                                    ListRequestsSuccess &&onSuccess,
                                    Failure &&onFailure) const;

    /**
     * @brief 查询当前用户发出的好友申请列表。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 查询成功回调。
     * @param onFailure 查询失败回调。
     */
    void listOutgoingFriendRequests(std::string accessToken,
                                    ListRequestsSuccess &&onSuccess,
                                    Failure &&onFailure) const;

    /**
     * @brief 查询当前用户的正式好友列表。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 查询成功回调。
     * @param onFailure 查询失败回调。
     */
    void listFriends(std::string accessToken,
                     ListFriendsSuccess &&onSuccess,
                     Failure &&onFailure) const;

  private:
    /**
     * @brief 校验并规范化发送好友申请请求。
     * @param request 待校验的好友申请请求。
     * @param error 校验失败时写入的业务错误。
     * @return true 表示校验通过；false 表示失败。
     */
    bool validateSendFriendRequest(
        protocol::dto::friendship::SendFriendRequest &request,
        ServiceError &error) const;

    repository::FriendRepository friendRepository_;
    repository::UserRepository userRepository_;
    RealtimePushService realtimePushService_;
};

}  // namespace chatserver::service

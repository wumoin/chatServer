#pragma once

#include "service/friend_service.h"

#include <drogon/HttpController.h>

namespace chatserver::transport::http {

class FriendController : public drogon::HttpController<FriendController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(FriendController::sendFriendRequest,
                  "/api/v1/friends/requests",
                  drogon::Post);
    ADD_METHOD_TO(FriendController::listIncomingFriendRequests,
                  "/api/v1/friends/requests/incoming",
                  drogon::Get);
    ADD_METHOD_TO(FriendController::listOutgoingFriendRequests,
                  "/api/v1/friends/requests/outgoing",
                  drogon::Get);
    ADD_METHOD_TO(FriendController::acceptFriendRequest,
                  "/api/v1/friends/requests/{1}/accept",
                  drogon::Post);
    ADD_METHOD_TO(FriendController::rejectFriendRequest,
                  "/api/v1/friends/requests/{1}/reject",
                  drogon::Post);
    METHOD_LIST_END

    /**
     * @brief 发送好友申请。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     */
    void sendFriendRequest(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 查询当前用户收到的好友申请。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     */
    void listIncomingFriendRequests(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 查询当前用户发出的好友申请。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     */
    void listOutgoingFriendRequests(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 同意好友申请。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     * @param requestId 路由中的好友申请 ID。
     */
    void acceptFriendRequest(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
        std::string requestId) const;

    /**
     * @brief 拒绝好友申请。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     * @param requestId 路由中的好友申请 ID。
     */
    void rejectFriendRequest(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
        std::string requestId) const;

  private:
    service::FriendService friendService_;
};

}  // namespace chatserver::transport::http

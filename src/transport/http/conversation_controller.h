#pragma once

#include "service/conversation_service.h"

#include <drogon/HttpController.h>

namespace chatserver::transport::http {

class ConversationController
    : public drogon::HttpController<ConversationController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConversationController::createPrivateConversation,
                  "/api/v1/conversations/private",
                  drogon::Post);
    ADD_METHOD_TO(ConversationController::listConversations,
                  "/api/v1/conversations",
                  drogon::Get);
    ADD_METHOD_TO(ConversationController::getConversationDetail,
                  "/api/v1/conversations/{1}",
                  drogon::Get);
    ADD_METHOD_TO(ConversationController::listMessages,
                  "/api/v1/conversations/{1}/messages",
                  drogon::Get);
    ADD_METHOD_TO(ConversationController::sendTextMessage,
                  "/api/v1/conversations/{1}/messages/text",
                  drogon::Post);
    METHOD_LIST_END

    /**
     * @brief 创建或复用一对一私聊会话。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     */
    void createPrivateConversation(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 查询当前用户的会话列表。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     */
    void listConversations(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback) const;

    /**
     * @brief 查询指定会话的详情和当前用户成员状态。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     * @param conversationId 路由中的会话 ID。
     */
    void getConversationDetail(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
        std::string conversationId) const;

    /**
     * @brief 查询指定会话的历史消息。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     * @param conversationId 路由中的会话 ID。
     */
    void listMessages(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
        std::string conversationId) const;

    /**
     * @brief 向指定会话发送文本消息。
     * @param request HTTP 请求对象。
     * @param callback HTTP 响应回调。
     * @param conversationId 路由中的会话 ID。
     */
    void sendTextMessage(
        const drogon::HttpRequestPtr &request,
        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
        std::string conversationId) const;

  private:
    service::ConversationService conversationService_;
};

}  // namespace chatserver::transport::http

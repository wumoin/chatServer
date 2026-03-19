#pragma once

#include "infra/security/token_provider.h"
#include "protocol/dto/conversation/conversation_dto.h"
#include "repository/conversation_repository.h"
#include "repository/friend_repository.h"
#include "repository/user_repository.h"
#include "service/service_error.h"

#include <functional>
#include <string>
#include <vector>

namespace chatserver::service {

class ConversationService
{
  public:
    using ConversationItemSuccess =
        std::function<void(protocol::dto::conversation::ConversationListItemView)>;
    using ConversationDetailSuccess =
        std::function<void(protocol::dto::conversation::ConversationDetailView)>;
    using ListConversationsSuccess =
        std::function<void(std::vector<protocol::dto::conversation::ConversationListItemView>)>;
    using ListMessagesSuccess =
        std::function<void(protocol::dto::conversation::ListConversationMessagesResult)>;
    using MessageSuccess =
        std::function<void(protocol::dto::conversation::ConversationMessageView)>;
    using Failure = std::function<void(ServiceError)>;

    /**
     * @brief 创建或复用一对一私聊会话。
     * @param request 发起私聊请求。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 查询或创建成功回调。
     * @param onFailure 失败回调。
     */
    void createOrFindPrivateConversation(
        protocol::dto::conversation::CreatePrivateConversationRequest request,
        std::string accessToken,
        ConversationItemSuccess &&onSuccess,
        Failure &&onFailure) const;

    /**
     * @brief 查询当前用户的会话列表。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 查询成功回调。
     * @param onFailure 失败回调。
     */
    void listConversations(std::string accessToken,
                           ListConversationsSuccess &&onSuccess,
                           Failure &&onFailure) const;

    /**
     * @brief 查询指定会话的详情和当前用户成员状态。
     * @param conversationId 会话 ID。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 查询成功回调。
     * @param onFailure 失败回调。
     */
    void getConversationDetail(std::string conversationId,
                               std::string accessToken,
                               ConversationDetailSuccess &&onSuccess,
                               Failure &&onFailure) const;

    /**
     * @brief 查询指定会话的历史消息。
     * @param conversationId 会话 ID。
     * @param request 历史消息分页参数。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 查询成功回调。
     * @param onFailure 失败回调。
     */
    void listMessages(
        std::string conversationId,
        protocol::dto::conversation::ListConversationMessagesRequest request,
        std::string accessToken,
        ListMessagesSuccess &&onSuccess,
        Failure &&onFailure) const;

    /**
     * @brief 向指定会话发送文本消息。
     * @param conversationId 会话 ID。
     * @param request 文本消息请求 DTO。
     * @param accessToken 当前登录态 access token。
     * @param onSuccess 发送成功回调。
     * @param onFailure 发送失败回调。
     */
    void sendTextMessage(
        std::string conversationId,
        protocol::dto::conversation::SendTextMessageRequest request,
        std::string accessToken,
        MessageSuccess &&onSuccess,
        Failure &&onFailure) const;

  private:
    /**
     * @brief 校验并解析当前 access token。
     * @param accessToken 当前登录态 access token。
     * @param claims 校验成功后写入的 claims。
     * @param onFailure 校验失败时调用的回调。
     * @return true 表示校验通过；false 表示失败。
     */
    bool resolveCurrentUserClaims(const std::string &accessToken,
                                  infra::security::AccessTokenClaims &claims,
                                  const Failure &onFailure) const;

    /**
     * @brief 把 repository 会话列表项转换成 DTO 视图。
     * @param item repository 会话列表项记录。
     * @return 面向 controller 的会话列表项视图。
     */
    protocol::dto::conversation::ConversationListItemView toConversationView(
        const repository::ConversationListItemRecord &item) const;

    /**
     * @brief 把 repository 会话详情记录转换成 DTO 视图。
     * @param item repository 会话详情记录。
     * @return 面向 controller 的会话详情视图。
     */
    protocol::dto::conversation::ConversationDetailView toConversationDetailView(
        const repository::ConversationDetailRecord &item) const;

    /**
     * @brief 把 repository 消息记录转换成 DTO 视图。
     * @param item repository 消息记录。
     * @return 面向 controller 的消息视图。
     */
    protocol::dto::conversation::ConversationMessageView toMessageView(
        const repository::ConversationMessageRecord &item) const;

    repository::ConversationRepository conversationRepository_;
    repository::FriendRepository friendRepository_;
    repository::UserRepository userRepository_;
};

}  // namespace chatserver::service

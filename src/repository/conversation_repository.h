#pragma once

#include <json/value.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

namespace chatserver::repository {

/**
 * @brief 发起私聊时需要写入的最小字段集合。
 */
struct CreateOrFindDirectConversationParams
{
    std::string conversationId;
    std::string currentUserId;
    std::string peerUserId;
    std::string directPairKey;
};

/**
 * @brief 创建或查询私聊会话后的最小结果。
 */
struct CreateOrFindDirectConversationResult
{
    std::string conversationId;
    bool created{false};
};

/**
 * @brief 会话列表里的对端用户记录。
 */
struct ConversationPeerUserRecord
{
    std::string userId;
    std::string account;
    std::string nickname;
    std::optional<std::string> avatarUrl;
};

/**
 * @brief 会话列表项记录。
 */
struct ConversationListItemRecord
{
    std::string conversationId;
    std::string conversationType;
    ConversationPeerUserRecord peerUser;
    std::int64_t lastMessageSeq{0};
    std::int64_t lastReadSeq{0};
    std::int64_t unreadCount{0};
    std::optional<std::string> lastMessagePreview;
    std::optional<std::int64_t> lastMessageAtMs;
    std::int64_t createdAtMs{0};
};

/**
 * @brief 当前用户在会话中的成员状态记录。
 */
struct ConversationMemberRecord
{
    std::string userId;
    std::string memberRole;
    std::int64_t joinedAtMs{0};
    std::int64_t lastReadSeq{0};
    std::optional<std::int64_t> lastReadAtMs;
};

/**
 * @brief 单条会话详情记录。
 */
struct ConversationDetailRecord
{
    std::string conversationId;
    std::string conversationType;
    ConversationPeerUserRecord peerUser;
    ConversationMemberRecord myMember;
    std::int64_t lastMessageSeq{0};
    std::int64_t unreadCount{0};
    std::optional<std::string> lastMessagePreview;
    std::optional<std::int64_t> lastMessageAtMs;
    std::int64_t createdAtMs{0};
};

/**
 * @brief 历史消息记录。
 */
struct ConversationMessageRecord
{
    std::string messageId;
    std::string conversationId;
    std::int64_t seq{0};
    std::string senderId;
    std::optional<std::string> clientMessageId;
    std::string messageType;
    Json::Value contentJson{Json::objectValue};
    std::int64_t createdAtMs{0};
};

/**
 * @brief 历史消息分页查询参数。
 */
struct ListMessagesParams
{
    std::string conversationId;
    std::size_t limit{50};
    std::optional<std::int64_t> beforeSeq;
    std::optional<std::int64_t> afterSeq;
};

/**
 * @brief 历史消息分页查询结果。
 */
struct ListMessagesResult
{
    std::vector<ConversationMessageRecord> items;
    bool hasMore{false};
    std::optional<std::int64_t> nextBeforeSeq;
    std::optional<std::int64_t> nextAfterSeq;
};

/**
 * @brief 发送文本消息时需要落库的最小字段集合。
 */
struct CreateTextMessageParams
{
    std::string messageId;
    std::string conversationId;
    std::string senderId;
    std::optional<std::string> clientMessageId;
    std::string text;
};

/**
 * @brief 发送图片消息时需要落库的最小字段集合。
 *
 * 这里保存的是消息最终展示所需的附件摘要，而不是临时上传态的引用。
 * 一旦消息写入成功，客户端后续只需要依赖 message.content 里的正式字段。
 */
struct CreateImageMessageParams
{
    std::string messageId;
    std::string conversationId;
    std::string senderId;
    std::optional<std::string> clientMessageId;
    std::string attachmentId;
    std::string fileName;
    std::string mimeType;
    std::int64_t sizeBytes{0};
    std::string downloadUrl;
    std::optional<std::string> caption;
    std::optional<int> imageWidth;
    std::optional<int> imageHeight;
};

class ConversationRepository
{
  public:
    using CreateOrFindDirectConversationSuccess =
        std::function<void(CreateOrFindDirectConversationResult)>;
    using FindConversationItemSuccess =
        std::function<void(std::optional<ConversationListItemRecord>)>;
    using FindConversationDetailSuccess =
        std::function<void(std::optional<ConversationDetailRecord>)>;
    using ListConversationsSuccess =
        std::function<void(std::vector<ConversationListItemRecord>)>;
    using ListConversationMemberUserIdsSuccess =
        std::function<void(std::vector<std::string>)>;
    using ListMessagesSuccess = std::function<void(ListMessagesResult)>;
    using CreateTextMessageSuccess =
        std::function<void(ConversationMessageRecord)>;
    using CreateImageMessageSuccess =
        std::function<void(ConversationMessageRecord)>;
    using RepositoryFailure = std::function<void(std::string)>;

    /**
     * @brief 为一对好友创建或复用稳定私聊会话。
     * @param params 发起会话所需的最小字段集合。
     * @param onSuccess 查询或创建成功后的回调。
     * @param onFailure 失败回调。
     */
    void createOrFindDirectConversation(
        CreateOrFindDirectConversationParams params,
        CreateOrFindDirectConversationSuccess &&onSuccess,
        RepositoryFailure &&onFailure) const;

    /**
     * @brief 查询当前用户的会话列表。
     * @param userId 当前登录用户 ID。
     * @param onSuccess 查询成功后的回调。
     * @param onFailure 查询失败后的回调。
     */
    void listConversations(std::string userId,
                           ListConversationsSuccess &&onSuccess,
                           RepositoryFailure &&onFailure) const;

    /**
     * @brief 查询当前用户可见的单条会话列表项。
     * @param userId 当前登录用户 ID。
     * @param conversationId 会话 ID。
     * @param onSuccess 查询成功后的回调；若会话不存在或当前用户不属于该会话则返回空值。
     * @param onFailure 查询失败后的回调。
     */
    void findConversationItem(std::string userId,
                              std::string conversationId,
                              FindConversationItemSuccess &&onSuccess,
                              RepositoryFailure &&onFailure) const;

    /**
     * @brief 查询当前用户可见的单条会话详情。
     * @param userId 当前登录用户 ID。
     * @param conversationId 会话 ID。
     * @param onSuccess 查询成功后的回调；若会话不存在或当前用户不属于该会话则返回空值。
     * @param onFailure 查询失败后的回调。
     */
    void findConversationDetail(std::string userId,
                                std::string conversationId,
                                FindConversationDetailSuccess &&onSuccess,
                                RepositoryFailure &&onFailure) const;

    /**
     * @brief 查询指定会话的全部成员用户 ID。
     * @param conversationId 会话 ID。
     * @param onSuccess 查询成功后的回调。
     * @param onFailure 查询失败后的回调。
     */
    void listConversationMemberUserIds(
        std::string conversationId,
        ListConversationMemberUserIdsSuccess &&onSuccess,
        RepositoryFailure &&onFailure) const;

    /**
     * @brief 查询历史消息分页结果。
     * @param params 历史消息分页参数。
     * @param onSuccess 查询成功后的回调。
     * @param onFailure 查询失败后的回调。
     */
    void listMessages(ListMessagesParams params,
                      ListMessagesSuccess &&onSuccess,
                      RepositoryFailure &&onFailure) const;

    /**
     * @brief 向指定会话写入一条文本消息。
     * @param params 待写入数据库的消息字段。
     * @param onSuccess 写入成功后的回调。
     * @param onFailure 写入失败后的回调。
     */
    void createTextMessage(CreateTextMessageParams params,
                           CreateTextMessageSuccess &&onSuccess,
                           RepositoryFailure &&onFailure) const;

    /**
     * @brief 向指定会话写入一条图片消息。
     * @param params 待写入数据库的图片消息字段。
     * @param onSuccess 写入成功后的回调。
     * @param onFailure 写入失败后的回调。
     */
    void createImageMessage(CreateImageMessageParams params,
                            CreateImageMessageSuccess &&onSuccess,
                            RepositoryFailure &&onFailure) const;

  private:
    /**
     * @brief 获取默认 PostgreSQL 客户端。
     * @return 当前服务统一使用的 Drogon 数据库客户端指针。
     */
    drogon::orm::DbClientPtr dbClient() const;
};

}  // namespace chatserver::repository

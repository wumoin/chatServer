#include "repository/conversation_repository.h"

#include <drogon/drogon.h>
#include <json/json.h>

#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ConversationRepository 封装 conversations / conversation_members / messages
// 相关的 SQL。这里的目标是把“如何查库”与“业务上为什么这样查”分开：
// service 关心规则和流程，repository 只关心参数、SQL 和结果映射。
namespace chatserver::repository {
namespace {

constexpr auto kCreateOrFindDirectConversationSql = R"SQL(
WITH inserted_conversation AS (
    INSERT INTO conversations (
        conversation_id,
        conversation_type,
        created_by,
        direct_pair_key
    )
    VALUES ($1, 'direct', $2, $3)
    ON CONFLICT (direct_pair_key) WHERE conversation_type = 'direct'
    DO NOTHING
    RETURNING conversation_id
), selected_conversation AS (
    SELECT conversation_id FROM inserted_conversation
    UNION ALL
    SELECT c.conversation_id
    FROM conversations c
    WHERE c.conversation_type = 'direct'
      AND c.direct_pair_key = $3
      AND NOT EXISTS (SELECT 1 FROM inserted_conversation)
    LIMIT 1
), inserted_members AS (
    INSERT INTO conversation_members (
        conversation_id,
        user_id,
        member_role
    )
    SELECT sc.conversation_id, member.user_id, 'member'
    FROM selected_conversation sc
    CROSS JOIN (VALUES ($2), ($4)) AS member(user_id)
    ON CONFLICT (conversation_id, user_id) DO NOTHING
)
SELECT
    sc.conversation_id,
    EXISTS (SELECT 1 FROM inserted_conversation) AS created
FROM selected_conversation sc
LIMIT 1
)SQL";

constexpr auto kListConversationsSql = R"SQL(
-- 这里的 peer_user 是“相对 self.user_id 的另一位成员”。
-- 因而查询结果天然带有调用者视角：同一会话对不同用户查询，peer_user 会不同。
SELECT
    c.conversation_id,
    c.conversation_type,
    c.last_message_seq,
    self.last_read_seq,
    GREATEST(c.last_message_seq - self.last_read_seq, 0)::BIGINT AS unread_count,
    (EXTRACT(EPOCH FROM c.created_at) * 1000)::BIGINT AS created_at_ms,
    CASE
        WHEN c.last_message_at IS NULL THEN NULL
        ELSE (EXTRACT(EPOCH FROM c.last_message_at) * 1000)::BIGINT
    END AS last_message_at_ms,
    peer.user_id AS peer_user_id,
    peer.account AS peer_account,
    peer.nickname AS peer_nickname,
    peer.avatar_url AS peer_avatar_url,
    CASE
        WHEN msg.message_id IS NULL THEN NULL
        WHEN msg.message_type = 'text' THEN NULLIF(COALESCE(msg.content_json->>'text', ''), '')
        ELSE '[' || msg.message_type || ']'
    END AS last_message_preview
FROM conversation_members self
JOIN conversations c ON c.conversation_id = self.conversation_id
LEFT JOIN conversation_members peer_member
    ON peer_member.conversation_id = c.conversation_id
   AND peer_member.user_id <> self.user_id
LEFT JOIN users peer ON peer.user_id = peer_member.user_id
LEFT JOIN messages msg
    ON msg.conversation_id = c.conversation_id
   AND msg.seq = c.last_message_seq
WHERE self.user_id = $1
ORDER BY COALESCE(c.last_message_at, c.created_at) DESC, c.conversation_id ASC
)SQL";

constexpr auto kFindConversationItemSql = R"SQL(
-- 与 listConversations 一样，这里的 peer_user 也是相对 self.user_id 解析出来的。
-- 上层如果要把结果转发给别的用户，必须按接收方 user_id 重新查询，不能直接复用。
SELECT
    c.conversation_id,
    c.conversation_type,
    c.last_message_seq,
    self.last_read_seq,
    GREATEST(c.last_message_seq - self.last_read_seq, 0)::BIGINT AS unread_count,
    (EXTRACT(EPOCH FROM c.created_at) * 1000)::BIGINT AS created_at_ms,
    CASE
        WHEN c.last_message_at IS NULL THEN NULL
        ELSE (EXTRACT(EPOCH FROM c.last_message_at) * 1000)::BIGINT
    END AS last_message_at_ms,
    peer.user_id AS peer_user_id,
    peer.account AS peer_account,
    peer.nickname AS peer_nickname,
    peer.avatar_url AS peer_avatar_url,
    CASE
        WHEN msg.message_id IS NULL THEN NULL
        WHEN msg.message_type = 'text' THEN NULLIF(COALESCE(msg.content_json->>'text', ''), '')
        ELSE '[' || msg.message_type || ']'
    END AS last_message_preview
FROM conversation_members self
JOIN conversations c ON c.conversation_id = self.conversation_id
LEFT JOIN conversation_members peer_member
    ON peer_member.conversation_id = c.conversation_id
   AND peer_member.user_id <> self.user_id
LEFT JOIN users peer ON peer.user_id = peer_member.user_id
LEFT JOIN messages msg
    ON msg.conversation_id = c.conversation_id
   AND msg.seq = c.last_message_seq
WHERE self.user_id = $1
  AND c.conversation_id = $2
LIMIT 1
)SQL";

constexpr auto kFindConversationDetailSql = R"SQL(
SELECT
    c.conversation_id,
    c.conversation_type,
    c.last_message_seq,
    self.user_id AS self_user_id,
    self.member_role AS self_member_role,
    self.last_read_seq,
    GREATEST(c.last_message_seq - self.last_read_seq, 0)::BIGINT AS unread_count,
    (EXTRACT(EPOCH FROM self.joined_at) * 1000)::BIGINT AS self_joined_at_ms,
    CASE
        WHEN self.last_read_at IS NULL THEN NULL
        ELSE (EXTRACT(EPOCH FROM self.last_read_at) * 1000)::BIGINT
    END AS self_last_read_at_ms,
    (EXTRACT(EPOCH FROM c.created_at) * 1000)::BIGINT AS created_at_ms,
    CASE
        WHEN c.last_message_at IS NULL THEN NULL
        ELSE (EXTRACT(EPOCH FROM c.last_message_at) * 1000)::BIGINT
    END AS last_message_at_ms,
    peer.user_id AS peer_user_id,
    peer.account AS peer_account,
    peer.nickname AS peer_nickname,
    peer.avatar_url AS peer_avatar_url,
    CASE
        WHEN msg.message_id IS NULL THEN NULL
        WHEN msg.message_type = 'text' THEN NULLIF(COALESCE(msg.content_json->>'text', ''), '')
        ELSE '[' || msg.message_type || ']'
    END AS last_message_preview
FROM conversation_members self
JOIN conversations c ON c.conversation_id = self.conversation_id
LEFT JOIN conversation_members peer_member
    ON peer_member.conversation_id = c.conversation_id
   AND peer_member.user_id <> self.user_id
LEFT JOIN users peer ON peer.user_id = peer_member.user_id
LEFT JOIN messages msg
    ON msg.conversation_id = c.conversation_id
   AND msg.seq = c.last_message_seq
WHERE self.user_id = $1
  AND c.conversation_id = $2
LIMIT 1
)SQL";

constexpr auto kListConversationMemberUserIdsSql = R"SQL(
SELECT user_id
FROM conversation_members
WHERE conversation_id = $1
ORDER BY user_id ASC
)SQL";

constexpr auto kListLatestMessagesSql = R"SQL(
WITH selected AS (
    SELECT
        message_id,
        conversation_id,
        seq,
        sender_id,
        client_message_id,
        message_type,
        content_json::TEXT AS content_json_text,
        (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT AS created_at_ms
    FROM messages
    WHERE conversation_id = $1
    ORDER BY seq DESC
    LIMIT $2
)
SELECT *
FROM selected
ORDER BY seq ASC
)SQL";

constexpr auto kListMessagesBeforeSql = R"SQL(
WITH selected AS (
    SELECT
        message_id,
        conversation_id,
        seq,
        sender_id,
        client_message_id,
        message_type,
        content_json::TEXT AS content_json_text,
        (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT AS created_at_ms
    FROM messages
    WHERE conversation_id = $1
      AND seq < $2
    ORDER BY seq DESC
    LIMIT $3
)
SELECT *
FROM selected
ORDER BY seq ASC
)SQL";

constexpr auto kListMessagesAfterSql = R"SQL(
SELECT
    message_id,
    conversation_id,
    seq,
    sender_id,
    client_message_id,
    message_type,
    content_json::TEXT AS content_json_text,
    (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT AS created_at_ms
FROM messages
WHERE conversation_id = $1
  AND seq > $2
ORDER BY seq ASC
LIMIT $3
)SQL";

constexpr auto kInsertTextMessageSql = R"SQL(
WITH updated_conversation AS (
    UPDATE conversations
    SET
        last_message_seq = last_message_seq + 1,
        last_message_at = NOW()
    WHERE conversation_id = $1
    RETURNING last_message_seq
)
INSERT INTO messages (
    message_id,
    conversation_id,
    seq,
    sender_id,
    client_message_id,
    message_type,
    content_json
)
    SELECT
        $2,
        $1,
        uc.last_message_seq,
        $3,
        $4,
        'text',
        jsonb_build_object('text', $5::TEXT)
    FROM updated_conversation uc
RETURNING
    message_id,
    conversation_id,
    seq,
    sender_id,
    client_message_id,
    message_type,
    content_json::TEXT AS content_json_text,
    (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT AS created_at_ms
)SQL";

Json::Value parseJsonText(const std::string &jsonText)
{
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    Json::Value value;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(jsonText.data(),
                       jsonText.data() + jsonText.size(),
                       &value,
                       &errors))
    {
        return Json::Value(Json::objectValue);
    }
    return value;
}

ConversationListItemRecord toConversationListItemRecord(const drogon::orm::Row &row)
{
    ConversationListItemRecord item;
    item.conversationId = row["conversation_id"].as<std::string>();
    item.conversationType = row["conversation_type"].as<std::string>();
    item.lastMessageSeq = row["last_message_seq"].as<std::int64_t>();
    item.lastReadSeq = row["last_read_seq"].as<std::int64_t>();
    item.unreadCount = row["unread_count"].as<std::int64_t>();
    item.createdAtMs = row["created_at_ms"].as<std::int64_t>();
    if (!row["last_message_at_ms"].isNull())
    {
        item.lastMessageAtMs = row["last_message_at_ms"].as<std::int64_t>();
    }
    item.peerUser.userId = row["peer_user_id"].as<std::string>();
    item.peerUser.account = row["peer_account"].as<std::string>();
    item.peerUser.nickname = row["peer_nickname"].as<std::string>();
    if (!row["peer_avatar_url"].isNull())
    {
        item.peerUser.avatarUrl = row["peer_avatar_url"].as<std::string>();
    }
    if (!row["last_message_preview"].isNull())
    {
        item.lastMessagePreview =
            row["last_message_preview"].as<std::string>();
    }
    return item;
}

ConversationDetailRecord toConversationDetailRecord(const drogon::orm::Row &row)
{
    ConversationDetailRecord item;
    item.conversationId = row["conversation_id"].as<std::string>();
    item.conversationType = row["conversation_type"].as<std::string>();
    item.peerUser.userId = row["peer_user_id"].as<std::string>();
    item.peerUser.account = row["peer_account"].as<std::string>();
    item.peerUser.nickname = row["peer_nickname"].as<std::string>();
    if (!row["peer_avatar_url"].isNull())
    {
        item.peerUser.avatarUrl = row["peer_avatar_url"].as<std::string>();
    }

    item.myMember.userId = row["self_user_id"].as<std::string>();
    item.myMember.memberRole = row["self_member_role"].as<std::string>();
    item.myMember.joinedAtMs = row["self_joined_at_ms"].as<std::int64_t>();
    item.myMember.lastReadSeq = row["last_read_seq"].as<std::int64_t>();
    if (!row["self_last_read_at_ms"].isNull())
    {
        item.myMember.lastReadAtMs =
            row["self_last_read_at_ms"].as<std::int64_t>();
    }

    item.lastMessageSeq = row["last_message_seq"].as<std::int64_t>();
    item.unreadCount = row["unread_count"].as<std::int64_t>();
    item.createdAtMs = row["created_at_ms"].as<std::int64_t>();
    if (!row["last_message_at_ms"].isNull())
    {
        item.lastMessageAtMs = row["last_message_at_ms"].as<std::int64_t>();
    }
    if (!row["last_message_preview"].isNull())
    {
        item.lastMessagePreview = row["last_message_preview"].as<std::string>();
    }
    return item;
}

std::vector<ConversationListItemRecord> toConversationList(
    const drogon::orm::Result &rows)
{
    std::vector<ConversationListItemRecord> items;
    items.reserve(rows.size());
    for (const auto &row : rows)
    {
        items.push_back(toConversationListItemRecord(row));
    }
    return items;
}

ConversationMessageRecord toConversationMessageRecord(const drogon::orm::Row &row)
{
    ConversationMessageRecord item;
    item.messageId = row["message_id"].as<std::string>();
    item.conversationId = row["conversation_id"].as<std::string>();
    item.seq = row["seq"].as<std::int64_t>();
    item.senderId = row["sender_id"].as<std::string>();
    if (!row["client_message_id"].isNull())
    {
        item.clientMessageId = row["client_message_id"].as<std::string>();
    }
    item.messageType = row["message_type"].as<std::string>();
    item.contentJson =
        parseJsonText(row["content_json_text"].as<std::string>());
    item.createdAtMs = row["created_at_ms"].as<std::int64_t>();
    return item;
}

ListMessagesResult normalizeMessagesResult(drogon::orm::Result rows,
                                          const std::size_t requestedLimit,
                                          const bool isAfterQuery)
{
    ListMessagesResult result;
    std::vector<ConversationMessageRecord> items;
    items.reserve(rows.size());
    for (const auto &row : rows)
    {
        items.push_back(toConversationMessageRecord(row));
    }

    if (items.size() > requestedLimit)
    {
        result.hasMore = true;
        items.resize(requestedLimit);
    }

    result.items = std::move(items);
    if (!result.items.empty())
    {
        if (isAfterQuery)
        {
            result.nextAfterSeq = result.items.back().seq;
        }
        else
        {
            result.nextBeforeSeq = result.items.front().seq;
        }
    }
    return result;
}

}  // namespace

void ConversationRepository::createOrFindDirectConversation(
    CreateOrFindDirectConversationParams params,
    CreateOrFindDirectConversationSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kCreateOrFindDirectConversationSql,
            [onSuccess = std::move(onSuccess),
             onFailure = std::move(onFailure)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onFailure("create or find conversations returned no rows");
                    return;
                }

                CreateOrFindDirectConversationResult result;
                result.conversationId =
                    rows[0]["conversation_id"].as<std::string>();
                result.created = rows[0]["created"].as<bool>();
                onSuccess(std::move(result));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            params.conversationId,
            params.currentUserId,
            params.directPairKey,
            params.peerUserId);
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void ConversationRepository::listConversations(
    std::string userId,
    ListConversationsSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kListConversationsSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                onSuccess(toConversationList(rows));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(userId));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void ConversationRepository::findConversationItem(
    std::string userId,
    std::string conversationId,
    FindConversationItemSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kFindConversationItemSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onSuccess(std::nullopt);
                    return;
                }
                onSuccess(toConversationListItemRecord(rows[0]));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(userId),
            std::move(conversationId));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void ConversationRepository::findConversationDetail(
    std::string userId,
    std::string conversationId,
    FindConversationDetailSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kFindConversationDetailSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onSuccess(std::nullopt);
                    return;
                }
                onSuccess(toConversationDetailRecord(rows[0]));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(userId),
            std::move(conversationId));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void ConversationRepository::listConversationMemberUserIds(
    std::string conversationId,
    ListConversationMemberUserIdsSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kListConversationMemberUserIdsSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                std::vector<std::string> userIds;
                userIds.reserve(rows.size());
                for (const auto &row : rows)
                {
                    userIds.push_back(row["user_id"].as<std::string>());
                }
                onSuccess(std::move(userIds));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            conversationId);
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void ConversationRepository::listMessages(
    ListMessagesParams params,
    ListMessagesSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        const std::size_t fetchLimit = std::max<std::size_t>(params.limit, 1) + 1;
        auto sharedFailure =
            std::make_shared<RepositoryFailure>(std::move(onFailure));

        if (params.afterSeq.has_value())
        {
            client->execSqlAsync(
                kListMessagesAfterSql,
                [onSuccess = std::move(onSuccess),
                 sharedFailure,
                 requestedLimit = params.limit](
                    const drogon::orm::Result &rows) mutable {
                    onSuccess(normalizeMessagesResult(
                        rows, requestedLimit, true));
                },
                [sharedFailure](const drogon::orm::DrogonDbException &exception) mutable {
                    (*sharedFailure)(exception.base().what());
                },
                params.conversationId,
                *params.afterSeq,
                static_cast<std::int64_t>(fetchLimit));
            return;
        }

        if (params.beforeSeq.has_value())
        {
            client->execSqlAsync(
                kListMessagesBeforeSql,
                [onSuccess = std::move(onSuccess),
                 sharedFailure,
                 requestedLimit = params.limit](
                    const drogon::orm::Result &rows) mutable {
                    onSuccess(normalizeMessagesResult(
                        rows, requestedLimit, false));
                },
                [sharedFailure](const drogon::orm::DrogonDbException &exception) mutable {
                    (*sharedFailure)(exception.base().what());
                },
                params.conversationId,
                *params.beforeSeq,
                static_cast<std::int64_t>(fetchLimit));
            return;
        }

        client->execSqlAsync(
            kListLatestMessagesSql,
            [onSuccess = std::move(onSuccess),
             sharedFailure,
             requestedLimit = params.limit](
                const drogon::orm::Result &rows) mutable {
                onSuccess(normalizeMessagesResult(rows, requestedLimit, false));
            },
            [sharedFailure](const drogon::orm::DrogonDbException &exception) mutable {
                (*sharedFailure)(exception.base().what());
            },
            params.conversationId,
            static_cast<std::int64_t>(fetchLimit));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void ConversationRepository::createTextMessage(
    CreateTextMessageParams params,
    CreateTextMessageSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kInsertTextMessageSql,
            [onSuccess = std::move(onSuccess),
             onFailure = std::move(onFailure)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onFailure("insert messages returned no rows");
                    return;
                }
                onSuccess(toConversationMessageRecord(rows[0]));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            params.conversationId,
            params.messageId,
            params.senderId,
            params.clientMessageId,
            params.text);
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

drogon::orm::DbClientPtr ConversationRepository::dbClient() const
{
    return drogon::app().getDbClient();
}

}  // namespace chatserver::repository

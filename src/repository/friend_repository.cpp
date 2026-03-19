#include "repository/friend_repository.h"

#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chatserver::repository {
namespace {

constexpr auto kCheckFriendshipSql = R"SQL(
SELECT 1
FROM friendships
WHERE user_id = $1 AND friend_user_id = $2
LIMIT 1
)SQL";

constexpr auto kInsertFriendRequestSql = R"SQL(
INSERT INTO friend_requests (
    request_id,
    requester_id,
    target_id,
    request_message
)
VALUES ($1, $2, $3, $4)
RETURNING
    request_id,
    requester_id,
    target_id,
    request_message,
    status,
    (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT AS created_at_ms,
    CASE
        WHEN handled_at IS NULL THEN NULL
        ELSE (EXTRACT(EPOCH FROM handled_at) * 1000)::BIGINT
    END AS handled_at_ms
)SQL";

constexpr auto kFindFriendRequestByIdSql = R"SQL(
SELECT
    request_id,
    requester_id,
    target_id,
    request_message,
    status,
    (EXTRACT(EPOCH FROM created_at) * 1000)::BIGINT AS created_at_ms,
    CASE
        WHEN handled_at IS NULL THEN NULL
        ELSE (EXTRACT(EPOCH FROM handled_at) * 1000)::BIGINT
    END AS handled_at_ms
FROM friend_requests
WHERE request_id = $1
LIMIT 1
)SQL";

constexpr auto kListIncomingFriendRequestsSql = R"SQL(
SELECT
    fr.request_id,
    fr.requester_id,
    fr.target_id,
    fr.request_message,
    fr.status,
    (EXTRACT(EPOCH FROM fr.created_at) * 1000)::BIGINT AS created_at_ms,
    CASE
        WHEN fr.handled_at IS NULL THEN NULL
        ELSE (EXTRACT(EPOCH FROM fr.handled_at) * 1000)::BIGINT
    END AS handled_at_ms,
    u.user_id AS peer_user_id,
    u.account AS peer_account,
    u.nickname AS peer_nickname,
    u.avatar_url AS peer_avatar_url
FROM friend_requests fr
JOIN users u ON u.user_id = fr.requester_id
WHERE fr.target_id = $1
ORDER BY fr.created_at DESC
)SQL";

constexpr auto kListOutgoingFriendRequestsSql = R"SQL(
SELECT
    fr.request_id,
    fr.requester_id,
    fr.target_id,
    fr.request_message,
    fr.status,
    (EXTRACT(EPOCH FROM fr.created_at) * 1000)::BIGINT AS created_at_ms,
    CASE
        WHEN fr.handled_at IS NULL THEN NULL
        ELSE (EXTRACT(EPOCH FROM fr.handled_at) * 1000)::BIGINT
    END AS handled_at_ms,
    u.user_id AS peer_user_id,
    u.account AS peer_account,
    u.nickname AS peer_nickname,
    u.avatar_url AS peer_avatar_url
FROM friend_requests fr
JOIN users u ON u.user_id = fr.target_id
WHERE fr.requester_id = $1
ORDER BY fr.created_at DESC
)SQL";

constexpr auto kListFriendsSql = R"SQL(
SELECT
    f.user_id,
    f.friend_user_id,
    (EXTRACT(EPOCH FROM f.created_at) * 1000)::BIGINT AS created_at_ms,
    u.user_id AS peer_user_id,
    u.account AS peer_account,
    u.nickname AS peer_nickname,
    u.avatar_url AS peer_avatar_url
FROM friendships f
JOIN users u ON u.user_id = f.friend_user_id
WHERE f.user_id = $1
ORDER BY f.created_at DESC, f.friend_user_id ASC
)SQL";

constexpr auto kAcceptFriendRequestSql = R"SQL(
WITH updated_request AS (
    UPDATE friend_requests
    SET
        status = 'accepted',
        handled_at = NOW()
    WHERE request_id = $1
      AND target_id = $2
      AND status = 'pending'
    RETURNING request_id, requester_id, target_id
), inserted_friendships AS (
    INSERT INTO friendships (
        user_id,
        friend_user_id,
        created_by_request_id
    )
    SELECT requester_id, target_id, request_id FROM updated_request
    UNION ALL
    SELECT target_id, requester_id, request_id FROM updated_request
    ON CONFLICT (user_id, friend_user_id) DO NOTHING
)
SELECT COUNT(*)::BIGINT AS updated_count
FROM updated_request
)SQL";

constexpr auto kRejectFriendRequestSql = R"SQL(
WITH updated_request AS (
    UPDATE friend_requests
    SET
        status = 'rejected',
        handled_at = NOW()
    WHERE request_id = $1
      AND target_id = $2
      AND status = 'pending'
    RETURNING request_id
)
SELECT COUNT(*)::BIGINT AS updated_count
FROM updated_request
)SQL";

bool isPendingFriendRequestConflict(
    const drogon::orm::DrogonDbException &exception)
{
    const auto *uniqueViolation =
        dynamic_cast<const drogon::orm::UniqueViolation *>(&exception.base());
    if (uniqueViolation != nullptr)
    {
        return true;
    }

    const auto *sqlError =
        dynamic_cast<const drogon::orm::SqlError *>(&exception.base());
    if (sqlError != nullptr && sqlError->sqlState() == "23505")
    {
        return true;
    }

    const std::string_view message = exception.base().what();
    return message.find("ux_friend_requests_pair_pending") !=
           std::string_view::npos;
}

FriendRequestRecord toFriendRequestRecord(const drogon::orm::Row &row)
{
    FriendRequestRecord record;
    record.requestId = row["request_id"].as<std::string>();
    record.requesterId = row["requester_id"].as<std::string>();
    record.targetId = row["target_id"].as<std::string>();
    if (!row["request_message"].isNull())
    {
        record.requestMessage = row["request_message"].as<std::string>();
    }
    record.status = row["status"].as<std::string>();
    record.createdAtMs = row["created_at_ms"].as<std::int64_t>();
    if (!row["handled_at_ms"].isNull())
    {
        record.handledAtMs = row["handled_at_ms"].as<std::int64_t>();
    }
    return record;
}

std::vector<FriendRequestListItemRecord> toFriendRequestList(
    const drogon::orm::Result &rows)
{
    std::vector<FriendRequestListItemRecord> items;
    items.reserve(rows.size());

    for (const auto &row : rows)
    {
        FriendRequestListItemRecord item;
        item.request = toFriendRequestRecord(row);
        item.peerUser.userId = row["peer_user_id"].as<std::string>();
        item.peerUser.account = row["peer_account"].as<std::string>();
        item.peerUser.nickname = row["peer_nickname"].as<std::string>();
        if (!row["peer_avatar_url"].isNull())
        {
            item.peerUser.avatarUrl =
                row["peer_avatar_url"].as<std::string>();
        }
        items.push_back(std::move(item));
    }

    return items;
}

std::vector<FriendListItemRecord> toFriendList(const drogon::orm::Result &rows)
{
    std::vector<FriendListItemRecord> items;
    items.reserve(rows.size());

    for (const auto &row : rows)
    {
        FriendListItemRecord item;
        item.createdAtMs = row["created_at_ms"].as<std::int64_t>();
        item.user.userId = row["peer_user_id"].as<std::string>();
        item.user.account = row["peer_account"].as<std::string>();
        item.user.nickname = row["peer_nickname"].as<std::string>();
        if (!row["peer_avatar_url"].isNull())
        {
            item.user.avatarUrl = row["peer_avatar_url"].as<std::string>();
        }
        items.push_back(std::move(item));
    }

    return items;
}

}  // namespace

void FriendRepository::hasFriendship(std::string userId,
                                     std::string friendUserId,
                                     CheckFriendshipSuccess &&onSuccess,
                                     RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kCheckFriendshipSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                onSuccess(!rows.empty());
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(userId),
            std::move(friendUserId));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void FriendRepository::createFriendRequest(
    CreateFriendRequestParams params,
    CreateFriendRequestSuccess &&onSuccess,
    CreateFriendRequestFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        auto sharedFailure =
            std::make_shared<CreateFriendRequestFailure>(std::move(onFailure));

        client->execSqlAsync(
            kInsertFriendRequestSql,
            [onSuccess = std::move(onSuccess), sharedFailure](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    (*sharedFailure)(CreateFriendRequestError{
                        CreateFriendRequestErrorKind::kDatabaseError,
                        "insert friend_requests returned no rows",
                    });
                    return;
                }

                onSuccess(toFriendRequestRecord(rows[0]));
            },
            [sharedFailure](const drogon::orm::DrogonDbException &exception) mutable {
                CreateFriendRequestError error;
                if (isPendingFriendRequestConflict(exception))
                {
                    error.kind =
                        CreateFriendRequestErrorKind::kPendingRequestAlreadyExists;
                    error.message = exception.base().what();
                }
                else
                {
                    error.kind = CreateFriendRequestErrorKind::kDatabaseError;
                    error.message = exception.base().what();
                }

                (*sharedFailure)(std::move(error));
            },
            params.requestId,
            params.requesterId,
            params.targetId,
            params.requestMessage);
    }
    catch (const std::exception &exception)
    {
        onFailure(CreateFriendRequestError{
            CreateFriendRequestErrorKind::kDatabaseError,
            exception.what(),
        });
    }
}

void FriendRepository::findFriendRequestById(
    std::string requestId,
    FindFriendRequestSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kFindFriendRequestByIdSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    onSuccess(std::nullopt);
                    return;
                }

                onSuccess(toFriendRequestRecord(rows[0]));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(requestId));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void FriendRepository::listIncomingFriendRequests(
    std::string targetUserId,
    ListFriendRequestsSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kListIncomingFriendRequestsSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                onSuccess(toFriendRequestList(rows));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(targetUserId));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void FriendRepository::listOutgoingFriendRequests(
    std::string requesterUserId,
    ListFriendRequestsSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kListOutgoingFriendRequestsSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                onSuccess(toFriendRequestList(rows));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            std::move(requesterUserId));
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void FriendRepository::listFriends(std::string userId,
                                   ListFriendsSuccess &&onSuccess,
                                   RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kListFriendsSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                onSuccess(toFriendList(rows));
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

void FriendRepository::acceptFriendRequest(
    HandleFriendRequestParams params,
    HandleFriendRequestSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kAcceptFriendRequestSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                UpdateFriendRequestResult result;
                if (rows.empty() ||
                    rows[0]["updated_count"].as<std::int64_t>() == 0)
                {
                    result.status =
                        UpdateFriendRequestStatus::kRequestNotPending;
                }
                onSuccess(std::move(result));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            params.requestId,
            params.targetUserId);
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

void FriendRepository::rejectFriendRequest(
    HandleFriendRequestParams params,
    HandleFriendRequestSuccess &&onSuccess,
    RepositoryFailure &&onFailure) const
{
    try
    {
        auto client = dbClient();
        client->execSqlAsync(
            kRejectFriendRequestSql,
            [onSuccess = std::move(onSuccess)](
                const drogon::orm::Result &rows) mutable {
                UpdateFriendRequestResult result;
                if (rows.empty() ||
                    rows[0]["updated_count"].as<std::int64_t>() == 0)
                {
                    result.status =
                        UpdateFriendRequestStatus::kRequestNotPending;
                }
                onSuccess(std::move(result));
            },
            [onFailure = std::move(onFailure)](
                const drogon::orm::DrogonDbException &exception) mutable {
                onFailure(exception.base().what());
            },
            params.requestId,
            params.targetUserId);
    }
    catch (const std::exception &exception)
    {
        onFailure(exception.what());
    }
}

drogon::orm::DbClientPtr FriendRepository::dbClient() const
{
    return drogon::app().getDbClient();
}

}  // namespace chatserver::repository

# 好友域 HTTP 接口文档

最近同步时间：2026-03-20

## 1. 文档目的

这份文档只记录当前已经真实落地的好友域 HTTP 接口协议，覆盖：

- 按账号搜索用户
- 查询好友列表
- 发送好友申请
- 查询收到 / 发出的好友申请
- 同意 / 拒绝好友申请

当前已落地接口：

- `GET /api/v1/users/search`
- `GET /api/v1/friends`
- `POST /api/v1/friends/requests`
- `GET /api/v1/friends/requests/incoming`
- `GET /api/v1/friends/requests/outgoing`
- `POST /api/v1/friends/requests/{request_id}/accept`
- `POST /api/v1/friends/requests/{request_id}/reject`

## 2. 通用约定

### 2.1 请求头

- `Authorization: Bearer <access_token>`
  - 必填
  - 作用：标识当前登录用户
- `Content-Type: application/json`
  - 仅 `POST` JSON 接口需要
- `X-Request-Id`
  - 可选
  - 作用：请求追踪 ID；若客户端传入，服务端会原样回显

### 2.2 响应结构

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_xxx",
  "data": {}
}
```

字段说明：

- `code`
  - 类型：`integer`
  - 作用：业务码，`0` 表示成功
- `message`
  - 类型：`string`
  - 作用：结果说明
- `request_id`
  - 类型：`string`
  - 作用：请求追踪 ID
- `data`
  - 类型：`object`
  - 作用：业务数据载荷；失败时当前统一返回空对象

### 2.3 用户资料结构

以下字段会在好友搜索、好友列表和好友申请返回中重复出现：

- `user_id`
- `account`
- `nickname`
- `avatar_url`

### 2.4 好友申请结构

以下字段会在发送好友申请、查询收件箱、查询发件箱、同意、拒绝接口中重复出现：

- `request_id`
  - 类型：`string`
  - 作用：好友申请 ID
- `peer_user`
  - 类型：`object`
  - 作用：和当前用户相对的对端用户资料
  - 字段：见“2.3 用户资料结构”
- `request_message`
  - 类型：`string`
  - 作用：好友申请附言
  - 可选：是
- `status`
  - 类型：`string`
  - 作用：申请状态
  - 当前可能值：
    - `pending`
    - `accepted`
    - `rejected`
- `created_at_ms`
  - 类型：`int64`
  - 作用：申请创建时间
- `handled_at_ms`
  - 类型：`int64`
  - 作用：申请处理时间
  - 可选：是

## 3. 按账号搜索用户

### 3.1 接口信息

- 方法：`GET`
- 路径：`/api/v1/users/search`
- 作用：按账号精确搜索一个用户是否存在

### 3.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `X-Request-Id`：可选

查询参数：

- `account`
  - 类型：`string`
  - 必填：是
  - 作用：要搜索的目标账号

### 3.3 返回字段

成功时返回：

- `data.exists`
  - 类型：`boolean`
  - 作用：目标账号是否存在
- `data.user`
  - 类型：`object`
  - 作用：当 `exists = true` 时返回命中的用户资料
  - 字段：见“2.3 用户资料结构”

### 3.4 成功示例

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_search_user_001",
  "data": {
    "exists": true,
    "user": {
      "user_id": "u_1001",
      "account": "alice_01",
      "nickname": "Alice",
      "avatar_url": "avatars/2026/03/19/avatar_xxx.png"
    }
  }
}
```

### 3.5 测试用例

#### 用例 1：搜索存在的账号

```bash
curl -i -sS "http://127.0.0.1:8848/api/v1/users/search?account=alice_01" \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_friend_search_001'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.exists = true`

#### 用例 2：缺少 account 参数

```bash
curl -i -sS "http://127.0.0.1:8848/api/v1/users/search" \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_friend_search_002'
```

预期结果：

- HTTP 状态码：`400 Bad Request`
- `code = 40001`

## 4. 查询我的好友列表

### 4.1 接口信息

- 方法：`GET`
- 路径：`/api/v1/friends`
- 作用：返回当前登录用户的正式好友列表

### 4.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `X-Request-Id`：可选

请求参数：

- 无

### 4.3 返回字段

成功时返回：

- `data.friends`
  - 类型：`array`
  - 作用：好友列表数组
  - 每个元素字段：
    - `user`
      - 类型：`object`
      - 作用：好友基础资料
      - 字段：见“2.3 用户资料结构”
    - `created_at_ms`
      - 类型：`int64`
      - 作用：好友关系建立时间

### 4.4 测试用例

#### 用例 1：查询好友列表成功

```bash
curl -i -sS http://127.0.0.1:8848/api/v1/friends \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_friend_list_001'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.friends` 为数组

#### 用例 2：token 非法

```bash
curl -i -sS http://127.0.0.1:8848/api/v1/friends \
  -H 'Authorization: Bearer invalid_token' \
  -H 'X-Request-Id: req_friend_list_002'
```

预期结果：

- HTTP 状态码：`401 Unauthorized`
- `code = 40102`

## 5. 发送好友申请

### 5.1 接口信息

- 方法：`POST`
- 路径：`/api/v1/friends/requests`
- 作用：向目标用户发起好友申请

### 5.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `Content-Type: application/json`
- `X-Request-Id`：可选

请求体示例：

```json
{
  "target_user_id": "u_2001",
  "request_message": "你好，我想加你为好友"
}
```

请求体字段说明：

- `target_user_id`
  - 类型：`string`
  - 必填：是
  - 作用：本次申请的目标用户 ID
- `request_message`
  - 类型：`string`
  - 必填：否
  - 作用：好友申请附言
  - 当前最长：`200`

### 5.3 返回字段

成功时返回：

- `data.request`
  - 类型：`object`
  - 作用：新创建的好友申请
  - 字段：见“2.4 好友申请结构”

### 5.4 成功示例

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_send_friend_001",
  "data": {
    "request": {
      "request_id": "fr_1001",
      "peer_user": {
        "user_id": "u_2001",
        "account": "bob_01",
        "nickname": "Bob"
      },
      "request_message": "你好，我想加你为好友",
      "status": "pending",
      "created_at_ms": 1773896508796
    }
  }
}
```

### 5.5 测试用例

#### 用例 1：发送好友申请成功

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/friends/requests \
  -H 'Authorization: Bearer access_xxx' \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_friend_request_001' \
  -d '{
    "target_user_id":"u_2001",
    "request_message":"你好，我想加你为好友"
  }'
```

预期结果：

- HTTP 状态码：`201 Created`
- `code = 0`
- `data.request.status = "pending"`

#### 用例 2：重复申请

再次向同一个目标发起申请。

预期结果：

- HTTP 状态码：`409 Conflict`
- `code = 40904`

## 6. 查询我收到的好友申请

### 6.1 接口信息

- 方法：`GET`
- 路径：`/api/v1/friends/requests/incoming`
- 作用：查询当前用户收到的好友申请列表

### 6.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `X-Request-Id`：可选

请求参数：

- 无

### 6.3 返回字段

成功时返回：

- `data.requests`
  - 类型：`array`
  - 作用：收到的好友申请数组
  - 每个元素字段：见“2.4 好友申请结构”

### 6.4 测试用例

#### 用例 1：查询收到的好友申请

```bash
curl -i -sS http://127.0.0.1:8848/api/v1/friends/requests/incoming \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_friend_incoming_001'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.requests` 为数组

## 7. 查询我发出的好友申请

### 7.1 接口信息

- 方法：`GET`
- 路径：`/api/v1/friends/requests/outgoing`
- 作用：查询当前用户发出的好友申请列表

### 7.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `X-Request-Id`：可选

请求参数：

- 无

### 7.3 返回字段

成功时返回：

- `data.requests`
  - 类型：`array`
  - 作用：发出的好友申请数组
  - 每个元素字段：见“2.4 好友申请结构”

### 7.4 测试用例

#### 用例 1：查询发出的好友申请

```bash
curl -i -sS http://127.0.0.1:8848/api/v1/friends/requests/outgoing \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_friend_outgoing_001'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.requests` 为数组

## 8. 同意好友申请

### 8.1 接口信息

- 方法：`POST`
- 路径：`/api/v1/friends/requests/{request_id}/accept`
- 作用：同意一条发给当前用户的好友申请

### 8.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `X-Request-Id`：可选

路径参数：

- `request_id`
  - 类型：`string`
  - 必填：是
  - 作用：要同意的好友申请 ID

请求体：

- 无

### 8.3 返回字段

成功时返回：

- `data.request`
  - 类型：`object`
  - 作用：处理后的好友申请
  - 字段：见“2.4 好友申请结构”
  - 当前成功返回：
    - `status = "accepted"`
    - `handled_at_ms` 有值

### 8.4 测试用例

#### 用例 1：同意好友申请成功

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/friends/requests/fr_1001/accept \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_friend_accept_001'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.request.status = "accepted"`

#### 用例 2：重复同意

对同一条申请再次执行同意。

预期结果：

- HTTP 状态码：`409 Conflict`
- `code = 40905`

## 9. 拒绝好友申请

### 9.1 接口信息

- 方法：`POST`
- 路径：`/api/v1/friends/requests/{request_id}/reject`
- 作用：拒绝一条发给当前用户的好友申请

### 9.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `X-Request-Id`：可选

路径参数：

- `request_id`
  - 类型：`string`
  - 必填：是
  - 作用：要拒绝的好友申请 ID

请求体：

- 无

### 9.3 返回字段

成功时返回：

- `data.request`
  - 类型：`object`
  - 作用：处理后的好友申请
  - 字段：见“2.4 好友申请结构”
  - 当前成功返回：
    - `status = "rejected"`
    - `handled_at_ms` 有值

### 9.4 测试用例

#### 用例 1：拒绝好友申请成功

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/friends/requests/fr_1002/reject \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_friend_reject_001'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.request.status = "rejected"`

## 10. 当前主要错误码

- `40001 invalid argument`
  - 常见原因：`account` 为空、`target_user_id` 非法、`request_message` 过长
- `40102 invalid access token`
  - 常见原因：未带 `Authorization`、不是 Bearer 格式、token 非法
- `40300 forbidden`
  - 常见原因：试图处理不属于自己的好友申请
- `40400 resource not found`
  - 常见原因：目标用户不存在、好友申请不存在
- `40903 friend already exists`
  - 常见原因：目标用户已经是当前用户好友
- `40904 friend request already pending`
  - 常见原因：双方之间已经存在待处理好友申请
- `40905 friend request already handled`
  - 常见原因：同一条申请被重复同意或拒绝

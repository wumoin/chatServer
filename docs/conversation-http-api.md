# 会话域 HTTP 接口文档

最近同步时间：2026-03-20

## 1. 文档目的

这份文档只记录当前已经真实落地的会话域 HTTP 接口协议，重点覆盖：

- 创建或复用私聊
- 查询会话列表
- 查询会话详情
- 查询历史消息
- 发送文本消息

当前已落地接口：

- `POST /api/v1/conversations/private`
- `GET /api/v1/conversations`
- `GET /api/v1/conversations/{conversation_id}`
- `GET /api/v1/conversations/{conversation_id}/messages`
- `POST /api/v1/conversations/{conversation_id}/messages/text`

## 2. 通用约定

### 2.1 路径前缀

- 当前会话域接口统一走 `/api/v1/conversations/...`

### 2.2 请求头

- `Authorization: Bearer <access_token>`
  - 必填
  - 作用：标识当前登录用户
- `Content-Type: application/json`
  - 仅 `POST` JSON 接口需要
- `X-Request-Id`
  - 可选
  - 作用：请求追踪 ID；若客户端传入，服务端会原样回显

### 2.3 响应结构

当前会话域接口统一返回：

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
  - 作用：当前请求结果说明
- `request_id`
  - 类型：`string`
  - 作用：请求追踪 ID
- `data`
  - 类型：`object`
  - 作用：业务数据载荷；失败时当前统一返回空对象

### 2.4 会话摘要结构

以下字段会在“创建私聊”“查询会话列表”“查询会话详情”的返回里重复出现：

- `conversation_id`
  - 类型：`string`
  - 作用：会话唯一标识
- `conversation_type`
  - 类型：`string`
  - 作用：会话类型
  - 当前固定：`direct`
- `peer_user`
  - 类型：`object`
  - 作用：当前私聊会话的对端用户资料
  - 字段：
    - `user_id`
    - `account`
    - `nickname`
    - `avatar_url`
- `last_message_seq`
  - 类型：`int64`
  - 作用：当前会话最后一条消息顺序号
- `last_read_seq`
  - 类型：`int64`
  - 作用：当前登录用户最后已读到的消息顺序号
- `unread_count`
  - 类型：`int64`
  - 作用：当前登录用户在该会话里的未读数
- `last_message_preview`
  - 类型：`string`
  - 作用：最后一条消息的摘要文案
  - 可选：是
- `last_message_at_ms`
  - 类型：`int64`
  - 作用：最后一条消息时间
  - 可选：是
- `created_at_ms`
  - 类型：`int64`
  - 作用：会话创建时间

## 3. 创建或复用一对一私聊会话

### 3.1 接口信息

- 方法：`POST`
- 路径：`/api/v1/conversations/private`
- 作用：与一个已经成为好友的目标用户创建或复用一条私聊会话

### 3.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `Content-Type: application/json`
- `X-Request-Id`：可选

请求体示例：

```json
{
  "peer_user_id": "u_1002"
}
```

请求体字段说明：

- `peer_user_id`
  - 类型：`string`
  - 必填：是
  - 作用：私聊对端用户 ID

### 3.3 返回字段

成功时返回：

- `data.conversation`
  - 类型：`object`
  - 作用：创建或复用后的会话摘要
  - 字段：见“2.4 会话摘要结构”

### 3.4 成功示例

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_create_direct_001",
  "data": {
    "conversation": {
      "conversation_id": "c_2001",
      "conversation_type": "direct",
      "peer_user": {
        "user_id": "u_1002",
        "account": "bob_01",
        "nickname": "Bob",
        "avatar_url": "avatars/2026/03/19/avatar_bob.png"
      },
      "last_message_seq": 0,
      "last_read_seq": 0,
      "unread_count": 0,
      "created_at_ms": 1773920000000
    }
  }
}
```

### 3.5 测试用例

#### 用例 1：创建或复用私聊成功

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/conversations/private \
  -H 'Authorization: Bearer access_xxx' \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_conv_private_001' \
  -d '{
    "peer_user_id":"u_1002"
  }'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.conversation.conversation_id` 有值

#### 用例 2：目标用户不是好友或不存在

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/conversations/private \
  -H 'Authorization: Bearer access_xxx' \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_conv_private_002' \
  -d '{
    "peer_user_id":"u_not_exists"
  }'
```

预期结果：

- HTTP 状态码：`404 Not Found` 或 `403 Forbidden`
- `code != 0`

## 4. 查询当前用户的会话列表

### 4.1 接口信息

- 方法：`GET`
- 路径：`/api/v1/conversations`
- 作用：返回当前登录用户的会话摘要列表

### 4.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `X-Request-Id`：可选

请求参数：

- 无

### 4.3 返回字段

成功时返回：

- `data.conversations`
  - 类型：`array`
  - 作用：当前用户的会话摘要数组
  - 每个元素字段：见“2.4 会话摘要结构”

### 4.4 测试用例

#### 用例 1：查询会话列表成功

```bash
curl -i -sS http://127.0.0.1:8848/api/v1/conversations \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_conv_list_001'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.conversations` 为数组

#### 用例 2：access token 非法

```bash
curl -i -sS http://127.0.0.1:8848/api/v1/conversations \
  -H 'Authorization: Bearer invalid_token' \
  -H 'X-Request-Id: req_conv_list_002'
```

预期结果：

- HTTP 状态码：`401 Unauthorized`
- `code = 40102`

## 5. 查询指定会话详情

### 5.1 接口信息

- 方法：`GET`
- 路径：`/api/v1/conversations/{conversation_id}`
- 作用：返回指定会话的完整摘要和当前用户成员状态

### 5.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `X-Request-Id`：可选

路径参数：

- `conversation_id`
  - 类型：`string`
  - 必填：是
  - 作用：目标会话 ID

### 5.3 返回字段

成功时返回：

- `data.conversation`
  - 类型：`object`
  - 作用：目标会话详情
  - 公共摘要字段：见“2.4 会话摘要结构”
  - 额外字段：
    - `my_member`
      - 类型：`object`
      - 作用：当前登录用户在该会话中的成员状态
      - 字段：
        - `user_id`
        - `member_role`
        - `joined_at_ms`
        - `last_read_seq`
        - `last_read_at_ms`（可选）

### 5.4 成功示例

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_conversation_detail_001",
  "data": {
    "conversation": {
      "conversation_id": "c_2001",
      "conversation_type": "direct",
      "peer_user": {
        "user_id": "u_1002",
        "account": "bob_01",
        "nickname": "Bob",
        "avatar_url": "avatars/2026/03/19/avatar_bob.png"
      },
      "my_member": {
        "user_id": "u_1001",
        "member_role": "member",
        "joined_at_ms": 1773920000000,
        "last_read_seq": 12,
        "last_read_at_ms": 1773920123456
      },
      "last_message_seq": 15,
      "unread_count": 3,
      "last_message_preview": "hello conversation",
      "last_message_at_ms": 1773920130000,
      "created_at_ms": 1773920000000
    }
  }
}
```

### 5.5 测试用例

#### 用例 1：查询会话详情成功

```bash
curl -i -sS http://127.0.0.1:8848/api/v1/conversations/c_2001 \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_conv_detail_001'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.conversation.conversation_id = "c_2001"`

#### 用例 2：会话不存在或当前用户不属于该会话

```bash
curl -i -sS http://127.0.0.1:8848/api/v1/conversations/c_not_exists \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_conv_detail_002'
```

预期结果：

- HTTP 状态码：`404 Not Found` 或 `403 Forbidden`
- `code != 0`

## 6. 查询指定会话的历史消息

### 6.1 接口信息

- 方法：`GET`
- 路径：`/api/v1/conversations/{conversation_id}/messages`
- 作用：按会话和分页游标查询历史消息

### 6.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `X-Request-Id`：可选

路径参数：

- `conversation_id`
  - 类型：`string`
  - 必填：是
  - 作用：目标会话 ID

查询参数：

- `limit`
  - 类型：`int`
  - 必填：否
  - 默认：`50`
  - 当前范围：`1 ~ 100`
  - 作用：本次返回的消息条数
- `before_seq`
  - 类型：`int64`
  - 必填：否
  - 作用：向前翻页游标，返回 `seq < before_seq` 的消息
- `after_seq`
  - 类型：`int64`
  - 必填：否
  - 作用：增量补拉游标，返回 `seq > after_seq` 的消息

约束：

- `before_seq` 和 `after_seq` 不能同时传
- 不传游标时返回最新一页

### 6.3 返回字段

成功时返回：

- `data.items`
  - 类型：`array`
  - 作用：本次分页返回的消息数组
  - 每个元素字段：
    - `message_id`
    - `conversation_id`
    - `seq`
    - `sender_id`
    - `client_message_id`
    - `type`
    - `content`
    - `created_at_ms`
- `data.has_more`
  - 类型：`boolean`
  - 作用：当前方向是否还有更多消息
- `data.next_before_seq`
  - 类型：`int64`
  - 作用：下一次继续向前翻页时应带的游标
  - 可选：是
- `data.next_after_seq`
  - 类型：`int64`
  - 作用：下一次继续增量补拉时应带的游标
  - 可选：是

当前消息内容字段说明：

- `type`
  - 当前只支持：`text`
- `content.text`
  - 类型：`string`
  - 作用：文本消息正文

### 6.4 成功示例

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_history_001",
  "data": {
    "items": [
      {
        "message_id": "m_3001",
        "conversation_id": "c_2001",
        "seq": 101,
        "sender_id": "u_1001",
        "client_message_id": "client_msg_001",
        "type": "text",
        "content": {
          "text": "hello conversation"
        },
        "created_at_ms": 1773920006123
      }
    ],
    "has_more": false,
    "next_before_seq": 101
  }
}
```

### 6.5 测试用例

#### 用例 1：查询最新一页历史消息

```bash
curl -i -sS "http://127.0.0.1:8848/api/v1/conversations/c_2001/messages?limit=20" \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_conv_history_001'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.items` 为数组

#### 用例 2：参数冲突

```bash
curl -i -sS "http://127.0.0.1:8848/api/v1/conversations/c_2001/messages?before_seq=10&after_seq=20" \
  -H 'Authorization: Bearer access_xxx' \
  -H 'X-Request-Id: req_conv_history_002'
```

预期结果：

- HTTP 状态码：`400 Bad Request`
- `code = 40001`

## 7. 发送文本消息

### 7.1 接口信息

- 方法：`POST`
- 路径：`/api/v1/conversations/{conversation_id}/messages/text`
- 作用：通过 HTTP 向指定会话发送一条文本消息

### 7.2 请求字段

请求头：

- `Authorization: Bearer <access_token>`
- `Content-Type: application/json`
- `X-Request-Id`：可选

路径参数：

- `conversation_id`
  - 类型：`string`
  - 必填：是
  - 作用：目标会话 ID

请求体示例：

```json
{
  "text": "hello conversation",
  "client_message_id": "client_msg_001"
}
```

请求体字段说明：

- `text`
  - 类型：`string`
  - 必填：是
  - 作用：本次发送的文本内容
  - 当前限制：不能为空，最长 `4000`
- `client_message_id`
  - 类型：`string`
  - 必填：否
  - 作用：客户端本地消息 ID

### 7.3 返回字段

成功时返回：

- `data.message`
  - 类型：`object`
  - 作用：服务端确认后的正式消息
  - 字段：
    - `message_id`
    - `conversation_id`
    - `seq`
    - `sender_id`
    - `client_message_id`（可选）
    - `type`
    - `content.text`
    - `created_at_ms`

### 7.4 成功示例

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_send_text_001",
  "data": {
    "message": {
      "message_id": "m_3001",
      "conversation_id": "c_2001",
      "seq": 101,
      "sender_id": "u_1001",
      "client_message_id": "client_msg_001",
      "type": "text",
      "content": {
        "text": "hello conversation"
      },
      "created_at_ms": 1773920006123
    }
  }
}
```

### 7.5 测试用例

#### 用例 1：发送文本消息成功

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/conversations/c_2001/messages/text \
  -H 'Authorization: Bearer access_xxx' \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_conv_send_text_001' \
  -d '{
    "text":"hello conversation",
    "client_message_id":"client_msg_001"
  }'
```

预期结果：

- HTTP 状态码：`200 OK`
- `code = 0`
- `data.message.seq > 0`

#### 用例 2：发送空消息

```bash
curl -i -sS -X POST http://127.0.0.1:8848/api/v1/conversations/c_2001/messages/text \
  -H 'Authorization: Bearer access_xxx' \
  -H 'Content-Type: application/json' \
  -H 'X-Request-Id: req_conv_send_text_002' \
  -d '{
    "text":"   "
  }'
```

预期结果：

- HTTP 状态码：`400 Bad Request`
- `code = 40001`

## 8. 当前主要错误码

- `40001 invalid argument`
  - 常见原因：请求体字段缺失、消息内容为空、分页参数冲突
- `40102 invalid access token`
  - 常见原因：未带 `Authorization`、token 非法或已过期
- `40300 forbidden`
  - 常见原因：当前用户不属于目标会话
- `40400 resource not found`
  - 常见原因：会话不存在
- `50000 internal error`
  - 常见原因：查询会话失败、写消息失败

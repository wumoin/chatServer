# WebSocket 事件接口文档

最近同步时间：2026-03-21

## 1. 文档目的

这份文档专门说明当前项目 WebSocket 事件怎么发、怎么回，以及字段分别代表什么。

它和另外两份 WebSocket 文档的分工如下：

- `docs/ws-protocol.md`
  - 定义统一信封、统一事件类型和公共规则
- `docs/ws-architecture.md`
  - 定义服务端 / 客户端模块结构和职责边界
- `docs/ws-api.md`
  - 定义具体事件的请求结构、响应结构和字段说明

### 1.1 当前已落地 WS API 一览

为了避免读者一上来就陷进长字段说明里，这里先单独给出“当前哪些 WS API 已经真实可用”的总览。

当前已经可用的顶层事件：

- `ws.auth`
  - 客户端发起 WebSocket 连接鉴权
- `ws.auth.ok`
  - 服务端确认当前连接已完成鉴权
- `ws.error`
  - 服务端返回统一错误
- `ws.send`
  - 客户端发起业务动作
- `ws.ack`
  - 服务端确认某次 `ws.send` 的处理结果
- `ws.new`
  - 服务端主动推送新的业务事件

当前已经真实接入的业务 `route`：

- 连接鉴权类：
  - `ws.auth`
  - `ws.auth.ok`
  - `ws.error`
- 消息发送类：
  - `ws.send + route=message.send_text`
  - `ws.ack + route=message.send_text`
  - `ws.send + route=message.send_image`
  - `ws.ack + route=message.send_image`
  - `ws.send + route=message.send_file`
  - `ws.ack + route=message.send_file`
  - `ws.new + route=message.created`
- HTTP 成功链路触发的推送类：
  - `ws.new + route=conversation.created`
  - `ws.new + route=friend.request.new`
  - `ws.new + route=friend.request.accepted`
  - `ws.new + route=friend.request.rejected`

当前还没有作为 WS 输入接口落地的业务：

- `ws.send + route=conversation.create_private`
- `ws.send + route=friend.request.create`
- `ws.send + route=friend.request.accept`
- `ws.send + route=friend.request.reject`

阅读建议：

- 如果你想先知道“现在到底能调哪些事件”，先看这一节
- 如果你想继续查字段细节和示例，再往下看各事件章节

### 1.2 当前实现状态

- 服务端已经能够识别并解析 `ws.send`
- 当前若 `route` 尚未接入业务处理，会返回 `ws.ack` 失败结果
- 服务端已新增统一推送服务层，用于后续发送 `ws.ack / ws.new`
- 当前已接入首批由 HTTP 成功链路触发的 `ws.new` 路由：
  - `friend.request.new`
  - `friend.request.accepted`
  - `friend.request.rejected`
  - `conversation.created`
- 当前已接入消息发送 WebSocket 业务路由：
  - `ws.send + route=message.send_text`
  - `ws.ack + route=message.send_text`
  - `ws.send + route=message.send_image`
  - `ws.ack + route=message.send_image`
  - `ws.send + route=message.send_file`
  - `ws.ack + route=message.send_file`
  - `ws.new + route=message.created`

## 2. 连接信息

当前服务端 WebSocket 默认连接信息：

- 路径：`/ws`
- 本地开发完整地址：`ws://127.0.0.1:8848/ws`

接入前置条件：

- 客户端必须已经完成 HTTP 登录
- 客户端本地必须已经拿到：
  - `access_token`
  - `device_session_id`
  - 稳定的 `device_id`

## 3. 统一信封

当前所有 WebSocket JSON 文本消息，都统一使用如下结构：

```json
{
  "version": 1,
  "type": "ws.send",
  "request_id": "req_ws_001",
  "ts_ms": 1770000000000,
  "payload": {}
}
```

顶层字段含义：

- `version`
  - 类型：`integer`
  - 作用：协议版本号
  - 当前固定为 `1`
- `type`
  - 类型：`string`
  - 作用：顶层事件类型
  - 当前使用：
    - `ws.auth`
    - `ws.auth.ok`
    - `ws.error`
    - `ws.send`
    - `ws.ack`
    - `ws.new`
- `request_id`
  - 类型：`string`
  - 作用：请求和响应之间的关联标识
- `ts_ms`
  - 类型：`integer`
  - 作用：发送该消息时的毫秒时间戳
- `payload`
  - 类型：`object`
  - 作用：当前事件真正的业务字段

业务事件统一约定：

- `ws.send.payload.route`
  - 表示客户端在发起什么动作
- `ws.ack.payload.route`
  - 表示服务端在确认哪种动作
- `ws.new.payload.route`
  - 表示服务端在推送哪种新事件

## 4. `ws.auth`

### 4.1 作用

`ws.auth` 是客户端建立 WebSocket 连接后发送的第一条业务消息。

它的作用是：

- 证明这条连接属于一个已登录用户
- 把这条连接绑定到一个具体 `device_session`
- 为后续 `ws.send / ws.ack / ws.new` 建立权限基础

### 4.2 客户端发送结构

```json
{
  "version": 1,
  "type": "ws.auth",
  "request_id": "req_ws_auth_001",
  "ts_ms": 1770000000000,
  "payload": {
    "access_token": "access_xxx",
    "device_id": "pc_windows_001",
    "device_session_id": "ds_7001",
    "client_version": "0.1.0"
  }
}
```

### 4.3 发送字段说明

- `access_token`
  - 类型：`string`
  - 是否必填：是
  - 作用：HTTP 登录返回的访问令牌

- `device_id`
  - 类型：`string`
  - 是否必填：是
  - 作用：客户端本地持久化的设备标识

- `device_session_id`
  - 类型：`string`
  - 是否必填：是
  - 作用：HTTP 登录返回的设备会话 ID

- `client_version`
  - 类型：`string`
  - 是否必填：否
  - 作用：客户端版本号，主要用于日志和兼容性排查

### 4.4 服务端处理规则

服务端当前会做这些校验：

1. 校验消息外层统一信封是否合法
2. 校验 `access_token` 是否合法且未过期
3. 校验 token 中的 `device_session_id` 是否和请求体一致
4. 校验数据库中该 `device_session` 当前是否仍是 `active`
5. 校验该 `device_session` 的 `device_id` 是否和请求体一致

校验通过后，服务端会：

- 把当前连接绑定到 `user_id + device_session_id + device_id`
- 把连接注册到进程内在线连接注册表
- 返回 `ws.auth.ok`

## 5. `ws.auth.ok`

### 5.1 作用

`ws.auth.ok` 是服务端对 `ws.auth` 的成功确认。

### 5.2 服务端返回结构

```json
{
  "version": 1,
  "type": "ws.auth.ok",
  "request_id": "req_ws_auth_001",
  "ts_ms": 1770000000100,
  "payload": {
    "user_id": "u_1001",
    "device_session_id": "ds_7001"
  }
}
```

### 5.3 返回字段说明

- `user_id`
  - 类型：`string`
  - 作用：服务端最终识别出的当前用户 ID

- `device_session_id`
  - 类型：`string`
  - 作用：服务端确认绑定成功的设备会话 ID

## 6. `ws.error`

### 6.1 作用

`ws.error` 是当前统一的 WebSocket 错误回包事件。

当前主要用于：

- `ws.auth` 失败
- 外层信封非法
- 业务事件字段非法
- 未认证连接发送了业务事件
- 当前业务路由暂不支持

### 6.2 服务端返回结构

```json
{
  "version": 1,
  "type": "ws.error",
  "request_id": "req_ws_auth_001",
  "ts_ms": 1770000000100,
  "payload": {
    "code": 40102,
    "message": "invalid access token"
  }
}
```

### 6.3 返回字段说明

- `code`
  - 类型：`integer`
  - 作用：业务错误码
  - 当前尽量和 HTTP 错误码体系保持一致

- `message`
  - 类型：`string`
  - 作用：错误说明文本

## 7. `ws.send`

### 7.1 作用

`ws.send` 是客户端统一的业务动作发起事件。

后续客户端要通过 WS 发起的业务，都统一使用 `type = "ws.send"`，再由 `payload.route` 区分具体动作。

### 7.2 通用结构

```json
{
  "version": 1,
  "type": "ws.send",
  "request_id": "req_msg_001",
  "ts_ms": 1770000000000,
  "payload": {
    "route": "message.send_text",
    "data": {}
  }
}
```

### 7.3 字段说明

- `payload.route`
  - 类型：`string`
  - 是否必填：是
  - 作用：具体业务动作标识

- `payload.data`
  - 类型：`object`
  - 是否必填：是
  - 作用：该业务动作实际参数

补充说明：

- `ws.send` 只表达“我要发起什么动作”
- `ws.send` 本身不携带：
  - `ok`
  - `code`
  - `message`

### 7.4 当前输入 `route` 状态

当前已经接入的 `ws.send` 业务路由：

- `message.send_text`
- `message.send_image`
- `message.send_file`

当前已经预留命名、但还没有接入服务端处理的 `ws.send` 业务路由：

- `conversation.create_private`
- `friend.request.create`
- `friend.request.accept`
- `friend.request.reject`

## 8. `ws.ack`

### 8.1 作用

`ws.ack` 是服务端对某次 `ws.send` 的统一确认事件。

它不是广播消息，而是“对这次业务动作的处理结果说明”。

### 8.2 通用结构

```json
{
  "version": 1,
  "type": "ws.ack",
  "request_id": "req_msg_001",
  "ts_ms": 1770000000100,
  "payload": {
    "route": "message.send_text",
    "ok": true,
    "code": 0,
    "message": "ok",
    "data": {}
  }
}
```

### 8.3 字段说明

- `payload.route`
  - 类型：`string`
  - 是否必填：是
  - 作用：这次确认对应哪个业务动作

- `payload.ok`
  - 类型：`boolean`
  - 是否必填：是
  - 作用：本次动作是否成功

- `payload.code`
  - 类型：`integer`
  - 是否必填：是
  - 作用：业务错误码，成功时通常为 `0`

- `payload.message`
  - 类型：`string`
  - 是否必填：是
  - 作用：结果说明

- `payload.data`
  - 类型：`object`
  - 是否必填：否
  - 作用：业务返回值；失败时可以为空对象或直接省略

补充说明：

- `ws.ack` 是“对某次 `ws.send` 的统一确认”
- 它通常只回给发起动作的那条连接
- 它不是广播事件

## 9. `ws.new`

### 9.1 作用

`ws.new` 是服务端统一的主动推送事件。

它表示：

- 有新的业务事件需要通知客户端

### 9.2 通用结构

```json
{
  "version": 1,
  "type": "ws.new",
  "request_id": "",
  "ts_ms": 1770000000200,
  "payload": {
    "route": "message.created",
    "data": {}
  }
}
```

### 9.3 字段说明

- `payload.route`
  - 类型：`string`
  - 是否必填：是
  - 作用：本次主动推送是什么业务事件

- `payload.data`
  - 类型：`object`
  - 是否必填：是
  - 作用：该事件的实际内容

补充说明：

- `ws.new` 是服务端主动推送
- 它一般不带：
  - `ok`
  - `code`
  - `message`

### 9.4 当前推送 `route` 状态

当前已经接入的 `ws.new` 业务路由：

- `conversation.created`
- `message.created`
- `friend.request.new`
- `friend.request.accepted`
- `friend.request.rejected`

## 10. 路由详细说明

### 10.1 怎么阅读这一节

这里开始不再只讲 `ws.send / ws.ack / ws.new` 的通用外壳，而是逐个说明“不同 `route` 到底代表什么业务动作”。

阅读时先记住三点：

- 同一个字符串 `route`，会出现在不同顶层事件里：
  - `ws.send.payload.route`
    - 表示客户端正在发起什么动作
  - `ws.ack.payload.route`
    - 表示服务端正在确认哪次动作
  - `ws.new.payload.route`
    - 表示服务端正在推送哪种新事件
- 并不是所有命名过的 `route` 都已经能直接通过 WS 调用
- 当前如果客户端发送了尚未接入的 `ws.send` 路由，服务端会回：
  - `type = "ws.ack"`
  - `payload.ok = false`
  - `payload.message = "暂不支持该 WebSocket 业务路由"`

### 10.2 `message.send_text`

- 当前状态：已落地
- 出现位置：
  - `ws.send + route=message.send_text`
  - `ws.ack + route=message.send_text`
- 作用：
  - 让已完成 `ws.auth` 的客户端，向某个自己已加入的会话发送一条文本消息

`ws.send.payload.data` 当前字段：

- `conversation_id`
  - 类型：`string`
  - 是否必填：是
  - 作用：目标会话 ID
- `client_message_id`
  - 类型：`string`
  - 是否必填：否
  - 作用：客户端本地消息占位 ID / 幂等对账辅助字段
  - 当前若传入，服务端会原样带回成功 `ws.ack`，并在正式 `message.created` 里继续透出
- `text`
  - 类型：`string`
  - 是否必填：是
  - 作用：消息正文
  - 当前限制：
    - trim 后不能为空
    - 长度不能超过 `4000`

服务端处理语义：

- 校验当前连接已经完成 `ws.auth`
- 校验 `payload.data` 是对象，且 `conversation_id / text` 结构合法
- 校验当前发送者属于该会话
- 把消息写入 `messages`
- 先向发送方这条连接回 `ws.ack`
- 再向会话所有在线成员推送 `ws.new + route=message.created`

成功时 `ws.ack.payload.data` 当前字段：

- `conversation_id`
  - 作用：该消息所属会话 ID
- `message_id`
  - 作用：服务端生成的正式消息 ID
- `seq`
  - 作用：该会话内的单调递增消息序号
- `sender_id`
  - 作用：最终入库的发送者用户 ID
- `message_type`
  - 作用：当前固定为 `text`
- `created_at_ms`
  - 作用：服务端消息创建时间
- `client_message_id`
  - 作用：仅当客户端有传该字段时才返回

常见失败场景：

- `40001`
  - `payload.data` 不是对象
  - `conversation_id` 缺失或不是字符串
  - `text` 缺失、为空或超长
- `40400`
  - 会话不存在
  - 或当前用户并不属于该会话
- `50000`
  - 会话查询失败
  - 或消息写库失败

和它配套的正式广播事件：

- 发送成功后，服务端会继续推：
  - `ws.new + route=message.created`
- 客户端应把 `message.created` 当作正式消息事实来源
  - `ws.ack` 更适合拿来对齐“这次发送有没有成功”
  - `ws.new` 更适合拿来更新最终消息列表

### 10.3 `message.send_image`

- 当前状态：已落地
- 出现位置：
  - `ws.send + route=message.send_image`
  - `ws.ack + route=message.send_image`
- 作用：
  - 让客户端把一个“已通过 HTTP 临时上传完成的图片附件”确认成正式消息

这条路由和文本消息最大的区别是：

- 它不直接通过 WS 发送图片二进制
- 客户端必须先调用：
  - `POST /api/v1/files/upload`
- 先拿到临时上传返回的：
  - `attachment_upload_key`
- 然后再把这个 key 带到 `message.send_image`

`ws.send.payload.data` 当前字段：

- `conversation_id`
  - 类型：`string`
  - 是否必填：是
  - 作用：目标会话 ID
- `client_message_id`
  - 类型：`string`
  - 是否必填：否
  - 作用：客户端本地图片消息占位 ID
- `attachment_upload_key`
  - 类型：`string`
  - 是否必填：是
  - 作用：临时附件上传接口返回的 upload key
  - 要求：
    - 不能为空
    - 必须属于当前登录用户
    - 当前 media kind 必须和图片消息要求匹配
- `caption`
  - 类型：`string`
  - 是否必填：否
  - 作用：图片附言
  - 当前限制：
    - trim 后若为空会按未提供处理
    - 长度不能超过 `4000`

服务端处理语义：

- 校验连接已认证、参数结构合法、发送者属于目标会话
- 根据 `attachment_upload_key` 读取临时文件和可信 metadata
- 把临时文件保存到正式附件目录，并写入 `attachments` 表
- 把图片消息写入 `messages`
- 先向发送方回 `ws.ack + route=message.send_image`
- 再向会话在线成员推送 `ws.new + route=message.created`
- 若消息最终落库成功：
  - 删除临时上传文件和临时 metadata
- 若正式附件已准备成功，但消息落库失败：
  - 回滚刚创建出的正式附件
  - 保留临时上传，方便客户端重试

成功时 `ws.ack.payload.data` 当前字段：

- `conversation_id`
- `message_id`
- `seq`
- `sender_id`
- `message_type`
  - 当前固定为 `image`
- `created_at_ms`
- `client_message_id`
  - 仅当客户端原请求带了该字段时才会出现

常见失败场景：

- `40001`
  - `attachment_upload_key` 缺失、为空或格式非法
  - `caption` 超长
  - 临时上传的 `media_kind` 和图片消息不匹配
  - 临时文件为空
- `40400`
  - 会话不存在
  - 或当前用户不属于该会话
  - 或 `attachment_upload_key` 不存在 / 不属于当前用户
- `50000`
  - 临时上传 metadata 损坏
  - 正式附件创建失败
  - 图片消息写库失败

### 10.4 `message.send_file`

- 当前状态：已落地
- 出现位置：
  - `ws.send + route=message.send_file`
  - `ws.ack + route=message.send_file`
- 作用：
  - 让客户端把一个“已通过 HTTP 临时上传完成的普通附件”确认成正式文件消息
  - 这里的“普通附件”当前不是少数固定扩展名；只要临时上传阶段最终被归类为 `media_kind = file`，就可以走这条路由

这条路由和 `message.send_image` 的整体骨架一致：

- 它不直接通过 WS 发送文件二进制
- 客户端必须先调用：
  - `POST /api/v1/files/upload`
- 先拿到临时上传返回的：
  - `attachment_upload_key`
- 然后再把这个 key 带到 `message.send_file`

`ws.send.payload.data` 当前字段：

- `conversation_id`
  - 类型：`string`
  - 是否必填：是
  - 作用：目标会话 ID
- `client_message_id`
  - 类型：`string`
  - 是否必填：否
  - 作用：客户端本地文件消息占位 ID
- `attachment_upload_key`
  - 类型：`string`
  - 是否必填：是
  - 作用：临时附件上传接口返回的 upload key
  - 要求：
    - 不能为空
    - 必须属于当前登录用户
    - 当前 media kind 必须和文件消息要求匹配
- `caption`
  - 类型：`string`
  - 是否必填：否
  - 作用：文件附言
  - 当前限制：
    - trim 后若为空会按未提供处理
    - 长度不能超过 `4000`

服务端处理语义：

- 校验连接已认证、参数结构合法、发送者属于目标会话
- 根据 `attachment_upload_key` 读取临时文件和可信 metadata
- 对于文件消息，服务端当前不会再按扩展名做白名单过滤；只要求临时附件 `media_kind = file`
- 把临时文件保存到正式附件目录，并写入 `attachments` 表
- 把文件消息写入 `messages`
- 先向发送方回 `ws.ack + route=message.send_file`
- 再向会话在线成员推送 `ws.new + route=message.created`
- 若消息最终落库成功：
  - 删除临时上传文件和临时 metadata
- 若正式附件已准备成功，但消息落库失败：
  - 回滚刚创建出的正式附件
  - 保留临时上传，方便客户端重试

成功时 `ws.ack.payload.data` 当前字段：

- `conversation_id`
- `message_id`
- `seq`
- `sender_id`
- `message_type`
  - 当前固定为 `file`
- `created_at_ms`
- `client_message_id`
  - 仅当客户端原请求带了该字段时才会出现

常见失败场景：

- `40001`
  - `attachment_upload_key` 缺失、为空或格式非法
  - `caption` 超长
  - 临时上传的 `media_kind` 和文件消息不匹配
  - 临时文件为空
- `40400`
  - 会话不存在
  - 或当前用户不属于该会话
  - 或 `attachment_upload_key` 不存在 / 不属于当前用户
- `50000`
  - 临时上传 metadata 损坏
  - 正式附件创建失败
  - 文件消息写库失败

### 10.5 `message.created`

- 当前状态：已落地
- 出现位置：
  - `ws.new + route=message.created`
- 作用：
  - 表示一条正式消息已经被服务端创建完成，并应被客户端视为“最终可展示的消息实体”

当前触发来源：

- `message.send_text` 成功后
- `message.send_image` 成功后
- `message.send_file` 成功后

当前推送范围：

- 会话所有在线成员
- 包括发送者自己

`ws.new.payload.data` 当前公共字段：

- `message_id`
  - 类型：`string`
  - 作用：正式消息 ID
- `conversation_id`
  - 类型：`string`
  - 作用：所属会话 ID
- `seq`
  - 类型：`integer`
  - 作用：会话内消息序号
- `sender_id`
  - 类型：`string`
  - 作用：发送者用户 ID
- `client_message_id`
  - 类型：`string`
  - 作用：若原始发送时提供，则会透传回来
- `type`
  - 类型：`string`
  - 作用：当前消息类型
  - 当前已落地：
    - `text`
    - `image`
    - `file`
- `content`
  - 类型：`object`
  - 作用：不同消息类型的业务内容
- `created_at_ms`
  - 类型：`integer`
  - 作用：消息创建时间

当 `type = "text"` 时，`content` 当前字段：

- `text`
  - 类型：`string`
  - 作用：文本正文

当 `type = "image"` 时，`content` 当前字段：

- `attachment_id`
  - 类型：`string`
  - 作用：正式附件 ID
- `file_name`
  - 类型：`string`
  - 作用：原始文件名
- `mime_type`
  - 类型：`string`
  - 作用：附件 MIME 类型
- `size`
  - 类型：`integer`
  - 作用：附件大小，当前和 `size_bytes` 含义相同
- `size_bytes`
  - 类型：`integer`
  - 作用：附件字节大小
- `url`
  - 类型：`string`
  - 作用：附件下载地址
- `download_url`
  - 类型：`string`
  - 作用：附件下载地址
  - 当前和 `url` 一致，都是 `/api/v1/files/{attachment_id}`
- `caption`
  - 类型：`string`
  - 作用：图片附言；未提供时可能直接省略
- `width`
  - 类型：`integer`
  - 作用：图片宽度；若上传时未提供则可能省略
- `height`
  - 类型：`integer`
  - 作用：图片高度；若上传时未提供则可能省略

当 `type = "file"` 时，`content` 当前字段：

- `attachment_id`
  - 类型：`string`
  - 作用：正式附件 ID
- `file_name`
  - 类型：`string`
  - 作用：原始文件名
- `mime_type`
  - 类型：`string`
  - 作用：附件 MIME 类型
- `size`
  - 类型：`integer`
  - 作用：附件大小，当前和 `size_bytes` 含义相同
- `size_bytes`
  - 类型：`integer`
  - 作用：附件字节大小
- `url`
  - 类型：`string`
  - 作用：附件下载地址
- `download_url`
  - 类型：`string`
  - 作用：附件下载地址
  - 当前和 `url` 一致，都是 `/api/v1/files/{attachment_id}`
- `caption`
  - 类型：`string`
  - 作用：文件附言；未提供时可能直接省略

客户端使用建议：

- 这条事件更适合作为本地消息列表的最终更新来源
- 即使发送方已经收到 `ws.ack`，也仍然应该继续消费 `message.created`
  - 这样发送方和接收方都能复用同一条正式消息更新逻辑

### 10.6 `conversation.created`

- 当前状态：已落地，但当前只作为 `ws.new` 推送事件存在
- 出现位置：
  - `ws.new + route=conversation.created`
- 作用：
  - 在“通过 HTTP 创建或复用私聊会话”成功后，把新会话通知给对端在线用户

当前触发来源：

- `POST /api/v1/conversations/private`

当前推送范围：

- 服务端当前只会最佳努力推给对端用户的全部在线连接
- 发起创建的那一端，当前主要依赖 HTTP 返回结果和后续列表刷新

`ws.new.payload.data` 当前字段：

- `conversation`
  - 类型：`object`
  - 作用：一个会话列表项视图

`data.conversation` 当前字段：

- `conversation_id`
  - 作用：会话 ID
- `conversation_type`
  - 作用：当前会话类型
  - 当前私聊一般为 `direct`
- `peer_user`
  - 类型：`object`
  - 作用：对端用户摘要
  - 当前字段：
    - `user_id`
    - `account`
    - `nickname`
    - `avatar_url`
- `last_message_seq`
  - 作用：当前会话最后一条消息序号
- `last_read_seq`
  - 作用：当前视角下本用户最后已读序号
- `unread_count`
  - 作用：当前视角下未读数
- `last_message_preview`
  - 作用：最后一条消息预览；若当前还没有消息则可能省略
- `last_message_at_ms`
  - 作用：最后一条消息时间；若当前还没有消息则可能省略
- `created_at_ms`
  - 作用：会话创建时间

当前实现说明：

- 服务端当前会按“接收这条推送的用户”重新回读一次会话摘要，再组装 `conversation.created`
- 因而 `peer_user / last_read_seq / unread_count` 这类视角敏感字段，推送时会尽量与接收方真实会话列表保持一致
- 客户端当前仍会把这条事件视为：
  - 一个会话增量到达提示
  - 并在写入本地列表后再补一次 `conversation detail` 回拉，进一步校正界面显示

### 10.6 `friend.request.new`

- 当前状态：已落地，但当前只作为 `ws.new` 推送事件存在
- 出现位置：
  - `ws.new + route=friend.request.new`
- 作用：
  - 在有人通过 HTTP 发起好友申请成功后，通知目标用户有一条新的申请进入收件箱

当前触发来源：

- `POST /api/v1/friends/requests`

当前推送范围：

- 目标用户的全部在线连接

`ws.new.payload.data` 当前字段：

- `request`
  - 类型：`object`
  - 作用：一条好友申请视图

`data.request` 当前字段：

- `request_id`
  - 作用：好友申请 ID
- `peer_user`
  - 类型：`object`
  - 作用：申请对端用户摘要
  - 当前字段：
    - `user_id`
    - `account`
    - `nickname`
    - `avatar_url`
- `request_message`
  - 作用：附言；未提供时可能省略
- `status`
  - 作用：当前申请状态
  - 对于这条事件，通常是 `pending`
- `created_at_ms`
  - 作用：申请创建时间
- `handled_at_ms`
  - 作用：处理时间；对于 `pending` 申请通常不存在

当前实现限制：

- 服务端当前复用了发起方视角的好友申请 DTO 去推给接收方
- 因而 `peer_user` 字段并不保证可以直接拿来作为“收件箱最终展示数据”
- 当前更稳妥的客户端处理方式是：
  - 把它当作“有新申请到达”的实时提示
  - 然后刷新 `GET /api/v1/friends/requests/incoming`

### 10.7 `friend.request.accepted`

- 当前状态：已落地，但当前只作为 `ws.new` 推送事件存在
- 出现位置：
  - `ws.new + route=friend.request.accepted`
- 作用：
  - 在目标用户通过 HTTP 同意好友申请后，通知原申请发起人这条申请已经变成 `accepted`

当前触发来源：

- `POST /api/v1/friends/requests/{request_id}/accept`

当前推送范围：

- 原申请发起人的全部在线连接

`ws.new.payload.data.request` 字段结构：

- 和 `friend.request.new` 使用同一套好友申请视图
- 主要差异是：
  - `status` 当前应为 `accepted`
  - `handled_at_ms` 通常会有值

当前实现限制：

- 服务端当前复用了处理方视角的好友申请 DTO 去推给申请发起人
- 因而这条事件更适合被客户端当成：
  - 发件箱刷新信号
  - 以及正式好友列表刷新信号

### 10.8 `friend.request.rejected`

- 当前状态：已落地，但当前只作为 `ws.new` 推送事件存在
- 出现位置：
  - `ws.new + route=friend.request.rejected`
- 作用：
  - 在目标用户通过 HTTP 拒绝好友申请后，通知原申请发起人这条申请已经变成 `rejected`

当前触发来源：

- `POST /api/v1/friends/requests/{request_id}/reject`

当前推送范围：

- 原申请发起人的全部在线连接

`ws.new.payload.data.request` 字段结构：

- 和 `friend.request.new` 使用同一套好友申请视图
- 主要差异是：
  - `status` 当前应为 `rejected`
  - `handled_at_ms` 通常会有值

当前实现限制：

- 和 `friend.request.accepted` 一样，这条事件当前更适合作为发件箱刷新信号
- 如果客户端要拿它更新最终展示，最好仍以随后的 HTTP 刷新结果为准

### 10.9 当前保留但尚未接入的 WS 输入 `route`

下面这些路由名已经在协议和文档里预留，但服务端当前还没有在 `ChatWsController` 中真正分发到业务处理器：

- `conversation.create_private`
  - 预期作用：创建或复用一条一对一私聊会话
  - 预期请求核心字段：
    - `peer_user_id`
  - 当前建议使用的实际入口：
    - `POST /api/v1/conversations/private`
  - 未来通常会和：
    - `ws.new + route=conversation.created`
      配合使用

- `friend.request.create`
  - 预期作用：发起好友申请
  - 预期请求核心字段：
    - `target_user_id`
    - `request_message`
  - 当前建议使用的实际入口：
    - `POST /api/v1/friends/requests`
  - 未来通常会和：
    - `ws.new + route=friend.request.new`
      配合使用

- `friend.request.accept`
  - 预期作用：同意一条好友申请
  - 预期请求核心字段：
    - `request_id`
  - 当前建议使用的实际入口：
    - `POST /api/v1/friends/requests/{request_id}/accept`
  - 未来通常会和：
    - `ws.new + route=friend.request.accepted`
      配合使用

- `friend.request.reject`
  - 预期作用：拒绝一条好友申请
  - 预期请求核心字段：
    - `request_id`
  - 当前建议使用的实际入口：
    - `POST /api/v1/friends/requests/{request_id}/reject`
  - 未来通常会和：
    - `ws.new + route=friend.request.rejected`
      配合使用

## 11. 示例

### 11.1 文本消息发送

客户端发送：

```json
{
  "version": 1,
  "type": "ws.send",
  "request_id": "req_msg_001",
  "ts_ms": 1770000000000,
  "payload": {
    "route": "message.send_text",
    "data": {
      "conversation_id": "c_2001",
      "client_message_id": "cm_5001",
      "text": "你好"
    }
  }
}
```

服务端确认：

```json
{
  "version": 1,
  "type": "ws.ack",
  "request_id": "req_msg_001",
  "ts_ms": 1770000000100,
  "payload": {
    "route": "message.send_text",
    "ok": true,
    "code": 0,
    "message": "ok",
    "data": {
      "conversation_id": "c_2001",
      "message_id": "m_3001",
      "seq": 101,
      "sender_id": "u_1001",
      "message_type": "text",
      "created_at_ms": 1770000000100,
      "client_message_id": "cm_5001"
    }
  }
}
```

服务端主动推送：

```json
{
  "version": 1,
  "type": "ws.new",
  "request_id": "",
  "ts_ms": 1770000000200,
  "payload": {
    "route": "message.created",
    "data": {
      "conversation_id": "c_2001",
      "message_id": "m_3001",
      "seq": 101,
      "sender_id": "u_1001",
      "client_message_id": "cm_5001",
      "type": "text",
      "content": {
        "text": "你好"
      },
      "created_at_ms": 1770000000100
    }
  }
}
```

### 11.2 私聊会话创建设计示例

这一节描述的是协议层已经预留、但当前服务端尚未真正接入的 `conversation.create_private` 设计示例。

如果你要调用当前真实可用的创建私聊能力，请先使用 HTTP：

- `POST /api/v1/conversations/private`

客户端发送：

```json
{
  "version": 1,
  "type": "ws.send",
  "request_id": "req_conv_001",
  "ts_ms": 1770000000000,
  "payload": {
    "route": "conversation.create_private",
    "data": {
      "peer_user_id": "u_2002"
    }
  }
}
```

服务端确认：

```json
{
  "version": 1,
  "type": "ws.ack",
  "request_id": "req_conv_001",
  "ts_ms": 1770000000100,
  "payload": {
    "route": "conversation.create_private",
    "ok": true,
    "code": 0,
    "message": "ok",
    "data": {
      "conversation_id": "c_3001"
    }
  }
}
```

服务端主动推送：

```json
{
  "version": 1,
  "type": "ws.new",
  "request_id": "",
  "ts_ms": 1770000000200,
  "payload": {
    "route": "conversation.created",
    "data": {
      "conversation": {
        "conversation_id": "c_3001",
        "conversation_type": "direct",
        "peer_user": {
          "user_id": "u_2002",
          "account": "alice_2002",
          "nickname": "Alice"
        },
        "last_message_seq": 0,
        "last_read_seq": 0,
        "unread_count": 0,
        "created_at_ms": 1770000000200
      }
    }
  }
}
```

## 12. 当前实现约束

当前服务端对 WebSocket 消息还有这些限制：

- 只支持文本帧承载的 JSON 业务协议，不支持二进制帧业务协议
- 连接建立后必须先完成 `ws.auth`
- 协议层连接保活统一依赖 WebSocket 协议层 `Ping/Pong`
- 当前不再定义业务层 JSON `ping / pong`
- 当前 `ws.send / ws.ack / ws.new` 基础框架已落地，且已支持：
  - `message.send_text`
  - `message.send_image`
  - `message.send_file`
- 当前 `ws.send` 若命中尚未实现的 `route`，会返回 `ws.ack` 失败确认

## 13. 测试用例

### 13.1 `ws.auth` 成功

前置条件：

1. 先通过 HTTP 登录拿到：
   - `access_token`
   - `device_session_id`
2. 建立 WebSocket 连接：`ws://127.0.0.1:8848/ws`

客户端发送：

```json
{
  "version": 1,
  "type": "ws.auth",
  "request_id": "req_ws_auth_manual_001",
  "ts_ms": 1770000000000,
  "payload": {
    "access_token": "access_xxx",
    "device_id": "desktop_manual_001",
    "device_session_id": "ds_xxx",
    "client_version": "0.1.0"
  }
}
```

预期结果：

- 服务端返回 `type = "ws.auth.ok"`
- 返回体里的 `payload.user_id`、`payload.device_session_id` 有值

### 13.2 `ws.auth` 失败

客户端发送非法 token：

```json
{
  "version": 1,
  "type": "ws.auth",
  "request_id": "req_ws_auth_manual_002",
  "ts_ms": 1770000000000,
  "payload": {
    "access_token": "invalid_token",
    "device_id": "desktop_manual_001",
    "device_session_id": "ds_xxx"
  }
}
```

预期结果：

- 服务端返回 `type = "ws.error"`
- `payload.code = 40102`

### 13.3 `ws.send + route=message.send_text` 成功

前置条件：

- 已完成 `ws.auth`
- 当前用户已经属于某个私聊会话

客户端发送：

```json
{
  "version": 1,
  "type": "ws.send",
  "request_id": "req_msg_manual_001",
  "ts_ms": 1770000000000,
  "payload": {
    "route": "message.send_text",
    "data": {
      "conversation_id": "c_2001",
      "client_message_id": "cm_manual_001",
      "text": "你好"
    }
  }
}
```

预期结果：

- 发送方先收到 `type = "ws.ack"`
- `payload.route = "message.send_text"`
- `payload.ok = true`
- 随后在线会话成员收到 `type = "ws.new"`
- `payload.route = "message.created"`

### 13.4 `ws.send + route=message.send_image` 成功

前置条件：

- 已完成 `ws.auth`
- 当前用户已经属于某个私聊会话
- 已先通过 `POST /api/v1/files/upload` 上传图片，并拿到 `attachment_upload_key`

客户端发送：

```json
{
  "version": 1,
  "type": "ws.send",
  "request_id": "req_ws_send_manual_002",
  "ts_ms": 1770000000000,
  "payload": {
    "route": "message.send_image",
    "data": {
      "conversation_id": "c_2001",
      "client_message_id": "cm_manual_img_001",
      "attachment_upload_key": "tmp/attachments/u_1001/2026/03/21/tmp_upload_xxx.png",
      "caption": "设计稿初版"
    }
  }
}
```

预期结果：

- 发送方先收到 `type = "ws.ack"`
- `payload.route = "message.send_image"`
- `payload.ok = true`
- `payload.data.message_type = "image"`
- 随后在线会话成员收到 `type = "ws.new"`
- `payload.route = "message.created"`
- `payload.data.type = "image"`
- `payload.data.content.attachment_id`、`payload.data.content.url` 有值

### 13.5 `ws.send + route=message.send_file` 成功

前置条件：

- 已完成 `ws.auth`
- 当前用户已经属于某个私聊会话
- 已先通过 `POST /api/v1/files/upload` 上传普通文件，并拿到 `attachment_upload_key`
- 这里的“普通文件”可以是任意非图片附件类型，不要求必须命中某个预设扩展名

客户端发送：

```json
{
  "version": 1,
  "type": "ws.send",
  "request_id": "req_ws_send_manual_003",
  "ts_ms": 1770000000000,
  "payload": {
    "route": "message.send_file",
    "data": {
      "conversation_id": "c_2001",
      "client_message_id": "cm_manual_file_001",
      "attachment_upload_key": "tmp/attachments/u_1001/2026/03/21/tmp_upload_xxx.zip",
      "caption": "会议资料压缩包"
    }
  }
}
```

预期结果：

- 发送方先收到 `type = "ws.ack"`
- `payload.route = "message.send_file"`
- `payload.ok = true`
- `payload.data.message_type = "file"`
- 随后在线会话成员收到 `type = "ws.new"`
- `payload.route = "message.created"`
- `payload.data.type = "file"`
- `payload.data.content.attachment_id`、`payload.data.content.url` 有值

### 13.6 HTTP 成功链路触发 `ws.new`

前置条件：

- 目标用户在线并已完成 `ws.auth`

测试方式：

1. 通过 HTTP 发送好友申请，验证目标在线用户收到：
   - `type = "ws.new"`
   - `payload.route = "friend.request.new"`
2. 通过 HTTP 同意 / 拒绝好友申请，验证申请发起人在线端收到：
   - `friend.request.accepted`
   - 或 `friend.request.rejected`
3. 通过 HTTP 创建或复用私聊，验证对端在线端收到：
   - `payload.route = "conversation.created"`

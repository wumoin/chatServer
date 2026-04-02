# WebSocket 协议文档

最近同步时间：2026-03-21

## 1. 文档目的

这份文档专门定义当前项目 WebSocket 的统一信封格式、统一事件类型，以及 `ws.auth` 的鉴权约定。

如果需要直接查“某个 WebSocket 事件怎么发、怎么回、字段分别代表什么”，请同时查看：

- `docs/ws-api.md`

如果需要查“服务端和客户端代码应该怎么分层、哪些文件负责什么”，请同时查看：

- `docs/ws-architecture.md`

当前状态：

- 服务端与客户端都已经打通最小 `/ws + ws.auth`
- 连接保活统一依赖 WebSocket 协议层 `Ping/Pong`
- 后续业务事件协议将统一收敛为：
  - `ws.send`
  - `ws.ack`
  - `ws.new`
- 具体业务语义通过 `payload.route` 区分，不再继续扩散大量顶层事件名

默认连接路径：

- 当前服务端 WebSocket 默认路径为 `/ws`

## 2. 协议设计原则

当前项目的 WebSocket 协议采用两层路由：

1. 顶层 `type`
2. 负载中的 `payload.route`

顶层 `type` 只保留极少的稳定类型：

- `ws.auth`
- `ws.auth.ok`
- `ws.error`
- `ws.send`
- `ws.ack`
- `ws.new`

其中：

- `ws.auth / ws.auth.ok / ws.error`
  - 负责连接鉴权与统一错误
- `ws.send`
  - 客户端主动发起一个业务动作
- `ws.ack`
  - 服务端对某次 `ws.send` 的处理结果确认
- `ws.new`
  - 服务端主动推送一个新的业务事件

具体是什么业务，不再靠顶层 `type` 区分，而是靠 `payload.route` 区分，例如：

- `conversation.create_private`
- `message.send_text`
- `message.send_image`
- `message.send_file`
- `conversation.created`
- `message.created`
- `friend.request.create`
- `friend.request.new`

这样做的目标是：

- 顶层协议稳定
- 新增业务时不必继续扩顶层事件名
- 客户端与服务端分发逻辑统一
- 后续协议演进成本更低

## 3. 统一信封结构

所有 WebSocket JSON 文本消息都统一使用如下顶层结构：

```json
{
  "version": 1,
  "type": "ws.send",
  "request_id": "req_ws_001",
  "ts_ms": 1770000000000,
  "payload": {}
}
```

## 4. 顶层字段说明

### 4.1 `version`

- 类型：`integer`
- 是否必填：是
- 作用：协议版本号

说明：

- 当前固定为 `1`
- 后续若有不兼容变化，再通过它做版本分支

### 4.2 `type`

- 类型：`string`
- 是否必填：是
- 作用：顶层事件类型

当前约定值：

- `ws.auth`
- `ws.auth.ok`
- `ws.error`
- `ws.send`
- `ws.ack`
- `ws.new`

说明：

- `ws.auth` 只用于连接建立后的首条鉴权消息
- 业务事件统一走 `ws.send / ws.ack / ws.new`

### 4.3 `request_id`

- 类型：`string`
- 是否必填：
  - 客户端主动发送 `ws.auth / send` 时必填
  - 服务端返回 `ws.auth.ok / ack / ws.error` 时原样回显
  - 服务端主动推送 `ws.new` 时可为空
- 作用：请求和响应关联标识

### 4.4 `ts_ms`

- 类型：`integer`
- 是否必填：是
- 作用：消息发送时间的毫秒时间戳

说明：

- 主要用于日志、排查和时序分析
- 它不是数据库最终排序依据

### 4.5 `payload`

- 类型：`object`
- 是否必填：是
- 作用：承载当前事件的业务内容

说明：

- 顶层只负责公共元信息
- 业务路由和业务字段统一放在 `payload` 内

## 5. 业务负载公共约定

`ws.send`、`ws.ack` 和 `ws.new` 共用同一套负载思路，但三者的 payload 形状并不完全相同。

先统一一个原则：

- 所有业务事件都应带 `payload.route`
- 真正的业务字段统一放在 `payload.data`
- 只有 `ws.ack` 才额外携带结果字段：
  - `ok`
  - `code`
  - `message`

也就是说：

- `ws.send`
  - 表示“客户端要做什么”
- `ws.ack`
  - 表示“服务端如何确认这次动作的处理结果”
- `ws.new`
  - 表示“服务端主动通知客户端发生了什么新事件”

### 5.1 `ws.send.payload`

推荐结构：

```json
{
  "route": "message.send_text",
  "data": {
    "conversation_id": "c_xxx",
    "client_message_id": "cm_xxx",
    "text": "你好"
  }
}
```

字段说明：

- `route`
  - 类型：`string`
  - 是否必填：是
  - 作用：本次客户端主动发起的业务动作
- `data`
  - 类型：`object`
  - 是否必填：是
  - 作用：该业务动作的实际参数

约束：

- `ws.send` 不带 `ok / code / message`
- `ws.send` 的目标是“发起动作”，不是“表达结果”

### 5.2 `ws.ack.payload`

推荐结构：

```json
{
  "route": "message.send_text",
  "ok": true,
  "code": 0,
  "message": "ok",
  "data": {
    "conversation_id": "c_xxx",
    "message_id": "m_xxx",
    "seq": 101
  }
}
```

字段说明：

- `route`
  - 类型：`string`
  - 是否必填：是
  - 作用：本次确认对应哪一个业务动作
- `ok`
  - 类型：`boolean`
  - 是否必填：是
  - 作用：本次动作是否处理成功
- `code`
  - 类型：`integer`
  - 是否必填：是
  - 作用：业务错误码，成功时通常为 `0`
- `message`
  - 类型：`string`
  - 是否必填：是
  - 作用：结果说明文本
- `data`
  - 类型：`object`
  - 是否必填：否
  - 作用：成功时返回的业务结果；失败时可以为空对象或直接省略

约束：

- `ws.ack` 是“对某次 `ws.send` 的统一确认”
- `ws.ack` 不是广播事件
- `ws.ack` 通常只回给发起这次动作的那条连接

### 5.3 `ws.new.payload`

推荐结构：

```json
{
  "route": "conversation.created",
  "data": {
    "conversation_id": "c_xxx"
  }
}
```

字段说明：

- `route`
  - 类型：`string`
  - 是否必填：是
  - 作用：本次服务端主动推送的业务事件类型
- `data`
  - 类型：`object`
  - 是否必填：是
  - 作用：该事件的业务内容

约束：

- `ws.new` 是服务端主动推送
- `ws.new` 一般不带 `ok / code / message`
- 如果服务端需要广播一个新的业务结果或状态变化，就统一走 `ws.new`

## 6. `ws.auth` 鉴权事件

### 6.1 作用

`ws.auth` 是客户端建立 WebSocket 连接后发送的第一条业务消息。

它的目标是：

- 把当前连接绑定到一个已登录用户
- 把当前连接绑定到一个具体 `device_session`
- 为后续 `ws.send / ws.ack / ws.new` 提供权限基础

### 6.2 请求结构

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

### 6.3 `ws.auth.payload` 字段说明

#### `access_token`

- 类型：`string`
- 是否必填：是
- 作用：登录接口返回的访问令牌

#### `device_id`

- 类型：`string`
- 是否必填：是
- 作用：客户端本地设备标识

#### `device_session_id`

- 类型：`string`
- 是否必填：是
- 作用：登录接口返回的设备会话 ID

#### `client_version`

- 类型：`string`
- 是否必填：否
- 作用：客户端版本号

## 7. `ws.auth` 成功响应

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

说明：

- `type` 固定为 `ws.auth.ok`
- `request_id` 必须原样回显
- 当前只返回最小成功信息

## 8. `ws.error` 失败响应

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

失败响应约定：

- `type` 固定为 `ws.error`
- `request_id` 原样回显
- `payload.code` 和 HTTP 错误码尽量对齐
- `payload.message` 返回适合客户端处理和排查的文本

## 9. 第一批推荐业务路由

当前建议先收敛为下面这批 `route`，并全部放在 `ws.send / ws.ack / ws.new` 框架下：

### 9.1 客户端主动发送的 `ws.send`

- `conversation.create_private`
- `message.send_text`
- `message.send_image`
- `message.send_file`
- `friend.request.create`
- `friend.request.accept`
- `friend.request.reject`

### 9.2 服务端确认返回的 `ws.ack`

- `conversation.create_private`
- `message.send_text`
- `message.send_image`
- `message.send_file`
- `friend.request.create`
- `friend.request.accept`
- `friend.request.reject`

### 9.3 服务端主动推送的 `ws.new`

- `conversation.created`
- `message.created`
- `friend.request.new`
- `friend.request.accepted`
- `friend.request.rejected`

说明：

- 这里的 `route` 可以继续扩展
- 但顶层 `type` 尽量保持稳定，不再扩散更多名字

## 10. 典型示例

### 10.1 发送文本消息

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
      "seq": 101
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
      "message_type": "text",
      "content": {
        "text": "你好"
      }
    }
  }
}
```

### 10.2 创建私聊会话

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
      "conversation_id": "c_3001"
    }
  }
}
```

## 11. 客户端和服务端职责边界

客户端：

- 负责建连
- 负责先发 `ws.auth`
- 负责后续统一发送 `ws.send`
- 负责按 `type + payload.route` 分发收到的消息
- 负责断线重连和重新鉴权

服务端：

- 负责 `/ws` 入口
- 负责 `ws.auth`
- 负责按 `type + payload.route` 分发业务消息
- 负责把业务处理结果统一包装成 `ack / new / ws.error`
- 负责把事件推给在线连接

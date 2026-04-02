# WebSocket 结构设计文档

最近同步时间：2026-03-21

## 1. 文档目的

这份文档专门说明当前项目的 WebSocket 结构方向。

它和 `docs/ws-protocol.md` 的分工不同：

- `docs/ws-protocol.md`
  - 回答“统一信封怎么长、`ws.send / ws.ack / ws.new` 怎么约定、`ws.auth` 怎么发”
- `docs/ws-api.md`
  - 回答“具体事件怎么发、怎么回、字段分别代表什么”
- `docs/ws-architecture.md`
  - 回答“WebSocket 代码怎么搭、哪些文件负责什么、业务事件怎么分层处理”

当前状态：

- 服务端已建立 `/ws + ws.auth + connection_registry`
- 服务端已建立 `ws.send / ws.ack / ws.new` 最小基础框架
- 客户端已建立 `ChatWsClient + ws.auth`
- 当前连接保活统一依赖协议层 `Ping/Pong`
- 下一步应进入具体业务 `route` 的实现阶段

## 2. WebSocket 在本项目中的角色

WebSocket 在这个项目里不是“替代 HTTP 的第二套接口”，而是“实时增量同步通道”。

建议遵循这条原则：

- HTTP：负责拉快照
- WebSocket：负责收增量

具体含义是：

- 程序第一次启动时，通过 HTTP 拉当前快照
- 切换账号重新进入聊天页时，通过 HTTP 拉当前快照
- 网络断开后重新恢复时，通过 HTTP 补齐快照
- 正常在线期间，消息、新会话、好友申请提醒、已读变化等增量由 WebSocket 推送

## 3. 统一事件框架

当前项目后续 WebSocket 业务事件统一收敛为：

- `ws.send`
- `ws.ack`
- `ws.new`

具体业务语义通过 `payload.route` 区分。

例如：

- `ws.send + route=message.send_text`
- `ws.send + route=message.send_image`
- `ws.send + route=message.send_file`
- `ws.ack + route=message.send_text`
- `ws.ack + route=message.send_image`
- `ws.ack + route=message.send_file`
- `ws.new + route=message.created`
- `ws.new + route=conversation.created`
- `ws.new + route=friend.request.new`

这样做的收益是：

- 顶层事件类型稳定
- 新业务只增加 `route`
- 控制器分发逻辑统一
- 客户端分发逻辑统一
- 后续变更成本更低

当前这套统一协议已经开始进入真实业务阶段：

- 已落地：`ws.auth / ws.auth.ok / ws.error`
- 已落地：`ws.send / ws.ack / ws.new`
- 已落地 WebSocket 消息发送路由：
  - `ws.send + route=message.send_text`
  - `ws.ack + route=message.send_text`
  - `ws.send + route=message.send_image`
  - `ws.ack + route=message.send_image`
  - `ws.send + route=message.send_file`
  - `ws.ack + route=message.send_file`
  - `ws.new + route=message.created`

## 4. 服务端目录与文件建议

建议在 `chatServer/src/` 下按下面结构推进。

### 4.1 `transport/ws/`

- `chat_ws_controller.h`
- `chat_ws_controller.cpp`

职责：

- 承接 WebSocket 路由入口
- 处理连接建立与关闭
- 解析统一信封
- 先处理 `ws.auth`
- 对已认证连接按：
  - `type`
  - `payload.route`
 进行分发
- 统一发送 `ws.error`

这一层只负责“接住连接和消息”，不直接承担重业务。

### 4.2 `protocol/dto/ws/`

当前已有：

- `ws_envelope_dto.h`
- `ws_auth_dto.h`

后续建议新增：

- `ws_message_dto.h`
- `ws_conversation_dto.h`
- `ws_friend_dto.h`

职责：

- 定义统一 WebSocket 信封
- 定义 `ws.auth` DTO
- 定义 `ws.send / ws.ack / ws.new` 下各个 `route` 对应的数据结构
- 负责 JSON 和 DTO 的互转

这一层只负责协议数据，不负责业务判断。

### 4.3 `service/`

当前已有：

- `ws_session_service.h`
- `ws_session_service.cpp`

职责：

- 校验 `ws.auth`
- 验证 `access_token`
- 验证 `device_session_id`
- 把连接与 `user_id / device_session_id / device_id` 绑定
- 在连接关闭时做解绑

当前已新增：

- `realtime_push_service.h`
- `realtime_push_service.cpp`

职责：

- 统一向在线连接推送事件
- 对外提供：
  - 按 `device_session_id` 推送
  - 按 `user_id` 推送
  - 按一组用户推送
- 统一发送：
  - `ws.ack`
  - `ws.new`

说明：

- `conversation.created`
- `message.created`
- `friend.request.new`

这些都应该属于推送服务层负责发送的事件，而不是单独新增一层“输入处理器”。

后续再按业务补输入处理服务，例如：

- `ws_message_service.*`
  - 处理 `ws.send + route=message.send_text`
  - 处理 `ws.send + route=message.send_image`
  - 处理 `ws.send + route=message.send_file`
- `ws_friend_service.*`
  - 处理 `ws.send + route=friend.request.create`

### 4.4 `infra/ws/`

当前已有：

- `connection_registry.h`
- `connection_registry.cpp`

职责：

- 保存在线连接映射
- 根据 `user_id` 或 `device_session_id` 查找连接
- 注册连接
- 移除连接
- 为后续主动推送提供基础能力

这一层是基础设施，不负责协议解析，也不负责业务校验。

## 5. 客户端目录与文件建议

建议在 `chatClient/src/` 下按下面结构推进。

### 5.1 `ws/`

当前已有：

- `chat_ws_client.h`
- `chat_ws_client.cpp`

职责：

- 基于 `QWebSocket` 建立和关闭连接
- 连接建立后发送 `ws.auth`
- 接收文本帧并统一解包
- 暴露连接状态和收到事件的信号
- 后续作为 `ConversationManager` 的实时事件来源

它不应该直接：

- 修改窗口控件
- 修改会话列表
- 修改消息列表

### 5.2 `dto/`

当前已有：

- `ws_dto.h`
- `ws_dto.cpp`

后续应补：

- `message`、`conversation`、`friend` 等 route 对应 DTO

职责：

- 定义统一 WebSocket 信封
- 定义 `ws.auth`
- 定义 `ws.send / ws.ack / ws.new` 事件的 payload 结构
- 负责 JSON 与 DTO 的转换

## 6. 第一版职责边界

### 6.1 服务端

`ChatWsController`

- 接收 WebSocket 消息
- 调用 DTO 解析
- 先处理 `ws.auth`
- 对已认证连接按 `type + route` 分发
- 统一发送 `ws.error`

`WsSessionService`

- 只负责连接鉴权与连接归属
- 不负责消息入库
- 不负责会话列表拼装
- 不负责历史消息分页

`RealtimePushService`

- 只负责把事件推给在线用户
- 不负责权限判断
- 不负责业务入库
- 不负责事务控制

`ConnectionRegistry`

- 只维护在线连接索引
- 不直接依赖 HTTP 业务对象

### 6.2 客户端

`ChatWsClient`

- 管连接
- 管鉴权
- 依赖协议层心跳
- 解包统一信封
- 把事件上抛

`ConversationManager`

- 统一接收 HTTP 快照与 WS 增量
- 根据 `type + route` 分发到：
  - `ConversationListModel`
  - `MessageModelRegistry`

`ConversationListModel`

- 只管中间栏会话摘要显示

`MessageModelRegistry`

- 只管按 `conversation_id` 管理 `MessageModel`

## 7. 推送服务层在整套架构里的位置

推送服务层的作用可以概括为：

**把已经确定要通知客户端的业务结果，发给对应在线连接。**

它的上游通常是：

- `ConversationService`
- `WsMessageService`
- `FriendService`

它的下游是：

- `ConnectionRegistry`
- WebSocket 连接对象

也就是说：

- 业务服务负责决定“该不该通知”
- 推送服务负责决定“通知发给谁、怎么发”

第一版建议至少提供这些能力：

- `pushToDeviceSession(...)`
- `pushToUser(...)`
- `pushToUsers(...)`
- `pushAck(...)`
- `pushNew(...)`

后续可以再继续封装成更具体的方法，例如：

- `pushMessageAck(...)`
- `pushConversationCreated(...)`
- `pushFriendRequestNew(...)`

## 8. 推荐实施顺序

我建议接下来按这个顺序推进：

1. 保持 `/ws + ws.auth` 稳定
2. 在 DTO 层补齐统一 `ws.send / ws.ack / ws.new` 所需的 `route` DTO
3. 新增 `RealtimePushService`
4. 先打通文本消息：
   - `message.send_text`
5. 再补需要临时文件确认的图片消息：
   - `message.send_image`
6. 再补需要临时文件确认的普通文件消息：
   - `message.send_file`
7. 再补：
   - `conversation.created`
   - 好友申请实时事件

## 9. 当前最值得优先保证稳定的实时链路

当前已经打通、并最值得优先保证稳定的实时链路有两条：

- `ws.send + route=message.send_text`
- `ws.ack + route=message.send_text`
- `ws.send + route=message.send_image`
- `ws.ack + route=message.send_image`
- `ws.send + route=message.send_file`
- `ws.ack + route=message.send_file`
- `ws.new + route=message.created`

这样当前最先拿到的是：

- 客户端和服务端已经有一套稳定的“文本消息 + 附件消息确认”实时骨架
- 服务端能返回发送确认
- 服务端能向在线会话成员推送新消息

这会是后面会话模型和历史消息模型真正开始依赖 WS 的第一个稳定基点。

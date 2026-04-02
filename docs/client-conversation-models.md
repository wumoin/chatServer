# 客户端会话模型说明

最近同步时间：2026-03-21

## 1. 文档目的

这份文档专门介绍客户端当前这套会话数据骨架：

- `ConversationListModel`
- `MessageModelRegistry`
- `ConversationManager`

它回答的重点是：

- 这几个类分别负责什么
- 为什么要这样拆
- 它们之间怎么配合
- 当前已经做到哪一步
- 后续应该怎么继续往真实聊天数据演进

## 2. 总体设计

当前客户端会话层的第一版目标不是一次性把缓存、分页、重连恢复都做满，而是先把边界立住。

当前建议的最小结构是：

```text
ChatWindow
  -> ConversationManager
     -> ConversationApiClient
     -> ChatWsClient
     -> ConversationListModel
     -> MessageModelRegistry
        -> MessageModel
```

这条链路的含义是：

- `ChatWindow` 不直接发会话域 HTTP
- `ChatWindow` 不直接处理 WebSocket 协议
- `ConversationManager` 统一接住 HTTP 返回和 WS 推送
- `ConversationListModel` 只负责中间栏“会话摘要”
- `MessageModelRegistry` 只负责右侧“某个会话对应的消息模型”

当前第一版暂时不单独拆 `ConversationStore`。  
最小缓存状态先放在 `ConversationManager` 内部维护，等后续分页、重连恢复、增量补拉明显变重时，再把这部分抽出来。

## 3. ConversationListModel

对应代码：

- `chatClient/src/model/conversationlistmodel.h`
- `chatClient/src/model/conversationlistmodel.cpp`

### 3.1 作用

`ConversationListModel` 只承接“会话列表应该显示什么”。

它只关心会话摘要数据，例如：

- `conversation_id`
- 会话类型
- 对端昵称
- 对端账号
- 对端头像 `avatar_url`
- 最后一条消息摘要
- 最后一条消息 `seq`
- 当前用户在这个会话里的 `last_read_seq`
- 未读数
- 最后活跃时间

### 3.2 它不负责什么

它明确不负责：

- 发 HTTP 请求
- 建立或管理 WebSocket
- 决定何时拉历史消息
- 维护分页游标
- 更新右侧消息区

也就是说，`ConversationListModel` 是纯展示模型，不是业务调度器。

### 3.3 当前能力

当前已经提供的最小能力有：

- `setConversations(...)`
  用完整快照整体替换当前会话摘要集合
- `upsertConversation(...)`
  插入或更新单条会话摘要
- `clear()`
- `hasConversation(...)`
- `conversationById(...)`

这意味着后面无论数据来自：

- `GET /api/v1/conversations`
- `conversation.created`
- `message.new`

最终都可以统一收口成对 `ConversationListModel` 的更新。

## 4. MessageModelRegistry

对应代码：

- `chatClient/src/model/messagemodelregistry.h`
- `chatClient/src/model/messagemodelregistry.cpp`

依赖的单会话消息模型：

- `chatClient/src/model/messagemodel.h`
- `chatClient/src/model/messagemodel.cpp`

### 4.1 作用

`MessageModelRegistry` 的职责是：

- 以 `conversation_id` 为 key
- 管理多个 `MessageModel`
- 让右侧消息区可以按当前会话切换对应的消息模型

它本质上是一个“会话 ID -> 消息模型”的注册表。

### 4.2 它不负责什么

它明确不负责：

- 发 HTTP 请求
- 处理 WebSocket 协议
- 判断要不要拉历史消息
- 判断要不要增量补拉
- 判断某条消息该落到哪个会话

这些决策都应该交给 `ConversationManager`。

### 4.3 当前能力

当前它已经支持：

- `ensureModel(conversationId)`
- `model(conversationId)`
- `hasModel(conversationId)`
- `conversationIds()`
- `upsertMessageItem(...)`
- `addTextMessage(...)`
- `addImageMessage(...)`
- `addFileMessage(...)`
- `replaceMessageItems(...)`
- `clearConversation(...)`

其中这次新增的重点是：

- `replaceMessageItems(...)`
- `upsertMessageItem(...)`

这让它不再只能做“本地追加演示消息”，也能接住后续的：

- 历史消息完整快照
- 某次重新同步后的整段替换
- `ws.ack / ws.new` 驱动下的消息去重更新

### 4.4 MessageModel 的位置

`MessageModel` 继续只负责“单个会话的消息列表怎么显示给 Qt 视图”。

这次也补了：

- `setMessageItems(...)`
- `clear()`
- `updateImagePayload(...)`
- `updateFilePayload(...)`
- `updateTransferState(...)`

这样 `MessageModelRegistry` 就能统一把某个会话的新消息集合整体写进去。
同时，图片和文件消息后续的“局部字段回填”也不需要整条消息反复 upsert。

## 5. ConversationManager

对应代码：

- `chatClient/src/service/conversation_manager.h`
- `chatClient/src/service/conversation_manager.cpp`

### 5.1 作用

`ConversationManager` 是当前第一版会话域的统一入口。

它负责持有并协调：

- `ConversationApiClient`
- `ChatWsClient`
- `ConversationListModel`
- `MessageModelRegistry`

`ChatWindow` 现在不再直接拿会话 API、WS 和消息注册表，而是统一通过 `ConversationManager` 使用这些能力。

### 5.2 当前职责

当前已经落下的职责有：

- 注入 `AuthService`
- 根据当前登录态连接 / 断开 WS
- 发起 `POST /api/v1/conversations/private`
- 更新 `ConversationListModel`
- 更新 `MessageModelRegistry`
- 维护每个会话的最小运行时状态
- 把 WS 的状态信号转发给界面层
- 接收 `ChatWsClient` 上抛的 `ws.new`
- 解析并处理 `conversation.created`
- 把好友申请类实时事件继续转发给界面层
- 在聊天窗口切换到某个会话时，本地将该会话标记为已读，并清空 `unreadCount`

### 5.3 当前内部状态

当前每个会话会维护一份轻量状态：

- `lastLoadedMaxSeq`
- `hasMoreBefore`
- `initialized`
- `loading`

这部分现在先内收在 `ConversationManager` 中，而不是单独拆一个 `ConversationStore`。

这样做的原因很简单：

- 当前已经有真实会话列表接入
- 当前已经有首次进入聊天窗口时的历史消息快照接入
- 当前已经开始接入 `ws.ack / ws.new`

如果现在就把缓存层拆得太细，会先进入“结构很多、业务很少”的状态。

### 5.4 当前已经提供的能力

当前 `ConversationManager` 已经暴露：

- `setAuthService(...)`
- `conversationListModel()`
- `messageModelRegistry()`
- `ensureMessageModel(...)`
- `connectRealtimeChannel()`
- `disconnectRealtimeChannel()`
- `initializeConversationDataIfNeeded()`
- `createPrivateConversation(...)`
- `applyConversationListSnapshot(...)`
- `applyConversationMessagesSnapshot(...)`
- `sendTextMessage(...)`
- `sendImageMessage(...)`
- `sendFileMessage(...)`
- `downloadFileMessage(...)`
- `conversationState(...)`

其中：

- 当前实时事件的处理边界已经明确：
  - `ChatWsClient` 只解析出 `route + data` 或 `route + ack`
  - `ConversationManager` 统一处理自己该管的 `ws.ack / ws.new`
  - `ConversationManager` 内部再按不同 `route` 分发到独立处理函数，避免把所有实时业务揉在一个大分支里
  - 好友申请类实时事件继续通过信号交给好友界面刷新

- `createPrivateConversation(...)`
  已经被 `ChatWindow` 的“发起会话”按钮接入
- `connectRealtimeChannel(...)`
  已经被聊天窗口接入最小 WS 连接
- `initializeConversationDataIfNeeded()`
  当前会在第一次进入聊天窗口并持有有效登录态时触发：
  - 先拉 `GET /api/v1/conversations`
  - 再为每个会话拉一页 `GET /api/v1/conversations/{conversation_id}/messages`
  - 最后把结果同步到 `ConversationListModel` 和 `MessageModelRegistry`
- `sendImageMessage(...)`
  当前已经负责：
  - 先把图片消息以“上传中”占位态写入 `MessageModelRegistry`
  - 监听 `FileApiClient::uploadProgressChanged` 并持续回写图片消息进度
  - 上传成功后组装 `ws.send + route=message.send_image`
  - 在收到 `ws.ack + route=message.send_image` 后把本地占位消息升级成正式消息
  - 在收到 `ws.new + route=message.created` 后继续用正式消息数据收敛同一行图片消息
  - 由于待上传状态是按 `client_message_id / upload_request_id / ws_request_id` 分别追踪的，所以 `ChatWindow` 一次选择多张图片时，可以并发调用多次 `sendImageMessage(...)`
- `sendFileMessage(...)`
  当前已经负责：
  - 先把文件消息以“上传中”占位态写入 `MessageModelRegistry`
  - 监听 `FileApiClient::uploadProgressChanged` 并持续回写文件消息进度
  - 上传成功后组装 `ws.send + route=message.send_file`
  - 在收到 `ws.ack + route=message.send_file` 后把本地占位消息升级成正式消息
  - 在收到 `ws.new + route=message.created` 后继续用正式消息数据收敛同一行文件消息
- `downloadFileMessage(...)`
  当前已经负责：
  - 把正式 file 消息切到 `Downloading` 状态
  - 通过 `FileApiClient` 下载正式附件
  - 持续回写下载进度到 `MessageModel`
  - 下载完成后只局部刷新 `localPath / fileName / mimeType / sizeBytes`
- `sendTextMessage(...)`
  当前已经负责：
  - 组装 `ws.send + route=message.send_text`
  - 记录待确认的本地发送状态
  - 在收到 `ws.ack` 后更新 `ConversationListModel` 和 `MessageModelRegistry`
  - 在收到 `ws.new + route=message.created` 后再次用正式消息数据同步更新这两个 model
- `applyConversationListSnapshot(...) / applyConversationMessagesSnapshot(...)`
  当前除了接住历史消息快照本体，还会继续为其中的 image 消息补一层
  “download_url -> 本地缓存 -> MessageModel 局部刷新”的展示链路

## 6. 当前数据流

### 6.1 创建私聊

当前已经打通的最小链路：

```text
ChatWindow
  -> ConversationManager::createPrivateConversation()
     -> ConversationApiClient::createPrivateConversation()
     -> 服务端 POST /api/v1/conversations/private
     -> ConversationManager 接收响应
     -> ConversationListModel::upsertConversation(...)
```

目前这一步还没有把结果正式接到中间栏会话列表，只是先把入口收口到了 manager。

### 6.2 首次进入聊天窗口时的初始化同步

当前第一次进入聊天窗口时，链路会变成：

```text
ChatWindow::setCurrentUserProfile(...)
  -> ConversationManager::initializeConversationDataIfNeeded()
     -> GET /api/v1/conversations
     -> ConversationListModel 更新
     -> 对每个会话继续请求
        GET /api/v1/conversations/{conversation_id}/messages
     -> MessageModelRegistry 更新对应会话的 MessageModel
     -> ChatWindow 把 ConversationListModel 当前快照同步到 QListWidget
```

这意味着当前虽然中间栏仍然是 `QListWidget`，但它已经不再长期依赖硬编码会话文本，而是由 `ConversationManager` 拉下来的真实快照驱动。

### 6.3 文本消息实时发送

```text
ChatWindow
  -> ConversationManager::sendTextMessage(...)
     -> ChatWsClient::sendBusinessEvent()
     -> 服务端 ws.send + route=message.send_text
     -> 收到 ws.ack
        -> ConversationManager 更新 ConversationListModel
        -> ConversationManager 更新 MessageModelRegistry
     -> 收到 ws.new + route=message.created
        -> ConversationManager 再次同步这两个 model
```

这里之所以要同时处理 `ack` 和 `new`，是因为：

- `ack` 代表“当前这次发送请求已被服务端确认”
- `new` 代表“会话里新增了一条正式消息”

客户端现在通过 `MessageModelRegistry::upsertMessageItem(...)` 去重，所以不会因为两次事件把同一条消息插入两遍。

### 6.4 图片消息上传与发送

```text
ChatWindow::handleSendImage()
  -> QFileDialog::getOpenFileNames(...)
  -> 对每个 localPath 分别调用
     ConversationManager::sendImageMessage(...)
     -> 先插入“上传中”本地图片占位消息
     -> FileApiClient::uploadTemporaryAttachment(...)
     -> 持续接收 uploadProgressChanged(...)
     -> 服务端 POST /api/v1/files/upload
     -> 上传成功后发送 ws.send + route=message.send_image
     -> 收到 ws.ack + route=message.send_image
     -> 收到 ws.new + route=message.created
     -> 用正式消息数据收敛占位消息
```

这条链路当前有两个比较关键的实现点：

- `ChatWindow` 已经改成 `QFileDialog::getOpenFileNames(...)`，所以一次选择多张图片时，会启动多条独立发送任务
- `ConversationManager` 对每张图片分别维护本地占位态、HTTP 上传 request_id 和 WS 发送 request_id，因此这些上传任务可以并发进行，而不会互相覆盖状态

### 6.5 文件消息上传、接收与下载

```text
ChatWindow::handleSendAttachment()
  -> 对每个本地附件判断类型
     -> 图片走 ConversationManager::sendImageMessage(...)
     -> 普通文件走 ConversationManager::sendFileMessage(...)
        -> 先插入“上传中”本地文件占位消息
        -> FileApiClient::uploadAttachment(...)
        -> 持续接收 uploadProgressChanged(...)
        -> 上传成功后发送 ws.send + route=message.send_file
        -> 收到 ws.ack + route=message.send_file
        -> 收到 ws.new + route=message.created
        -> 用正式 file 消息收敛本地占位
```

接收侧当前也已经形成闭环：

- HTTP 历史消息和 `ws.new + route=message.created` 里的 file 消息都会被转换成 `MessageType::File`
- `MessageDelegate` 会直接把它画成文件卡片，而不是退回成纯文本占位
- `MessageListView` 负责文件卡片点击与右键菜单
- `ChatWindow` 会把这些交互再转给 `ConversationManager::downloadFileMessage(...)`
- 下载完成后，文件消息会被局部回写本地路径，因此“打开文件 / 打开所在目录”都能继续命中同一行消息

### 6.6 实时通道状态

当前最小 WS 状态流是：

```text
ChatWsClient
  -> ConversationManager
     -> ChatWindow
```

也就是说：

- `ChatWsClient` 负责连 `/ws` 和 `ws.auth`
- `ConversationManager` 负责持有它，并转发状态信号
- `ChatWindow` 只负责展示状态文本

## 7. 为什么先不拆 ConversationStore

当前很多人会自然想到再加一层 `ConversationStore`。  
这个方向长期没问题，但现阶段先不拆更合适。

原因有 3 个：

1. 当前真实会话数据链路虽然已经落到 UI  
   但缓存状态仍然比较轻，继续把入口收口在 `ConversationManager` 里，仍然比现在就引入额外 store 层更合适。

2. 当前运行时状态还很轻  
   只有：
   - 是否已初始化
   - 是否正在加载
   - `lastLoadedMaxSeq`
   - `hasMoreBefore`

3. 先让 `ConversationManager` 成为稳定入口  
   等后面出现：
   - 多会话缓存明显变重
   - 分页逻辑变复杂
   - 重连恢复策略单独成型
   
   再把内部状态抽成 `ConversationStore`，会更自然。

所以当前判断是：

- 现在：`ConversationManager + ConversationListModel + MessageModelRegistry`
- 以后：如果状态明显变重，再拆 `ConversationStore`

## 8. 当前边界总结

### 8.1 允许做的事

- `ConversationListModel` 只存会话摘要
- `MessageModelRegistry` 只管消息模型映射
- `ConversationManager` 统一收口 HTTP 和 WS

### 8.2 不允许做的事

- 不要让 `ConversationListModel` 直接发 HTTP
- 不要让 `MessageModelRegistry` 直接处理 WS 事件
- 不要让 `ChatWindow` 再直接拿 `ConversationApiClient`
- 不要让 `ChatWindow` 再直接拿 `ChatWsClient`

## 9. 下一步最自然的演进

基于当前这套骨架，后续最自然的演进顺序是：

1. 继续补强“已读 / 未读”真实同步
   - 当前客户端切换会话时已经会先本地清零未读数
   - 后续可以把 read state 与服务端做更严谨的双向同步

2. 继续补强图片 / 附件消息的展示生命周期
   - 例如更明确的缓存淘汰策略
   - 更完整的失败重试和取消发送能力

3. 继续补强断线重连后的增量恢复
   - 让 `ConversationManager` 在重连成功后更稳地做会话和消息补拉

4. 接入更多 `ws.send / ws.ack / ws.new` 业务路由
   - 仍然统一先进入 `ConversationManager`
   - 再更新列表模型和消息模型

5. 如果缓存、分页和重连恢复状态明显变重
   - 再从 `ConversationManager` 中抽出 `ConversationStore`

## 10. 一句话总结

当前这套模型和 manager 的核心原则是：

**`ConversationManager` 做统一调度，`ConversationListModel` 只管会话摘要展示，`MessageModelRegistry` 只管每个会话的消息模型映射。**

先把边界立住，后面的真实会话列表、历史消息和 WS 增量同步才能稳稳接进去。

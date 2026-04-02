# 项目开发进度

最近同步时间：2026-03-21

## 1. 当前阶段

当前项目整体处于“服务端基础脚手架、认证链路、好友链路、私聊会话 HTTP 基础接口，以及客户端 / 服务端 WebSocket 最小长连接已落地，下一步进入真实会话数据接入”的阶段。

按 `docs/im-system-design.md` 当前重排后的分阶段方案判断：

- 第 1 步“搭服务端基础脚手架”：部分完成
- 第 2 步“完成受保护接口基础与登录态管理”：进行中
- 第 3 步“完成用户搜索与好友申请”：基本完成
- 第 4 步“完成私聊会话模型与会话列表”：服务端已完成，客户端已开始

## 2. 当前完成情况

- 已把 API 协议文档规范统一为：每个已落地接口都同时说明请求字段、返回字段，并提供至少一个对应测试用例；当前已补齐会话域、好友域、文件域和 WebSocket 事件文档

### 2.1 chatClient

已完成：

- Qt Widgets 基础窗口结构
- 登录页和聊天页 UI 原型
- `QListView + Model + Delegate` 消息展示链路
- `MessageDelegate` 已开始按消息类型区分渲染，当前已支持：
  - 图片气泡、本地缩略图与上传状态覆盖层
  - 文件卡片、文件大小/状态文案以及下载进度条
- 文本消息基础输入、显示和复制交互
- 已建立 `chatClient/config/app.json` 客户端配置文件
- 已建立 `src/config/appconfig.*` 配置读取层
- 已把应用展示名、窗口标题以及 HTTP / WebSocket 基础地址统一改为从配置读取
- 已建立客户端统一日志模块 `src/log/app_logger.*`
- 已把客户端日志配置并入 `chatClient/config/app.json`
- 已将客户端日志统一输出到控制台和 `chatClient/logs/chatclient.log`
- 已完成客户端配置加载、编译验证和离屏启动验证
- 已补充 `docs/chatclient-architecture.md`，把客户端目标分层、目录职责、登录链路和消息收发链路单独整理成文
- 已新增 `docs/client-conversation-models.md`，单独整理会话模型、消息模型注册表和会话调度器的职责边界
- 已建立客户端最小认证分层：`dto/auth_dto.*`、`api/auth_api_client.*`、`service/auth_service.*`
- 已把注册页接到真实服务端接口，支持提交注册、显示本地校验错误、显示服务端失败提示
- 已把登录页接到真实服务端接口，支持提交登录、显示本地校验错误、显示服务端失败提示
- 已把认证接口常见英文错误提示统一翻译为中文界面提示
- 已把认证错误中文提示映射拆分为独立本地化文件，便于后续维护
- 已在客户端本地保存 `device_session_id + access_token`，登录成功后可直接切到聊天页
- 已新增 `FileApiClient + file_dto`，支持临时附件上传、正式附件下载，以及上传 / 下载进度信号；客户端图片上传前大小校验已统一放宽到 `1 GB`
- 已与服务端对齐“同一用户同一设备重复登录返回 409 冲突”的行为
- 已补充客户端认证错误翻译示例文档
- 已将聊天页重构为“左侧导航栏 + 中间列表栏 + 右侧内容区”三段式骨架
- 已支持左侧“消息 / 好友”模式切换
- 已在左侧主导航入口下方补充“切换账号 / 登出”两个按钮
- “切换账号”当前会登出当前会话并返回登录页；“登出”会在请求服务端登出后直接退出程序
- 当前程序关闭时也会自动向服务端发送登出请求，并清理本地登录态
- 会话列表当前继续使用 `QListWidget`
- 好友模式已补充独立的“添加好友”弹窗，并已接入真实好友搜索、发送好友申请、我已发送申请记录拉取、我收到的申请拉取，以及同意 / 拒绝处理链路
- 服务端当前已补正式好友列表接口 `GET /api/v1/friends`
- 客户端好友主页当前已接入真实好友列表接口，进入好友模式时会刷新并展示正式好友数据
- 好友模式右侧详情 hero 区已补选中好友头像展示，当前会按用户 ID 拉取真实头像
- 已修正好友列表接入后 `ChatWindow::updateFriendList(...)` 的签名不一致诊断，当前头文件与实现保持一致
- “添加好友”弹窗的“申请好友”页已固定顶部搜索结果区和验证消息区高度，底部已发送申请列表按窗口剩余空间自适应伸缩
- 注册页已补头像选择与临时上传链路，选择头像后会立即调用 `POST /api/v1/users/avatar/temp`
- 注册页头像设置区已调整到表单首项，并补了更高的窗口高度以避免控件重叠
- 登录页和注册页的键盘 Tab 顺序已按界面上下顺序显式整理
- 注册提交当前会自动携带 `avatar_upload_key`
- 聊天页左上角导航头像已支持按用户 ID 拉取真实头像文件
- 已新增 `src/dto/ws_dto.*` 与 `src/ws/chat_ws_client.*`
- 聊天窗口当前已在持有有效登录态时连接 `/ws`，完成 `ws.auth` 的最小客户端接入
- 已新增 `src/model/conversationlistmodel.*` 和 `src/service/conversation_manager.*`
- 已扩展 `MessageModel / MessageModelRegistry`，支持整段消息集合替换
- `ChatWindow` 当前已把会话 API、最小 WS 和消息模型注册表收口到 `ConversationManager`
- 当前第一次进入聊天窗口时，会由 `ConversationManager` 统一拉取会话列表和对应会话的一页历史消息，并同步更新到 `ConversationListModel` 和 `MessageModelRegistry`
- 当前首次进入聊天窗口时，会话列表快照会在客户端本地统一清空未读数，并把 `last_read_seq` 推进到最新消息
- 当前中间栏会话列表虽然仍保留 `QListWidget`，但其数据已经开始由会话模型快照驱动
- 当前会话列表项已显式显示最后一条消息预览与未读数角标
- 已完成客户端注册功能的编译验证和离屏启动验证
- 已完成客户端登录功能的编译验证和离屏启动验证
- 已完成客户端最小 WebSocket 接入的编译验证和离屏启动验证
- 已完成第一版会话模型骨架的编译验证和离屏启动验证
- `ChatWsClient` 当前已开始解析 `ws.ack / ws.new`，并把业务路由往上抛给 `ConversationManager`
- `ConversationManager` 当前已开始接入首批实时业务路由：
  - `ws.ack + route=message.send_text`
  - `ws.ack + route=message.send_image`
  - `ws.ack + route=message.send_file`
  - `ws.new + route=message.created`
  - `conversation.created`：先实时 upsert 会话摘要，再按当前登录用户视角补拉 detail，纠正会话列表显示
  - `friend.request.new`
  - `friend.request.accepted`
  - `friend.request.rejected`
- `ConversationManager` 当前已把 `ws.ack / ws.new` 改成按不同 `route` 分发到独立处理函数
- 聊天窗口当前发送文本消息时，已经不再直接本地追加，而是通过 `ConversationManager -> ChatWsClient -> ws.send`
- 聊天窗口输入区“表情”按钮当前已接入常用 emoji 面板，点击后可直接把选中的 emoji 插入输入框当前光标位置
- 聊天窗口当前已经把输入区入口统一成“附件”按钮：
  - 文件选择框当前默认展示“所有文件 (*)”，不再默认卡在少数扩展名筛选上
  - 可识别为图片的本地文件会走 `sendImageMessage(...)`
  - 其它任意本地文件会走 `sendFileMessage(...)`
  - 当前支持一次选择多张本地附件，并并发上传到服务端临时附件区
  - 每个附件上传完成后会分别发送 `ws.send + route=message.send_image / message.send_file`
  - 最终由 `ws.ack / ws.new + route=message.created` 收敛成正式消息
- `MessageDelegate` 当前已为图片消息补上传状态覆盖层，可直接展示：
  - 上传中进度
  - 发送中状态
  - 上传/发送失败状态
- 正式图片消息当前也会补本地缓存展示：
  - HTTP 拉下来的历史 image 消息会根据 `download_url` 异步下载并缓存
  - `ws.new + route=message.created` 进来的 image 消息也会复用同一条缓存链路
  - 缓存命中时直接复用本地图片路径，未命中时在下载完成后局部刷新消息模型
- `MessageModelRegistry` 当前已支持按 `message_id / client_message_id / seq` 做消息 upsert，避免 `ack/new` 重复插入
- 文件消息当前也已经接入客户端第一版闭环：
  - 本地文件发送占位、上传进度、`message.send_file` ack/new 收敛
  - 历史 / 实时 file 消息解析与展示
  - 点击文件卡片后下载、打开文件、打开所在目录
- 当前切换到某个会话时，会在客户端本地立即清空该会话未读数，并更新 `last_read_seq`
- 消息页右侧详情区当前已完成轻重构：头部与输入区相关控件显式收口到 `ChatWindow` 成员，并通过 setter 统一修改属性
- 好友申请弹窗当前已接入上述好友申请实时路由：
  - 收到 `friend.request.new` 时自动刷新“新的朋友”
  - 收到 `friend.request.accepted / rejected` 时自动刷新“已发送申请”
- 好友主页当前会在收到 `friend.request.accepted` 后自动刷新正式好友列表

未完成：

- WebSocket 实时消息事件与路由接入，例如：
  - 更多 `ws.send / ws.ack / ws.new`
  - 更多业务 `route`
- refresh token 续期与本地登录态恢复策略
- 更完整的 DTO / service / viewmodel 分层

### 2.2 chatServer

已完成：

- 初始化 `chatServer/CMakeLists.txt`
- 接入 Drogon
- 在 `chatServer/3rdparty/` 下统一管理服务端第三方依赖
- 建立 `chatServer/config/app.json`
- 建立 `chatServer/db/`
- 建立 `chatServer/src/app/application.*` 应用初始化层
- 提供最小可运行入口 `chatServer/src/main.cpp`
- 在 `app.json` 中写入 PostgreSQL 连接信息并建立数据库客户端初始化
- 在 `app.json` 中写入 Redis 连接信息并建立 Redis 客户端初始化
- 已为 `app.json` 当前使用字段补充中文说明
- 已建立统一日志模块 `infra/log/app_logger.*`
- 已把 Drogon / Trantor 日志统一输出到控制台和 `chatServer/logs/chatserver.log`
- 建立首个认证迁移执行脚本 `init_sql.py`，并按对象拆分为多脚本结构
- `init_sql.py` 已支持重复执行时自动跳过已存在的表和函数
- 已将 `users.account` 的唯一性规则调整为按原值比较，登录账号大小写敏感
- 提供 `GET /health` 健康检查接口，并返回 PostgreSQL / Redis 可用性状态
- 完成本地配置、编译、启动验证
- 已清理第三方库中不参与当前构建的示例、测试、CI 元数据和本地 git 元数据
- 已将 `3rdparty/hiredis` 从 gitlink 记录改为普通源码目录追踪
- 已为当前脚手架代码和 CMake 补充中文注释
- 已将服务端启动代码注释风格对齐客户端
- 已补充一份面向当前项目的 Drogon 基础知识与接口参考文档
- 已将 Drogon 核心函数说明统一补充为“函数作用 / 参数解析 / 返回值解析 / 最小示例”
- 已建立 `transport/http`、`service`、`repository`、`infra/id`、`infra/security` 的最小认证落点
- 已实现 `POST /api/v1/auth/register`
- 已完成注册接口的成功、参数非法、账号冲突三条主路径验证
- 已实现 `POST /api/v1/auth/login`
- 已完成登录接口返回 `device_session_id / access_token / refresh_token / expires_in_sec` 验证
- 已完成登录成功后写入 `device_sessions` 验证
- 已将同一用户同一设备的重复登录改为显式 `409` 冲突返回
- 已实现 `POST /api/v1/auth/logout`，可按当前 `access_token` 失效对应设备会话
- 已建立认证 HTTP 协议文档 `docs/auth-http-api.md`
- 已建立认证 HTTP 字段说明文档 `docs/auth-http-fields.md`
- 已建立 WebSocket 协议文档 `docs/ws-protocol.md`
- 已建立 WebSocket 事件接口文档 `docs/ws-api.md`
- 已建立 WebSocket 结构设计文档 `docs/ws-architecture.md`
- 已将 WebSocket 业务事件协议口径统一为 `ws.send / ws.ack / ws.new + route`
- 已建立 `src/transport/ws/chat_ws_controller.*`、`src/service/ws_session_service.*`、`src/infra/ws/connection_registry.*` 和 `src/protocol/dto/ws/*.h`
- 已打通 WebSocket 路径 `/ws`，并完成 `ws.auth` 最小联调；当前保活改为依赖协议层 `Ping/Pong`
- 已新增 `src/protocol/dto/ws/ws_business_dto.h` 与 `src/service/realtime_push_service.*`
- 已建立 `ws.send / ws.ack / ws.new` 最小基础框架，服务端当前可解析 `ws.send` 并返回基础 `ws.ack`
- 已在 HTTP 成功链路接入首批 `ws.new` 主动推送：
  - 发送好友申请后推送 `friend.request.new`
  - 同意好友申请后推送 `friend.request.accepted`
  - 拒绝好友申请后推送 `friend.request.rejected`
  - 创建或复用私聊后推送 `conversation.created`
- 已建立用户资料与头像接口文档 `docs/user-http-api.md`
- 已建立文件上传与下载接口文档 `docs/file-http-api.md`
- 已补强注册链路核心代码的中文注释，便于顺着代码阅读调用路径
- 已把 `chatServer` 头文件中的核心函数声明统一补成 Doxygen 风格注释
- 已删除单独的数据库客户端名常量头文件，repository 直接使用 Drogon 默认 `getDbClient()`
- 已建立统一文件存储抽象 `src/storage/file_storage.*`
- 已建立本地文件存储实现 `src/storage/local_storage.*`
- 已在启动阶段自动创建 `uploads/tmp`、`uploads/files`、`uploads/avatars`
- 已将文件存储配置并入 `chatServer/config/app.json` 的 `chatserver.storage`
- 已实现临时头像上传、临时头像预览、资料更新和按用户 ID 获取头像接口
- 已将注册接口接入 `avatar_upload_key`，并把 `users.avatar_url` 统一作为头像 `storage key`
- 已在迁移脚本中加入 `friend_requests`、`friendships`、`conversations`、`conversation_members`、`messages` 和 `attachments` 等聊天域基础表
- 已实现 `POST /api/v1/files/upload`，支持以流式 multipart 方式上传临时聊天附件并返回 `attachment_upload_key`；当前业务大小上限已放宽到 `1 GB`
- 普通附件当前不会按扩展名做服务端白名单限制；除图片链路要求 `mime_type` 为 `image/*` 外，其它任意文件都会按 `media_kind = file` 处理
- 已实现 `GET /api/v1/files/{attachment_id}`，支持按附件 ID 读取附件文件内容
- 已实现 `POST /api/v1/conversations/private`
- 已实现 `GET /api/v1/conversations`
- 已实现 `GET /api/v1/conversations/{id}/messages`
- 已实现 `POST /api/v1/conversations/{id}/messages/text`
- 已建立会话域协议文档 `docs/conversation-http-api.md`
- 已新增 `src/service/ws_message_service.*`
- 已接入 WebSocket 消息发送业务路由：
  - `ws.send + route=message.send_text`
  - `ws.ack + route=message.send_text`
  - `ws.send + route=message.send_image`
  - `ws.ack + route=message.send_image`
  - `ws.send + route=message.send_file`
  - `ws.ack + route=message.send_file`
  - `ws.new + route=message.created`
- 当前文本消息实时链路已支持：
  - 当前连接成员权限校验
  - 文本消息写库
  - 文本消息 `ws.ack`
  - 向会话在线成员推送 `message.created`
- 当前图片消息实时链路已支持：
  - 先消费 `POST /api/v1/files/upload` 返回的临时 `attachment_upload_key`
  - 临时附件上传阶段会边接收边写盘，不再把整份 multipart 文件整理进内存
  - 在消息确认阶段把临时附件转成正式附件并写入 `attachments`
  - 正式附件准备阶段直接从临时文件导入正式目录，不再整文件回读内存
  - 写入 image 消息并回 `ws.ack + route=message.send_image`
  - 向会话在线成员推送 `ws.new + route=message.created`
  - 已修复图片消息链路里 `attachment_upload_key` 在异步 lambda 中被过早 move 后偶发丢失的问题
- 当前文件消息实时链路也已支持：
  - 先消费 `POST /api/v1/files/upload` 返回的临时 `attachment_upload_key`
  - 再要求当前临时附件 `media_kind = file`
  - 在消息确认阶段把临时附件转成正式附件并写入 `attachments`
  - 写入 file 消息并回 `ws.ack + route=message.send_file`
  - 向会话在线成员推送 `ws.new + route=message.created`
  - 和图片消息共用同一套附件确认与回滚骨架
  - 已修复共享异步骨架里 `PreparedAttachmentResult` 被重复 `std::move` 后导致正式附件字段偶发为空的问题
- 已完成一轮真实联调验证：
  - 发送方收到 `ws.ack + route=message.send_text`
  - 发送方和接收方都收到 `ws.new + route=message.created`
- 客户端已补齐 `ConversationApiClient` 对上述 5 条会话域 HTTP 接口的最小调用封装，但还未接入真实会话列表、历史消息和消息发送界面
- 客户端会话模型方案已按更轻的第一版开始落地：`ConversationManager + ConversationListModel + MessageModelRegistry`，最小缓存状态先放在 `ConversationManager` 内部

未完成：

- refresh token 续期接口
- `ws.send / ws.ack / ws.new` 的更多业务 `route`
- `infra/db` 深化封装

## 3. 阶段进度拆分

### 第 1 步：搭服务端基础脚手架

状态：进行中

已完成：

- `chatServer/CMakeLists.txt`
- Drogon 接入
- 配置文件
- 数据库迁移目录
- 应用初始化层
- PostgreSQL 连接初始化
- Redis 连接初始化
- 统一日志模块
- token provider
- 第一版认证 SQL 脚本及统一执行入口
- 健康检查接口
- 第三方依赖目录规范
- 文件存储抽象
- 本地文件存储实现与上传目录自动创建

还缺：

- 数据库基础设施

完成判定：

- 服务端存在统一配置入口
- 服务端存在应用初始化骨架
- PostgreSQL / Redis 连接初始化有明确落点
- 迁移目录已落盘
- 文件存储抽象已落盘
- 默认本地存储目录已能随启动自动创建

### 第 2 步：完成受保护接口基础与登录态管理

状态：进行中

前置条件：

- 第 1 步完成
- 配置体系可承载数据库连接和 JWT 相关配置
- HTTP 控制器和 service 分层初步建立

已完成：

- 注册接口 `POST /api/v1/auth/register`
- 注册参数校验
- `users` 表真实写入
- bcrypt 密码哈希生成
- 账号唯一约束冲突映射为 `409/account already exists`
- 登录接口 `POST /api/v1/auth/login`
- access token / refresh token 生成与返回
- `device_sessions` 写入
- `docs/ws-protocol.md` 已建立
- 客户端真实登录链路
- 客户端本地 `access_token` 持久化（当前暂不接 refresh）

还缺：

- 服务端统一 Bearer Token 受保护接口基础
- 当前登录用户 / 当前 device_session 提取能力
- 客户端登录态恢复策略深化
- refresh token 续期接口

### 第 3 步及之后

状态：好友链路服务端已完成，客户端大体完成；私聊会话服务端已完成，客户端未开始

说明：

- `friend_requests` 和 `friendships` 基础表已进入迁移脚本
- 服务端已完成用户搜索、发送好友申请、接受/拒绝好友申请、查询收件箱/发件箱接口
- 客户端“添加好友”弹窗已接入真实用户搜索、发送好友申请、发件箱、收件箱和接受/拒绝交互
- 客户端好友主页已接入真实 `GET /api/v1/friends`
- 服务端已完成私聊会话创建、会话详情、会话列表、历史消息和文本发消息 5 个 HTTP 接口
- 客户端当前已接入“好友详情页 -> 创建或复用私聊会话”这条最小链路
- 客户端会话列表、历史消息、文本消息真实链路、附件、在线状态、音视频都还没有正式进入开发

## 4. 当前缺口

从“可以继续推进开发”的角度看，当前最关键的缺口是：

- 服务端虽然已经打通注册、登录、当前设备登出和私聊会话 HTTP 基础接口，但统一的 Bearer Token 受保护接口基础还没单独收口
- 客户端好友主页已经接成真实好友列表，但会话列表和聊天历史仍是演示数据
- Redis 只完成基础接入，presence、缓存、Pub/Sub 等真正业务用途还没有开始
- 客户端虽然已经具备注册 / 登录 HTTP 闭环和真实好友链路，但聊天仍停留在演示数据阶段
- 客户端虽然已经落下最小认证分层，但好友、会话和消息真实链路仍未落地
- 客户端与服务端的 WebSocket 最小长连接已经打通，当前只承接 `ws.auth`

## 5. 下一步要做什么

当前建议的下一步顺序如下：

1. 客户端接入真实 `GET /api/v1/conversations`
2. 客户端接入真实 `GET /api/v1/conversations/{id}/messages`
3. 客户端接入真实 `POST /api/v1/conversations/{id}/messages/text`
4. 在真实会话数据链路稳定后，再把模型层和 WebSocket 实时消息事件接进去

## 6. 近期目标

近期目标已经从“补客户端长连接接入”进入“补客户端真实会话数据接入”阶段。

当前最近的里程碑应定义为：

- 客户端接入真实会话列表、历史消息和最小文本发送链路

这个里程碑完成后，才进入：

- 私聊会话模型、消息模型和 WebSocket 实时消息链路开发

## 7. 更新规则

以后每次开发完成后，这份文档至少要同步以下内容：

- 当前阶段有没有变化
- 哪些内容刚完成
- 还缺什么
- 下一步准备做什么

如果某次改动会影响整体阶段判断，也要同时更新 `docs/im-system-design.md`。

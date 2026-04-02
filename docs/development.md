# 项目开发文档

最近同步时间：2026-03-21

## 1. 文档目的

这份文档负责记录项目当前的开发落地状态、构建运行方式、第三方依赖管理方式，以及开发过程中的文档同步约定。

它和其它文档的分工如下：

- `docs/development.md`：记录当前实际落地情况、目录结构、构建命令、开发约定
- `docs/progress.md`：记录当前开发进度、已完成项、缺口、下一步计划
- `docs/chatclient-architecture.md`：记录客户端分层设计、目录职责、消息收发链路和落地顺序
- `docs/client-conversation-models.md`：记录客户端会话模型、消息模型注册表和会话调度器的职责边界与调用关系
- `docs/drogon-guide.md`：记录当前项目真正会用到的 Drogon 基础知识、常用接口和使用约定
- `docs/auth-http-api.md`：记录当前已落地的认证 HTTP 接口协议
- `docs/auth-http-fields.md`：记录当前认证 HTTP 请求头、请求体、响应体和字段含义
- `docs/user-http-api.md`：记录用户资料、临时头像上传、头像读取等用户域 HTTP 接口协议
- `docs/friend-http-api.md`：记录用户搜索、好友申请、接受/拒绝和好友申请列表等好友域 HTTP 接口协议
- `docs/conversation-http-api.md`：记录私聊会话创建、会话详情、会话列表、历史消息和文本发消息等会话域 HTTP 接口协议
- `docs/file-http-api.md`：记录聊天附件上传和按附件 ID 下载等文件域 HTTP 接口协议
- `docs/ws-protocol.md`：记录 WebSocket 统一信封、`ws.auth`，以及统一 `ws.send / ws.ack / ws.new + route` 协议约定
- `docs/ws-api.md`：记录当前已落地的 WebSocket 事件、发送结构、返回结构和字段含义
- `docs/ws-architecture.md`：记录 WebSocket 模块结构方向、目录分工、文件职责和实施顺序
- `docs/im-system-design.md`：记录整体系统设计、模块边界、分阶段实施方案

## 2. 文档同步约定

从现在开始，代码修改必须同步更新对应文档，不再允许“代码已经变了，文档还停留在旧状态”。

同步规则如下：

- 修改构建方式、目录结构、第三方依赖管理方式时，更新本文档
- 修改当前开发阶段、完成度、缺口、下一步计划时，更新 `docs/progress.md`
- 修改客户端目录边界、消息收发链路、登录链路和 service 设计时，更新 `docs/chatclient-architecture.md`
- 修改客户端会话模型、消息模型注册表和会话调度器职责边界时，更新 `docs/client-conversation-models.md`
- 修改 Drogon 接入方式、框架使用约定、常用接口说明时，更新 `docs/drogon-guide.md`
- 修改认证 HTTP 协议和参数约定时，更新 `docs/auth-http-api.md`
- 修改认证接口字段含义、公共响应结构、请求头字段说明时，更新 `docs/auth-http-fields.md`
- 修改用户资料、临时文件上传、头像读取等用户域 HTTP 协议时，更新 `docs/user-http-api.md`
- 修改用户搜索、好友申请、接受/拒绝、好友申请列表等好友域 HTTP 协议时，更新 `docs/friend-http-api.md`
- 修改私聊会话创建、会话详情、会话列表、历史消息和文本发消息等会话域 HTTP 协议时，更新 `docs/conversation-http-api.md`
- 修改聊天附件上传、附件下载等文件域 HTTP 协议时，更新 `docs/file-http-api.md`
- HTTP 协议文档必须同时说明“发送字段”和“返回字段”，不允许只写响应、不写请求
- 每一条已落地的 HTTP API 都必须在对应协议文档里提供“测试用例”小节，至少包含一个成功用例；如果该接口存在明确高频失败分支，也应补一个失败用例
- 修改 WebSocket 统一信封、鉴权事件，以及 `ws.send / ws.ack / ws.new + route` 实时事件约定时，更新 `docs/ws-protocol.md`
- WebSocket 事件接口文档也应同时说明“客户端发送字段”和“服务端返回字段”，不允许只写单边
- 每一条已落地的 WebSocket 事件也必须在对应协议文档里提供可复现的测试方式，至少说明触发条件、发送示例和预期返回
- 修改 WebSocket 事件的作用、发送结构、返回结构和字段含义时，更新 `docs/ws-api.md`
- 修改 WebSocket 模块目录、文件分工、连接生命周期和实施顺序时，更新 `docs/ws-architecture.md`
- 修改整体架构、模块边界、实施阶段判断时，更新 `docs/im-system-design.md`
- 新增或修改对外 HTTP / WebSocket 协议时，除更新设计文档外，还应补充专门的协议文档
- 如果某次改动同时影响“实现现状”和“设计判断”，两份文档都要更新
- `docs/drogon-guide.md` 中的函数说明统一包含：函数作用、参数解析、返回值解析、最小示例

## 3. 当前仓库结构

当前仓库主要由以下部分组成：

- `chatClient/`：Qt Widgets 客户端，当前已具备登录页、聊天页和消息展示原型
- `chatServer/`：Drogon 服务端工程，当前已完成最小脚手架、认证最小闭环和本地文件存储抽象
- `docs/`：设计文档、开发文档、开发进度文档和 Drogon 参考文档

## 4. 当前开发状态

### 4.1 chatClient

客户端当前状态：

- 已完成 Qt Widgets 基础界面
- 已完成 `QListView + Model + Delegate` 的消息展示链路
- `MessageDelegate` 当前已支持按消息类型渲染图片卡片和文件卡片：
  - 图片消息会优先绘制本地缩略图，并在上传中显示覆盖层进度
  - 文件消息会显示文件名、大小、状态文案和下载进度条
- 已建立客户端配置文件 `chatClient/config/app.json`
- 已建立客户端配置读取层 `chatClient/src/config/appconfig.*`
- 已建立客户端最小认证分层落点：`src/dto/auth_dto.*`、`src/api/auth_api_client.*`、`src/service/auth_service.*`
- 已通过 `QNetworkAccessManager` 接入真实注册接口 `POST /api/v1/auth/register`
- 已通过 `QNetworkAccessManager` 接入真实登录接口 `POST /api/v1/auth/login`
- 已补齐 `src/dto/conversation_dto.*` 与 `src/api/conversation_api_client.*`，当前已可调用会话域 5 条 HTTP 接口：`POST /api/v1/conversations/private`、`GET /api/v1/conversations`、`GET /api/v1/conversations/{conversation_id}`、`GET /api/v1/conversations/{conversation_id}/messages`、`POST /api/v1/conversations/{conversation_id}/messages/text`
- 已新增 `src/dto/file_dto.*` 与 `src/api/file_api_client.*`，当前已支持临时附件上传、正式附件下载，以及上传 / 下载进度信号透出；客户端上传前大小校验已统一放宽到 `1 GB`
- 已新增 `src/model/conversationlistmodel.*`，用于承载会话摘要展示数据，不直接发 HTTP，也不直接处理 WS
- 已扩展 `MessageModel / MessageModelRegistry`，支持整段消息集合替换，以及按消息身份做 upsert，便于接历史消息快照和 `ws.ack / ws.new`
- 已新增 `src/service/conversation_manager.*`，统一持有 `ConversationApiClient`、`ChatWsClient`、`ConversationListModel` 和 `MessageModelRegistry`
- `ChatWindow` 当前已把会话 API、最小 WS 和消息注册表的直接依赖收口到 `ConversationManager`
- 当前第一次进入聊天窗口时，会由 `ConversationManager` 统一拉取 `GET /api/v1/conversations`，并继续为每个会话拉取一页历史消息，再同步到会话模型和消息模型
- 当前首次进入聊天窗口时，会话列表快照会在客户端本地统一归零 `unreadCount`，并把 `last_read_seq` 推进到当前 `last_message_seq`
- 当前中间栏会话列表虽然仍是 `QListWidget`，但已开始从 `ConversationManager` 的会话快照同步展示，不再只依赖硬编码演示项
- 当前会话列表项已显式展示最后一条消息预览和未读数角标，不再只把这些信息藏在内部 role 中
- 当前客户端会话模型方案已收敛为 `ConversationManager + ConversationListModel + MessageModelRegistry`，最小缓存状态先内收在 `ConversationManager`，暂不强制拆 `ConversationStore`
- `ChatWsClient` 当前已开始解析 `ws.ack / ws.new` 并把业务路由转交给 `ConversationManager`
- `ConversationManager` 当前已接入：
  - `ws.ack + route=message.send_text`
  - `ws.ack + route=message.send_image`
  - `ws.ack + route=message.send_file`
  - `ws.new + route=message.created`
  - `ws.new + route=conversation.created`
- `ConversationManager` 当前对 `ws.ack / ws.new` 都按不同 `route` 分发到独立处理函数，避免实时事件入口继续膨胀
- 聊天窗口文本消息发送当前已经改为 `ConversationManager -> ChatWsClient -> ws.send`
- 聊天窗口输入区“表情”按钮当前已接入轻量 emoji 面板；点击按钮会弹出常用表情栏，点击任一 emoji 后会把字符插入 `QTextEdit` 当前光标位置
- 聊天窗口当前已把输入区入口调整为统一“附件”按钮：
  - 文件选择框当前默认展示“所有文件 (*)”，因此 UI 层不会再把能力限制成少数扩展名
  - 可识别为图片的本地文件会走 `ws.send + route=message.send_image`
  - 其它任意本地文件会走 `ws.send + route=message.send_file`
  - 当前支持一次选择一张或多张本地附件；每个附件都会先写入一条“上传中”本地占位消息，再通过 `FileApiClient` 独立上传临时附件，因此多条图片/文件消息都可以并发上传
- 正式图片消息当前也会补本地可显示资源：
  - HTTP 历史消息里的 image 消息会根据 `download_url` 异步下载图片并写入本地缓存
  - `ws.new + route=message.created` 推送进来的 image 消息也会复用同一条缓存下载链路
  - 同一 `remote_url` 在单次运行中只会发起一次下载，完成后再局部刷新对应消息行的图片字段
- 正式文件消息当前也已接入完整接收与交互链路：
  - HTTP 历史消息和 `ws.new + route=message.created` 的 file 消息都会被解析成 `MessageType::File`
  - `MessageListView` 会为文件卡片提供右键菜单，支持下载到默认位置、下载到自定义路径、打开文件、打开所在文件夹
  - `ConversationManager::downloadFileMessage(...)` 会负责正式附件下载、进度回写，以及下载完成后的 `localPath` 局部刷新
- 当前切换到某个会话时，会先在本地把该会话 `unreadCount` 清零，并把 `last_read_seq` 推进到当前最新消息
- `createMessageContentPage()` 当前已完成一次轻重构：消息页头部和输入区相关控件现在显式挂在 `ChatWindow` 成员上，并新增了若干 setter 统一更新标题、副标题、提示文案和按钮状态
- 好友申请相关实时路由当前已接入到好友界面：
  - `friend.request.new` 会刷新“新的朋友”
  - `friend.request.accepted / rejected` 会刷新“已发送申请”
  - `friend.request.accepted` 还会刷新正式好友列表
- 已新增最小 `src/dto/ws_dto.*` 与 `src/ws/chat_ws_client.*`，当前已接入 `ws.auth`
- 已把注册页从纯 UI 原型改为真实 HTTP 提交链路，并补充昵称输入、提交中状态和失败提示
- 已把登录页从占位按钮改为真实 HTTP 提交链路，并补充登录中状态、失败提示和登录成功后的页面切换
- 已将注册 / 登录接口返回的常见英文错误提示统一映射为中文界面文案
- 已将认证错误中文化逻辑从 `AuthService` 中拆分到独立文件 `chatClient/src/service/auth_error_localizer.*`
- 已在客户端本地持久化 `device_session_id + access_token`，当前统一把 `access_token` 当作长期可复用令牌
- 若同一用户同一设备重复登录，客户端当前会显示“当前设备已登录该账号”，不再覆盖旧会话
- 已在 `docs/chatclient-architecture.md` 中补充客户端认证错误翻译示例与翻译规则说明
- 已在 `docs/chatclient-architecture.md` 中补充聊天页结构建议，明确左侧导航负责“消息 / 好友”模式切换，“添加好友”入口放在好友模式对应的中间栏顶部，会话列表当前可继续使用 `QListWidget`
- 已新增 `docs/client-conversation-models.md`，单独说明 `ConversationListModel`、`MessageModelRegistry` 和 `ConversationManager`
- 已将应用展示名、窗口标题以及 HTTP / WebSocket 基础地址统一收口到配置文件
- 已建立 `chatClient/src/log/app_logger.*` 统一日志模块
- 已接管 Qt 全局消息输出，并把客户端日志同时输出到控制台和 `chatClient/logs/chatclient.log`
- 已将客户端日志配置统一写入 `chatClient/config/app.json` 的 `log` 对象
- 客户端启动时会优先加载默认配置文件，加载失败会直接弹窗并终止启动
- 已将聊天页重构为“左侧导航栏 + 中间列表栏 + 右侧内容区”三段式骨架
- 已支持左侧“消息 / 好友”主入口切换，并联动切换中间栏与右侧内容区
- 已在左侧主导航入口下方补充“切换账号 / 登出”两个按钮
- 会话列表当前继续使用 `QListWidget`
- 好友模式当前已补“先搜索账号、再申请添加”的独立弹窗骨架，并已拆到单独窗口类 `qt_widget/addfrienddialog.*`
- 注册页已补头像选择入口，选择图片后会立即调用临时头像上传接口
- 注册页头像区当前已调整到表单第一项，并为注册页增加纵向空间，避免和后续输入项重叠
- 登录页和注册页当前都已显式指定键盘 Tab 顺序，注册页按“头像 -> 账号 -> 昵称 -> 密码 -> 确认密码 -> 注册”流转
- 客户端已新增 `src/api/user_api_client.*` 和 `src/dto/user_dto.*`，用于承接临时头像上传和头像文件读取
- 客户端已新增 `src/dto/friend_dto.*`、`src/api/friend_api_client.*`、`src/service/friend_service.*` 和 `src/service/friend_error_localizer.*`
- 注册成功前，客户端会先拿到 `avatar_upload_key`，再在 `POST /api/v1/auth/register` 中一并提交
- 聊天页左上角导航头像当前已支持“优先加载真实头像，失败时退回到用户名文本占位”
- 客户端已新增 `logout_path` 配置，并通过 `AuthApiClient/AuthService` 调用服务端 `POST /api/v1/auth/logout`
- “切换账号”当前会登出当前会话并返回登录页；“登出”会在请求服务端登出后直接退出整个程序
- 当前程序关闭时也会自动向服务端发送登出请求，并清理本地登录态
- “添加好友”弹窗当前已接入真实好友搜索、发送好友申请和“我已发送申请记录”拉取链路
- “添加好友”弹窗当前已接入真实“新的朋友”链路，支持拉取收到的好友申请并直接同意 / 拒绝
- 服务端好友域当前已补 `GET /api/v1/friends`，可直接按当前登录态查询正式好友列表
- 客户端好友主页当前已接入真实 `GET /api/v1/friends`，进入“好友”模式和关闭“添加好友”弹窗后都会刷新中间栏好友列表
- 好友模式右侧详情 hero 区当前已补对应好友头像，选中好友后会按用户 ID 拉取真实头像文件，失败时退回到昵称文本占位
- 好友详情页“发起会话”按钮当前已接入真实 `POST /api/v1/conversations/private`，但暂不把会话结果写入本地会话列表
- 已修正好友列表接入后 `ChatWindow::updateFriendList(...)` 的声明 / 定义签名不一致问题，避免编辑器持续报红
- 弹窗当前结构已调整为顶部选项栏，分别切换“申请好友”和“新的朋友”两个页面；打开弹窗时会同时拉取收件箱和发件箱
- “申请好友”页当前已固定顶部搜索结果区和验证消息输入区高度，仅让底部“已发送申请记录”列表承担弹性伸缩，避免结果内容与附言输入区互相挤压
- 当前已接入真实注册 / 登录 HTTP 链路与最小 WebSocket 长连接，但会话列表、历史消息和实时消息事件仍未开始

### 4.2 chatServer

服务端当前状态：

- 已初始化 `chatServer/CMakeLists.txt`
- 已接入 Drogon
- 已建立 `chatServer/config/app.json`
- 已建立 `chatServer/db/`
- 已建立 `chatServer/src/app/application.*` 应用初始化层
- 已提供最小可运行入口 `chatServer/src/main.cpp`
- 已在 `app.json` 中写入 PostgreSQL 连接信息并建立数据库客户端初始化
- 已在 `app.json` 中写入 Redis 连接信息并建立 Redis 客户端初始化
- 已建立 `chatServer/src/infra/log/app_logger.*` 统一日志模块
- 已统一接管 Drogon / Trantor 日志输出，并把日志同时输出到控制台和 `chatServer/logs/chatserver.log`
- 已将首个认证迁移拆分为多脚本结构，并提供统一执行脚本 `chatServer/db/init_sql.py`
- 已暴露 `GET /health` 健康检查接口，并返回 PostgreSQL / Redis 可用性状态
- 已统一把第三方依赖收口到 `chatServer/3rdparty/`
- 已为当前服务端脚手架代码和 CMake 补充中文注释，便于后续维护
- 已将服务端启动代码注释风格对齐客户端，统一采用“职责边界 + 分点说明”的写法
- 已建立正式业务分层的最小落点：`transport/http`、`service`、`repository`、`infra/id`、`infra/security`
- 已实现 `POST /api/v1/auth/register` 注册接口，并完成数据库真实写入验证
- 已实现 `POST /api/v1/auth/login` 登录接口，并完成 `device_session_id / access_token / refresh_token` 返回验证
- 已实现 `POST /api/v1/auth/logout` 登出接口，并支持按当前 `access_token` 失效对应 `device_session`
- 已建立 `chatServer/src/infra/security/token_provider.*`，统一生成 access token / refresh token
- 已在登录成功时写入 `device_sessions`，并同步更新 `users.last_login_at`
- 同一用户同一设备若已存在活跃会话，当前会明确返回 `409/device already logged in`
- 已将 `users.avatar_url` 的实际语义统一为头像 `storage key`
- 已实现 `POST /api/v1/users/avatar/temp`，支持未登录上传临时头像并返回 `avatar_upload_key`
- 已实现 `GET /api/v1/users/avatar/temp`，支持按 `avatar_upload_key` 预览临时头像
- 已实现 `PATCH /api/v1/users/me/profile`，支持通过可选字段更新昵称和头像
- 已实现 `GET /api/v1/users/{user_id}/avatar`，支持按用户 ID 读取正式头像文件
- 已将注册接口改为通过 `avatar_upload_key` 确认使用临时头像
- 已建立统一文件存储抽象 `chatServer/src/storage/file_storage.*`
- 已建立本地文件存储实现 `chatServer/src/storage/local_storage.*`
- 已在启动阶段初始化默认文件存储，并自动创建 `chatServer/uploads/tmp`、`chatServer/uploads/files`、`chatServer/uploads/avatars`
- 已在 `chatServer/config/app.json` 中补充 `chatserver.storage` 配置，用于统一描述本地文件存储根目录与子目录
- 已将注册接口协议单独整理到 `docs/auth-http-api.md`
- 已建立用户资料与头像接口文档 `docs/user-http-api.md`
- 已建立好友搜索与好友申请接口文档 `docs/friend-http-api.md`
- 已建立会话与消息接口文档 `docs/conversation-http-api.md`
- 已建立文件上传与下载接口文档 `docs/file-http-api.md`
- 已建立 `docs/ws-protocol.md`，单独说明统一 WebSocket 信封、`ws.auth` 和统一 `ws.send / ws.ack / ws.new + route` 协议
- 已建立 `docs/ws-api.md`，单独说明当前已落地的 WebSocket 事件、发送结构和返回结构
- 已建立 `docs/ws-architecture.md`，单独说明 WebSocket 的目录结构、职责边界和实施顺序
- 已为注册链路核心代码补充更完整的中文注释，覆盖 DTO、controller、service、repository、ID 生成和密码哈希
- 已将 `chatServer` 头文件中的核心函数声明统一补充为接近客户端风格的 Doxygen 注释，格式包含 `@brief / @param / @return`
- 已在 `db/init_sql/` 中新增 `friend_requests`、`friendships`、`conversations`、`conversation_members`、`messages` 和 `attachments` 等社交关系、会话和附件基础表
- 已实现 `POST /api/v1/files/upload`，支持已登录用户以流式 multipart 方式上传临时聊天附件并返回 `attachment_upload_key`；当前 controller 会边接收边写 staging file，业务大小上限已放宽到 `1 GB`
- 当 `POST /api/v1/files/upload` 被用于普通附件时，服务端当前不会按扩展名对白名单做限制；除图片消息要求 `mime_type` 以 `image/` 开头外，其它任意文件都会被归类到 `media_kind = file`
- 已实现 `GET /api/v1/files/{attachment_id}`，支持按附件 ID 下载已上传文件
- `init_sql.py` 当前已覆盖认证域、好友关系域、私聊会话域和附件域的最小表结构，不再只执行认证相关表
- `init_sql.py` 当前会在执行前检查目标函数 / 表是否已存在，已存在对象会直接跳过，便于重复执行
- 已实现 `GET /api/v1/users/search`，支持按账号确认目标用户是否存在
- 已实现 `POST /api/v1/friends/requests`，支持发送好友申请
- 已实现 `POST /api/v1/friends/requests/{request_id}/accept` 和 `.../reject`
- 已实现 `GET /api/v1/friends/requests/incoming` 和 `.../outgoing`
- 已建立 `src/repository/friend_repository.*`、`src/service/friend_service.*` 和 `src/transport/http/friend_controller.*`
- 已建立 `src/repository/conversation_repository.*`、`src/service/conversation_service.*` 和 `src/transport/http/conversation_controller.*`
- 已实现 `POST /api/v1/conversations/private`，支持在好友之间创建或复用稳定私聊会话
- 已实现 `GET /api/v1/conversations/{conversation_id}`，支持查询单条会话详情和当前成员状态
- 已实现 `GET /api/v1/conversations`，支持按当前登录态查询会话列表
- 已实现 `GET /api/v1/conversations/{conversation_id}/messages`，支持按 `before_seq / after_seq` 拉取历史消息
- 已实现 `POST /api/v1/conversations/{conversation_id}/messages/text`，支持最小文本消息落库并更新会话摘要
- 已建立 `src/transport/ws/chat_ws_controller.*`、`src/service/ws_session_service.*`、`src/infra/ws/connection_registry.*` 和 `src/protocol/dto/ws/*.h`
- 已打通 WebSocket 路径 `/ws`，支持 `ws.auth`、`ws.auth.ok`、`ws.error`
- 已新增 `src/protocol/dto/ws/ws_business_dto.h` 与 `src/service/realtime_push_service.*`
- 已建立 `ws.send / ws.ack / ws.new` 最小基础框架，并支持 `ws.send` 的基础解析和 `ws.ack` 失败确认
- 已在好友申请和私聊创建的 HTTP 成功链路接入首批 `ws.new` 推送：
  - `friend.request.new`
  - `friend.request.accepted`
  - `friend.request.rejected`
  - `conversation.created`
    - 服务端当前会按接收方视角重新回读会话摘要后再推送，不再直接复用创建者视角 DTO
    - 客户端收到事件后仍会再补一次 `fetchConversationDetail(...)`，把会话列表标题、头像和未读数纠正到当前登录用户视角
- 已新增 `src/service/ws_message_service.*`，专门处理 WebSocket 消息发送路由
- 当前服务端已支持：
  - `ws.send + route=message.send_text`
  - `ws.ack + route=message.send_text`
  - `ws.send + route=message.send_image`
  - `ws.ack + route=message.send_image`
  - `ws.send + route=message.send_file`
  - `ws.ack + route=message.send_file`
  - `ws.new + route=message.created`
- `message.send_image` 当前服务端实现已补齐“临时附件转正式附件”的真实链路：
  - 先校验发送方属于目标会话
  - 再根据 `attachment_upload_key` 准备正式附件
  - 正式附件准备阶段会直接从临时文件导入正式目录，不再把整份大文件重新读回内存
  - 然后写入图片消息并广播 `message.created`
  - 已修复异步 lambda 中同时读取并 move 请求对象导致 `attachment_upload_key` 偶发变空的问题
- `message.send_file` 当前服务端也已按同样模式落地：
  - 先消费 `POST /api/v1/files/upload` 返回的临时 `attachment_upload_key`
  - 再校验发送方属于目标会话且附件 `media_kind = file`
  - 然后把临时附件转正式附件、写入 file 消息并广播 `message.created`
  - 文件消息和图片消息当前共用同一套“附件确认 -> 正式消息 -> 回 ack/new”骨架，只是在最终 `message_type / content` 字段口径上不同
  - 当前还额外修正了共享异步骨架里 `PreparedAttachmentResult` 被重复 `std::move` 的问题，避免正式附件字段在写消息前被提前掏空，导致 `sender_id / attachment_id / file_name / download_url` 异常为空
- 当前已完成一轮真实联调验证：
  - 发送方收到 `ws.ack + route=message.send_text`
  - 发送方和接收方都收到 `ws.new + route=message.created`
- 客户端当前已把图片发送按钮接到真实链路：
  - 当前支持一次选择多张图片；`ChatWindow` 会为每张图片分别调用 `ConversationManager::sendImageMessage(...)`
  - 先通过 `FileApiClient` 上传临时图片附件
  - 上传中持续回写 `MessageDelegate` 图片气泡里的进度覆盖层
  - 上传成功后发送 `ws.send + route=message.send_image`
  - 再由 `ws.ack / ws.new + route=message.created` 收敛成正式图片消息
- 当前连接保活统一依赖 WebSocket 协议层 `Ping/Pong`，不再处理业务层 JSON `ping / pong`
- 已完成服务端 WebSocket 最小联调验证：`ws.auth.ok` 和 `pong` 均能正常返回
- 客户端已建立 `src/dto/ws_dto.*` 与 `src/ws/chat_ws_client.*`
- 聊天窗口当前已在持有有效登录态时建立到 `/ws` 的最小 WebSocket 长连接，并完成 `ws.auth`

当前还未开始或未完成的部分：

- `infra/db` 深化封装
- 除消息发送外，其它 WebSocket 输入型业务 `route` 仍未开始，例如：
  - `conversation.create_private`
  - `friend.request.create`
- 客户端真实会话列表与历史消息接入
- WebSocket 实时消息路由与推送事件，例如：
  - 更多 `route`

当前约束：

- 当前项目已经接入 `PostgreSQL + Redis`
- 当前 `Redis` 只完成基础连接、配置加载和健康检查探测
- presence、缓存、Pub/Sub、跨实例协作等 Redis 业务能力仍未开始

## 5. 服务端构建与运行

从现在开始，统一使用项目根目录下的 `tmpbuild/` 作为本地构建输出目录。

约定如下：

- 服务端默认构建目录：`tmpbuild/chatServer`
- 后续新增客户端或测试目标时，也统一放在 `tmpbuild/` 下的对应子目录
- 不再每次随手创建新的临时构建目录，避免重复全量编译

推荐命令：

```bash
cmake -S chatServer -B tmpbuild/chatServer
cmake --build tmpbuild/chatServer -j
./tmpbuild/chatServer/chatServer
```

客户端配置约定：

- 客户端默认配置文件路径为 `chatClient/config/app.json`
- 当前通过 CMake 编译定义把默认配置文件绝对路径注入到客户端程序
- 这样即使从不同工作目录启动 `tmpbuild/chatClient/chatClient`，也能稳定读取同一份配置
- 当前配置文件承载三类信息：应用展示信息、服务端基础地址信息、客户端日志输出策略
- 已落地字段包括 `app.display_name`、`app.login_window_title`、`app.chat_window_title`、`services.http.base_url`、`services.http.register_path`、`services.http.login_path`、`services.http.logout_path`、`services.http.avatar_temp_upload_path`、`services.http.user_avatar_path_template`、`services.ws.url`
- 当前好友相关 HTTP 配置字段还包括 `services.http.user_search_path`、`services.http.friend_send_request_path`、`services.http.friend_outgoing_requests_path`
- 当前好友相关 HTTP 配置字段还包括 `services.http.friend_incoming_requests_path`、`services.http.friend_accept_request_path_template`、`services.http.friend_reject_request_path_template`
- 当前会话相关 HTTP 配置字段还包括 `services.http.conversation_private_path`
- 当前会话相关 HTTP 配置字段还包括 `services.http.conversation_list_path`、`services.http.conversation_detail_path_template`
- 当前会话相关 HTTP 配置字段还包括 `services.http.conversation_messages_path_template`、`services.http.conversation_send_text_path_template`
- 当前日志配置字段包括 `log.app_name`、`log.minimum_level`、`log.enable_console`、`log.enable_file`、`log.display_local_time`、`log.directory`、`log.file_name`
- 后续客户端的 HTTP / WebSocket service 应统一读取 `AppConfig`，不再在窗口类或业务代码中散落硬编码地址
- 当前 `chatClient/CMakeLists.txt` 已接入 `Qt Network + Qt WebSockets`，用于支撑 HTTP 请求和最小实时通道

当前默认客户端日志配置：

- 客户端统一日志入口为 `chatClient/src/log/app_logger.*`
- 当前通过 `qInstallMessageHandler(...)` 接管 Qt 全局日志输出
- 日志级别由 `chatClient/config/app.json` 中的 `log.minimum_level` 控制
- 控制台 / 文件输出开关由 `log.enable_console`、`log.enable_file` 控制
- 相对日志目录以 `app.json` 所在目录为基准解析；当前 `../logs` 最终落到 `chatClient/logs/`
- 当前默认同时输出到控制台和 `chatClient/logs/chatclient.log`
- 当前统一日志前缀格式为 `时间 + 级别 + [chatClient] + 业务组件`

当前默认数据库连接配置：

- PostgreSQL 连接信息统一写在 `chatServer/config/app.json` 的 `db_clients` 中
- 当前默认客户端名统一使用 `default`
- `rdbms` 明确配置为 `postgresql`
- `host`、`port`、`dbname`、`user`、`passwd`、`number_of_connections`、`timeout` 等字段都已补充中文注释
- 本地数据库账号信息以当前开发环境实际配置为准，不再在本文档里重复抄写

当前默认 Redis 连接配置：

- Redis 连接信息统一写在 `chatServer/config/app.json` 的 `redis_clients` 中
- `host`、`port`、`username`、`passwd`、`db`、`number_of_connections`、`timeout` 等字段都已补充中文注释
- 当前本地开发配置默认连接 `127.0.0.1:6379`

当前默认日志配置：

- Drogon / Trantor 的日志级别仍由 `chatServer/config/app.json` 中的 `app.log.log_level` 控制
- 本地时间显示仍由 `app.log.display_local_time` 控制
- 自定义日志输出策略由 `chatServer/config/app.json` 中的 `chatserver.log` 控制
- 当前默认同时输出到控制台和 `chatServer/logs/chatserver.log`
- 日志文件目录由统一日志模块在启动时自动创建
- 当前日志输出统一带 `[chatServer]` 应用名前缀和业务组件前缀，例如 `[bootstrap]`、`[auth.register]`

当前默认认证 token 配置：

- access token / refresh token 配置统一写在 `chatServer/config/app.json` 的 `chatserver.auth` 下
- 当前字段包括 `access_token_secret`、`access_token_expires_in_sec`、`refresh_token_expires_in_sec`
- 登录成功后服务端会返回 `device_session_id`、`access_token`、`refresh_token`、`expires_in_sec`
- refresh token 原文只返回给客户端，数据库只保存 `refresh_token_hash`
- 当前客户端仅持久化 `device_session_id` 和 `access_token`，暂不接入 refresh token 续期逻辑

当前默认文件存储配置：

- 文件存储配置统一写在 `chatServer/config/app.json` 的 `chatserver.storage` 下
- 当前 `provider` 固定为 `local`
- `chatserver.storage.local.root_dir` 统一指定上传根目录，当前默认解析到 `chatServer/uploads/`
- `chatserver.storage.local.tmp_dir` 用于上传中的临时文件，当前默认解析到 `chatServer/uploads/tmp/`
- `chatserver.storage.local.attachments_dir` 用于聊天图片和普通附件，当前默认解析到 `chatServer/uploads/files/`
- `chatserver.storage.local.avatars_dir` 用于用户头像，当前默认解析到 `chatServer/uploads/avatars/`
- 启动时由 `Application` 调用 `LocalStorage::createFromConfig(...)` 自动校验并创建这些目录
- 当前默认存储实例会注册到 `StorageRegistry`，供后续文件上传、头像保存和消息附件服务复用

当前默认大文件上传相关配置：

- `chatServer/config/app.json` 中的 `app.enable_request_stream` 当前已开启
- `app.client_max_body_size` 当前为 `1G`
- `app.client_max_memory_body_size` 当前为 `1M`
- `POST /api/v1/files/upload` 的文件 part 会先流式落到 staging file，再由 `FileService` 导入存储层

启动后默认监听：

- `http://127.0.0.1:8848`

健康检查接口：

- `GET /health`

当前已落地的正式业务 HTTP 接口：

- `POST /api/v1/auth/register`
- `POST /api/v1/auth/login`
- `POST /api/v1/auth/logout`

预期返回：

```json
{"database":{"available":true,"backend":"postgresql","client":"default"},"framework":"drogon","redis":{"available":true,"backend":"redis","client":"default"},"service":"chatServer","status":"ok"}
```

## 5.1 数据库迁移

当前已落盘的迁移脚本与执行入口：

- `chatServer/db/init_sql.py`
- `chatServer/db/init_sql/0000_set_updated_at_function.sql`
- `chatServer/db/init_sql/0001_users.sql`
- `chatServer/db/init_sql/0002_device_sessions.sql`
- `chatServer/db/init_sql/0003_friend_requests.sql`
- `chatServer/db/init_sql/0004_friendships.sql`

这一版迁移当前已覆盖认证和好友关系的最小表结构：

- `users`
- `device_sessions`
- `friend_requests`
- `friendships`

当前认证表的账号规则：

- `users.account` 的唯一性直接基于原值比较
- 登录账号大小写敏感，`Alice` 和 `alice` 会被视为两个不同账号

迁移结构约定：

- 顶层 `init_sql.py` 是统一执行入口，负责读取 `app.json`、建立连接参数和控制执行顺序
- `init_sql.py` 当前会先检查每个迁移步骤对应的目标对象是否存在，只把缺失对象加入本次事务
- 子目录 `init_sql/` 按“一个基础对象 / 一张表一个脚本”拆分
- 当前入口脚本会直接按顺序执行 `init_sql/` 下的子脚本，因此后续新增认证域对象时继续按这个模式追加即可

作用边界：

- 支撑注册时写入账号和密码哈希
- 支撑登录时按 `account` 查用户并校验密码哈希
- 支撑登录成功后写入 `device_session`
- 支撑 refresh token 与 `device_session_id` 绑定
- 支撑主动登出、被踢下线、refresh 失效时让设备会话失效
- 支撑好友申请、接受/拒绝前的流程落库
- 支撑正式好友关系稳定落库

在当前开发环境中，推荐直接执行：

```bash
python3 chatServer/db/init_sql.py
```

注意：

- 当前入口脚本依赖 `python3` 和 `psql`
- 当前入口脚本会直接从 `chatServer/config/app.json` 的 `db_clients` 中读取连接信息
- 当前脚本会创建真实表，不是演示 SQL
- 重复执行同一迁移脚本时，已存在的函数和表会被自动跳过
- 生产环境后续应接入正式迁移管理方式，而不是手工重复执行脚本

## 6. 第三方依赖管理

服务端第三方依赖统一放在 `chatServer/3rdparty/` 下，不使用散落在多个目录中的临时 vendor 方式。

当前已接入：

- `drogon/`：Drogon `v1.9.12`
- `drogon/trantor/`：与当前 Drogon 版本匹配的 Trantor 提交 `5000e2a72687232c8675b28ce86a29ed7d44309e`
- `hiredis/`：hiredis `v1.2.0`
- `jsoncpp/`：JsonCpp `1.9.6`

当前第三方目录已做的清理：

- 已删除 examples、tests、docker、CI 配置、`hiredis/.git` 等不参与当前构建的文件
- 当前保留的是 `chatServer` 现阶段编译所需的最小可用 vendored 内容
- `3rdparty/hiredis` 已从 gitlink 形式改为普通源码目录提交，后续 push 会随仓库一起上传到 GitHub

约束如下：

- 新增服务端第三方库时，优先放到 `chatServer/3rdparty/`
- 新增或升级第三方库时，必须同步更新本文档中的版本说明
- 第三方库一律使用固定版本，不直接依赖漂移分支
- `3rdparty/` 下的 vendored 库必须按普通文件提交，不保留嵌套仓库的 gitlink 记录
- 清理第三方库时，必须在删除后重新验证 `cmake -S chatServer -B tmpbuild/chatServer && cmake --build tmpbuild/chatServer -j`

## 7. 当前服务端入口说明

当前 `chatServer/src/main.cpp` 只承担最小进程入口职责：

- 创建 `Application`
- 统一兜底启动异常

当前 `chatServer/src/app/application.cpp` 负责：

- 初始化统一日志模块
- 加载 `config/app.json`
- 从 `app.json` 读取 PostgreSQL `db_clients` 和 Redis `redis_clients` 配置
- 从 `app.json` 读取 `chatserver.storage` 配置并初始化默认文件存储
- 注册 `/health` 路由
- 用 PostgreSQL 状态检查和 Redis `PING` 组成最小探活结果
- 启动事件循环

当前 `chatServer/src/transport/http/auth_controller.cpp` 负责：

- 暴露 `POST /api/v1/auth/register`
- 暴露 `POST /api/v1/auth/login`
- 暴露 `POST /api/v1/auth/logout`
- 解析注册请求 JSON 和 `X-Request-Id`
- 解析登录请求 JSON、`Authorization: Bearer <access_token>` 和 `X-Request-Id`
- 调用 `AuthService`
- 把结果封装成统一响应体 `code / message / request_id / data`

当前 `chatServer/src/service/auth_service.cpp` 负责：

- 校验注册参数
- 校验登录参数和 access token
- 生成 `user_id`
- 生成 bcrypt 密码哈希
- 生成 `device_session_id`、`access_token` 和 `refresh_token`
- 调用 `UserRepository` 写入 `users`
- 调用 `DeviceSessionRepository` 创建和失效 `device_sessions`
- 通过统一日志模块输出 `auth.register` 相关告警和错误日志

当前 `chatServer/src/repository/user_repository.cpp` 负责：

- 通过 Drogon PostgreSQL client 执行异步 SQL
- 识别账号唯一约束冲突
- 把数据库返回结果映射成上层可用的用户记录
- 当前直接使用 `drogon::app().getDbClient()` 获取默认数据库客户端，不再保留单独的 `db_client_names.h`

当前注册接口额外依赖：

- 系统 `libcrypt`
- bcrypt setting 由 `crypt_gensalt_rn()` 生成
- bcrypt 哈希由 `crypt_r()` 生成

当前 `chatServer/CMakeLists.txt` 和 `chatServer/src/main.cpp` 已补充较完整的中文注释，
用于说明第三方依赖接入方式、PostgreSQL / Redis 构建开关、统一日志模块和当前启动流程的职责边界。

它现在不是正式业务入口，只是第 1 步基础脚手架的最小可运行版本。后续会逐步拆到：

- `src/app/`
- `src/transport/http/`
- `src/transport/ws/`
- `src/service/`
- `src/repository/`

## 8. 后续开发优先级

在当前基础上，服务端下一批优先事项建议是：

1. 把客户端“添加好友”弹窗接成真实搜索和发送申请链路
2. 补“新的朋友”列表与接受/拒绝好友申请的客户端交互
3. 建立 `transport/ws`
4. 建立 `infra/db` 的进一步封装
5. 在本地文件存储抽象之上继续补文件上传服务与附件元数据落库

后续如果这些内容发生变化，必须同步更新本文档。

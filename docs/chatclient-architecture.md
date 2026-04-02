# chatClient 客户端建设说明

最近同步时间：2026-03-21

## 1. 文档目的

这份文档专门说明 `chatClient` 后续应该怎么建设。

它不替代整体设计文档，而是把客户端相关内容单独展开，重点回答这些问题：

- 当前客户端已经做到哪了
- 客户端后续应该补哪些分层
- 每个目录和文件分别负责什么
- 登录、消息发送、消息接收分别怎么走
- 为什么不能继续把网络逻辑直接写进窗口类

如果需要单独查看当前第一版会话模型、消息模型注册表和会话调度器的职责边界，请配合阅读：

- `docs/client-conversation-models.md`

## 2. 当前客户端现状

当前 `chatClient/src/` 真实已经落地的目录有：

- `config/`
- `log/`
- `api/`
- `service/`
- `dto/`
- `qt_widget/`
- `model/`
- `view/`
- `delegate/`

当前已经具备的能力：

- 登录页和聊天页基础界面
- `QListView + QAbstractListModel + QStyledItemDelegate` 的消息展示链路
- 文本消息的演示输入与展示
- 客户端统一配置入口 `chatClient/config/app.json`
- `AppConfig` 配置读取层
- 基于 Qt 全局消息处理器的统一日志模块 `log/app_logger.*`
- 基于 `QNetworkAccessManager` 的最小认证 HTTP 客户端
- `auth_service` 驱动的注册提交链路
- `auth_service` 驱动的登录提交链路
- 已新增 `FileApiClient + file_dto`，支持临时附件上传、正式附件下载，以及上传 / 下载进度信号；客户端上传前大小校验当前已统一为 `1 GB`
- 注册页真实调用服务端 `POST /api/v1/auth/register`
- 登录页真实调用服务端 `POST /api/v1/auth/login`
- 注册页已补头像选择入口，选择图片后会立即调用 `POST /api/v1/users/avatar/temp`
- 注册请求当前会携带 `avatar_upload_key`
- 聊天页左上角头像已支持按用户 ID 拉取真实头像文件
- 已将认证错误中文化逻辑拆到 `service/auth_error_localizer.*`
- 登录成功后把 `device_session_id + access_token` 持久化到本地
- 登录成功后从登录页切换到聊天页
- 同一用户同一设备重复登录时，客户端会收到 `409/device already logged in`，并显示中文提示，而不是覆盖旧会话
- 已建立 `ConversationManager + ConversationListModel + MessageModelRegistry` 第一版会话模型骨架
- 已在首次进入聊天窗口时由 `ConversationManager` 统一拉取会话列表和对应会话的首屏历史消息
- 当前中间栏会话列表虽然仍使用 `QListWidget`，但其数据已开始由 `ConversationManager` 的会话快照同步驱动
- `ChatWsClient` 当前已开始解析 `ws.ack / ws.new`
- `ConversationManager` 当前已开始接入首批实时消息路由：
  - `ws.ack + route=message.send_text`
  - `ws.ack + route=message.send_image`
  - `ws.ack + route=message.send_file`
  - `ws.new + route=message.created`
  - `ws.new + route=conversation.created`
- 聊天窗口文本消息发送当前已经改为 `ConversationManager -> ChatWsClient -> ws.send`
- 聊天窗口输入区“表情”按钮当前已经不再是占位入口：`ChatWindow` 内部会弹出一个轻量 emoji 面板，点击某个 emoji 后会把字符插入输入框当前光标位置
- 聊天窗口输入区当前已改为统一“附件”入口：
  - 文件选择器当前默认就是“所有文件 (*)”，不再把“常见扩展名集合”放在第一项
  - 可识别为图片的本地文件走 `ConversationManager -> FileApiClient -> ws.send(message.send_image)`
  - 其它任意本地文件走 `ConversationManager -> FileApiClient -> ws.send(message.send_file)`
  - `ChatWindow` 当前支持一次选择多张本地附件，因此图片和文件都可以并发上传 / 发送
  - 上传阶段会先插入本地占位消息，并持续更新上传进度
  - 成功后再由 `ws.ack / ws.new` 收敛成正式图片消息或正式文件消息
- `MessageDelegate` 当前已支持两类富消息卡片：
  - 图片消息：优先使用本地路径绘制缩略图，上传慢时会直接在图片区域展示进度 / 失败状态
  - 文件消息：会显示文件名、大小、状态文案和下载进度条；文件卡片点击后会按“已下载则打开、未下载则启动下载”处理
- `MessageListView` 当前已补文件消息交互：
  - 右键文件卡片会弹出文件菜单
  - 支持“下载到默认位置”“下载到...”“打开所在文件夹”“打开文件”
  - 若文件消息带附言，仍可继续复制附言文本
- `ConversationManager` 当前还会负责把正式图片消息补齐成本地可显示资源：
  - 冷启动 HTTP 历史消息里的 image 消息会按 `download_url` 下载并写入本地缓存
  - `ws.new(message.created)` 进来的 image 消息同样会触发缓存下载
  - `MessageModel` 只做图片字段的局部刷新，不会为此整条消息反复重写
- `ConversationManager` 当前也已接入正式文件消息下载链路：
  - 历史消息和 `ws.new(message.created)` 里的 file 消息都会保留 `download_url / file_name / mime_type / size`
  - 点击文件卡片或使用右键菜单时，会通过 `FileApiClient` 下载正式附件
  - 下载过程中会持续把 `Downloading + 进度文案` 回写到 `MessageModel`
  - 下载完成后只局部刷新该行文件消息的 `localPath / mimeType / size` 等字段
- `MessageModelRegistry` 当前已支持按 `message_id / client_message_id / seq` 做消息 upsert，避免 `ack/new` 重复插入
- 消息页右侧详情区当前已把头部按钮、提示文案和发送区按钮显式收口到 `ChatWindow` 成员，并通过小型 setter 统一更新属性，不再依赖 `createMessageContentPage()` 内的局部变量
- 好友申请相关 `ws.new` 路由当前已接到好友界面：
  - `friend.request.new` 会刷新“新的朋友”
  - `friend.request.accepted / rejected` 会刷新“已发送申请”
  - `friend.request.accepted` 还会刷新正式好友列表

当前还没有真正落地的能力：

- refresh token 续期与完整登录态恢复策略
- 更完整的 DTO / service / viewmodel 分层

因此，客户端当前更准确的定位是：

- UI 原型已经成立
- 配置入口已经成立
- 最小认证 HTTP 链路已经开始
- 最小 WebSocket 长连接已经开始
- 会话列表、历史消息、文本消息和图片消息发送链路已经形成第一版闭环
- 文件消息当前也已经具备“发送 -> 收到 ack/new -> 点击下载 / 打开”的第一版闭环

## 3. 目标结构

建议客户端最终形成如下结构：

```text
chatClient/src/
  config/
    appconfig.h
    appconfig.cpp
  log/
    app_logger.h
    app_logger.cpp
  api/
    auth_api_client.h
    friend_api_client.h
    user_api_client.h
    conversation_api_client.h
    file_api_client.h
  ws/
    chat_ws_client.h
  service/
    auth_error_localizer.h
    auth_error_localizer.cpp
    auth_service.h
    conversation_manager.h
    chat_service.h
    friend_service.h
    call_service.h
  dto/
    auth_dto.h
    user_dto.h
    ws_dto.h
    message_dto.h
    conversation_dto.h
    friend_dto.h
  viewmodel/
    chat_message_view_model.h
  qt_widget/
  model/
    conversationlistmodel.h
  view/
  delegate/
```

这套结构的核心原则是：

- `qt_widget` 只负责界面和交互
- `service` 负责业务流程和状态编排
- `api` 负责 HTTP 请求
- `ws` 负责 WebSocket 长连接
- `dto` 负责协议数据对象
- `viewmodel` 负责把协议数据转换成界面能直接使用的数据
- 现有 `model/view/delegate` 继续负责 Qt 消息列表展示
- 会话域第一版推荐建立“`ConversationManager -> ConversationListModel / MessageModelRegistry`”这条专用数据链路
- 会话列表当前阶段不强制引入 `QListView + 自定义 model`，可以继续使用 `QListWidget`

## 4. 依赖方向

推荐的依赖方向如下：

```text
qt_widget -> service
qt_widget -> viewmodel
service -> api
service -> ws
service -> store
service -> model
service -> dto
api -> dto
ws -> dto
viewmodel -> dto
model/view/delegate <- viewmodel
config -> 所有需要读取运行配置的层
log -> 所有需要统一输出日志的层
```

这个方向的含义是：

- 窗口类不直接发 HTTP
- 窗口类不直接操作 `QWebSocket`
- 窗口类不直接处理原始 JSON
- 会话列表如果继续使用 `QListWidget`，也仍然由 `service` 提供数据，不直接在窗口类里长期堆硬编码文本
- `api` 和 `ws` 只负责通信，不负责复杂业务状态
- `service` 统一协调登录态、重连、消息收发、错误处理

### 4.1 推荐会话数据骨架

为了让客户端后续稳定接入真实会话列表、历史消息和实时推送，建议单独收口一套“会话数据骨架”：

- `ConversationApiClient`
  负责会话域 HTTP 通信，只回答“怎么请求服务端”。
  当前已经补齐 5 条最小会话域接口封装：
  - `POST /api/v1/conversations/private`
  - `GET /api/v1/conversations`
  - `GET /api/v1/conversations/{conversation_id}`
  - `GET /api/v1/conversations/{conversation_id}/messages`
  - `POST /api/v1/conversations/{conversation_id}/messages/text`
  当前这些接口已经统一收口到第一版 `ConversationManager` 中，并已经接到会话列表和历史消息界面。
- `ConversationListModel`
  负责会话列表展示，只回答“中间栏该显示什么”。
  当前已落地为 `src/model/conversationlistmodel.*`，支持整段会话摘要替换和单条会话 upsert。
- `ConversationManager`
  负责把 `ConversationApiClient`、`ConversationListModel`、现有 `MessageModelRegistry` 和 `ChatWsClient` 组织起来，对 `ChatWindow` 提供统一入口。
  第一版先由它内部维护轻量会话状态，例如：
  - 每个会话是否已初始化
  - `minSeq / maxSeq`
  - 是否仍有更早消息
  - 当前是否正在拉取历史消息

推荐原则：

- `ChatWindow` 最好只直接依赖 `ConversationManager`
- `ConversationListModel` 不直接发 HTTP，不直接管理历史消息缓存
- `ConversationManager` 内部统一协调 `ConversationApiClient`、`ConversationListModel`、`MessageModelRegistry`
- 第一版不单独拆 `ConversationStore`，先把最小缓存状态收口在 `ConversationManager` 内部
- 如果后续缓存、分页、重连恢复逻辑明显变重，再把这部分从 `ConversationManager` 中抽成独立 `ConversationStore`
- 后续接入 `ChatWsClient` 时，也优先从 `ConversationManager` 入口并入，而不是让窗口类直接处理实时推送
- 当前 `ChatWsClient` 已开始把 `ws.new` 的 `route + data` 上抛给 `ConversationManager`
- 当前 `ConversationManager` 已开始接管 `conversation.created` 这类会话增量事件：
  - 会先把 WS 里的增量摘要注入本地列表
  - 再按当前登录用户视角补拉一次 `conversation detail`，纠正标题、头像和未读状态这类视角敏感字段

推荐调用链：

```text
ChatWindow
  -> ConversationManager
     -> ConversationApiClient
     -> ConversationListModel
     -> MessageModelRegistry
     -> ChatWsClient (后续)
```

这样分层的目的只有一个：把“网络请求、轻量缓存、列表展示、消息展示”拆开，不让任何一个类同时承担 UI、缓存和通信三种职责。

## 5. 各目录职责

### 5.1 `config/`

作用：

- 承接客户端运行配置
- 统一保存应用展示信息、服务基础地址和日志输出策略
- 避免 URL、窗口标题、接口路径散落在窗口类里

当前已落地：

- `appconfig.h`
- `appconfig.cpp`
- `chatClient/config/app.json`

当前配置至少包括：

- 应用展示名
- 登录窗口标题
- 聊天窗口标题
- HTTP 基础地址
- 注册路径
- 登录路径
- 登出路径
- 临时头像上传路径
- 按用户 ID 获取头像的路径模板
- 按账号搜索用户路径
- 正式好友列表路径
- 发送好友申请路径
- 查询我发出的好友申请路径
- 查询我收到的好友申请路径
- 同意 / 拒绝好友申请路径模板
- 创建或复用私聊会话路径
- WebSocket 地址
- 日志应用名
- 日志级别
- 日志文件开关与路径

### 5.2 `api/`

作用：

- 封装所有 HTTP 请求
- 隔离 `QNetworkAccessManager`、`QNetworkRequest`、`QNetworkReply`
- 统一处理 URL 拼接、请求头、JSON 序列化、错误码解析

边界：

- `api` 不保存登录态业务规则
- `api` 不直接更新界面
- `api` 不处理聊天窗口状态

建议文件职责：

- `auth_api_client.h`
  负责注册、登录、refresh、登出等认证接口
- `user_api_client.h`
  负责临时头像上传、正式头像文件读取等用户资料相关接口
- `friend_api_client.h`
  负责按账号搜索用户、发送好友申请、拉取收件箱/发件箱，以及处理同意 / 拒绝好友申请
- `conversation_api_client.h`
  负责创建或复用私聊、查询会话列表、查询会话详情、拉取历史消息、发送文本消息等接口；当前 API 层已补齐这 5 条接口，但还未统一接入真实会话列表和历史消息 UI
- `file_api_client.h`
  负责临时附件上传、正式附件下载，以及文件传输进度相关接口；聊天图片上传前的 `1 GB` 本地大小校验也统一收口在这里

### 5.2 `log/`

作用：

- 承接客户端统一日志输出
- 接管 Qt 框架日志和业务日志
- 统一控制台 / 文件输出、日志级别过滤和日志路径解析

边界：

- `log` 不负责业务流程
- `log` 不直接依赖具体窗口布局
- `log` 只负责“怎么统一记录日志”，不负责“日志里该记哪些业务字段”

当前已落地：

- `app_logger.h`
- `app_logger.cpp`

当前配置至少包括：

- 日志应用名
- 最小日志级别
- 是否输出到控制台
- 是否输出到文件
- 是否使用本地时间
- 日志目录
- 日志文件名

### 5.3 `ws/`

作用：

- 封装 `QWebSocket`
- 统一管理建连、鉴权、心跳、重连、断线提示、事件分发
- 负责实时消息、在线状态、已读回执、通话信令收发

边界：

- `ws` 只负责实时通道，不负责历史消息拉取
- `ws` 不直接操作聊天窗口控件
- `ws` 不负责完整业务流程决策

当前已落地：

- `chat_ws_client.h`
- `chat_ws_client.cpp`
- `ws_dto.h`
- `ws_dto.cpp`

当前实现状态：

- 已通过 `QWebSocket` 打通最小长连接
- 已支持 `ws.auth`
- 当前连接保活统一依赖 WebSocket 协议层 `Ping/Pong`
- 已支持最小断线重连
- 当前仍由 `ChatWindow` 直接持有 `ChatWsClient` 完成最小接线
- 后续再收口到 `ConversationManager`，避免窗口类继续吸收实时状态逻辑

建议文件职责：

- `chat_ws_client.h`
  负责整个客户端的主 WebSocket 长连接

### 5.4 `service/`

作用：

- 承接客户端主要业务流程
- 组合 `api`、`ws`、`config` 和本地状态
- 向 UI 暴露更稳定的业务接口，而不是暴露底层网络细节

边界：

- `service` 不直接绘制界面
- `service` 不直接持有复杂 QWidget 布局逻辑
- `service` 是 UI 与网络之间的主协调层

建议文件职责：

- `auth_error_localizer.h`
  负责把认证接口返回的业务码和英文错误消息翻译成适合界面直接展示的中文提示
- `auth_service.h`
  负责登录、注册、token 保存、refresh、登出、登录态恢复
- `conversation_manager.h`
  负责会话域统一入口；内部协调 `ConversationApiClient`、`ConversationListModel`、`MessageModelRegistry`，并向 `ChatWindow` 提供“创建会话、切换会话、拉历史、发送消息”的稳定调用面。第一版最小缓存状态先内收在这个类里
- `chat_service.h`
  负责更高层的聊天业务流程；如果后续保留 `chat_service`，它更适合作为 `ConversationManager + ChatWsClient` 的轻量业务门面，而不是重复承载底层缓存职责
- `friend_service.h`
  负责好友搜索、正式好友列表拉取、发送好友申请、收件箱/发件箱拉取、好友关系变更，以及“添加好友”弹窗的业务流程
- `call_service.h`
  负责通话发起、接听、挂断和信令流程协调

### 5.5 `store/`

当前第一版不强制建立独立 `store/` 目录。

更轻的推荐做法是：

- 先由 `ConversationManager` 内部维护最小缓存状态
- 先把 HTTP 快照和 WS 增量统一收口到 `ConversationManager`
- 等后续出现更复杂的分页、重连恢复和多会话缓存需求时，再把这部分提炼成独立 `ConversationStore`

也就是说，`store/` 当前是**后续可选抽取层**，不是第一阶段必需层。

### 5.6 `dto/`

作用：

- 定义客户端和服务端协议对应的数据对象
- 承接 HTTP / WebSocket 收发数据
- 保持字段语义稳定，不混入界面展示逻辑

边界：

- DTO 不是数据库模型
- DTO 不是界面展示模型
- DTO 不负责文本格式化和气泡布局状态

建议文件职责：

- `auth_dto.h`
  定义注册、登录、refresh 等认证相关协议对象
- `user_dto.h`
  定义临时头像上传成功响应和用户资料相关统一错误对象
- `message_dto.h`
  定义消息协议对象，例如 `message_id`、`conversation_id`、`seq`、`sender_id`、`type`
- `conversation_dto.h`
  定义会话协议对象，例如标题、最后消息摘要、未读数、更新时间
- `friend_dto.h`
  定义好友搜索结果、好友申请列表项和申请提交相关协议对象

### 5.7 `viewmodel/`

作用：

- 把 DTO 转换成界面能直接消费的数据结构
- 隔离 UI 文案、状态映射和时间格式化
- 避免把协议字段直接塞给现有 Qt 视图层

边界：

- ViewModel 不直接发网络请求
- ViewModel 不保存底层网络连接状态
- ViewModel 更接近“给界面看”的对象

建议文件职责：

- `chat_message_view_model.h`
  把消息 DTO 转成聊天气泡所需字段，例如显示文本、时间文本、是否自己发送、发送状态
- 当前阶段会话列表如果继续使用 `QListWidget`，可以先不单独建立 `conversation_item_view_model`
- 等后续会话项状态复杂到需要统一格式化时，再补这层也不迟

### 5.8 `qt_widget/`

作用：

- 放登录窗口、聊天窗口等界面类
- 负责控件布局、用户输入、交互反馈

边界：

- 不直接构造 HTTP 请求
- 不直接管理 WebSocket 生命周期
- 不直接操作原始 JSON

### 5.9 `model/`、`view/`、`delegate/`

作用：

- 承接 Qt Model/View 体系
- 负责消息列表等高频展示区域的渲染和交互

当前已落地价值：

- 现有消息展示链路已经适合作为后续真实消息数据的最终展示落点
- 后续只需要把 `viewmodel` 转成这里需要的显示数据，而不必推倒 UI 重写
- 会话列表当前阶段数据量不大，继续使用 `QListWidget` 是可以接受的，不必为了统一形式提前复杂化

会话域建议补充：

- `conversationlistmodel.h`
  负责中间栏会话列表展示数据，例如：
  - `conversation_id`
  - 标题 / 对端昵称
  - 对端头像
  - 最后一条消息摘要
  - 未读数
  - 最后活跃时间
- 当前已有的 `MessageModelRegistry`
  继续负责右侧消息区的 `conversation_id -> MessageModel` 绑定关系

这里最重要的边界是：

- `ConversationListModel` 只负责展示，不直接发 HTTP
- `MessageModelRegistry` 继续负责右侧消息展示模型，不直接承担分页缓存
- 当前第一版由 `ConversationManager` 内部维护最小会话状态；如果后续缓存明显变重，再把它抽成独立 `ConversationStore`

## 6. 聊天页结构建议

当前聊天页后续建议拆成三块：

1. 左侧窄栏
2. 中间会话栏
3. 右侧聊天主体

### 6.1 左侧窄栏

建议承载：

- 当前登录用户头像和昵称
- 主导航入口，例如“消息”“好友”
- 次级动作，例如“登出”

作用：

- 把“当前是谁”“当前在线状态如何”“怎么退出登录”放到稳定位置
- 避免这些能力散落到聊天头部或会话列表里
- 把“当前中间栏正在展示哪一类内容”明确收口到导航切换上

当前实现约定：

- 左侧导航栏顶部展示默认头像和主导航入口
- 当前若服务端存在正式头像，则优先按 `user_id` 拉取真实头像文件；拉取失败时退回到用户名文本占位
- “切换账号 / 登出”按钮放在“消息 / 好友”主入口下方，作为次级动作单独展示
- 点击“切换账号”后，由 `ChatWindow` 发出切换账号信号，`LoginWindow` 再调用 `AuthService`
- “切换账号”会请求服务端登出当前 `device_session`，清理本地 `device_session_id + access_token`，然后回到登录页
- 点击“登出”后，会请求服务端登出当前 `device_session`，清理本地登录态，并直接退出整个程序
- 当前程序关闭时，也会沿用同样的登出链路向服务端发送请求，避免下次启动仍恢复旧会话

### 6.2 中间会话栏

建议承载：

- 会话搜索框
- 当前导航入口对应的列表内容

导航联动建议：

- 左侧点“消息”时，中间栏显示：
  - 会话搜索框
  - 会话列表
- 左侧点“好友”时，中间栏显示：
  - 好友搜索框
  - `添加好友` 入口
  - 真实好友列表 / 好友申请相关入口
- 右侧好友详情区当前会在 hero 卡片右侧显示选中好友头像；若头像下载失败，则退回到昵称文本占位
- 右侧好友详情区当前“发起会话”按钮已接入真实 `POST /api/v1/conversations/private`
- 当前这一步只负责为好友创建或复用私聊会话，不直接把会话写入本地列表；真实会话列表和历史消息后续再接入

`添加好友` 的设计建议：

- 入口放在“好友”导航对应的中间栏顶部
- 点击后打开独立的“添加好友”对话框
- 弹窗顶部使用选项栏切换两种模式：
  1. `申请好友`
  2. `新的朋友`
- `申请好友` 页面：
  - 顶部显示搜索结果
  - 底部显示我已发出的好友申请记录
- `新的朋友` 页面：
  - 显示我收到的好友申请
  - 每条申请可直接同意 / 拒绝
- 弹窗打开时应主动调用服务端接口，拉取“我已发出的好友申请记录”和“我收到的好友申请记录”
- 这个对话框建议独立为单独窗口类，不把布局和状态逻辑继续堆在 `ChatWindow` 中
- 不放在右侧聊天区，也不直接放在“消息”模式下的会话栏里

这样放的原因：

- “添加好友”本质上属于好友关系管理，不属于消息会话入口
- 左侧导航先切换模式，再让中间栏切换内容，信息结构更稳定
- 后续补“好友列表”“好友申请”“黑名单”“新的朋友”时，也更容易继续扩展
- 真实逻辑上也更合理：应先校验目标用户是否存在，再允许发送好友申请

会话列表实现建议：

- 在“消息”导航下，当前阶段继续使用 `QListWidget`
- 每一项展示：标题、最后消息摘要、时间、未读数
- 数据仍然由 `chat_service` 或后续会话 service 提供
- 窗口层负责把结果填充成 `QListWidgetItem`

当前为什么可以接受 `QListWidget`：

- 数据量不大
- 当前项目还没进入上千会话、复杂虚拟化滚动的阶段
- 先把会话切换、未读数和真实数据接起来，收益比提前抽复杂列表架构更高

### 6.2.1 注册页头像链路

当前已落地的最小流程：

1. 用户在注册页点击“设置头像”
2. 客户端选择本地图片文件
3. `user_api_client` 立即调用 `POST /api/v1/users/avatar/temp`
4. 服务端返回 `avatar_upload_key`
5. 客户端在真正提交注册时，把 `avatar_upload_key` 和账号、昵称、密码一起交给 `auth_service`
6. 服务端注册成功后，将正式头像 `storage key` 写入用户资料

这条链路的作用是：

- 注册阶段不要求用户先登录
- 头像上传和注册写库解耦
- 后续资料修改时可以继续复用同样的 `avatar_upload_key` 确认模式
- 当前为了先打通注册体验，这条头像链路仍是偏薄的客户端切片；后续如果资料功能继续扩展，再把这部分进一步上收为独立 `service` 即可

### 6.2.2 添加好友弹窗链路

当前建议的真实链路是：

1. 用户在“好友”模式点击“添加好友”
2. 弹窗创建后立即调用 `friend_service` 拉取我发出的好友申请记录
3. 顶部搜索框输入账号并点击搜索
4. `friend_service` 调用 `friend_api_client`，再请求 `GET /api/v1/users/search`
5. 若搜索命中，则顶部结果区显示目标用户；若未命中，则顶部显示空结果提示
6. 用户填写附言后点击“申请添加”
7. `friend_service` 调用 `POST /api/v1/friends/requests`
8. 发送成功后，弹窗重新刷新底部“已发送申请记录”

“新的朋友”页当前建议链路是：

1. 弹窗打开时立即调用 `friend_service` 拉取 `GET /api/v1/friends/requests/incoming`
2. 用户切到“新的朋友”页时，直接查看收到的申请列表
3. 对某条待处理申请点击“同意”或“拒绝”
4. `friend_service` 分别调用：
   - `POST /api/v1/friends/requests/{request_id}/accept`
   - `POST /api/v1/friends/requests/{request_id}/reject`
5. 服务端返回处理后的申请记录，客户端更新当前行状态

这条链路的目的有两个：

- 让窗口类不直接操作 HTTP 和 access token
- 让“搜索结果”和“已发送记录”在一个弹窗里形成闭环，而不是仍然停留在演示数据

### 6.2.3 好友主页链路

当前好友主页已建议走这条真实链路：

1. `ChatWindow` 在注入 `AuthService` 后创建 `FriendService`
2. 切到“好友”模式或关闭“添加好友”弹窗后，调用 `friend_service` 拉取 `GET /api/v1/friends`
3. `friend_api_client` 解析 `data.friends`
4. `ChatWindow` 把结果填充到中间栏 `QListWidget`
5. 右侧好友详情区根据当前选中项同步展示昵称、账号和用户 ID 摘要

这样做的作用是：

- 好友主页不再依赖硬编码演示数据
- 同意好友申请后，关闭弹窗即可刷新正式好友列表
- 后续补“发消息”“查看资料”“备注名”时，仍然可以沿用同一条真实数据链路

### 6.3 右侧聊天主体

建议承载：

- 当前会话标题和成员信息
- 消息列表
- 输入区
- 连接状态 / 空状态 / 发送失败状态

原则：

- 当前会话的内容和状态留在右侧
- 社交关系操作例如“添加好友”不直接混进消息输入区
- 右侧区域主要响应“中间栏当前选中的项”，而不是承担模式切换本身

## 7. 关键链路

### 7.1 启动链路

推荐流程：

1. `main.cpp` 启动 Qt 应用
2. `AppConfig` 加载 `chatClient/config/app.json`
3. 初始化全局 service
4. 显示登录窗口

作用：

- 配置错误能在启动阶段提前暴露
- 后续所有 HTTP / WebSocket 组件都从同一配置源读取地址

### 7.2 登录链路

推荐流程：

1. `LoginWindow` 收集账号密码
2. 调用 `AuthService::login(...)`
3. `AuthService` 调用 `AuthApiClient`
4. `AuthApiClient` 通过 HTTP 请求服务端登录接口
5. 返回登录 DTO
6. `AuthService` 保存 `access_token`、`device_session_id`
7. 当前先把 `access_token` 当作长期令牌保存，暂不接 refresh
8. 如果服务端返回认证失败，`auth_error_localizer` 会把常见英文消息转换成中文提示
9. 如果服务端返回 `409/device already logged in`，客户端会提示“当前设备已登录该账号”
10. UI 根据结果进入聊天页
11. 后续再由 `AuthService` 驱动 `ChatWsClient` 建立实时连接

边界：

- `LoginWindow` 不直接拿 `QNetworkReply`
- token 不直接保存在窗口类成员里
- WebSocket 建连不由窗口类直接发起
- 认证错误中文提示不直接散落在窗口类里，而是统一收口到 `auth_error_localizer.*`

当前已落地的客户端认证错误翻译示例：

- `40901 / account already exists` -> `账号已存在`
- `40101 / invalid credentials` -> `账号或密码错误`
- `40902 / device already logged in` -> `当前设备已登录该账号`
- `40301 / account disabled` -> `账号已被禁用`
- `40302 / account locked` -> `账号已被锁定`
- `account must not contain leading or trailing spaces` -> `账号不能包含前后空格`
- `device_platform may contain only letters, digits, '_' and '-'` -> `设备平台只允许字母、数字、下划线和短横线`

当前翻译规则：

- 优先按服务端稳定业务码翻译
- 其次按当前已知英文 `message` 做兼容翻译
- 最后再回退到原始消息或通用错误提示

### 7.3 历史消息加载链路

推荐流程：

1. `ChatWindow` 切换到某个会话
2. 调用 `ChatService`
3. `ChatService` 通过 `ConversationApiClient` 拉历史消息
4. HTTP 返回 `message_dto`
5. 转成 `chat_message_view_model`
6. 写入现有 `MessageModel`
7. `MessageListView` 显示出来

### 7.4 发送消息链路

推荐流程：

1. `ChatWindow` 把输入内容交给 `ChatService`
2. `ChatService` 生成本地发送任务和 `client_msg_id`
3. `ChatService` 先插入一条“发送中”的本地消息
4. `ChatService` 调用 `ChatWsClient` 发送 `message.send`
5. 服务端返回 `ack`
6. `ChatService` 更新该条消息状态为已发送或失败
7. 更新 `MessageModel`

职责拆分：

- `ChatWindow` 负责收集输入
- `ChatService` 负责发送流程和状态机
- `ChatWsClient` 负责实时发送
- `ViewModel + MessageModel` 负责展示状态

### 7.5 接收消息链路

推荐流程：

1. 服务端通过 WebSocket 推送 `message.new`
2. `ChatWsClient` 收到并解析为 `message_dto`
3. `ChatService` 判断所属会话、是否当前会话、是否增加未读数
4. 转成 `chat_message_view_model`
5. 写入当前 `MessageModel` 或更新会话未读状态
6. UI 刷新显示

职责拆分：

- `ChatWsClient` 负责接收
- `ChatService` 负责业务判断
- `ViewModel` 负责转换成展示数据
- `qt_widget + model/view/delegate` 负责最终展示

## 8. DTO、ViewModel、UI Model 的区别

- DTO
  表示服务端协议字段，字段名和语义尽量接近接口定义
- ViewModel
  表示界面真正要显示的数据，强调“如何显示”
- UI Model
  表示 Qt 视图系统真正消费的数据结构，例如 `MessageModel` 里的行数据

在客户端里，这三层不要混用。

一个简单例子：

- `message_dto.sent_at` 可能是时间戳
- `chat_message_view_model.time_text` 可能是 `"09:14"`
- `MessageItem.timeText` 则是当前 Qt 列表最终展示字段

## 9. 当前最合理的落地顺序

建议按下面顺序推进：

1. `auth_api_client` 注册方法
2. `auth_service` 注册流程
3. 注册页接入真实注册接口
4. 登录窗口接入真实登录接口
5. `access_token` 与 `device_session_id` 本地保存
6. `chat_ws_client`
7. `chat_service`
8. `conversation_dto` / `message_dto`
9. 先把会话列表接到真实数据和好友入口
10. `chat_message_view_model`
11. 把现有消息列表切到真实数据链路

这样做的原因是：

- 先完成认证闭环，客户端才能进入真正在线状态
- 没有登录态和 WebSocket，后面的会话与消息链路无法形成闭环
- 现有 UI 已经能承接展示，先补中间层最划算
- 会话列表当前保持 `QListWidget`，可以减少前期改造成本

## 10. 当前约束

- 当前客户端已经有统一配置文件，后续服务地址一律从 `AppConfig` 读取
- 窗口类不再继续新增硬编码接口地址
- 新增 HTTP 能力时，优先落在 `api/`
- 新增实时能力时，优先落在 `ws/`
- 新增业务流程时，优先落在 `service/`
- 同一用户同一设备的重复登录当前按冲突处理，不做“自动顶掉旧会话”
- 现有 `model/view/delegate` 继续保留，不推倒重写
- 会话列表当前允许继续使用 `QListWidget`，不强制切换到 `QListView + 自定义 model`
- 左侧导航应提供“消息 / 好友”等主入口，并驱动中间栏内容切换
- “添加好友”入口优先放在“好友”导航对应的中间栏顶部，并通过独立弹窗承接后续流程

## 11. 和其它文档的关系

- `docs/im-system-design.md`
  负责整体系统设计和阶段方案
- `docs/development.md`
  负责记录当前真实落地状态
- `docs/progress.md`
  负责记录当前开发进度

如果后续客户端目录、边界或消息链路判断发生变化，应优先同步更新这份文档，再同步更新设计文档中的总览部分。

# IM 项目整体设计说明

## 1. 文档目的

这份文档用于说明当前项目后续要实现的完整功能体系，并给出一套适合长期演进的整体设计方案。

目标功能包括：

- 登录
- 注册
- 好友管理
- 私聊
- 群聊
- 文本消息
- 图片消息
- 文件消息
- 语音通话
- 视频通话

这份文档重点回答 4 个问题：

1. 这些功能应该如何拆分设计
2. 服务端应该如何搭建
3. 客户端和服务端应该如何通信
4. 如何保证后期扩展性强，而不是越做越乱


## 2. 当前项目现状

当前仓库可以明确分成两个部分：

- `chatClient`：已有 Qt Widgets 图形界面，能够正常编译；当前除 UI 原型和消息展示模型外，已建立客户端配置入口、统一日志模块，并落地了基于 `QNetworkAccessManager` 的最小认证链路（`auth_dto -> auth_api_client -> auth_service -> 注册页/登录页`），登录成功后当前已将 `device_session_id + access_token` 持久化到本地并进入聊天页，但暂不接 refresh token；同时已接入临时头像上传、按用户 ID 拉取头像、好友搜索、好友申请收发箱，以及正式好友列表查询；客户端当前也已落地 `src/ws/chat_ws_client.* + src/dto/ws_dto.*`，并在聊天窗口中完成最小 `ws.auth` 长连接接入，连接保活统一依赖 WebSocket 协议层 `Ping/Pong`；此外客户端已补齐 `ConversationApiClient` 对 5 条会话域 HTTP 接口的调用封装，已把会话列表、聊天历史、文本消息发送和图片消息真实上传发送接入真实界面状态，并新增 `FileApiClient` 承接临时附件上传与正式附件下载；当前图片消息会先显示本地图片占位，再在 `MessageDelegate` 内持续展示上传进度 / 发送状态，直到被正式 `message.created` 收敛
- `chatServer`：已完成最小服务端脚手架，当前已接入 `Drogon`、已建立 `app.json`、PostgreSQL / Redis 开发连接、统一日志模块，并将首个认证迁移拆分为“`init_sql.py` 统一执行脚本 + 多个子 SQL 脚本”结构，可独立编译运行，并提供返回数据库与 Redis 状态的 `GET /health` 健康检查接口；同时已落地 `POST /api/v1/auth/register`、`POST /api/v1/auth/login`、`POST /api/v1/auth/logout`，并新增了临时头像上传、资料更新和按用户 ID 读取头像的接口；登录成功后已能返回 `device_session_id / access_token / refresh_token` 并写入 `device_sessions`；当前迁移脚本也已加入 `friend_requests`、`friendships`、`conversations`、`conversation_members`、`messages` 和 `attachments` 等社交关系与私聊会话基础表；并已实现 `POST /api/v1/conversations/private`、`GET /api/v1/conversations`、`GET /api/v1/conversations/{id}`、`GET /api/v1/conversations/{id}/messages`、`POST /api/v1/conversations/{id}/messages/text`、`POST /api/v1/files/upload`、`GET /api/v1/files/{attachment_id}`，以及 `ws.send + route=message.send_text / message.send_image / message.send_file` 这几条最小聊天主链路；文件域当前采用“临时上传 -> 正式消息确认时转正”的语义；同时已建立 `storage/file_storage + local_storage` 并可在启动时自动创建头像、附件和临时文件目录，MinIO 实现仍未开始；WebSocket 方面，服务端已落下 `transport/ws + ws_session_service + connection_registry` 骨架，并打通 `/ws` 与 `ws.auth`，连接保活统一依赖协议层 `Ping/Pong`

从现有客户端代码可以看出几个事实：

- 登录页和聊天页已经有界面，注册页和登录页都已接入真实认证请求，好友模式也已接入真实好友列表与好友申请链路，最小 WebSocket 长连接也已开始，但真实会话数据和实时消息仍未接入
- 消息列表已经做成 `QListView + Model + Delegate` 结构，说明消息展示层具备扩展空间
- 当前 `MessageItem` 更偏向 UI 展示模型，而不是网络协议模型或数据库模型
- 客户端网络技术路线已经明确为 Qt 原生网络栈，并已经落下最小认证 HTTP 切片，但完整网络层仍未正式落地

因此，后续设计不能从“补几个接口”开始，而应从“定义领域模型、协议模型、服务端模块边界”开始。


## 3. 总体设计原则

### 3.1 单体优先，模块清晰

第一阶段推荐采用“模块化单体”架构，而不是一开始就拆微服务。

原理：

- 当前项目仍处于功能搭建期，需求边界会不断调整
- 微服务虽然看起来扩展性强，但会提前引入服务注册、鉴权透传、跨服务事务、部署编排、日志追踪等复杂度
- 对 IM 初期来说，真正重要的是模块边界清晰，而不是物理拆分成多个进程

作用：

- 降低早期开发成本
- 提高调试效率
- 后期如果并发量上来，可以基于现有模块边界继续拆分，而不是推倒重来


### 3.2 领域模型、协议模型、UI 模型分离

不要让一个结构体同时承担数据库存储、网络传输、UI 展示三个职责。

原理：

- 数据库存储关注可查询、可索引、可持久化
- 协议模型关注字段稳定、版本兼容、跨端一致
- UI 模型关注显示文案、气泡状态、是否本人消息、时间格式化

作用：

- 避免一个字段变动牵一发而动全身
- 避免数据库字段直接污染界面逻辑
- 后期新增移动端、Web 端时，协议层可以复用


### 3.3 先定义协议和数据模型，再写业务逻辑

原理：

- IM 系统不是普通 CRUD，它对时序、幂等、重连、分页、消息确认都有要求
- 如果不先定义协议，后面每加一个功能都会改接口、改表、改客户端消息结构

作用：

- 避免后期协议反复推翻
- 能够让客户端和服务端并行开发
- 能够让“聊天消息”和“音视频通话”保持边界清晰


### 3.4 消息系统和音视频系统解耦

原理：

- 文本、图片、文件消息是持久化消息
- 音视频媒体流是实时媒体传输，底层依赖 WebRTC，不适合直接塞进普通消息收发模块
- 通话本身需要房间、参与者、信令状态，但不需要像文本消息那样逐条落库为普通内容

作用：

- 后续可以独立升级音视频能力
- 群通话引入 SFU 时，不会影响已有消息系统
- 文本聊天和实时通话两条链路可以各自演进


## 4. 技术选型（已确定）

这一章不只是列出技术名词，而是明确这些技术在本项目中的职责边界、使用方式和后续演进方向。

### 4.1 服务端主框架

当前确定：`Drogon`

原理：

- Drogon 自带 HTTP、WebSocket、JSON、文件上传下载、异步数据库支持
- 对当前项目来说，服务端需要同时处理 REST 接口和长连接推送，Drogon 能覆盖这两类需求
- 相比直接用裸 Asio/Beast，开发效率更高，结构更利于业务落地

作用：

- 快速搭建注册登录、好友、会话、历史消息等接口
- 快速接入 WebSocket 实时消息推送
- 降低自建网络框架的维护成本

详细说明：

- 在当前项目里，`Drogon` 的核心职责是统一承载 HTTP API、WebSocket 长连接、配置、日志、中间件、文件上传下载和基础设施初始化
- 它适合放在 `transport/http` 和 `transport/ws` 这一层，负责把外部请求转换成内部 service 调用，而不是把业务逻辑全部写在 controller 中
- 对 IM 早期阶段来说，一个进程同时处理 REST 和 WebSocket 非常合适，因为注册登录、会话列表、历史消息、实时推送本来就是强相关功能
- `Drogon` 可以直接配合 PostgreSQL 和 Redis 使用，减少为数据库连接池、异步回调、请求上下文和基础缓存能力再单独搭一层底座的成本

推荐使用方式：

- HTTP 接口使用 `HttpController` 或 `HttpSimpleController` 承载登录、注册、好友、会话、历史消息、文件上传下载
- WebSocket 接口使用 `WebSocketController` 承载实时消息、在线状态、已读回执、好友申请提醒、音视频信令
- controller 只负责参数解析、鉴权入口、错误码映射和响应封装
- 真正的业务逻辑放在 `service` 层，数据库读写放在 `repository` 层
- 全局配置通过 `app.json` 统一管理，避免把端口、数据库地址、上传目录写死在代码里

边界约束：

- `Drogon` 负责业务 API 和信令，不负责音视频媒体流转发
- 不要把 SQL 直接散落在 controller 中
- 不要让 controller 直接持有复杂业务状态或连接状态映射
- 如果后期引入更多服务实例，`Drogon` 仍然只是接入和业务编排层；Redis 可以继续承担在线状态、辅助缓存和跨实例广播这类基础设施角色

为什么不是裸 Asio / Beast：

- 裸网络库更接近“构建网络框架”的工作，而当前项目更需要“构建业务系统”
- IM 系统的复杂点主要在协议、状态一致性、重连、分页、幂等，而不是自己重复搭 HTTP 和 WebSocket 基础设施
- 先用 `Drogon` 跑通系统，后续如果要做更细分的服务拆分，也可以保留现有业务边界


### 4.2 客户端网络库

当前确定：

- HTTP：`QNetworkAccessManager`
- WebSocket：`QWebSocket`

原理：

- 客户端本身就是 Qt 桌面应用，使用 Qt 自带网络模块能天然融入 Qt 事件循环
- `QNetworkAccessManager` 适合登录、注册、列表查询、文件上传下载
- `QWebSocket` 适合实时消息、在线状态、音视频信令

作用：

- 减少客户端线程模型复杂度
- 避免 Qt 事件循环和自建 Asio 循环混用
- 方便未来扩展成统一的 Qt 网络服务层

详细说明：

- 客户端是 Qt Widgets 桌面应用，因此网络层最重要的原则不是“功能最全”，而是“与 Qt 事件循环天然兼容”
- 统一采用 Qt 自带网络模块，可以让请求、信号槽、窗口状态、重连逻辑都运行在同一套事件模型下
- 这套组合也更适合后续抽出 `api/`、`ws/`、`service/` 三层，而不是把网络代码写进窗口类

#### 4.2.1 HTTP：`QNetworkAccessManager`

适合承担：

- 注册、登录、刷新 token
- 拉取好友列表、会话列表、历史消息
- 文件上传和下载
- 启动阶段的初始化同步请求

推荐使用方式：

- 在客户端维护少量长期存活的 `QNetworkAccessManager` 实例，而不是每发一个请求就创建一个新的 manager
- 在 `api/` 层统一封装基础 URL、请求头、超时、JSON 解析和错误码处理
- access token 的注入、刷新 token 失败后的登出、常见业务错误提示，统一放在 `service` 层处理
- 文件上传使用 `multipart/form-data`，文件下载统一走 HTTP 下载链路，不和 WebSocket 混用

适合本项目的原因：

- `QNetworkAccessManager` 天然支持异步请求，不需要额外线程池就能完成大部分桌面端业务 API
- 它和 `QJsonDocument`、`QHttpMultiPart`、`QFile` 配合顺畅，足够覆盖 IM 客户端大部分非实时请求
- 后续如果增加头像上传、历史附件下载、断点下载等能力，也可以在现有基础上扩展

边界约束：

- `QNetworkAccessManager` 负责请求 - 响应型业务，不承担实时消息推送
- 不要把 `QNetworkReply` 直接暴露给窗口类
- 不要让每个页面各自维护 token 和请求细节，应该由上层 service 统一管理

#### 4.2.2 WebSocket：`QWebSocket`

适合承担：

- 文本消息实时推送
- 在线状态变化
- 已读回执
- 好友申请实时通知
- 音视频通话信令

推荐使用方式：

- 每个已登录客户端维护一条主要的长连接，由专门的 `chat_ws_client` 或等价组件统一管理
- 连接建立、鉴权、心跳、重连、断线恢复、事件分发统一收口在 `ws/` 或 `service/` 层
- WebSocket 消息统一使用信封结构，避免不同事件直接发送互不兼容的 JSON
- 收到事件后先转成协议 DTO，再转换成 UI 所需的 ViewModel，不直接把原始 JSON 塞给界面

适合本项目的原因：

- `QWebSocket` 和 Qt 的信号槽配合自然，适合桌面端持续在线连接场景
- 相比自己引入 Asio 或其他网络循环，`QWebSocket` 更容易和登录态、窗口生命周期、断线提示整合
- 后期音视频仍然需要 WebSocket 负责信令，因此它不是“消息功能专用”，而是整个实时控制面的基础通道

边界约束：

- `QWebSocket` 只承载控制面和实时事件，不传大文件、不承载音视频媒体流
- 不要把消息发送成功的唯一判定建立在“本地已发出”，而要依赖服务端 `ws.ack`
- 不要让聊天窗口自己维护重连和心跳逻辑


### 4.3 数据库与缓存

当前确定：

- 主数据库：`PostgreSQL`
- 缓存与在线状态底座：`Redis`

原理：

- PostgreSQL 适合处理关系型数据、成员关系、消息索引和复杂查询
- Redis 适合维护在线状态、连接映射、短期缓存、限流和跨进程广播辅助信息

作用：

- 关系数据和实时状态分离
- 消息历史与好友关系能稳定落库
- 后期可支持多实例服务端横向扩容

详细说明：

- 当前工程已经在 `chatServer/config/app.json` 中同时建立了 `db_clients` 和 `redis_clients`，
  并通过 `GET /health` 完成了 PostgreSQL / Redis 的最小连通性探测。
- 这里的核心原则是“持久化真相放 PostgreSQL，临时状态放 Redis”
- 聊天系统里很多信息看起来都可以缓存，但真正需要长期可信的数据仍然应该落在关系数据库中
- Redis 负责速度和协作，PostgreSQL 负责可靠和可查询，两者职责必须清晰分开

#### 4.3.1 主数据库：`PostgreSQL`

建议承载的数据：

- 用户账号、登录设备会话、refresh token 关联信息
- 好友关系、好友申请、黑名单等社交关系
- 会话、会话成员、消息、附件元数据
- 通话会话记录、通话状态流水、离线通知落库信息

适合本项目的原因：

- IM 的核心对象本质上都是强关系数据，天然适合用关系型数据库组织
- 会话成员、消息顺序、好友关系、已读位置这些数据都需要稳定的约束和查询能力
- 后期需要做历史消息分页、复杂检索、统计分析时，`PostgreSQL` 比纯 KV 或文档库更稳妥

推荐落地方向：

- `messages` 表按 `conversation_id + seq` 组织消息顺序
- `conversation_members`、`friendships`、`device_sessions` 等关键关系表建立明确唯一约束
- 重要写入操作使用事务，尤其是“创建会话 + 写成员”“保存消息 + 更新会话摘要”这类多表联动动作
- 附件实体只保存元数据和存储引用，不直接把二进制内容塞进数据库字段

边界约束：

- 不要把在线状态、连接映射这类短期数据硬塞进数据库高频更新
- 不要让消息查询依赖页码翻页，应该依赖 `seq` 或稳定游标
- 不要直接把数据库模型暴露给客户端协议层

#### 4.3.2 缓存与在线状态：`Redis`

建议承载的数据：

- 用户在线状态和最后活跃时间
- 用户 ID 到连接 ID、实例 ID 的映射
- 短期会话缓存、热点数据缓存
- WebSocket 跨实例广播辅助信息
- 限流、验证码、短期幂等控制和去重辅助信息

适合本项目的原因：

- 在线状态、连接映射这类信息更新频率高、生命周期短，不适合频繁写 PostgreSQL
- 如果服务端后期部署多个实例，需要一个高效的共享状态和广播辅助层
- `Redis` 对 presence、短 TTL 缓存、Pub/Sub 辅助广播这类需求非常合适

推荐落地方向：

- 在线状态尽量带 TTL，避免异常断线后脏状态长期残留
- 多实例实时推送可借助 Redis Pub/Sub 或其他轻量机制同步路由信息
- 未读计数、限流、重连增量同步标记可以先使用 Redis 辅助，但最终一致性仍回到 PostgreSQL

边界约束：

- 不要把 Redis 当作消息历史主存储
- 不要把只有 Redis 有、数据库没有的关键业务状态设计成唯一真相来源
- 不要让缓存命中失败就导致系统无法恢复核心数据


### 4.4 文件存储

当前确定：

- 开发阶段：本地文件目录
- 中后期：`MinIO` 或兼容 S3 的对象存储

原理：

- 图片和文件不应直接保存在数据库字段中
- 消息正文只保留附件引用，附件实体交给专门存储层处理

作用：

- 简化大文件处理
- 降低数据库压力
- 后期便于扩容和 CDN 化

详细说明：

- 文件系统设计的关键点不是“文件放哪”，而是“附件实体和聊天消息正文分离”
- 消息里只应该保存附件引用、展示名、大小、类型等元信息，真正的图片和文件内容由独立存储层管理
- 这可以避免大文件把数据库拖垮，也便于以后把存储实现从本地目录切到对象存储

#### 4.4.1 开发阶段：本地文件目录

推荐使用方式：

- 服务端通过统一的 `file_storage` 接口写入本地目录
- 目录结构尽量按日期、业务类型、哈希前缀等方式组织，避免单目录堆积过多文件
- 数据库只保存附件 ID、存储路径、展示名、大小、MIME 类型等元数据

适合当前阶段的原因：

- 本地目录部署最简单，便于快速联调上传、下载、图片预览和权限校验
- 不需要一开始就引入对象存储运维复杂度

边界约束：

- 本地文件目录只适合开发和小规模部署
- 不建议把绝对路径直接暴露给客户端
- 上传接口和消息发送接口要分开，先上传拿到附件引用，再发送消息

#### 4.4.2 当前头像存储与资料更新约定

当前已落地的设计：

- `users.avatar_url` 当前不再表示外部头像 URL，而是头像文件的 `storage key`
- 临时头像先写入 `uploads/tmp/YYYY/MM/DD/...`
- 正式头像写入 `uploads/avatars/YYYY/MM/DD/...`
- 注册和资料更新都不直接传最终头像地址，而是传 `avatar_upload_key`

原理：

- 注册时用户可能还没有登录态，因此不能强依赖“已登录才能上传头像”
- 如果把头像文件直接混进注册 JSON，后续修改资料、头像预览、临时清理都会越来越乱
- 先上传临时头像，再在注册或资料更新时确认使用，可以把“文件存储”和“用户资料写库”解耦

当前接口职责建议：

- `POST /api/v1/users/avatar/temp`
  接收临时头像上传，可以不要求已登录，返回 `avatar_upload_key` 和预览地址
- `GET /api/v1/users/avatar/temp?avatar_upload_key=...`
  用于客户端在注册页或资料编辑页即时预览临时头像
- `POST /api/v1/auth/register`
  可选携带 `avatar_upload_key`，注册成功后再把临时头像确认成正式头像，并把最终头像 `storage key` 写入 `users.avatar_url`
- `PATCH /api/v1/users/me/profile`
  资料修改接口，字段应全部可选；如果带 `avatar_upload_key`，表示把新的临时头像确认成当前正式头像
- `GET /api/v1/users/{user_id}/avatar`
  返回指定用户当前头像文件，客户端不需要自己拼磁盘路径

为什么 `users.avatar_url` 直接存 `storage key`：

- 当前开发阶段使用本地目录存储，不存在稳定的公网 URL
- `storage key` 能稳定定位到具体文件，同时不会把绝对路径暴露给上层
- 后续即使切到 `MinIO`，数据库仍可继续保存同一语义的对象键，上层字段不必重构

边界约束：

- `avatar_upload_key` 只是“临时头像引用”，不是最终头像 URL
- 客户端不应直接拼接 `uploads/...` 本地路径
- 头像文件的真实访问应始终通过受控接口或未来的受控下载地址完成
- 聊天附件和图片消息后续也应复用同样思路：先上传得到引用，再由消息正文引用，不直接把二进制内容塞进消息协议

#### 4.4.3 中后期：`MinIO` 或兼容 S3 的对象存储

推荐使用方式：

- 继续沿用统一的存储抽象接口，把底层实现从本地目录切到 `MinIO`
- 由服务端控制上传策略、下载授权、对象命名规则和生命周期管理
- 客户端只感知附件 ID、下载 URL 或受控访问地址，不依赖底层存储细节

适合中后期的原因：

- 对象存储更适合海量图片和文件，便于做扩容、备份、分发和后期 CDN 化
- 和数据库、应用服务解耦更彻底，便于独立运维

边界约束：

- 即便使用对象存储，消息正文仍只存引用，不存实体
- 客户端不应绕过服务端权限控制直接决定对象路径
- 存储切换不应影响上层消息协议字段设计


### 4.5 音视频能力

当前确定：

- 实时媒体：`WebRTC`
- NAT 穿透 / TURN：`coturn`

原理：

- 音视频通话不是普通 TCP 或 WebSocket 库可以替代的
- WebRTC 负责媒体协商、编码、网络自适应、打洞和低延迟传输
- TURN 服务器负责复杂网络环境下的中继

作用：

- 支持一对一语音视频通话
- 为后期群语音、群视频打基础
- 不需要自己实现媒体传输协议

详细说明：

- 这里最重要的原则是“消息系统负责信令和会话控制，媒体系统负责真正的音视频传输”
- 文本消息和音视频通话虽然都依赖实时连接，但它们不是同一种系统能力
- 如果把音视频直接塞进普通消息模块，后期几乎一定会失控

#### 4.5.1 实时媒体：`WebRTC`

适合承担：

- 音频和视频采集
- 编码解码
- 媒体协商
- 网络自适应和低延迟传输

适合本项目的原因：

- 音视频通话需要抗抖动、码率控制、回声处理、NAT 穿透等能力，这些都不是普通 WebSocket 可以替代的
- `WebRTC` 已经是业界成熟方案，适合一对一通话，也为后期群通话保留扩展空间

在本项目中的定位：

- 客户端通过 WebSocket 收发信令
- 客户端之间通过 `WebRTC` 建立真正的媒体通道
- 服务端记录通话会话、参与者状态和信令流转，但不直接转发媒体内容

边界约束：

- `WebRTC` 负责媒体流，不负责账号体系、好友关系、历史消息
- 不要试图用 WebSocket 传真正音视频数据替代 `WebRTC`
- 未来如果群通话规模变大，可以在信令层稳定后再考虑引入 SFU，不影响当前文本消息系统

#### 4.5.2 NAT 穿透 / TURN：`coturn`

适合承担：

- STUN / TURN 服务
- 复杂网络环境下的中继
- 提高弱网和企业网络场景下的通话成功率

适合本项目的原因：

- 真实网络环境中，纯 P2P 直连成功率并不稳定
- 没有 TURN，中继失败时很多用户会表现为“看起来已接通但没有声音和画面”

在本项目中的定位：

- `coturn` 是音视频基础设施，不是业务服务
- 聊天服务端只负责给客户端提供 ICE 相关配置和通话状态，不负责替代 TURN

边界约束：

- 不要把 TURN 凭证和业务鉴权完全混成一套逻辑
- 不要让通话能力依赖聊天消息服务端自己实现 NAT 穿透

### 4.6 技术栈协作关系

为了避免后期模块混乱，整个系统默认按以下方式协作：

- `Drogon + HTTP`：服务端对外提供注册、登录、列表查询、历史消息、文件上传下载等请求 - 响应型接口
- `QNetworkAccessManager`：客户端访问上述 HTTP API
- `Drogon + WebSocket`：服务端对外提供实时消息推送、在线状态、已读回执、通话信令
- `QWebSocket`：客户端维持长期连接并处理实时事件
- `PostgreSQL`：保存持久化关系数据和消息历史
- `Redis`：维护在线状态、连接映射、短期缓存和跨实例广播辅助状态
- 本地文件目录 / `MinIO`：保存图片和文件实体
- `WebRTC + coturn`：负责真正的音视频媒体传输和复杂网络中继


## 5. 总体系统结构

建议整体拆成以下模块：

```text
客户端
  ├─ UI 层
  ├─ ViewModel 层
  ├─ HTTP API 客户端
  ├─ WebSocket 客户端
  ├─ 文件传输客户端
  └─ RTC 信令与通话控制

服务端
  ├─ auth 模块
  ├─ social 模块
  ├─ conversation 模块
  ├─ message 模块
  ├─ file 模块
  ├─ presence 模块
  └─ rtc-signaling 模块

基础设施
  ├─ PostgreSQL
  ├─ Redis
  ├─ Local File Storage / MinIO
  └─ coturn
```

原理：

- 功能按职责拆分，而不是按页面拆分
- 每个模块只关注自己的核心对象和流程
- 模块之间通过明确接口协作

作用：

- 好友系统不会污染消息系统
- 音视频信令不会侵入文本消息收发逻辑
- 后期可以局部替换模块实现

与已确定技术栈的对应关系：

- 服务端接入层默认基于 `Drogon`
- 客户端 HTTP 请求默认基于 `QNetworkAccessManager`
- 客户端实时连接默认基于 `QWebSocket`
- 关系数据和历史消息默认落在 `PostgreSQL`
- 在线状态和跨实例辅助状态默认放在 `Redis`
- 附件实体默认先落本地文件目录，后续可平滑切到 `MinIO`
- 音视频通话默认采用 `WebRTC + coturn`


## 6. 核心领域模型设计

以下模型是整个 IM 系统的核心。

阅读这一章时要注意：

- 这里描述的是“领域对象和业务真相”，不是直接对外暴露的 JSON 结构
- 同一个概念在不同层可以同名，但不一定同形
- 领域模型关注“系统里有什么对象、它们之间是什么关系、生命周期如何流转”
- 协议模型关注“接口字段怎么命名、怎么做兼容、怎么跨端传输”
- 持久化模型关注“数据库怎么建表、怎么建索引、怎么做事务和约束”
- UI 模型关注“界面怎么显示、发送态怎么表达、时间怎么格式化”

例如：

- 领域层可以叫 `Message.id`
- 协议层对外可以返回 `message_id`
- 数据库层可以落成 `messages.id`
- UI 层还可能额外加上 `is_self`、`send_state`、`display_time`

因此，不建议用一个结构体同时承担领域对象、数据库实体、协议 DTO 和界面 ViewModel 这四种职责。

### 6.1 User

职责：

- 表示一个用户账号
- 承担登录身份、展示昵称、头像、在线状态等属性

为什么需要：

- 登录注册、好友管理、发消息、发起通话都以用户为起点


### 6.2 DeviceSession

职责：

- 表示用户在某个设备上的登录状态
- 用于 refresh token、设备管理、异地登录控制

原理：

- 用户可能同时在多个端登录
- 登录状态不应该只依赖一个长期不变的 token

作用：

- 便于实现自动登录
- 便于实现多端在线
- 后期可以做“踢下线”“设备列表”


### 6.3 Friendship 和 FriendRequest

职责：

- `FriendRequest` 表示好友申请流程
- `Friendship` 表示最终好友关系

原理：

- “申请”是过程
- “好友关系”是结果
- 两者不能混成一个表，否则状态难维护

作用：

- 容易实现申请、接受、拒绝、撤回
- 好友列表查询清晰
- 后期可扩展黑名单、备注名、分组


### 6.4 Conversation

职责：

- 表示一次聊天会话
- 会话类型可分为 `direct` 和 `group`

原理：

- 私聊和群聊的共同点是“都属于会话”
- 私聊不是一个特殊页面，而是两个成员的直接会话
- 群聊不是完全不同的一套系统，而是成员数更多的会话

作用：

- 私聊和群聊统一处理
- 消息、未读、置顶、草稿等能力都可以围绕会话做
- 后期支持频道、讨论组时也容易扩展


### 6.5 ConversationMember

职责：

- 表示用户在某个会话中的成员身份
- 记录角色、昵称、已读游标、禁言状态等

原理：

- 会话本体只描述“这个会话是什么”
- 成员表描述“谁在里面，以及是什么身份”
- 第一版 `last_read_seq` 建议按 `user_id + conversation_id` 维度保存，不按设备拆分

作用：

- 支持群管理员、群主、普通成员
- 支持已读回执
- 支持成员级个性化状态
- 支持同一用户多端共享已读位置


### 6.6 Message

职责：

- 表示会话中的一条消息

领域上至少需要关注的核心属性：

- `id`
- `conversation_id`
- `sender_id`
- `seq`
- `client_msg_id`
- `type`
- `content`
- `created_at`
- `deleted_at`

原理：

- `id` 是全局唯一标识
- `seq` 是会话内有序编号
- `client_msg_id` 用于幂等去重
- `content` 表示消息的业务内容，领域层不要求它必须以某种具体存储格式存在

作用：

- 支持历史分页
- 支持失败重试
- 支持扩展消息类型而不频繁改表

落地说明：

- 在协议层，`id` 通常会映射成 `message_id`
- 在数据库层，`content` 可以落成 `content_json`、`attachment_id` 或其他更适合查询和存储的结构
- 在 UI 层，还会附加 `send_state`、`is_self`、`display_time` 等纯展示字段


### 6.7 Attachment

职责：

- 表示一条消息引用的文件、图片、音频等附件

原理：

- 二进制资源不应该直接和消息文本混在一起
- 附件应该有独立元数据，如文件名、大小、宽高、时长

作用：

- 支持图片、文件消息
- 后期支持语音留言、视频文件、缩略图
- 有利于统一权限和下载控制


### 6.8 CallSession

职责：

- 表示一次语音或视频通话

建议字段：

- `id`
- `conversation_id`
- `initiator_id`
- `type`
- `status`
- `started_at`
- `ended_at`

原理：

- 通话不是普通文本消息
- 通话有独立生命周期：发起、响铃、接听、拒绝、取消、结束

补充说明：

- 领域层关注的是正式 `call_id` 和通话生命周期
- 如果客户端需要对“发起通话”做幂等控制，协议层可以先传 `client_call_id`
- 服务端受理后再生成正式 `call_id`，后续 `offer / answer / ice_candidate / end` 都围绕正式 `call_id` 进行

作用：

- 便于管理一对一通话和群通话
- 便于记录未接来电、通话时长等系统事件


## 7. 数据库设计建议

建议第一版数据库核心表如下：

### 7.1 用户与登录

- `users`
- `device_sessions`

作用：

- 完成注册、登录、刷新 token、自动登录、多设备登录
- 为设备级登出、被踢下线、refresh token 轮换提供落地点


### 7.2 社交关系

- `friend_requests`
- `friendships`

作用：

- 支撑好友申请、接受、拒绝、好友列表

当前推荐建模：

- `friend_requests` 只负责“好友申请流程”，例如 pending / accepted / rejected / canceled / expired
- `friendships` 只负责“正式好友关系”
- 两者不能混成一张表，否则好友列表查询和历史申请审计都会越来越混乱

当前约束建议：

- `friend_requests` 应拦住“自己申请自己”
- 同一对用户在任意方向上，同时只允许存在一条 pending 申请
- `friendships` 当前采用“双行单向”建模

为什么 `friendships` 推荐双行单向：

- A 和 B 成为好友后，落两条记录：A -> B、B -> A
- 查询“我的好友列表”时只需要按 `user_id` 查一边
- 后续如果要加单方备注名、单方置顶、单方免打扰，更容易直接扩展在这张表上


### 7.3 会话与成员

- `conversations`
- `conversation_members`

作用：

- 统一支撑私聊和群聊
- 支撑成员关系、已读、角色
- 让 `last_read_seq` 以用户维度稳定落库


### 7.4 消息与附件

- `messages`
- `attachments`

作用：

- 支撑文本、图片、文件消息
- 支撑历史消息分页和文件引用


### 7.5 通话与信令状态

- `call_sessions`
- `call_participants`

作用：

- 支撑语音和视频通话流程
- 支撑通话记录和群通话成员状态


## 8. 通信方式设计

整个系统建议分三条通信链路。

### 8.1 HTTP

适合：

- 注册
- 登录
- 刷新 token
- 获取好友列表
- 获取会话列表
- 拉取历史消息
- 文件上传和下载

原理：

- 这些操作大多是请求 - 响应型，不需要保持长连接
- HTTP 更适合携带大文件、分页查询和通用接口规范

作用：

- 接口边界清晰
- 易于调试和抓包
- 适合作为稳定的业务 API 层

在本项目中的技术映射：

- 客户端：`QNetworkAccessManager`
- 服务端：`Drogon` HTTP Controller
- 典型接口：登录、注册、会话列表、历史消息分页、文件上传下载
- 设计要求：统一错误码、统一响应结构、统一 token 注入方式


### 8.2 WebSocket

适合：

- 实时消息推送
- 在线状态
- 已读回执
- 好友申请实时提醒
- 音视频通话信令

原理：

- IM 核心场景是服务端主动推送
- 单纯轮询会导致延迟高、请求浪费严重

作用：

- 实时性强
- 客户端可以长期保持一条连接
- 易于做事件驱动设计

在本项目中的技术映射：

- 客户端：`QWebSocket`
- 服务端：`Drogon` WebSocket Controller
- 典型事件：`message.send`、`message.ack`、`message.new`、`presence.changed`、`call.offer`
- 设计要求：统一事件信封、心跳、重连、鉴权和断线恢复策略


### 8.3 WebRTC

适合：

- 真实音视频媒体流

原理：

- 媒体流需要低延迟、抗抖动、音视频同步和网络自适应
- 这些能力不应该由普通聊天服务端自己实现

作用：

- 支持语音视频低延迟传输
- 降低自研媒体协议风险

在本项目中的技术映射：

- 客户端媒体层：`WebRTC`
- 网络穿透与中继：`coturn`
- 服务端职责：通过 WebSocket 做信令交换，不直接承载媒体流
- 设计要求：把“信令通道”和“媒体通道”明确分开


### 8.4 WebSocket 知识模块

#### 8.4.1 WebSocket 是什么

WebSocket 是一种建立在 TCP 之上的全双工长连接协议。

原理：

- 客户端会先发起一个 HTTP 请求，要求把连接从普通 HTTP 升级成 WebSocket
- 服务端同意后，返回 `101 Switching Protocols`
- 此后双方不再按普通 HTTP 请求 - 响应方式通信，而是基于一条长期连接进行消息帧收发

作用：

- 服务端可以主动向客户端推送事件
- 客户端和服务端都可以随时发消息
- 特别适合聊天、在线状态、消息确认、通话信令


#### 8.4.2 WebSocket 为什么适合 IM

原理：

- IM 的核心不是“发一个请求拿一个响应”，而是“服务端要随时推送新消息”
- 如果不用长连接，就只能轮询
- 轮询会带来更多无效请求、更高延迟和更差的实时体验

作用：

- 新消息可以实时推送
- 已读、撤回、好友申请、通话邀请都能事件化处理
- 一条连接可以承担整个登录后的大部分实时业务


#### 8.4.3 WebSocket 在本项目中的角色

建议承载：

- 实时聊天消息
- 在线状态
- 已读回执
- 好友申请提醒
- 通话信令

不建议承载：

- 登录注册
- 历史消息分页
- 大文件上传下载
- 真正的音视频媒体流

原理：

- 登录和分页更适合 HTTP
- 文件上传更适合独立 HTTP 传输
- 音视频媒体流需要更低延迟和更复杂的网络适配能力

作用：

- 让 WebSocket 专注于“实时事件同步”
- 避免把所有业务都压到同一条链路上


#### 8.4.4 WebSocket 通信过程

一次典型的 WebSocket 通信可以理解为：

1. 用户先通过 HTTP 登录
2. 客户端拿到 token
3. 客户端建立 WebSocket 连接
4. 建连成功后发送鉴权消息
5. 服务端校验通过后把该连接与用户绑定
6. 后续所有实时事件都通过这条连接收发

作用：

- 形成“登录后长期在线”的通信通道
- 为聊天和通话信令提供统一入口


### 8.5 WebRTC 知识模块

#### 8.5.1 WebRTC 是什么

WebRTC 不是单独一个协议，而是一整套实时通信技术栈。

原理：

- 它主要解决音频、视频和实时数据在两个终端之间的低延迟传输问题
- 它通常优先使用 UDP，以降低延迟
- 它内置了编码协商、网络自适应、回声处理、丢包对抗等能力

作用：

- 适合语音通话
- 适合视频通话
- 适合屏幕共享和低延迟实时互动


#### 8.5.2 WebRTC 为什么不能被普通 WebSocket 替代

原理：

- WebSocket 建立在 TCP 之上，可靠但重传会带来额外延迟
- 音视频场景更重视低延迟，部分数据丢失往往比长时间等待重传更可接受
- WebRTC 专门为实时媒体设计，具备抖动缓冲、码率控制、回声消除、网络探测等能力

作用：

- 保证语音视频体验不会因为 TCP 重传机制变得卡顿
- 让通话质量更适应复杂网络环境


#### 8.5.3 WebRTC 的关键知识点

必须理解以下几个术语：

- `Signaling`：信令层，负责交换通话控制信息，本项目建议用 WebSocket 承载
- `SDP`：会话描述，告诉对方“我支持哪些音视频能力”
- `ICE`：候选路径探测与选择机制，负责找到双方最可达的网络路径
- `STUN`：帮助终端知道自己的公网映射地址
- `TURN`：直连失败时，由中继服务器转发媒体
- `DTLS / SRTP`：负责媒体通道安全与加密

原理：

- WebRTC 不只是“把视频发出去”
- 在真正传输媒体前，双方必须先交换能力、探测网络、建立安全通道

作用：

- 帮助理解为什么音视频通话实现起来比文本消息复杂得多
- 帮助后续拆分“信令层”和“媒体层”


#### 8.5.4 WebRTC 在本项目中的角色

建议承载：

- 一对一语音通话媒体流
- 一对一视频通话媒体流
- 后期群语音和群视频的媒体能力

不建议承载：

- 登录注册
- 普通聊天消息
- 历史消息同步

原理：

- WebRTC 的职责是“实时媒体”
- 它不负责业务认证、聊天历史和普通消息路由

作用：

- 让通话模块和消息模块边界清晰
- 后期引入 SFU 或第三方 RTC 基础设施时更容易迁移


### 8.6 WebSocket 和 WebRTC 的配合关系

两者不是竞争关系，而是上下游协作关系。

原理：

- WebSocket 负责“传递协商信息”
- WebRTC 负责“真正传输音视频”

最典型的分工是：

- `HTTP`：登录、注册、列表、历史消息、文件上传
- `WebSocket`：聊天实时事件、在线状态、通话信令
- `WebRTC`：音频和视频媒体流

作用：

- 系统分层明确
- 每个技术只做自己擅长的事
- 后期排障时容易定位问题是在业务层还是媒体层


### 8.7 文本消息通信过程说明

文本消息链路建议如下：

1. 客户端通过 WebSocket 发送 `message.send`
2. 服务端校验 token 和会话成员身份
3. 服务端写入数据库，生成 `message_id` 和 `seq`
4. 服务端先返回 `message.ack`
5. 服务端再向会话内其他在线设备会话推送 `message.new`
6. 离线成员通过历史接口或重连同步补齐消息

原理：

- 先确认落库成功，再广播，能保证消息状态明确
- 使用 `seq` 能保证消息在会话内有稳定顺序

作用：

- 支持发送中、成功、失败状态
- 支持断线重连和重复发送去重


### 8.8 语音视频通话过程说明

一条完整的通话链路建议如下：

1. A 通过 WebSocket 发送 `call.invite`，带上 `client_call_id`
2. 服务端创建正式 `call_id`
3. B 收到来电提醒
4. A 和 B 后续都围绕正式 `call_id` 交换信令
5. A 创建 WebRTC `offer`
6. A 通过 WebSocket 把 `offer` 发给 B
7. B 创建 `answer`
8. B 通过 WebSocket 把 `answer` 发回 A
9. 双方继续通过 WebSocket 交换 `ICE candidates`
10. ICE 选择最优网络路径
11. 若能直连则走 P2P，若不能直连则走 TURN 中继
12. WebRTC 建立安全媒体通道，开始音视频传输

原理：

- 通话控制信息和媒体流是两条不同链路
- WebSocket 负责协商，WebRTC 负责传输

作用：

- 能清晰解释“为什么通话需要同时依赖 WebSocket 和 WebRTC”
- 为后续实现一对一语音、一对一视频、群通话提供统一认知模型


## 9. 功能设计

### 9.1 登录与注册

#### 设计方式

- 注册通过 HTTP 提交用户名、密码、展示名等信息
- 登录通过 HTTP 验证账号密码
- 登录成功后返回 `access token + refresh token`
- `access_token` 短期有效，`refresh_token` 与 `device_session` 绑定
- refresh 成功后轮换新的 `refresh_token`
- 客户端登录成功后建立 WebSocket 连接

#### 原理

- 登录属于短事务，适合 HTTP
- WebSocket 不适合直接承担密码登录流程
- token 模型比单纯的 session id 更适合多端和后期扩展

#### 作用

- 支撑基础认证
- 支撑自动登录
- 支撑多端登录和续期
- 支撑设备级登录态管理和被踢下线


### 9.2 好友管理

#### 设计方式

- 添加好友先创建好友申请
- 对方接受后写入正式好友关系
- 好友关系建立后，允许发起私聊或自动创建私聊会话
- 第一版默认不开放陌生人直接创建私聊会话

#### 原理

- “申请”和“关系”是两个阶段
- 好友关系和私聊会话不是同一件事

#### 作用

- 支撑标准 IM 社交链路
- 便于扩展黑名单、备注、好友分组


### 9.3 私聊

#### 设计方式

- 私聊本质上是 `Conversation(type=direct)`
- 成员固定为两个用户
- 会话模型独立于好友关系，但第一版用户入口默认要求双方已经是好友

#### 原理

- 私聊和群聊都只是会话的不同类型
- 统一成会话模型后，消息存储和未读逻辑可以复用
- 会话模型本身不应该被好友关系绑死，否则后续系统会话、客服会话、临时会话都很难扩展
- 但第一版业务规则可以先收紧，只允许好友之间建立新的私聊，降低权限复杂度和骚扰风险

#### 作用

- 避免为私聊单独维护一套消息表和接口
- 后期切换到更多聊天形态更容易
- 让“模型可扩展”和“第一版准入可控”同时成立


### 9.4 群聊

#### 设计方式

- 群聊本质上是 `Conversation(type=group)`
- 成员列表放在 `conversation_members`
- 群资料、群主、管理员、禁言都围绕成员关系扩展

#### 原理

- 群聊和私聊区别主要在成员数量和成员角色
- 只要会话和成员关系设计合理，群能力都可以叠加

#### 作用

- 支持群管理、群角色、群成员变更
- 支持群通话和群通知


### 9.5 文本消息

#### 设计方式

- 客户端发送 `message.send`
- 服务端校验身份和成员资格
- 服务端分配消息 `id` 和 `seq`
- 服务端返回 `message.ack`
- 服务端广播 `message.new`

#### 原理

- 文本消息是最基础的消息类型
- 发送成功与显示成功必须通过确认机制分离

#### 作用

- 支持“发送中 / 成功 / 失败”状态
- 支持断线重发和去重


### 9.6 图片与文件消息

#### 设计方式

分两步：

1. 先上传文件到文件服务
2. 再发送一条引用附件的消息

#### 原理

- 文件传输和消息广播是两个完全不同的问题
- 二进制上传失败不应直接破坏消息收发链路

#### 作用

- 上传链路可独立重试
- 消息内容结构统一
- 后期支持秒传、断点续传、缩略图


### 9.7 语音与视频通话

#### 设计方式

- 聊天服务端负责信令
- WebRTC 负责媒体流
- `CallSession` 负责通话状态

#### 原理

- 信令和媒体是两层
- 聊天服务端只负责协商过程和参与者状态
- 不负责真正的音视频编码和网络适配

#### 作用

- 可以先做一对一，再升级群通话
- 可逐步从 P2P 过渡到 SFU


## 10. 协议设计原则

这一章按协议类型来说明，而不是把所有原则混在一起。

阅读时可以把协议理解成 7 个模块：

1. 通用约定模块
2. HTTP 协议模块
3. WebSocket 通用模块
4. 消息协议模块
5. 文件与附件协议模块
6. 在线状态与已读同步模块
7. 通话信令协议模块

最后再统一补充错误处理、兼容性和排障要求。

### 10.1 通用约定模块

这一层不关心“具体是什么业务”，只定义所有协议共同遵守的基础规则。

#### 为什么要先定义这一层

- 如果基础约定不统一，后面每个模块都会各写一套字段风格
- 时间、ID、枚举值、版本号一旦混乱，协议文档和代码都会越来越难维护
- 这些规则应该是全项目通用的，不应该分散在消息、文件、通话等各章节里

#### 该怎么做

- 所有字段统一使用 `snake_case`
- 对外暴露的 ID 一律使用字符串，例如 `user_id`、`conversation_id`、`message_id`
- 时间统一使用 `epoch_ms`
- 布尔值统一使用 `true/false`
- 枚举统一使用稳定字符串，例如 `type=text`、`status=online`
- 可选字段缺失时直接省略，不大量传 `null`
- 所有协议都保留版本概念
- HTTP 路径使用版本前缀，例如 `/api/v1/...`
- WebSocket 信封保留 `version`

#### 能达到什么效果

- 客户端和服务端字段含义更一致
- 多端扩展时不容易出现“同一字段，不同理解”
- 后续协议迭代时更容易做兼容

#### 字段规范表

| 规范项 | 推荐写法 | 不推荐写法 | 说明 |
| --- | --- | --- | --- |
| 字段命名 | `user_id` | `userId` / `uid` | 统一使用 `snake_case`，避免不同模块命名风格混乱 |
| ID 类型 | `"u_1001"` | `1001` | 对外 ID 统一使用字符串，便于跨语言和跨端处理 |
| 时间字段 | `created_at_ms: 1770000000000` | `created_at: "2026-03-10 10:00:00"` | 统一使用 `epoch_ms`，便于排序、比较和跨时区显示 |
| 枚举值 | `type: "text"` | `type: 1` | 使用稳定字符串，比数字枚举更直观、更易扩展 |
| 布尔值 | `is_muted: true` | `is_muted: 1` / `is_muted: "yes"` | 统一使用 JSON 原生布尔值 |
| 可选字段 | 缺失时省略 | `field: null` 大量出现 | 只有确实需要表达空值时才用 `null` |
| 版本控制 | `version: 1` | 完全不带版本 | 为后续兼容和升级留空间 |

#### 推荐 / 不推荐示例

推荐：

```json
{
  "user_id": "u_1001",
  "conversation_id": "c_2001",
  "created_at_ms": 1770000000000,
  "type": "text",
  "is_muted": false
}
```

不推荐：

```json
{
  "uid": 1001,
  "conversationId": 2001,
  "createdAt": "2026-03-10 10:00:00",
  "type": 1,
  "isMuted": 0
}
```


### 10.2 HTTP 协议模块

HTTP 模块负责所有“请求 - 响应型”业务。

#### 负责什么

- 注册
- 登录
- 刷新 token
- 好友列表、会话列表查询
- 历史消息分页
- 文件上传下载

#### 为什么这些能力应该走 HTTP

- 这些操作本质上都是一次请求对应一次结果
- HTTP 更适合鉴权、分页、文件传输和调试抓包
- 如果把这些操作硬塞进 WebSocket，协议会逐渐变成“伪 HTTP”

#### 该怎么做

- 接口统一走 `/api/v1/...`
- 登录、注册这类公开接口不带 `Authorization: Bearer <access_token>`
- 受保护接口统一使用 `Authorization: Bearer <access_token>`
- refresh 接口显式携带 `refresh_token + device_session_id`
- 每个请求建议带 `X-Request-Id`
- 响应结构保持统一，例如返回 `code`、`message`、`request_id`、`data`
- 历史消息分页使用 `before_seq` / `after_seq`，不使用页码

建议形式：

- `GET /api/v1/conversations/{id}/messages?before_seq=1000&limit=50`

#### 能达到什么效果

- 接口边界清晰
- 分页和列表查询更稳定
- 文件传输不会污染实时链路

#### 示例

注册接口示例：

```http
POST /api/v1/auth/register
X-Request-Id: req_register_001
Content-Type: application/json
```

```json
{
  "account": "Wumo_01",
  "password": "Password123",
  "nickname": "Wumo",
  "avatar_upload_key": "tmp/2026/03/16/tmp_avatar_001.png"
}
```

响应示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_register_001",
  "data": {
    "user": {
      "user_id": "u_1001",
      "account": "Wumo_01",
      "nickname": "Wumo",
      "avatar_url": "avatars/2026/03/16/avatar_001.png",
      "created_at_ms": 1770000000000
    }
  }
}
```

登录接口示例：

```http
POST /api/v1/auth/login
X-Request-Id: req_login_001
Content-Type: application/json
```

```json
{
  "account": "wumo",
  "password": "123456"
}
```

响应示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_login_001",
  "data": {
    "user": {
      "user_id": "u_1001",
      "nickname": "Wumo",
      "avatar_url": "avatars/2026/03/16/avatar_001.png"
    },
    "device_session_id": "ds_7001",
    "access_token": "access_xxx",
    "refresh_token": "refresh_xxx",
    "expires_in_sec": 7200
  }
}
```

字段作用说明：

- `code`：业务状态码，`0` 通常表示成功
- `message`：本次请求的文本说明
- `request_id`：服务端回显的请求追踪 ID，便于排查问题
- `data`：真正的业务数据载荷
- `user`：当前登录用户的基础信息
- `user_id`：用户唯一标识
- `nickname`：展示昵称
- `avatar_upload_key`：临时头像上传接口返回的临时引用，注册或资料修改时用它确认头像
- `avatar_url`：当前阶段实际表示头像文件的 `storage key`，例如 `avatars/2026/03/16/avatar_001.png`，不是公网外链
- `device_session_id`：当前这次登录对应的设备会话 ID，后续 refresh、踢下线、WebSocket 鉴权都会围绕它工作
- `access_token`：后续访问 HTTP 和 WebSocket 鉴权使用的短期令牌
- `refresh_token`：用于刷新新的 `access_token`
- `expires_in_sec`：`access_token` 的有效时长

临时头像上传示例：

```http
POST /api/v1/users/avatar/temp
X-Request-Id: req_avatar_temp_001
Content-Type: multipart/form-data
```

表单字段：

- `avatar`：头像文件本体

响应示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_avatar_temp_001",
  "data": {
    "avatar_upload_key": "tmp/2026/03/16/tmp_avatar_001.png",
    "preview_url": "/api/v1/users/avatar/temp?avatar_upload_key=tmp%2F2026%2F03%2F16%2Ftmp_avatar_001.png"
  }
}
```

好友列表示例：

```http
GET /api/v1/friends
Authorization: Bearer access_xxx
X-Request-Id: req_friends_001
```

响应示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_friends_001",
  "data": {
    "friends": [
      {
        "user": {
          "user_id": "u_1002",
          "account": "friend_01",
          "nickname": "李华",
          "avatar_url": "avatars/2026/03/16/avatar_002.png"
        },
        "created_at_ms": 1770000100000
      }
    ]
  }
}
```

字段作用说明：

- `friends`：当前登录用户的正式好友集合
- `user`：好友基础资料
- `created_at_ms`：双方成为好友的时间戳，便于后续排序和展示

资料更新接口示例：

```http
PATCH /api/v1/users/me/profile
Authorization: Bearer access_xxx
X-Request-Id: req_profile_001
Content-Type: application/json
```

```json
{
  "nickname": "NewName",
  "avatar_upload_key": "tmp/2026/03/16/tmp_avatar_002.png"
}
```

设计约束：

- 资料更新请求中的字段都应为可选
- 只更新本次明确传入的字段，不应把未传字段误清空
- `avatar_upload_key` 一旦确认成功，就应转成正式头像 `storage key`
- 客户端获取最终头像文件时，优先走 `GET /api/v1/users/{user_id}/avatar`


### 10.3 WebSocket 通用模块

WebSocket 模块负责所有“实时事件同步”。

补充说明：

- `docs/ws-protocol.md` 专门描述 WebSocket 统一信封、`ws.auth` 和统一 `ws.send / ws.ack / ws.new + route` 事件协议
- `docs/ws-api.md` 专门描述当前已落地 WebSocket 事件的发送结构、返回结构和字段含义
- `docs/ws-architecture.md` 专门描述 WebSocket 的目录结构、文件分工、连接生命周期和实施顺序

#### 负责什么

- 实时消息推送
- 发送确认
- 在线状态变化
- 已读同步
- 好友申请提醒
- 通话协商信令

#### 为什么要单独做一个通用模块

- WebSocket 是长期连接，事件类型会越来越多
- 如果没有统一外层结构，服务端路由和客户端解析会迅速失控
- 长连接问题通常最难排查，必须从第一版开始预留追踪字段

#### 该怎么做

- 所有 WebSocket 消息都使用统一信封
- 顶层至少保留 `version`、`type`、`payload`
- 客户端主动发起的请求必须带 `request_id`
- 请求响应类消息原样回传 `request_id`
- 服务端主动推送消息可以不带 `request_id`，但结构仍保持一致
- 客户端登录成功后保存 `device_session_id`
- `ws.auth` 建议同时携带 `access_token`、本地 `device_id` 和服务端分配的 `device_session_id`
- 业务事件统一收敛为：
  - `ws.send`
  - `ws.ack`
  - `ws.new`
- 具体业务动作由 `payload.route` 区分，例如：
  - `message.send_text`
  - `conversation.create_private`
  - `message.created`
  - `friend.request.new`

建议结构：

```json
{
  "version": 1,
  "type": "ws.send",
  "request_id": "req_001",
  "ts_ms": 1770000000000,
  "payload": {
    "route": "message.send_text",
    "data": {}
  }
}
```

字段含义：

- `version`：协议版本
- `type`：顶层事件类型，例如 `ws.auth`、`ws.send`、`ws.ack`、`ws.new`
- `request_id`：一次请求和对应响应的关联标识
- `ts_ms`：消息发送时间
- `payload`：该事件真正的业务内容
- `payload.route`：具体业务动作或事件类型

#### 能达到什么效果

- 服务端可以先按 `type` 路由，再解析 `payload`
- 客户端可以先统一解包，再交给不同处理器
- 后续新增事件不需要重做整套消息结构

#### 示例

WebSocket 鉴权请求示例：

```json
{
  "version": 1,
  "type": "ws.auth",
  "request_id": "req_auth_001",
  "ts_ms": 1770000000000,
  "payload": {
    "access_token": "access_xxx",
    "device_id": "pc_windows_001",
    "device_session_id": "ds_7001",
    "client_version": "0.1.0"
  }
}
```

鉴权成功响应示例：

```json
{
  "version": 1,
  "type": "ws.auth.ok",
  "request_id": "req_auth_001",
  "ts_ms": 1770000000100,
  "payload": {
    "user_id": "u_1001",
    "server_time_ms": 1770000000100
  }
}
```

字段作用说明：

- `version`：协议版本号
- `type`：当前事件类型，这里分别是 `ws.auth` 和 `ws.auth.ok`
- `request_id`：把这次鉴权请求和响应关联起来
- `ts_ms`：消息发送时间
- `payload`：当前事件的业务数据
- `access_token`：用于 WebSocket 鉴权的登录令牌
- `device_id`：客户端本地设备标识，适合区分“这是一台什么设备”
- `device_session_id`：服务端分配的本次登录会话标识，适合区分“这次登录实例是谁”
- `client_version`：客户端版本号，便于兼容判断和排障
- `user_id`：鉴权成功后绑定的用户 ID
- `server_time_ms`：服务端当前时间，可用于时间校准和调试


### 10.4 消息协议模块

消息协议模块是整个 IM 系统的核心。

#### 负责什么

- 文本消息发送
- 文本消息确认
- 新消息推送
- 消息顺序控制
- 历史消息同步

#### 为什么这个模块要单独强调

- 聊天系统最难的不是“发一个 JSON”，而是保证不重、不错序、可重试、可补拉
- 这里一旦设计错，后面的历史记录、未读、重连恢复都会一起出问题

#### 该怎么做

- 客户端每次发送消息都生成 `client_msg_id`
- 同一条消息重试时必须复用原来的 `client_msg_id`
- 服务端按 `conversation_id + sender_id + client_msg_id` 做幂等判断
- 服务端为每条正式消息生成 `message_id`
- 服务端为每个会话生成严格递增的 `seq`
- 客户端显示顺序、补拉逻辑、已读游标都以 `seq` 为准
- `message.ack` 只返回给发起发送的那条连接
- `message.new` 推送给会话内所有在线设备会话，包括发送者的其他在线设备

推荐发送时序：

1. 客户端发送 `message.send`
2. 服务端校验身份和会话成员资格
3. 服务端落库
4. 服务端生成 `message_id` 和 `seq`
5. 服务端返回 `message.ack`
6. 服务端向会话内其他在线设备会话推送 `message.new`

#### 能达到什么效果

- 支持弱网重试而不重复落库
- 多端消息顺序一致
- 本地临时消息可以和正式消息准确对齐
- 历史消息、未读、断线补拉都能围绕同一套顺序体系工作
- 发送者自己的其他设备也能及时收到正式消息

#### 示例

发送文本消息：

```json
{
  "version": 1,
  "type": "message.send",
  "request_id": "req_msg_001",
  "ts_ms": 1770000001000,
  "payload": {
    "conversation_id": "c_2001",
    "client_msg_id": "cm_5001",
    "message": {
      "type": "text",
      "content": {
        "text": "今晚 8 点开会"
      }
    }
  }
}
```

服务端确认：

```json
{
  "version": 1,
  "type": "message.ack",
  "request_id": "req_msg_001",
  "ts_ms": 1770000001080,
  "payload": {
    "conversation_id": "c_2001",
    "client_msg_id": "cm_5001",
    "message_id": "m_3001",
    "seq": 101,
    "created_at_ms": 1770000001080
  }
}
```

服务端推送新消息：

```json
{
  "version": 1,
  "type": "message.new",
  "ts_ms": 1770000001085,
  "payload": {
    "conversation_id": "c_2001",
    "message": {
      "message_id": "m_3001",
      "seq": 101,
      "sender_id": "u_1001",
      "type": "text",
      "content": {
        "text": "今晚 8 点开会"
      },
      "created_at_ms": 1770000001080
    }
  }
}
```

字段作用说明：

- `conversation_id`：这条消息属于哪个会话
- `client_msg_id`：客户端生成的消息幂等键，用于重试去重
- `message_id`：服务端正式分配的消息 ID
- `seq`：会话内严格递增的顺序号
- `sender_id`：真正发送者的用户 ID
- `type`：消息类型，这里是 `text`
- `content.text`：文本消息正文
- `created_at_ms`：服务端确认的消息创建时间

三个示例的分工：

- `message.send`：客户端发起发送请求
- `message.ack`：服务端确认这次发送已经成功落库
- `message.new`：服务端把正式消息推送给会话成员

多端同步规则：

- 发起发送的原始连接一般只处理 `message.ack`，用它把本地临时消息升级成正式消息
- 发送者的其他在线设备和接收方设备处理 `message.new`
- 如果第一版希望逻辑更简单，也可以让原始连接同时收到 `message.new`，但必须保证客户端不会把同一条消息显示两次


### 10.5 文件与附件协议模块

这一模块解决的不是“聊天”，而是“附件如何进入消息系统”。

#### 负责什么

- 图片上传
- 文件上传
- 附件元数据引用
- 图片、文件消息的协议表示

#### 为什么必须单独拆出来

- 文件传输和消息广播不是一回事
- WebSocket 适合传控制信息，不适合直接传大块二进制
- 上传失败和消息发送失败应该能各自处理、各自重试

#### 该怎么做

- 所有消息体统一保留 `type` 和 `content`
- 文本消息使用 `type=text`
- 图片消息使用 `type=image`
- 文件消息使用 `type=file`
- 图片和文件先通过 HTTP 上传
- 服务端返回 `attachment_upload_key`
- 发送消息时只在 `content` 中引用 `attachment_id`、`caption`、`file_name` 等摘要字段
- 上传接口不直接暴露底层存储路径
- 下载和预览仍由服务端先做权限校验，再返回下载流或受控 URL

#### 能达到什么效果

- 消息协议结构更稳定
- 文件传输和消息广播完全解耦
- 后续扩展语音留言、短视频、位置、名片消息更容易

#### 示例

第一步，先上传附件：

```http
POST /api/v1/files/upload
Authorization: Bearer <access_token>
Content-Type: multipart/form-data
```

上传成功返回：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_upload_001",
  "data": {
    "upload": {
      "attachment_upload_key": "tmp/attachments/u_1001/2026/03/21/tmp_upload_xxx.png",
      "file_name": "design.png",
      "mime_type": "image/png",
      "size_bytes": 245120,
      "media_kind": "image"
    }
  }
}
```

第二步，再发送图片或文件消息：

```json
{
  "version": 1,
  "type": "ws.send",
  "request_id": "req_msg_img_001",
  "ts_ms": 1770000002000,
  "payload": {
    "route": "message.send_image",
    "data": {
      "conversation_id": "c_2001",
      "client_message_id": "cm_5002",
      "attachment_upload_key": "tmp/attachments/u_1001/2026/03/21/tmp_upload_xxx.png",
      "caption": "设计稿初版"
    }
  }
}
```

字段作用说明：

- `attachment_upload_key`：临时附件引用，正式消息确认时由服务端把它转成真正的 `attachment_id`
- `file_name`：文件原始展示名
- `mime_type`：文件 MIME 类型，用于判断预览和处理方式
- `size_bytes`：文件大小
- `caption`：图片或文件的附带说明文本
- `request_id`：上传请求的追踪 ID
- `route=message.send_image / message.send_file`：表示这是一条附件消息确认动作
- `type=image / file`：表示最终正式消息的附件类别


### 10.6 在线状态与已读同步模块

这部分经常被低估，但它直接决定聊天产品像不像真正的 IM。

#### 负责什么

- 用户在线状态变化
- 已读位置同步
- 断线重连后的增量补拉

#### 为什么要单独成模块

- 在线状态和已读不是普通消息，但它们又和消息顺序强相关
- 如果把它们零散塞进别的模块，后期状态恢复会很难维护

#### 该怎么做

- 已读状态使用 `last_read_seq`
- 第一版 `last_read_seq` 按用户维度保存，不按设备拆分
- 历史消息按 `before_seq` / `after_seq` 分页
- `before_seq` 和 `after_seq` 都使用“排除型游标”，即返回结果不包含游标本身
- 首次进入会话时拉最新一页
- 上拉历史使用 `before_seq`
- 重连补拉和未读追赶使用 `after_seq`
- 返回结果带上 `has_more`、`next_before_seq`、`next_after_seq`
- 历史接口返回的 `items` 统一按 `seq` 升序排列
- 任一设备上报更大的 `last_read_seq` 后，服务端更新用户级游标，并通过 `conversation.read.updated` 同步给该用户其他在线设备
- 在线状态内部按 `device_session` 维护，对外默认按 `user_id` 聚合
- 对外只同步业务需要的最小在线状态信息，例如 `status`、`last_active_ms`

建议形式：

- `GET /api/v1/conversations/{id}/messages?before_seq=1000&limit=50`

#### 能达到什么效果

- 历史分页稳定
- 已读同步有统一基准
- 断线恢复更自然
- 客户端不需要处理页码错位问题

#### 示例

已读同步事件：

```json
{
  "version": 1,
  "type": "message.read",
  "request_id": "req_read_001",
  "ts_ms": 1770000003000,
  "payload": {
    "conversation_id": "c_2001",
    "last_read_seq": 101
  }
}
```

在线状态推送：

```json
{
  "version": 1,
  "type": "presence.changed",
  "ts_ms": 1770000003200,
  "payload": {
    "user_id": "u_1002",
    "status": "online",
    "last_active_ms": 1770000003200
  }
}
```

同一用户其他设备收到的已读同步：

```json
{
  "version": 1,
  "type": "conversation.read.updated",
  "ts_ms": 1770000003050,
  "payload": {
    "conversation_id": "c_2001",
    "last_read_seq": 101
  }
}
```

历史消息分页响应示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_history_001",
  "data": {
    "items": [
      {
        "message_id": "m_2990",
        "seq": 90,
        "sender_id": "u_1002",
        "type": "text",
        "content": {
          "text": "收到"
        },
        "created_at_ms": 1769999999000
      }
    ],
    "has_more": true,
    "next_before_seq": 90
  }
}
```

重连后的增量补拉示例：

```json
{
  "code": 0,
  "message": "ok",
  "request_id": "req_history_after_001",
  "data": {
    "items": [
      {
        "message_id": "m_3001",
        "seq": 101,
        "sender_id": "u_1001",
        "type": "text",
        "content": {
          "text": "今晚 8 点开会"
        },
        "created_at_ms": 1770000001080
      }
    ],
    "has_more": false,
    "next_after_seq": 101
  }
}
```

字段作用说明：

- `last_read_seq`：当前用户在该会话中最后已读到的消息序号
- `status`：在线状态，例如 `online`、`offline`
- `last_active_ms`：最后活跃时间
- `items`：本次分页返回的消息列表
- `has_more`：是否还有更多历史消息
- `next_before_seq`：下一次向前翻页时应继续使用的游标
- `next_after_seq`：下一次做增量补拉时应继续使用的最新游标
- `request_id`：本次 HTTP 响应对应的请求追踪 ID


### 10.7 通话信令协议模块

这一模块只负责“协商过程”，不负责真正的媒体传输。

#### 负责什么

- 发起通话
- 接听、拒绝、取消、结束
- `offer / answer / ice_candidate`

#### 为什么要把它和普通消息协议分开

- 通话有独立生命周期：发起、响铃、接听、拒绝、取消、结束
- 通话信令虽然走 WebSocket，但它不是普通聊天消息
- 真正的音视频媒体流由 WebRTC 负责，不应该塞进聊天消息通道

#### 该怎么做

- 通话协商消息统一放在单独事件命名空间中，例如 `call.invite`、`call.accept`、`call.offer`
- 信令只传协商所需字段，不混入 UI 展示字段
- 服务端负责路由、状态校验和通话状态记录
- 客户端收到信令后再驱动 WebRTC 建立媒体连接
- 发起通话时客户端先传 `client_call_id`
- 服务端受理后生成正式 `call_id`
- 后续 `offer / answer / ice_candidate / end` 都使用正式 `call_id`

#### 能达到什么效果

- 聊天消息链路和通话链路边界清晰
- 后续做一对一语音、一对一视频、群通话时更容易扩展
- 未来如果引入 SFU，也不需要推翻普通消息协议

#### 示例

发起通话：

```json
{
  "version": 1,
  "type": "call.invite",
  "request_id": "req_call_001",
  "ts_ms": 1770000004000,
  "payload": {
    "client_call_id": "cc_7001",
    "conversation_id": "c_2001",
    "call_type": "video"
  }
}
```

服务端确认通话创建：

```json
{
  "version": 1,
  "type": "call.invite.ok",
  "request_id": "req_call_001",
  "ts_ms": 1770000004050,
  "payload": {
    "client_call_id": "cc_7001",
    "call_id": "call_7001",
    "status": "ringing"
  }
}
```

发送 WebRTC offer：

```json
{
  "version": 1,
  "type": "call.offer",
  "request_id": "req_call_offer_001",
  "ts_ms": 1770000004200,
  "payload": {
    "call_id": "call_7001",
    "to_user_id": "u_1002",
    "sdp": "v=0..."
  }
}
```

发送 ICE candidate：

```json
{
  "version": 1,
  "type": "call.ice_candidate",
  "request_id": "req_call_ice_001",
  "ts_ms": 1770000004300,
  "payload": {
    "call_id": "call_7001",
    "to_user_id": "u_1002",
    "candidate": {
      "candidate": "candidate:...",
      "sdp_mid": "0",
      "sdp_mline_index": 0
    }
  }
}
```

字段作用说明：

- `client_call_id`：客户端发起通话时生成的幂等标识
- `call_id`：服务端正式创建成功后的通话唯一标识
- `conversation_id`：通话所属会话
- `call_type`：通话类型，例如 `audio` 或 `video`
- `to_user_id`：当前信令要发给的目标用户
- `status`：当前通话状态，这里示例为 `ringing`
- `sdp`：WebRTC 协商用的会话描述
- `candidate`：ICE 候选网络路径对象
- `sdp_mid`：对应的媒体通道标识
- `sdp_mline_index`：对应的媒体描述行下标


### 10.8 错误处理与连接管理模块

这一模块负责把“失败、鉴权、保活、重连”这类通用问题统一下来。

#### 负责什么

- HTTP 错误语义
- WebSocket 错误响应
- WebSocket 鉴权
- 心跳
- 重连和状态恢复

#### 为什么要统一

- 用户不会关心错误来自 HTTP 还是 WebSocket，只会感知“未登录”“没有权限”“发送失败”
- 长连接真正麻烦的地方也不是连上，而是鉴权、保活、掉线和恢复
- 如果这些规则不统一，客户端会写出大量特殊分支

#### 该怎么做

- HTTP 状态码表达错误大类，例如 `400`、`401`、`403`、`404`、`409`、`429`、`500`
- 业务层再使用统一 `code`
- WebSocket 错误响应至少包含 `code`、`message`、`request_id`
- 客户端先 HTTP 登录拿 `access_token`
- `access_token` 保持短期有效，`refresh_token` 与 `device_session_id` 绑定
- refresh 成功后轮换新的 `refresh_token`
- WebSocket 建立后先发送 `ws.auth`
- 服务端通过后返回 `ws.auth.ok`
- 双方定义心跳事件
- 主动登出、被踢下线、refresh 失效时，服务端应使对应 `device_session` 失效并关闭其 WebSocket 连接
- 重连成功后按本地游标做增量补拉

#### 能达到什么效果

- 客户端错误处理逻辑更统一
- 登录态和长连接状态更稳定
- 掉线、token 过期、重复登录更容易处理

#### 示例

WebSocket 错误响应：

```json
{
  "version": 1,
  "type": "error",
  "request_id": "req_msg_001",
  "ts_ms": 1770000005000,
  "payload": {
    "code": 40301,
    "message": "not conversation member"
  }
}
```

被踢下线示例：

```json
{
  "version": 1,
  "type": "auth.kicked",
  "ts_ms": 1770000005050,
  "payload": {
    "device_session_id": "ds_7001",
    "reason": "logged_in_elsewhere"
  }
}
```

字段作用说明：

- `type=error`：表示这是一条错误响应
- `payload.code`：统一业务错误码
- `payload.message`：错误说明
- `type=auth.kicked`：表示当前设备会话被服务端强制下线
- `device_session_id`：被关闭的设备会话 ID
- `reason`：强制下线原因，例如异地登录、管理员踢下线、refresh 失效
- 当前连接保活统一依赖 WebSocket 协议层 `Ping/Pong`
- 业务层不再定义 JSON `ping / pong` 事件


### 10.9 兼容性与排障模块

最后这一层解决的是“协议怎么长期活下去”。

#### 负责什么

- 版本演进
- 向后兼容
- 日志追踪
- 抓包排障

#### 为什么必须从第一版就考虑

- 协议一旦上线，就不再只是本地代码约定，而是线上兼容契约
- IM 的很多问题发生在时序边界，没有足够追踪字段几乎无法定位

#### 该怎么做

- 所有协议都保留版本概念
- 新增字段优先追加，不随意修改旧字段含义
- 客户端忽略未知但非关键字段
- 服务端对缺失的新字段提供默认处理
- 关键事件日志记录 `request_id`、`conversation_id`、`message_id`、`client_msg_id`、`seq`

#### 能达到什么效果

- 协议可以平滑迭代
- 旧客户端不会被轻易打崩
- 日志、抓包、监控可以更快还原问题现场

#### 示例

兼容性示例：

```json
{
  "version": 1,
  "type": "message.new",
  "payload": {
    "conversation_id": "c_2001",
    "message": {
      "message_id": "m_3002",
      "seq": 102,
      "sender_id": "u_1002",
      "type": "text",
      "content": {
        "text": "明天补充文档"
      },
      "created_at_ms": 1770000006000,
      "mentions": []
    }
  }
}
```

这里的 `mentions` 就属于“新增字段”。旧客户端即使暂时不认识它，也应该忽略而不是报错。

日志追踪示例：

```text
type=message.send request_id=req_msg_001 conversation_id=c_2001 client_msg_id=cm_5001 user_id=u_1001
type=message.ack request_id=req_msg_001 conversation_id=c_2001 message_id=m_3001 seq=101 user_id=u_1001
```

字段作用说明：

- `mentions`：新增字段示例，用于表示被提及用户列表
- `request_id`：一次请求和响应的追踪主键
- `conversation_id`：便于按会话定位问题
- `client_msg_id`：便于排查重复发送和重试问题
- `message_id`：便于定位数据库中的正式消息
- `seq`：便于排查消息顺序和分页问题



## 11. 服务端目录结构建议

建议结构如下：

  ```text
  chatServer/
    CMakeLists.txt
    config/
    app.json
    db/
      migrations/
  src/
    main.cpp
    app/
      application.h
      application.cpp
    protocol/
      dto/
      codec/
      error/
    domain/
      user/
      social/
      conversation/
      message/
      call/
    repository/
      user_repository.h
      friendship_repository.h
      conversation_repository.h
      message_repository.h
      call_repository.h
    service/
      auth_service.h
      social_service.h
      conversation_service.h
      message_service.h
      file_service.h
      presence_service.h
      rtc_signaling_service.h
    transport/
      http/
      ws/
    storage/
      file_storage.h
      local_storage.h
      minio_storage.h
    infra/
      db/
      redis/
      log/
      config/
      id/
  tests/
```

### 原理

- 按职责分层，不按接口数量堆文件
- `transport` 只管接入层
- `service` 只管业务
- `repository` 只管持久化

与技术栈的对应关系：

- `transport/http` 默认基于 `Drogon` HTTP Controller
- `transport/ws` 默认基于 `Drogon` WebSocket Controller
- `infra/db` 负责 `PostgreSQL` 访问封装
- `infra/redis` 负责 `Redis` 状态管理与广播辅助
- `storage` 负责屏蔽本地文件目录和 `MinIO` 的实现差异

### 作用

- 代码边界清晰
- 后期替换数据库或存储实现更容易
- 单元测试和集成测试更容易定位问题


## 12. 客户端后续建议结构

客户端也不要继续把网络逻辑直接塞到窗口类里。

更完整的客户端建设说明见 `docs/chatclient-architecture.md`。那份文档单独展开了：

- 当前客户端真实现状
- `config / api / ws / service / dto / viewmodel` 的依赖方向
- 登录链路、历史消息链路、消息发送链路、消息接收链路
- DTO、ViewModel 和现有 Qt UI Model 的边界
- 建议的客户端分阶段落地顺序

建议新增：

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
    auth_service.h
    auth_error_localizer.h
    conversation_manager.h
    chat_service.h
    friend_service.h
    friend_error_localizer.h
    call_service.h
  store/
    conversation_store.h
  dto/
    auth_dto.h
    user_dto.h
    message_dto.h
    conversation_dto.h
    friend_dto.h
  viewmodel/
    chat_message_view_model.h
    conversation_item_view_model.h
  model/
    conversationlistmodel.h
```

其中：

- `api/` 内部统一基于 `QNetworkAccessManager`
- `ws/` 内部统一基于 `QWebSocket`
- `service/` 负责组合 HTTP、WebSocket、本地缓存和业务状态
- 会话域建议以 `ConversationManager` 作为对 `ChatWindow` 的统一入口
- `ConversationManager` 内部协调 `ConversationApiClient`、`ConversationListModel`、`MessageModelRegistry` 和后续 `ChatWsClient`
- 第一版由 `ConversationManager` 内部维护最小会话状态，例如初始化标记、分页游标和 `minSeq / maxSeq`
- `ConversationListModel` 只负责中间栏会话列表展示，不直接承载网络请求和历史消息缓存
- 如果后续缓存、重连恢复和分页逻辑明显变重，再把 `ConversationManager` 内部状态抽成独立 `ConversationStore`
- `dto/` 负责承载协议字段，不直接承担 UI 展示职责
- `viewmodel/` 负责把 DTO 转成界面可消费的数据结构
- `call_service` 负责协调 WebSocket 信令和 `WebRTC` 通话控制

### 原理

- 窗口类应该负责界面
- 网络请求和业务状态变更应该放到 service 层

### 作用

- 避免界面类越来越臃肿
- 更容易实现重连、缓存、错误提示、状态同步


## 13. 分阶段实施方案

下面是基于当前真实进度重新整理后的推荐实施顺序。调整原则只有两个：

- 优先把已经有 UI 骨架和认证基础的链路接成真实业务闭环
- 避免过早进入 WebSocket、重连、音视频这类高耦合模块

每一步都给出“原理”和“作用”，目的是让后续开发时一眼能看出为什么先做它。

### 第 1 步：搭服务端基础脚手架

内容：

- 初始化 `chatServer/CMakeLists.txt`
- 接入 Drogon
- 建立配置文件
- 建立日志模块
- 建立数据库连接
- 建立数据库迁移目录
- 建立本地文件存储目录和统一存储抽象

当前进度（2026-03-16）：

- 已完成 `chatServer/CMakeLists.txt`
- 已接入 `Drogon`
- 已建立 `config/app.json`
- 已建立 `db/init_sql.py`
- 已将认证迁移按基础函数、users、device_sessions 拆分为多个子脚本，并由 Python 执行入口顺序执行
- 已建立 `src/app/application.*`
- 已建立 `src/transport/http/auth_controller.*`
- 已建立 `src/service/auth_service.*`
- 已建立 `src/repository/user_repository.*`
- 已建立 `src/infra/id/*` 和 `src/infra/security/*`
- 已建立 PostgreSQL 开发连接
- 已建立 Redis 开发连接
- 已建立统一日志模块
- 已建立最小可运行入口并提供返回数据库与 Redis 状态的 `GET /health`
- 已实现 `POST /api/v1/auth/register`
- 已把当前服务端第三方依赖统一放到 `chatServer/3rdparty/`
- 已建立统一文件存储抽象 `storage/file_storage.*`
- 已建立本地文件存储实现 `storage/local_storage.*`
- 已将本地文件存储目录配置统一并入 `app.json`
- 已支持在启动时自动创建 `uploads/tmp`、`uploads/files`、`uploads/avatars`

原理：

- 任何业务功能都依赖运行时基础设施
- 如果没有配置、日志、数据库连接池，后续每个模块都会各自实现一套基础逻辑

作用：

- 给后续所有业务模块提供统一基础设施
- 让服务端具备最小可运行能力
- 提前把 `Drogon + PostgreSQL + Redis + 文件存储` 这条底座链路搭稳


### 第 2 步：完成受保护接口基础与登录态管理

内容：

- 建立统一的 Bearer Token 校验能力
- 建立“当前登录用户 / 当前 device_session”提取能力
- 让后续 HTTP 受保护接口不再重复手写 token 解析
- 建立客户端登录态恢复和失效处理策略
- 明确 `logout / token invalid` 的客户端行为

原理：

- 后续所有好友、会话、消息接口都会变成受保护接口
- 如果不先把统一鉴权和当前用户上下文立住，后面每个接口都会各写一遍认证逻辑

作用：

- 给后续所有业务接口提供稳定的“当前是谁”能力
- 为好友搜索、好友申请、会话列表和 `ws.auth` 打基础

当前进度（2026-03-14）：

- 已实现 `POST /api/v1/auth/register`
- 注册参数校验、账号唯一性冲突处理、bcrypt 密码哈希、`users` 表真实写入已完成
- 已实现 `POST /api/v1/auth/login`
- 已返回 `access_token`、`refresh_token` 和 `device_session_id`
- 已完成 `device_sessions` 写入
- 已实现 `POST /api/v1/auth/logout`
- 已实现 `POST /api/v1/users/avatar/temp`
- 已实现 `GET /api/v1/users/avatar/temp?avatar_upload_key=...`
- 已实现 `PATCH /api/v1/users/me/profile`
- 已实现 `GET /api/v1/users/{user_id}/avatar`
- 已将 `users.avatar_url` 的语义固定为头像 `storage key`
- 已形成“临时头像上传 -> `avatar_upload_key` 确认 -> 正式头像写库”的统一流程
- 已完成客户端注册 / 登录 / 切换账号 / 登出链路
- 已完成客户端本地 `access_token + device_session_id` 存储
- 统一 Bearer Token 受保护接口基础、登录态恢复策略深化、refresh token 续期仍未开始


### 第 3 步：完成用户搜索与好友申请

内容：

- 实现“按账号搜索用户是否存在”接口
- 建立好友申请表和好友关系表
- 实现发送申请、接受、拒绝、好友列表、新的朋友列表
- 明确“只有好友才能创建私聊”还是“允许陌生人先建会话”的产品规则
- 把客户端“添加好友”弹窗接成真实搜索和申请链路

原理：

- 这是认证之后最短、最稳的一条真实业务链路
- 客户端当前已经有“好友”模式和“添加好友”弹窗骨架，接好友域的收益最大

作用：

- 让客户端好友页不再只是演示列表
- 验证受保护 HTTP 接口、当前用户上下文和客户端业务分层是否真正可用
- 为私聊入口和一对一会话创建建立前置关系

当前进度（2026-03-19）：

- 已在迁移脚本中加入 `friend_requests` 和 `friendships` 两张基础表
- 服务端已完成用户搜索接口、好友列表接口、发送好友申请接口、接受/拒绝接口，以及“我收到的好友申请 / 我发出的好友申请”接口
- 客户端“添加好友”弹窗已接入真实用户搜索、发送好友申请、发件箱、收件箱和接受/拒绝交互
- 客户端好友主页已接入真实 `GET /api/v1/friends`，进入好友模式时会刷新正式好友列表
- 好友模式右侧详情区当前已能展示选中好友的真实头像，失败时退回到昵称文本占位
- 好友主页本地搜索过滤和好友资料页仍未开始
- 好友详情页“发起会话”按钮当前已接入真实 `POST /api/v1/conversations/private`，但暂不把结果写入本地会话列表


### 第 4 步：完成私聊会话模型与会话列表

内容：

- 建立 `conversations` 和 `conversation_members`
- 实现一对一私聊会话模型和查询能力
- 实现会话详情接口
- 实现会话列表接口
- 实现历史消息分页接口
- 把客户端中间栏会话列表接到真实数据源
- 第一版先不做群聊创建
- 私聊创建入口优先和好友关系联动

原理：

- 聊天的容器是会话
- 只有先把“聊什么”和“和谁聊”建模清楚，消息模块才有归属目标

作用：

- 私聊和群聊有统一承载模型
- 客户端可以真正开始渲染会话列表
- 为后续把私聊入口和好友关系联动做好准备

当前进度（2026-03-19）：

- 已在迁移脚本中加入 `conversations`、`conversation_members` 和 `messages` 三张聊天域基础表
- 当前会话层数据库已具备“会话容器 / 成员关系 / 历史消息”三层最小骨架
- 服务端已完成 `POST /api/v1/conversations/private`、`GET /api/v1/conversations`、`GET /api/v1/conversations/{id}`、`GET /api/v1/conversations/{id}/messages` 和 `POST /api/v1/conversations/{id}/messages/text`
- 当前服务端已经具备“好友关系 -> 私聊会话 -> 文本消息落库 -> 历史消息查询”的最小 HTTP 闭环
- 客户端当前已开始接入“好友详情页 -> 创建或复用私聊会话”的最小链路
- 客户端当前已经接入：
  - 首次进入聊天窗口时的会话列表快照拉取
  - 对应会话首屏历史消息拉取
  - `ws.send + route=message.send_text`
  - `ws.ack + route=message.send_text`
  - `ws.new + route=message.created`
- 客户端当前已开始通过 `ConversationManager` 统一消化 `ws.ack / ws.new`，并同步更新 `ConversationListModel` 和 `MessageModelRegistry`


### 第 5 步：完成 WebSocket 鉴权与文本消息链路

内容：

- 服务端建立 `transport/ws`、`protocol/dto/ws`、`service/ws_session_service`、`infra/ws/connection_registry`
- 服务端补齐 `protocol/dto/ws/ws_business_dto.h` 与 `service/realtime_push_service.*`
- 客户端建立 `src/ws/chat_ws_client.*` 和 `src/dto/ws_dto.*`
- 先完成 `ws.auth`，并确认协议层 `Ping/Pong` 正常保活
- 实现 WebSocket 连接和鉴权
- 建立 `ws.send / ws.ack / ws.new` 基础框架，并逐步接入具体业务 `route`
- 在现有 `messages` 表基础上接入实时消息链路
- 明确发送者其他在线设备的同步策略
- 客户端通过 `QWebSocket` 建立稳定长连接并完成断线重连

原理：

- 文本消息是 IM 最核心闭环
- 但它必须建立在“已认证 + 有会话 + 有历史接口”的基础上，否则会把实时链路和会话建模缠在一起

作用：

- 打通真正的实时聊天能力
- 为图片、文件和系统消息复用消息通道
- 为后续 `ConversationManager / ConversationListModel / MessageModelRegistry` 提供稳定的实时增量来源；如果缓存明显变重，再把状态抽成独立 `ConversationStore`


### 第 6 步：完成图片和文件消息

内容：

- 文件上传接口
- 附件表
- 图片和文件消息结构
- 客户端上传后发送附件引用消息
- 保持本地文件目录和后续 `MinIO` 兼容的存储抽象

原理：

- 文件传输比文本复杂
- 必须把“文件上传”与“消息广播”拆开

作用：

- 支持图片、文件聊天
- 形成完整消息类型扩展机制


### 第 7 步：完成 refresh、在线状态、已读和重连恢复

内容：

- 完成 refresh token 续期接口
- 客户端接入 access token 过期后的自动续期
- Redis 在线状态
- `last_read_seq`
- 未读计数
- 断线重连后增量同步
- 同一用户多设备已读同步

原理：

- 真正的 IM 不只是“能发消息”
- 当文本链路跑通之后，产品体验的关键就变成“会不会掉、掉了能不能恢复、不同设备能不能同步”

作用：

- 显著提升产品完整度
- 为多端同步打基础


### 第 8 步：完成一对一语音通话

内容：

- 建立 `CallSession`
- WebSocket 信令事件
- WebRTC 协商
- coturn 接入
- 客户端完成“信令控制面”和“媒体通道”的职责分离

原理：

- 音视频通话和文字消息属于不同系统层级
- 先做一对一，能把信令、房间、媒体这三层关系验证清楚

作用：

- 让项目具备实时通话能力
- 为视频通话直接复用信令结构


### 第 9 步：完成一对一视频通话

内容：

- 在现有通话信令上增加视频能力
- 客户端增加摄像头和渲染控制

原理：

- 一对一视频通话本质上是语音通话的增强形态
- 如果前一步音频信令设计合理，这一步主要新增媒体轨道和 UI 控制

作用：

- 快速扩展到视频通话
- 验证通话模型是否具备复用性


### 第 10 步：完成群语音和群视频

内容：

- 扩展 `CallSession` 到多参与者
- 评估并接入 SFU
- 扩展通话成员状态管理

原理：

- 群通话不能简单依赖多路 P2P，规模稍大就会爆炸
- 必须引入更专业的媒体转发架构

作用：

- 支撑多人会议和群视频
- 形成可继续扩容的音视频架构


## 14. 扩展性控制要点

为了保证后期不失控，开发过程中必须坚持以下规则：

### 14.1 不让 QWidget 直接承担业务逻辑

怎么做：

- 窗口类只负责界面事件、状态展示和控件生命周期
- 网络请求、登录态、重连、会话同步都放到 `service` 层
- 窗口类通过 ViewModel 或 service 暴露的接口拿数据，不直接操作 `QNetworkReply` 或 `QWebSocket`

作用：

- 避免窗口类越来越难维护


### 14.2 不让消息展示结构直接等于协议结构

怎么做：

- 协议 DTO 只保留 `message_id`、`seq`、`sender_id`、`content` 这类稳定字段
- UI ViewModel 再补 `is_self`、`send_state`、`display_time`、`selection_state` 等界面字段
- 数据库存储结构和消息气泡展示结构不要互相反向渗透

作用：

- 避免 UI 变动影响协议


### 14.3 不把音视频通话塞进普通消息模块

怎么做：

- 普通消息模块只处理文本、图片、文件和系统消息
- 通话协商走单独的 `call.*` 事件命名空间
- 真正的音视频媒体流只交给 WebRTC，不经普通聊天消息通道

作用：

- 保持通话系统独立演进能力


### 14.4 不按页码分页历史消息

怎么做：

- 所有历史消息查询都围绕 `seq` 设计
- 向前翻页使用 `before_seq`
- 重连补拉使用 `after_seq`
- 明确游标是排除型还是包含型，第一版建议统一为排除型

作用：

- 保证消息分页稳定


### 14.5 所有发送动作都必须可幂等

怎么做：

- 文本、图片、文件、通话发起都要有客户端生成的幂等键
- 重试时必须复用原来的幂等键
- 服务端围绕业务主键和幂等键建立唯一约束或幂等检查

作用：

- 防止网络抖动导致重复发送


### 14.6 所有协议都保留版本号

怎么做：

- HTTP 路径保留 `/api/v1/...`
- WebSocket 信封保留 `version`
- 新增字段优先追加，不随意改旧字段含义
- 客户端默认忽略未知但非关键字段

作用：

- 为未来多客户端兼容留空间


## 15. 结论

这套 IM 系统后续最重要的不是“先做哪个界面”，而是先建立一套稳定的架构骨架。

当前确定的技术栈如下：

- 客户端继续使用 Qt Widgets
- 客户端网络层使用 `QNetworkAccessManager + QWebSocket`
- 服务端使用 `Drogon`
- 数据库使用 `PostgreSQL`
- 缓存和在线状态使用 `Redis`
- 文件存储使用本地存储或 `MinIO`
- 音视频使用 `WebRTC + coturn`

整体设计核心是：

- 用 `Conversation` 统一私聊和群聊
- 用 `Message` 统一文本、图片、文件消息
- 用 `CallSession` 单独承载语音视频通话
- 用清晰的模块边界保证后期扩展性

如果后续严格按这份文档推进，项目可以先稳定落地文本聊天，再逐步演进到图片、文件、语音和视频，而不需要中途重构整套系统。

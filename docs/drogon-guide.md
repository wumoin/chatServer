# Drogon 基础与项目使用说明

最近同步时间：2026-03-12

## 1. 文档目的

这份文档不是 Drogon 全量手册，而是面向当前项目整理的一份“够用版”说明。

目标是回答四类问题：

- Drogon 在本项目里负责什么
- 当前和下一阶段会用到哪些核心类与函数
- 这些接口应该放在哪一层使用
- 使用时最容易踩的坑是什么

从这次整理开始，文中的核心函数统一按下面四个维度说明：

- 函数作用：这个函数到底负责什么
- 参数解析：每个参数在当前项目里应该怎么理解
- 返回值解析：调用后你能拿到什么，以及该怎么继续用
- 最小示例：给出最小可运行或最小可读的写法

本文档的内容基于当前 vendored 的 `Drogon v1.9.12` 源码和官方示例整理，主要参考：

- `chatServer/3rdparty/drogon/README.zh-CN.md`
- `chatServer/3rdparty/drogon/lib/inc/drogon/HttpAppFramework.h`
- `chatServer/3rdparty/drogon/lib/inc/drogon/HttpController.h`
- `chatServer/3rdparty/drogon/lib/inc/drogon/WebSocketController.h`
- `chatServer/3rdparty/drogon/examples/helloworld/`
- `chatServer/3rdparty/drogon/examples/websocket_server/`
- `chatServer/3rdparty/drogon/examples/redis/`

## 2. Drogon 在本项目中的角色

对于当前 IM 项目，Drogon 的职责应该限定为：

- 承载 HTTP API
- 承载 WebSocket 长连接和实时事件
- 提供配置加载入口
- 提供日志与应用生命周期管理入口
- 为后续 PostgreSQL / Redis / 文件上传下载提供框架层接入能力

它不负责：

- 业务领域规则本身
- 音视频媒体流转发
- 数据库存储设计
- 客户端协议 UI 展示逻辑

换句话说，Drogon 在本项目中属于“接入层和应用运行底座”，不是“业务逻辑层”。

## 3. 先理解几个基础概念

### 3.1 `drogon::app()`

`drogon::app()` 是整个框架的全局应用入口，可以理解为一个应用级单例。

它负责：

- 监听端口
- 加载配置
- 设置线程数
- 设置日志级别
- 注册 HTTP 路由
- 注册 WebSocket 控制器
- 管理数据库和 Redis 客户端
- 启动事件循环

当前我们在 `main.cpp` 里直接通过 `drogon::app()` 完成这些事情；后续会把这部分逻辑下沉到 `src/app/application.*`。

### 3.2 事件循环和异步回调

Drogon 是异步框架。HTTP 处理函数通常不会“直接 return 一个响应对象”，而是通过回调把响应交回框架。

这意味着：

- 请求处理函数通常接收 `HttpRequestPtr`
- 同时接收 `std::function<void(const HttpResponsePtr &)> &&callback`
- 真正构造好响应后调用 `callback(resp)`

这种写法的意义是：

- 便于框架在异步模型中统一调度
- 便于后续接入异步数据库、Redis、文件 IO
- 可以用较少线程处理较多并发请求

### 3.3 Controller 自动注册

Drogon 的 `HttpController<T>` 和 `WebSocketController<T>` 使用模板和静态注册机制，把路径和控制器类绑定起来。

这带来两个结果：

- `main.cpp` 不需要显式一个个注册所有业务控制器
- 业务路由可以写在控制器类内部，更利于模块化

因此：

- 临时或极少量的启动接口可以用 `registerHandler()`
- 正式业务接口应优先放进 `transport/http` 或 `transport/ws` 下的控制器类

### 3.4 JsonCpp

Drogon 的 JSON 能力依赖 JsonCpp。

当前项目里最常见的用法会是：

- 用 `Json::Value` 构造 JSON 响应
- 用 `req->getJsonObject()` 读取 JSON 请求体
- 用 `HttpResponse::newHttpJsonResponse()` 生成 JSON 响应

### 3.5 Trantor

Trantor 是 Drogon 底层网络库。

你现在最直接能看到的 Trantor 接口是：

- `trantor::Logger::kInfo`
- `trantor::Logger::kWarn`
- `trantor::Logger::kDebug`

在当前项目里，Trantor 主要作为 Drogon 的底层依赖存在，不应该直接承担业务逻辑。

## 4. 当前项目最相关的启动函数

### 4.1 `drogon::app().addListener(host, port)`

简化理解：

- 告诉 Drogon “我要在哪个地址和端口上对外提供服务”

常见签名：

```cpp
app().addListener(ip, port, useSSL, certFile, keyFile);
```

函数作用：

- 为服务端增加一个监听地址和端口
- 让框架知道后续要在哪个地址上接收 HTTP 或 HTTPS 请求
- 这个函数只是在“登记监听配置”，并不会立刻启动服务

参数解析：

- `ip`
  监听地址。开发联调时最常见的是 `127.0.0.1` 和 `0.0.0.0`。

- `port`
  监听端口，例如当前脚手架里的 `8848`。

- `useSSL`
  是否把这条监听当成 HTTPS 监听。当前项目脚手架阶段通常传 `false`。

- `certFile`
  证书文件路径。只有启用 HTTPS 时才真正需要。

- `keyFile`
  私钥文件路径。只有启用 HTTPS 时才真正需要。

- 其余参数
  Drogon 还有 `useOldTLS`、`sslConfCmds` 这类更细的 SSL 参数。当前项目在接 HTTPS 之前不需要优先使用。

返回值解析：

- 返回 `HttpAppFramework &`
- 这不是“监听结果对象”，而是应用对象本身
- 主要用途是支持链式调用，例如 `addListener(...).setThreadNum(...).run()`
- 真正开始监听要等 `run()` 被调用以后

当前项目里的用途：

- 让 `chatServer` 对外暴露 HTTP 服务

典型场景：

- 本地开发时监听 `127.0.0.1` 或 `0.0.0.0`
- 后续如果需要监听多个地址，可以多次调用

注意：

- 这个调用必须在 `run()` 前完成
- 可以多次调用，从而同时监听多个地址或端口
- `0.0.0.0` 表示监听所有网卡地址，更适合联调；`127.0.0.1` 只允许本机访问

最小示例：

```cpp
drogon::app()
    .addListener("0.0.0.0", 8848)
    .run();
```

常见误区：

- 以为 `addListener()` 调完服务就已经启动了。不是，真正开始监听要等 `run()`
- 把监听参数散落在多个地方写死。当前脚手架可以先写死，后续应统一迁移到 `app.json`

### 4.2 `drogon::app().setThreadNum(n)`

简化理解：

- 告诉 Drogon “我准备给你多少个工作线程处理请求”

常见签名：

```cpp
app().setThreadNum(threadNum);
```

函数作用：

- 设置 Drogon 的工作线程数
- 控制框架内部 IO 事件循环的线程数量
- 影响 HTTP 和 WebSocket 请求的并发处理能力

参数解析：

- `threadNum`
  工作线程数量。传 `4` 表示显式使用 4 个线程。
- 传 `0` 时，Drogon 会按 CPU 核心数选择线程数
- 当前项目更推荐传明确值，避免不同机器上行为不直观

返回值解析：

- 返回 `HttpAppFramework &`
- 主要为了继续链式调用，不表示“线程已经启动”
- 线程真正创建和进入工作状态发生在 `run()` 之后

当前项目里的用途：

- 控制 HTTP / WebSocket 处理线程数量

当前建议：

- 在脚手架阶段可以显式设置为硬件并发数或一个明确值
- 不要把线程数写得过于随意，后续改为配置文件驱动

最小示例：

```cpp
const auto workerThreads = std::max(1u, std::thread::hardware_concurrency());
drogon::app().setThreadNum(static_cast<size_t>(workerThreads));
```

常见误区：

- 以为线程越多越快。对 IO 型服务不一定成立，线程太多反而增加调度开销
- 在业务代码里临时修改线程数。线程数属于启动配置，不应在运行中随意调整

### 4.3 `drogon::app().setLogLevel(level)`

简化理解：

- 告诉 Drogon “哪些级别的日志要打印出来”

常见签名：

```cpp
app().setLogLevel(trantor::Logger::kInfo);
```

函数作用：

- 设置日志级别
- 控制框架会打印哪些等级以上的日志
- 影响排障信息量和日志噪声水平

参数解析：

- `level`
  类型是 `trantor::Logger::LogLevel`，常见值有：
  - `trantor::Logger::kTrace`
  - `trantor::Logger::kDebug`
  - `trantor::Logger::kInfo`
  - `trantor::Logger::kWarn`
  - `trantor::Logger::kError`

返回值解析：

- 返回 `HttpAppFramework &`
- 主要用于继续链式配置
- 不会返回“旧日志级别”或“设置结果”

当前项目里的用途：

- 在服务刚启动、接口刚接通的阶段保留足够日志，便于联调

最小示例：

```cpp
drogon::app().setLogLevel(trantor::Logger::kInfo);
```

常见误区：

- 把日志级别长期固定在过于详细的 `kTrace` / `kDebug`，后续会把日志淹没
- 业务错误只靠 `LOG_INFO` 打印。真正异常应该使用更合适的级别

当前项目补充约定：

- `chatServer` 当前仍然通过 `app.log.log_level` 和 `app.log.display_local_time` 控制日志级别与时间显示
- 但日志输出目标已经不再完全依赖 Drogon 默认行为
- 当前项目在 `src/infra/log/app_logger.cpp` 中通过 `trantor::Logger::setOutputFunction(...)` 统一接管日志输出
- 统一日志模块会把同一份日志同时写到控制台和 `chatServer/logs/chatserver.log`

### 4.4 `drogon::app().loadConfigFile(path)`

简化理解：

- 让 Drogon 从一个 JSON 配置文件里读取监听、日志、数据库、Redis 等配置

常见签名：

```cpp
app().loadConfigFile("config/app.json");
```

函数作用：

- 从 JSON 配置文件加载框架配置
- 统一读取监听地址、日志、数据库、Redis、插件等初始化配置
- 让运行参数从代码里迁移到配置文件里

参数解析：

- `path`
  配置文件路径。可以是相对路径，也可以是绝对路径。当前项目后续推荐使用 `chatServer/config/app.json`。

返回值解析：

- 返回 `HttpAppFramework &`
- 主要用于链式调用，例如 `loadConfigFile(...).run()`
- 这个函数可能抛异常；常见原因是文件不存在、JSON 格式非法、配置内容不符合要求

当前项目里的用途：

- 后续会用它加载 `chatServer/config/app.json`

适合放到什么时候用：

- 当端口、日志、数据库、Redis、静态资源路径不再适合写死在代码里时

当前状态：

- 还未在项目代码里接入，但应是下一阶段优先事项

最小示例：

```cpp
int main()
{
    drogon::app().loadConfigFile("config/app.json").run();
}
```

常见误区：

- 以为 `loadConfigFile()` 只是读端口。实际上它还可以承载日志、数据库、Redis 等配置
- 先手写一堆代码配置，再让配置文件覆盖，导致初始化顺序混乱。后续要统一入口

### 4.5 `drogon::app().run()`

简化理解：

- 真正启动服务器，让框架开始接收请求

常见签名：

```cpp
app().run();
```

函数作用：

- 启动框架并进入事件循环
- 让前面注册好的监听、路由、控制器、数据库客户端真正开始工作
- 把当前线程交给 Drogon 运行时接管

特点：

- 这是阻塞调用
- 一旦进入 `run()`，框架开始接管监听、连接处理、路由调度

参数解析：

- 这个函数没有参数
- 这意味着所有监听、日志、线程、配置等初始化都要在它之前完成

返回值解析：

- 返回 `void`
- 一旦调用成功，当前线程会阻塞在事件循环中
- 正常情况下它不会立刻返回；只有应用退出时才会结束

注意：

- 大多数初始化操作都应在它之前完成

什么时候才算“服务启动了”：

- 不是 `addListener()` 之后
- 不是 `registerHandler()` 之后
- 而是执行到 `run()` 并成功进入事件循环之后

最小示例：

```cpp
drogon::app()
    .addListener("127.0.0.1", 8848)
    .run();
```

常见误区：

- 在 `run()` 之后继续写初始化代码，结果这些代码永远不会执行到
- 忘了它是阻塞调用，导致主线程控制流判断错误

### 4.6 `drogon::app().quit()`

简化理解：

- 告诉 Drogon “准备停服务并退出事件循环”

函数作用：

- 退出应用事件循环，停止服务
- 通知框架开始停机流程
- 常用于测试、信号处理、优雅关闭场景

当前项目里的典型场景：

- 测试环境优雅退出
- 收到退出信号时主动停机

参数解析：

- 这个函数没有参数
- 你不需要告诉它“关哪个端口”或“关哪些连接”，这些由框架统一处理

返回值解析：

- 返回 `void`
- 调用成功后不会给你一个“停机结果对象”
- 它的效果是触发应用退出流程，真正退出时机取决于事件循环和连接收尾情况

最小示例：

```cpp
drogon::app().quit();
```

注意：

- 这是停机控制接口，不是普通业务代码里随便调用的函数
- 更适合测试、运维或信号处理流程

## 5. HTTP 相关核心接口

### 5.1 `registerHandler()`

简化理解：

- 直接注册“路径 -> 处理函数”的映射，适合小而简单的接口

简化签名：

```cpp
app().registerHandler(
    "/health",
    [](const HttpRequestPtr &req,
       std::function<void(const HttpResponsePtr &)> &&callback) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("ok");
        callback(resp);
    },
    {drogon::Get});
```

函数作用：

- 直接注册一个 HTTP 路由和对应处理函数
- 更适合少量临时接口，不需要单独建控制器类
- 典型用途是健康检查、探活、极简调试接口

适用场景：

- 启动期的临时接口
- 健康检查
- 极少量、不值得单独建控制器的接口

不适合的场景：

- 大量正式业务接口
- 复杂 REST API

原因：

- 如果把大量路由都堆到 `main.cpp`，代码会迅速失控

参数解析：

- 第 1 个参数：路径模式，例如 `"/health"`
- 第 2 个参数：处理函数，可以是 lambda、普通函数、可调用对象
- 第 3 个参数：约束列表，常见是 HTTP 方法，例如 `{drogon::Get}`
- 第 4 个参数：可选的 `handlerName`，主要用于标识这个处理器，当前项目通常可以先不传

处理函数参数解析：

- `const HttpRequestPtr &req`
  当前请求对象，用来取参数、JSON、Header、Cookie 等

- `std::function<void(const HttpResponsePtr &)> &&callback`
  响应回调，处理完成后必须通过它把响应交回框架

返回值解析：

- 返回 `HttpAppFramework &`
- 主要用于继续链式注册其它配置
- 它不会返回“路由句柄”或“处理结果”
- 真正是否收到请求，要等应用 `run()` 之后才会发生

什么时候应该用它：

- `/health`
- `/metrics`
- 非常少量的调试接口

什么时候不该继续用它：

- 注册登录接口
- 会话和消息业务接口
- 需要 service / repository 分层的正式业务 API

常见误区：

- 在 lambda 里直接堆大量业务逻辑
- 忘记调用 `callback(resp)`，导致请求一直悬着不返回
- 在一个接口里多次调用 `callback`，造成响应行为异常

### 5.2 `registerHandlerViaRegex()`

简化理解：

- 和 `registerHandler()` 类似，但路径匹配规则换成正则表达式

函数作用：

- 用正则表达式注册路由
- 让路径匹配规则比普通字符串路径更灵活
- 当普通路径模式不够表达需求时才考虑使用

适用场景：

- 路由模式比较灵活
- 明确需要正则匹配时

参数解析：

- 第 1 个参数：正则表达式字符串，例如 `R"(^/api/v1/users/(\\d+)$)"`
- 第 2 个参数：处理函数，写法和 `registerHandler()` 一样
- 第 3 个参数：约束列表，例如 HTTP 方法、中间件
- 第 4 个参数：可选的处理器名字

处理函数参数解析：

- `req` 和 `callback` 的意义与 `registerHandler()` 完全一致
- 如果正则里有捕获组，Drogon 会把匹配到的子串按顺序映射成处理函数后续参数

返回值解析：

- 返回 `HttpAppFramework &`
- 作用仍然是链式调用，不是匹配结果
- 真正的正则匹配发生在请求到来时，不发生在注册阶段

当前项目建议：

- 默认优先普通路径注册
- 非必要不要上正则路由，避免可读性下降

什么时候真的值得用：

- 你明确需要复杂路径匹配
- 普通路径模式无法表达需求

常见误区：

- 仅仅为了“看起来灵活”就上正则，结果后期没人敢改

### 5.3 `HttpController<T>`

简化理解：

- 把一组 HTTP 接口放进一个类里管理，是正式业务 API 的主力写法

作用：

- 用类的方式组织一组 HTTP 接口

在本项目中的推荐位置：

- `chatServer/src/transport/http/`

为什么适合：

- 路由定义和接口处理函数放在一起
- 比把所有 lambda 写进 `main.cpp` 更可维护
- 更适合和 `service` 层配合

最小结构示例：

```cpp
class AuthController : public drogon::HttpController<AuthController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::login, "/api/v1/auth/login", drogon::Post);
    METHOD_LIST_END

    void login(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};
```

一句话判断什么时候该建 controller：

- 当这个接口已经属于正式业务能力，而不是临时探活或演示接口时

### 5.4 `METHOD_LIST_BEGIN / METHOD_LIST_END`

简化理解：

- 这是 `HttpController` 的“路由声明区”

作用：

- 定义 `HttpController` 的路由列表

它们通常和以下宏一起使用：

- `METHOD_ADD`
- `ADD_METHOD_TO`
- `ADD_METHOD_VIA_REGEX`

最小示例：

```cpp
METHOD_LIST_BEGIN
ADD_METHOD_TO(AuthController::login, "/api/v1/auth/login", drogon::Post);
METHOD_LIST_END
```

常见误区：

- 把宏写在类外面
- 只声明了处理函数，没有在路由列表里注册

### 5.5 `METHOD_ADD`

简化理解：

- 根据控制器类名自动推导路径前缀

作用：

- 以“控制器类名作为路径前缀”的方式注册路由

特点：

- 路径会自动带上控制器类或命名空间前缀

在本项目中的建议：

- 除非你非常明确想使用 Drogon 的默认控制器路径规则，否则业务 API 不建议依赖它生成最终对外路径

原因：

- 我们更希望对外路径稳定，例如 `/api/v1/auth/login`
- 不希望路径强绑定类名

示例含义：

```cpp
METHOD_ADD(UserController::getInfo, "/{id}", drogon::Get);
```

它不会单纯注册成 `/{id}`，而会把类名路径前缀也拼进去。

所以：

- 对外 API 需要稳定路径时，不要过度依赖它
- 如果你接受类名参与路由生成，它会比较省事

### 5.6 `ADD_METHOD_TO`

简化理解：

- 把处理函数显式绑定到你写出来的路径上

作用：

- 直接把处理函数绑定到指定路径

在本项目中的建议：

- 这是后续正式 HTTP API 更推荐使用的方式

原因：

- 路径可控
- 更符合版本化 API 设计
- 不会因为类名变化影响对外路由

最小示例：

```cpp
ADD_METHOD_TO(AuthController::login, "/api/v1/auth/login", drogon::Post);
```

这一行的含义是：

- 当收到 `POST /api/v1/auth/login`
- Drogon 调用 `AuthController::login`

这是本项目后续最推荐的注册方式。

### 5.7 常见处理函数签名

HTTP 处理函数最常见的签名形式是：

- `const HttpRequestPtr &req`
- `std::function<void(const HttpResponsePtr &)> &&callback`
- 后面再跟路径参数或查询参数映射出来的函数参数

这意味着：

- 请求读取从 `req` 取
- 响应通过 `callback(resp)` 返回

最小示例：

```cpp
void AuthController::login(
    const drogon::HttpRequestPtr &req,
    std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setBody("ok");
    callback(resp);
}
```

如果有路径参数，签名会继续往后加参数，例如：

```cpp
void getUser(const HttpRequestPtr &req,
             std::function<void(const HttpResponsePtr &)> &&callback,
             int userId);
```

这里的 `userId` 就是从路由模式映射出来的参数。

### 5.8 `HttpRequestPtr` 常用读取接口

当前项目最常用的几个接口：

- `req->getParameter("key")`
  读取字符串参数，不存在时通常返回空字符串

- `req->getOptionalParameter<T>("key")`
  读取可选参数，适合区分“没传”和“传了空值”

- `req->getJsonObject()`
  读取 JSON 请求体，返回 `std::shared_ptr<Json::Value>`

在本项目中的典型用途：

- 登录接口读取用户名、密码
- 发送消息接口读取 JSON body
- WebSocket 握手阶段读取 query 参数

更详细地看：

#### `req->getParameter("key")`

一句话理解：

- 直接按字符串取参数值

函数作用：

- 从请求参数表里读取名为 `key` 的字符串值
- 适合读取查询参数、表单参数这类“本来就是字符串”的输入

参数解析：

- `key`
  参数名，例如 `token`、`page`、`conversationId`

返回值解析：

- 返回 `const std::string &`
- 这是对请求内部参数值的只读引用，不是新拷贝出来的字符串
- 如果参数不存在，通常得到空字符串
- 因为“不存在”和“确实传了空字符串”都会表现为空，所以它不适合做严格校验

适合：

- 你只想拿到一个字符串
- 不太在意“参数不存在”和“参数为空字符串”的区别

最小示例：

```cpp
std::string user = req->getParameter("user");
std::string passwd = req->getParameter("passwd");
```

注意：

- 如果参数不存在，通常就是空字符串
- 所以它不适合严格区分“没传”和“传了空值”的场景

#### `req->getOptionalParameter<T>("key")`

一句话理解：

- 按目标类型读取参数，失败时返回空的 `optional`

函数作用：

- 在读取参数的同时完成类型转换
- 帮你区分“参数不存在 / 参数格式不对 / 参数解析成功”

参数解析：

- 模板参数 `T`
  目标类型，例如 `int`、`int64_t`、`bool`

- `key`
  要读取的参数名

返回值解析：

- 返回 `std::optional<T>`
- 成功时得到 `optional` 中的值
- 参数不存在时返回空 `optional`
- 参数存在但无法转换成 `T` 时，也返回空 `optional`

适合：

- 你要区分参数缺失
- 你希望顺手完成类型转换

最小示例：

```cpp
auto page = req->getOptionalParameter<int>("page");
if (!page)
{
    // 参数不存在，或者不能转成 int
}
```

注意：

- 如果原始参数无法转换成目标类型，也会得到空 optional
- 所以它既能判断缺失，也能判断格式错误

#### `req->getJsonObject()`

一句话理解：

- 把请求体当成 JSON 去解析

函数作用：

- 从请求体中读取 JSON 数据
- 把业务最常见的 `POST/PUT/PATCH` JSON body 转成 `Json::Value`

参数解析：

- 这个函数没有参数
- 它直接基于当前请求对象内部的 body 和 `Content-Type` 进行解析

返回值解析：

- 返回 `const std::shared_ptr<Json::Value> &`
- 返回值非空时，表示框架已经成功解析出一个 JSON 对象
- 返回值为空时，通常说明 `Content-Type` 不是 `application/json`，或者 JSON 解析失败
- 为空时可以继续用 `req->getJsonError()` 查看解析失败原因

适合：

- `POST / PUT / PATCH` 这类 JSON API

最小示例：

```cpp
auto json = req->getJsonObject();
if (!json)
{
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k400BadRequest);
    callback(resp);
    return;
}

std::string account = (*json)["account"].asString();
```

注意：

- 请求 `Content-Type` 应该是 `application/json`
- 如果解析失败，返回的是空指针
- 遇到空指针时可以结合 `req->getJsonError()` 看原因

项目里最常见的选择原则：

- 查询参数、简单表单参数：先看 `getParameter()` / `getOptionalParameter()`
- JSON body：优先 `getJsonObject()`

### 5.9 `HttpResponse` 常用构造接口

当前项目最常用的几个接口：

- `HttpResponse::newHttpResponse()`
  创建普通响应对象

- `HttpResponse::newHttpJsonResponse(json)`
  创建 JSON 响应对象

- `resp->setBody(...)`
  设置响应体

- `resp->setStatusCode(...)`
  设置状态码

当前项目里的使用建议：

- JSON API 一律优先返回 JSON 响应
- 状态码和业务错误体要分开设计，不要只靠 body 文本判断错误

更详细地看：

#### `HttpResponse::newHttpResponse()`

一句话理解：

- 创建一个普通响应对象，之后你再手工设置 body、状态码、header

函数作用：

- 创建一个空白的 HTTP 响应对象
- 适合你想自己逐步设置状态码、响应体和 header 的场景

参数解析：

- 这个函数没有参数

返回值解析：

- 返回 `HttpResponsePtr`
- 这是一个智能指针，指向新创建的响应对象
- 你后续通常会继续调用 `setStatusCode()`、`setBody()`、`addHeader()` 之类的接口，然后把它交给 `callback(resp)`

最小示例：

```cpp
auto resp = drogon::HttpResponse::newHttpResponse();
resp->setStatusCode(drogon::k200OK);
resp->setBody("ok");
callback(resp);
```

适合：

- 返回简单文本
- 需要细粒度控制响应内容

#### `HttpResponse::newHttpJsonResponse(json)`

一句话理解：

- 直接创建一个 JSON 响应，并自动带上 `application/json`

函数作用：

- 用一份 `Json::Value` 直接构造 JSON 响应
- 这是当前项目后续正式业务 API 最推荐的响应构造方式

参数解析：

- `json`
  要返回给客户端的 JSON 数据对象，类型是 `Json::Value`

返回值解析：

- 返回 `HttpResponsePtr`
- 响应对象会自动带上 JSON 内容类型
- 你依然可以在返回前继续补充状态码、header 或 cookie

最小示例：

```cpp
Json::Value body;
body["code"] = 0;
body["message"] = "ok";
auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
callback(resp);
```

适合：

- 几乎所有正式业务 API

这是本项目后续最推荐的响应构造方式。

#### `resp->setBody(...)`

一句话理解：

- 手工设置响应体内容

函数作用：

- 把一段文本或字节内容写入响应体
- 常用于普通文本响应，或者你想自己完全控制 body 内容的场景

参数解析：

- `body`
  要写入响应体的内容，可以是字符串，也可以是字符数组或长度明确的缓冲区

返回值解析：

- 返回 `void`
- 这说明它是“原地修改响应对象”，不是返回一个新的响应对象
- 调完之后还需要继续把同一个 `resp` 传给 `callback`

通常搭配：

- `newHttpResponse()`

#### `resp->setStatusCode(...)`

一句话理解：

- 设置 HTTP 状态码

函数作用：

- 设置协议层的 HTTP 状态码
- 用来表达“请求是否合法、是否鉴权通过、资源是否存在、服务是否出错”等协议层结果

参数解析：

- `code`
  类型是 `drogon::HttpStatusCode`，常见值包括 `k200OK`、`k400BadRequest`、`k401Unauthorized`、`k500InternalServerError`

返回值解析：

- 返回 `void`
- 这是对现有响应对象的原地修改
- 它不会自动生成错误 body，所以业务错误内容仍要你自己填到 JSON 或文本响应里

最常见的状态码场景：

- `k200OK`
- `k201Created`
- `k400BadRequest`
- `k401Unauthorized`
- `k403Forbidden`
- `k404NotFound`
- `k500InternalServerError`

项目里建议：

- HTTP 状态码表达“协议层是否成功”
- JSON body 表达“业务层的详细结果”

## 6. WebSocket 相关核心接口

### 6.1 `WebSocketController<T>`

简化理解：

- 用一个类来接住 WebSocket 的建连、收消息、断连三个关键时刻

作用：

- 用类的方式组织 WebSocket 连接与消息处理逻辑

在本项目中的推荐位置：

- `chatServer/src/transport/ws/`

适合本项目的原因：

- IM 实时消息、在线状态、已读回执、通话信令都适合走 WebSocket
- 连接生命周期管理需要单独收口，不应写进普通 HTTP controller

最小结构示例：

```cpp
class ChatWsController : public drogon::WebSocketController<ChatWsController>
{
  public:
    void handleNewMessage(const WebSocketConnectionPtr &,
                          std::string &&,
                          const WebSocketMessageType &) override;
    void handleConnectionClosed(const WebSocketConnectionPtr &) override;
    void handleNewConnection(const HttpRequestPtr &,
                             const WebSocketConnectionPtr &) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/chat", drogon::Get);
    WS_PATH_LIST_END
};
```

一句话判断什么时候该建 WebSocketController：

- 当这条能力是“持续在线连接上的实时事件”，而不是普通请求响应接口时

### 6.2 `WS_PATH_LIST_BEGIN / WS_PATH_LIST_END`

简化理解：

- 这是 `WebSocketController` 的路径声明区

作用：

- 定义 WebSocket 路径列表

常和以下宏一起使用：

- `WS_PATH_ADD`
- `WS_ADD_PATH_VIA_REGEX`

最小示例：

```cpp
WS_PATH_LIST_BEGIN
WS_PATH_ADD("/ws/chat", drogon::Get);
WS_PATH_LIST_END
```

### 6.3 `handleNewConnection()`

简化理解：

- 新的 WebSocket 连接刚建立成功时，先走这里

函数作用：

- WebSocket 建连成功后触发
- 这是连接生命周期里的第一个业务切入点
- 适合在这里做鉴权、建立连接上下文、登记在线状态

当前项目里的典型用途：

- 连接鉴权
- 记录用户和连接的映射
- 初始化连接上下文

参数解析：

- `const HttpRequestPtr &req`
  这里拿到的是握手阶段对应的 HTTP 请求，可读取 query 参数、header、token

- `const WebSocketConnectionPtr &conn`
  新建立的 WebSocket 连接对象

返回值解析：

- 返回 `void`
- 这意味着你不能靠返回值“拒绝连接”或“返回一段响应体”
- 如果鉴权失败，通常需要通过 `conn->send(...)` 给出错误事件，或者直接关闭连接

最小示例：

```cpp
void ChatWsController::handleNewConnection(const HttpRequestPtr &req,
                                           const WebSocketConnectionPtr &conn)
{
    auto token = req->getParameter("token");
    conn->send("connected");
}
```

常见误区：

- 连接一建立就默认用户已登录，不做任何鉴权
- 不保存连接上下文，后面消息处理时无法知道是谁发的

### 6.4 `handleNewMessage()`

简化理解：

- 每收到一条 WebSocket 消息，就会进这里

函数作用：

- 收到新 WebSocket 消息后触发
- 是实时消息、回执、在线状态变更、通话信令的主要入口
- 一条连接上的每条入站消息都会经过这里

当前项目里的典型用途：

- 解析信令或实时事件
- 把事件分发到 `message_service`、`presence_service`、`rtc_signaling_service`

参数解析：

- `const WebSocketConnectionPtr &conn`
  当前是哪条连接发来的消息

- `std::string &&message`
  收到的消息内容

- `const WebSocketMessageType &type`
  消息类型，例如文本、二进制、ping

返回值解析：

- 返回 `void`
- 处理结果通常通过副作用体现，例如调用 `conn->send(...)`、写数据库、更新 Redis、分发到 service
- 不存在“return 一个 WebSocket 响应对象”这种模式

最小示例：

```cpp
void ChatWsController::handleNewMessage(const WebSocketConnectionPtr &conn,
                                        std::string &&message,
                                        const WebSocketMessageType &type)
{
    if (type == drogon::WebSocketMessageType::Text)
    {
        conn->send("ack");
    }
}
```

常见误区：

- 把所有消息都当文本 JSON，不区分 `type`
- 在这里直接堆完整业务逻辑，不经过 service 层

### 6.5 `handleConnectionClosed()`

简化理解：

- 连接断掉之后，做清理收尾

函数作用：

- WebSocket 断开后触发
- 负责连接生命周期的收尾动作
- 适合做在线状态清理、连接映射解绑、订阅关系释放

当前项目里的典型用途：

- 清理连接状态
- 更新在线状态
- 解绑连接和用户映射

参数解析：

- `const WebSocketConnectionPtr &conn`
  已经关闭或正在关闭的连接对象。你通常会根据它身上的上下文做清理。

返回值解析：

- 返回 `void`
- 清理动作依靠函数内部逻辑完成，不通过返回值表达
- 如果这里漏掉清理，后续在线状态和连接映射很容易变脏

最小示例：

```cpp
void ChatWsController::handleConnectionClosed(const WebSocketConnectionPtr &conn)
{
    // 清理 conn 对应的用户在线状态
}
```

常见误区：

- 建连时保存了状态，断连时却不清理
- 把“是否在线”的唯一真相只放在内存里，不做 Redis 辅助

### 6.6 `WebSocketConnectionPtr`

这是单条 WebSocket 连接的抽象对象。

当前项目最常见的用法会是：

- `conn->send(...)`
  向客户端发送消息

- `conn->setContext(...)`
  挂接连接上下文

- `conn->getContextRef<T>()`
  读取连接上下文

适合保存到上下文里的信息：

- 当前用户 ID
- 当前设备会话 ID
- 当前订阅的会话或房间

更详细地看：

#### `conn->send(...)`

一句话理解：

- 主动往客户端推一条 WebSocket 消息

函数作用：

- 向当前连接对应的客户端发送一条 WebSocket 消息
- 这是服务端主动下行推送消息、回执、错误事件的基本接口

参数解析：

- `msg`
  要发送的消息内容。常见是 JSON 字符串。

- `len`
  只有使用 `send(const char *, uint64_t, type)` 这个重载时才需要，表示消息长度。

- `type`
  消息类型。默认是 `WebSocketMessageType::Text`；发送二进制数据时才改成 `Binary`。

返回值解析：

- 返回 `void`
- 这不是“对方已收到”的确认，只表示你把发送动作交给了框架
- 如果上层要做业务 ack，必须由应用协议自己设计确认消息

最小示例：

```cpp
conn->send(R"({"event":"message.ack"})");
```

#### `conn->setContext(...)`

一句话理解：

- 给这条连接挂一份你自己定义的上下文对象

函数作用：

- 在连接对象上保存一份“连接级状态”
- 让你后续在 `handleNewMessage()` 和 `handleConnectionClosed()` 里能拿回当前用户、设备、会话等信息

参数解析：

- `context`
  一个 `std::shared_ptr<void>`，实际上传进去的通常是 `std::make_shared<YourContext>(...)`

返回值解析：

- 返回 `void`
- 上下文是直接挂到当前连接对象上的，不会再单独返回一个句柄

最小示例：

```cpp
struct SessionContext
{
    std::string userId;
    std::string deviceSessionId;
};

conn->setContext(std::make_shared<SessionContext>(SessionContext{"u1", "d1"}));
```

#### `conn->getContextRef<T>()`

一句话理解：

- 把之前挂上去的上下文再取出来用

函数作用：

- 从连接对象里直接取回先前保存的上下文引用
- 适合在你已经非常确定上下文类型和存在性的场景下使用

参数解析：

- 模板参数 `T`
  你期望取出的上下文真实类型，例如 `SessionContext`

返回值解析：

- 返回 `T &`
- 这是上下文对象本体的引用，不是拷贝
- 如果上下文不存在，或者你传入的 `T` 不对，后果会很危险，所以调用前最好先确保上下文已正确设置

最小示例：

```cpp
auto &ctx = conn->getContextRef<SessionContext>();
LOG_INFO << ctx.userId;
```

注意：

- 只有你确定上下文类型正确时才能这么取
- 上下文适合存“连接级状态”，不适合存太大的业务对象

## 7. 数据库和 Redis 相关接口

这一节是“下一阶段会用到”的内容。

注意：

- 当前 `chatServer/CMakeLists.txt` 已开启 `PostgreSQL + Redis` 支持
- 当前工程通过 `loadConfigFile()` 读取 `chatServer/config/app.json` 中的 `db_clients` 和 `redis_clients`
- 当前 `src/app/application.cpp` 已实际使用 `areAllDbClientsAvailable()`、`getRedisClient()` 和 `execCommandAsync("PING")` 组成 `/health` 探活结果
- 因此这一节里 PostgreSQL 和 Redis 两部分都属于当前工程已经开始落地的能力

### 7.1 `addDbClient(config)`

简化理解：

- 向 Drogon 注册一个数据库客户端配置，让后续代码能按名字拿到它

函数作用：

- 向框架注册数据库客户端
- 把数据库连接配置交给 Drogon 管理
- 后续代码可以通过名字拿到这个客户端并复用连接池

说明：

- 当前 Drogon 中更推荐 `addDbClient(...)`
- 旧的 `createDbClient(...)` 已被标记为 deprecated

参数解析：

- `config`
  类型是 `drogon::orm::DbConfig`，本质上是一个配置对象。
- 如果接 PostgreSQL，常见字段包括：
  - `host`：数据库地址
  - `port`：数据库端口
  - `databaseName`：数据库名
  - `username` / `password`：认证信息
  - `connectionNumber`：连接池大小
  - `name`：客户端名字，后续通过这个名字 `getDbClient(name)`
  - `timeout`：SQL 执行超时

返回值解析：

- 返回 `HttpAppFramework &`
- 主要用于链式初始化
- 不是数据库连接对象本身；真正取连接对象要靠 `getDbClient()`

在本项目中的预期位置：

- 应用初始化层
- `src/app/application.cpp` 或配置初始化模块

当前项目里的实际落地方式：

- 先用 `loadConfigFile()` 加载 `chatServer/config/app.json`
- PostgreSQL 连接信息直接写在 `app.json` 的 `db_clients` 中
- `src/app/application.cpp` 只负责加载配置并注册健康检查
- 当前工程没有在代码里直接调用 `addDbClient(config)`，而是把这一步统一交给 `app.json`

不应该放在哪里：

- controller
- repository 的业务函数内部

原因：

- 数据库客户端属于应用级基础设施，只应初始化一次

### 7.2 `getDbClient(name)`

简化理解：

- 按名字取得已经注册好的数据库客户端

函数作用：

- 取得指定名字的数据库客户端
- 为 repository 或数据库基础设施封装提供数据库访问入口
- 这是“使用数据库”的入口，不是“创建数据库客户端”的入口

重要注意：

- 这个接口要求框架已经运行起来后再取

参数解析：

- `name`
  客户端名称，默认是 `"default"`。必须和初始化时注册的名字一致。

返回值解析：

- 返回 `orm::DbClientPtr`
- 这是数据库客户端的智能指针，后续可以在它上面执行 SQL、事务和异步查询
- 如果名字不对，或者初始化流程不完整，通常会在运行期暴露问题

在本项目中的用途：

- repository 层访问 PostgreSQL

什么时候该用它：

- 在 repository 实现里需要执行 SQL

什么时候不该直接用它：

- controller 直接拿来写 SQL
- 业务 service 到处散着直接查库

一句话边界：

- `getDbClient()` 是基础设施入口，不是业务逻辑入口

### 7.3 `createRedisClient(...)`

简化理解：

- 创建一个 Redis 客户端，供后续按名字拿出来复用

函数作用：

- 创建 Redis 客户端
- 把 Redis 连接配置注册到 Drogon
- 为后续在线状态、发布订阅、短期缓存提供基础设施入口

当前项目里的用途：

- 当前已落地用途是 `/health` 中的 Redis 连通性探测
- 后续会继续承载在线状态、跨实例广播辅助和短期缓存

参数解析：

- `ip`
  Redis 服务地址

- `port`
  Redis 端口

- `name`
  客户端名字，默认是 `"default"`

- `password`
  Redis 密码，没有则留空

- `connectionNum`
  连接数，控制连接池规模

- `isFast`
  是否创建 fast redis client。当前项目一般先用普通客户端即可。

- `timeout`
  命令超时秒数。小于等于 `0` 通常表示不设超时。

- `db`
  逻辑库编号，例如 `0`

- `username`
  Redis ACL 用户名，只有启用 ACL 时才需要

返回值解析：

- 返回 `HttpAppFramework &`
- 用途仍然是链式初始化
- 真正要执行 Redis 命令，后续还要用 `getRedisClient(name)` 取出客户端

在本项目中的预期位置：

- 应用初始化层

当前项目里的实际落地方式：

- 当前工程优先使用 `loadConfigFile()` 读取 `app.json` 里的 `redis_clients`
- 因此 `createRedisClient(...)` 目前属于“等价初始化方式说明”，不是现有代码里正在调用的接口

一句话理解它和 `getRedisClient()` 的关系：

- `createRedisClient()` 负责“创建并注册”
- `getRedisClient()` 负责“按名字拿来用”

### 7.4 `getRedisClient(name)`

简化理解：

- 取得已经注册好的 Redis 客户端

函数作用：

- 获取 Redis 客户端
- 让业务层或基础设施层按名字拿到已经注册好的 Redis 连接
- 这是后续执行缓存、在线状态、发布订阅逻辑的入口

重要注意：

- 和数据库客户端一样，这个接口通常要求在框架启动后再使用

参数解析：

- `name`
  Redis 客户端名，默认是 `"default"`。必须与创建时的名字一致。

返回值解析：

- 返回 `nosql::RedisClientPtr`
- 这是 Redis 客户端智能指针，后续通过它调用 `execCommandAsync()` 等接口
- 它不是 Redis 查询结果，真正结果要等命令执行回调

在本项目中的典型落点：

- `src/app/application.cpp` 的 `/health` 探活
- `presence_service`
- `message_service`
- `rtc_signaling_service`
- Redis 相关基础设施封装

不建议直接出现在：

- 普通 controller 的大量业务代码中

### 7.5 `execCommandAsync(...)`

简化理解：

- 异步发一条 Redis 命令，成功和失败分别走回调

函数作用：

- 异步执行 Redis 命令
- 把 Redis 操作放进异步模型里执行
- 适合高并发场景下的缓存读写、在线状态、短期幂等控制

当前项目中的可能用途：

- `SET/GET`
- 发布订阅
- presence 状态维护
- 幂等键或短期去重控制

最小示例：

```cpp
auto redisClient = drogon::app().getRedisClient("default");
redisClient->execCommandAsync(
    [](const drogon::nosql::RedisResult &) {
        // 例如当前项目里的 /health，会把 Redis 视为 available
    },
    [](const drogon::nosql::RedisException &ex) {
        // 例如当前项目里的 /health，会把 ex.what() 写进错误信息
    },
    "PING");
```

参数解析：

- 第 1 个回调：Redis 执行成功后的处理
- 第 2 个回调：执行异常后的处理
- 后面的格式串和参数：真正要发送的 Redis 命令

更具体地说：

- `resultCallback`
  成功回调，参数通常是 `const drogon::nosql::RedisResult &`

- `exceptionCallback`
  失败回调，参数通常是 `const std::exception &`

- `command`
  Redis 命令模板，例如 `"set %s %s"`、`"get %s"`

- `...`
  用于填充命令模板的变参

返回值解析：

- 返回 `void`
- 执行结果不会同步返回给调用方
- 成功结果只能在 `resultCallback` 里拿，异常只能在 `exceptionCallback` 里处理

常见误区：

- 在回调里写太重的同步逻辑
- 忘记失败回调，出错时只剩日志没有兜底处理

## 8. 本项目应该怎么用 Drogon

### 8.1 当前阶段

当前阶段可以接受：

- 在 `src/app/application.*` 中用 `loadConfigFile()` 加载 `chatServer/config/app.json`
- 在应用初始化层中用 `registerHandler()` 保留 `/health`
- 在 `/health` 里用 `areAllDbClientsAvailable()` 和 `getRedisClient()->execCommandAsync("PING")` 做最小探活
- 正式业务 HTTP 接口放到 `transport/http/*Controller`，例如当前的 `POST /api/v1/auth/register`

当前阶段不应该继续扩大：

- 不要把正式业务接口都继续写成 `registerHandler()` lambda

### 8.2 下一阶段 HTTP 约定

后续正式 HTTP API 建议这样落：

- `transport/http` 放 `HttpController`
- controller 只做参数解析、鉴权入口、调用 service、封装响应
- 业务逻辑放 `service`
- 数据访问放 `repository`
- 当前项目已经按这个约定落地了第一个正式接口：`AuthController -> AuthService -> UserRepository`

### 8.3 下一阶段 WebSocket 约定

后续实时事件建议这样落：

- `transport/ws` 放 `WebSocketController`
- WebSocket 层只做连接管理、事件分发、鉴权
- 消息收发、在线状态、信令处理都转给对应 service

### 8.4 配置约定

当前硬编码参数后续要迁移到：

- `chatServer/config/app.json`

应迁移的内容包括：

- 监听地址
- 监听端口
- 线程数
- 日志级别
- 上传目录

当前项目的实际约定：

- `app.json` 负责监听地址、端口、线程数、日志级别、PostgreSQL `db_clients` 和 Redis `redis_clients`
- 当前 Redis 已经完成基础连接接入，但业务层还没有正式开始使用缓存、在线状态和 Pub/Sub

## 9. 当前最容易踩的坑

### 9.1 把大量业务路由堆进 `main.cpp`

后果：

- 启动入口膨胀
- 难以测试
- 难以分层

### 9.2 在 controller 里直接写复杂业务逻辑

后果：

- controller 变成“上帝类”
- service 层失去意义
- 后续重用和测试都很痛苦

### 9.3 把 WebSocket 当成“大一统传输层”

后果：

- 文件上传、历史消息拉取、实时事件混在一起
- 协议边界混乱

本项目正确做法：

- HTTP 负责请求响应型业务
- WebSocket 负责实时事件和信令

### 9.4 过早直接依赖 Drogon 默认路由风格

后果：

- 类名或命名空间调整时，对外路由可能跟着漂移

本项目更稳的做法：

- 正式 API 路径显式声明

### 9.5 忽略“接口必须在框架运行后才能拿到”的约束

特别要注意：

- `getDbClient()`
- `getRedisClient()`

这类接口的使用时机不能想当然，应用初始化顺序要设计清楚。

## 10. 建议的学习顺序

如果后续要继续在这个项目里写 Drogon 代码，建议按这个顺序看：

1. 先看当前 `chatServer/src/main.cpp`
2. 再看 `HttpAppFramework.h` 里的应用生命周期接口
3. 再看 `HttpController.h`
4. 再看 `WebSocketController.h`
5. 再看 `examples/helloworld/`
6. 再看 `examples/websocket_server/`
7. 最后看 Redis 和数据库相关示例

## 11. 当前项目最可能直接会用到的接口清单

下一阶段最可能直接进入我们代码的接口有：

- `drogon::app()`
- `loadConfigFile(...)`
- `addListener(...)`
- `setThreadNum(...)`
- `setLogLevel(...)`
- `run()`
- `registerHandler(...)`
- `HttpController<T>`
- `ADD_METHOD_TO(...)`
- `HttpRequestPtr::getJsonObject()`
- `HttpRequestPtr::getParameter(...)`
- `HttpResponse::newHttpJsonResponse(...)`
- `WebSocketController<T>`
- `WebSocketConnectionPtr::send(...)`
- `getDbClient(...)`
- `getRedisClient(...)`

随着当前工程继续把 repository、presence 和实时链路落地，这份文档也要继续同步扩展。

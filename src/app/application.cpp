#include "app/application.h"

#include <drogon/drogon.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace chatserver::app {
namespace {

// 健康检查路径：
// 当前固定为 /health，后续如果引入正式 HttpController，
// 这个接口也仍然可以保留在应用初始化层。
constexpr auto kHealthPath = "/health";

// 默认基础设施客户端名：
// 当前 app.json 中的 PostgreSQL 和 Redis 都只配置一个 default 客户端，
// 健康检查返回中也统一暴露这个名字。
constexpr auto kDbClientName = "default";
constexpr auto kRedisClientName = "default";

// 默认配置文件路径由 CMake 编译定义提供。
// 这里封装成函数，避免在多个地方直接展开宏，后续若改为命令行参数或环境变量时更容易收口。
std::string defaultConfigPath()
{
    return CHATSERVER_DEFAULT_CONFIG_PATH;
}

}  // namespace

void Application::configure()
{
    // 启动前最小装配顺序：
    // 1. 先加载框架配置，让 Drogon 知道监听地址、线程数、db_clients 和 redis_clients；
    // 2. 再注册健康检查接口，保证服务启动后第一时间可探活。
    loadFrameworkConfig();
    //registerHealthHandler();
}

void Application::run()
{
    // 当前阶段把关键启动信息尽量收口到少量日志里：
    // 1. 当前读取的是哪份 app.json；
    // 2. PostgreSQL / Redis 客户端都由 app.json 中的配置创建；
    // 3. 健康检查路径仍然固定为 /health。
    LOG_INFO << "chatServer config loaded from " << configPath_;
    LOG_INFO << "chatServer PostgreSQL client[" << kDbClientName
             << "] configured via app.json";
    LOG_INFO << "chatServer Redis client[" << kRedisClientName
             << "] configured via app.json";
    LOG_INFO << "chatServer bootstrap ready on /health";

    // 进入 Drogon 主事件循环。
    // 从这一行开始，监听、连接管理、路由分发和数据库客户端生命周期都交给框架接管。
    drogon::app().run();
}

void Application::loadFrameworkConfig()
{
    // 先解析出编译期注入的默认配置路径。
    // 当前阶段统一从源码树读取 chatServer/config/app.json，避免启动工作目录变化导致找不到配置文件。
    configPath_ = defaultConfigPath();
    if (!std::filesystem::exists(configPath_))
    {
        throw std::runtime_error("Drogon config file was not found: " + configPath_);
    }

    // app.json 现在同时承载：
    // 1) Drogon 监听地址、线程数、日志级别；
    // 2) PostgreSQL db_clients 配置；
    // 3) Redis redis_clients 配置。
    // 这样服务端的运行参数都统一从同一份配置文件进入。
    drogon::app().loadConfigFile(configPath_);
}

void Application::registerHealthHandler()
{
    // 健康检查仍然先用 registerHandler() 留在应用初始化层：
    // 1) 当前只有一个极简接口，不值得单独建 HttpController；
    // 2) 它本质上属于“进程基础设施探活”，不是正式业务接口；
    // 3) 后续即使引入 transport/http，这个接口也可以继续保持在这里。
    //
    // 这里保持异步写法，是因为 Redis 连通性探测需要走 execCommandAsync("PING")。
    drogon::app().registerHandler(
        kHealthPath,
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            const bool databaseAvailable =
                drogon::app().areAllDbClientsAvailable();

            // 最终响应统一收口到一个小闭包里：
            // - PostgreSQL 可用性直接用同步结果；
            // - Redis 可用性由 PING 成功 / 失败决定；
            // - 如果 Redis 失败，额外把错误消息带回响应体，便于本地排查。
            auto respond = [callback = std::move(callback),
                            databaseAvailable](bool redisAvailable,
                                               const std::string &redisError = {})
                               mutable {
                Json::Value body(Json::objectValue);
                body["status"] =
                    (databaseAvailable && redisAvailable) ? "ok" : "degraded";
                body["service"] = "chatServer";
                body["framework"] = "drogon";

                Json::Value database(Json::objectValue);
                database["backend"] = "postgresql";
                database["client"] = kDbClientName;
                database["available"] = databaseAvailable;
                body["database"] = std::move(database);

                Json::Value redis(Json::objectValue);
                redis["backend"] = "redis";
                redis["client"] = kRedisClientName;
                redis["available"] = redisAvailable;
                if (!redisError.empty())
                {
                    redis["error"] = redisError;
                }
                body["redis"] = std::move(redis);

                auto response = drogon::HttpResponse::newHttpJsonResponse(body);
                callback(response);
            };

            try
            {
                auto redisClient = drogon::app().getRedisClient(kRedisClientName);

                // Redis PING 成功就说明：
                // 1) Redis client 已创建；
                // 2) 当前服务能和 Redis 建立正常命令交互。
                redisClient->execCommandAsync(
                    [respond](const drogon::nosql::RedisResult &) mutable {
                        respond(true);
                    },
                    [respond](const drogon::nosql::RedisException &ex) mutable {
                        respond(false, ex.what());
                    },
                    "PING");
            }
            catch (const std::exception &ex)
            {
                // 如果连 Redis client 都拿不到，直接把异常文本带回健康检查结果。
                respond(false, ex.what());
            }
        },
        {drogon::Get});
}

}  // namespace chatserver::app

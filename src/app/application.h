#pragma once

#include <cstdint>
#include <string>

namespace chatserver::app {

// Application 仅承担“服务端启动装配”职责：
// 1) 解析默认配置文件路径；
// 2) 初始化统一日志模块；
// 3) 加载 Drogon 运行配置（监听地址、线程数、日志级别、db_clients、redis_clients）；
// 4) 注册最小健康检查接口，暴露进程、PostgreSQL 和 Redis 可用性；
// 5) 启动 Drogon 事件循环，把连接和请求处理交给框架。
//
// 注意：这里不承载正式业务逻辑，不处理认证、会话、消息、文件等领域能力。
class Application {
  public:
    /**
     * @brief 完成服务端启动前的最小配置装配。
     *
     * 当前阶段会做四件事：
     * 1) 解析并校验 app.json 路径；
     * 2) 初始化统一日志模块；
     * 3) 加载 Drogon 配置；
     * 4) 注册 /health 健康检查接口。
     */
    void configure();

    /**
     * @brief 启动 Drogon 事件循环。
     *
     * 这一层只负责打印关键启动日志并进入框架主循环。
     */
    void run();

  private:
    /**
     * @brief 解析并校验默认配置文件路径。
     *
     * 当前默认配置文件路径由 CMake 编译定义注入，
     * 并在这里统一完成存在性校验。
     */
    void resolveConfigPath();

    /**
     * @brief 初始化服务端统一日志模块。
     *
     * 当前会从 `app.json` 读取日志级别、时间显示方式和输出目标，
     * 并统一接管 Drogon / Trantor 的日志输出。
     */
    void initializeLogging();

    /**
     * @brief 初始化认证安全基础设施。
     *
     * 当前主要负责读取 token 相关配置，
     * 让登录接口后续可以统一生成 access token / refresh token。
     */
    void initializeSecurity();

    /**
     * @brief 加载 Drogon 配置文件。
     *
     * 这里假设配置文件路径已经由 `resolveConfigPath()` 解析完成。
     */
    void loadFrameworkConfig();

    /**
     * @brief 注册最小健康检查接口。
     *
     * 当前仅保留 GET /health，用于进程探活、数据库连通性确认和 Redis 连通性确认。
     */
    void registerHealthHandler();

    // 当前实际加载的配置文件绝对路径。
    std::string configPath_;
};

}  // namespace chatserver::app

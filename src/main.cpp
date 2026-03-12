#include <drogon/drogon.h>

#include <algorithm>
#include <thread>

namespace {
// 当前脚手架阶段先把监听地址、端口和健康检查路径收口为常量，
// 便于后续切换到配置文件时统一替换，而不是在 main() 中散落魔法值。
constexpr auto kListenHost = "0.0.0.0";
constexpr uint16_t kListenPort = 8848;
constexpr auto kHealthPath = "/health";
} // namespace

int main()
{
    // 注册最小健康检查接口：
    // 1. 证明 Drogon HTTP 服务已经正常启动；
    // 2. 为后续进程探活、容器健康检查保留统一入口；
    // 3. 当前只返回最基本的服务状态，不承载业务逻辑。
    drogon::app().registerHandler(
        kHealthPath,
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            // 健康检查返回 JSON，而不是纯文本，
            // 这样后续若要补充版本号、环境、构建信息时可以平滑扩展字段。
            Json::Value body(Json::objectValue);
            body["status"] = "ok";
            body["service"] = "chatServer";
            body["framework"] = "drogon";

            auto response = drogon::HttpResponse::newHttpJsonResponse(body);
            callback(response);
        },
        {drogon::Get});

    // 线程数选择策略：
    // 优先使用硬件并发值；如果标准库返回 0，则回退到 1，
    // 避免把 0 直接传给 Drogon 导致线程池配置不明确。
    const auto workerThreads = std::max(1u, std::thread::hardware_concurrency());

    // 当前阶段把运行参数直接写在启动入口里：
    // - 监听所有网卡地址，便于本机和局域网联调；
    // - 端口固定为 8848；
    // - 日志级别保持 INFO，确保启动阶段的问题可见。
    // 等 config/app.json 落地后，这些参数会迁移到配置层统一管理。
    drogon::app().addListener(kListenHost, kListenPort);
    drogon::app().setThreadNum(static_cast<size_t>(workerThreads));
    drogon::app().setLogLevel(trantor::Logger::kInfo);

    // 启动日志尽量带上最关键的访问入口，方便本地联调时直接确认服务地址。
    LOG_INFO << "chatServer bootstrap ready on http://127.0.0.1:"
             << kListenPort
             << kHealthPath;

    // 进入 Drogon 事件循环。
    // 从这一行开始，HTTP 请求处理、连接管理和线程池调度都交给框架接管。
    drogon::app().run();
    return 0;
}

#include "app/application.h"
#include "infra/log/app_logger.h"

#include <exception>
#include <iostream>

int main()
{
    // 进程入口只负责“创建应用并启动”：
    // 1) 不在这里堆配置解析；
    // 2) 不在这里注册业务接口；
    // 3) 不在这里直接写数据库初始化。
    //
    // 这样后续 main() 可以长期保持很薄，
    // 真正的启动装配逻辑统一下沉到 Application。
    try
    {
        // main() 现在只保留最薄的一层职责：
        // 1. 创建应用装配对象；
        // 2. 执行配置与初始化；
        // 3. 把异常统一兜底到进程出口。
        chatserver::app::Application application;
        application.configure();
        application.run();
        return 0;
    }
    catch (const std::exception &ex)
    {
        if (chatserver::infra::log::AppLogger::isInitialized())
        {
            CHATSERVER_LOG_FATAL("bootstrap")
                << "chatServer failed to start: " << ex.what();
        }
        else
        {
            std::cerr << "chatServer failed to start: " << ex.what() << '\n';
        }
        return 1;
    }
}

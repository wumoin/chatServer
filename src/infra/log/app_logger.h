#pragma once

#include <string>

#include <trantor/utils/Logger.h>

namespace chatserver::infra::log {

/**
 * @brief 服务端统一日志初始化模块。
 *
 * 当前职责：
 * 1) 从 `app.json` 读取日志配置；
 * 2) 统一设置 Trantor/Drogon 的日志级别和本地时间显示策略；
 * 3) 统一接管日志输出目标，把日志写到控制台和可选日志文件。
 */
class AppLogger
{
  public:
    /**
     * @brief 使用指定配置文件初始化日志模块。
     * @param configPath `app.json` 的绝对路径。
     *
     * 如果配置不合法、日志目录无法创建或日志文件无法打开，会抛出异常。
     * 重复调用时会直接复用已经初始化好的日志模块。
     */
    static void initialize(const std::string &configPath);

    /**
     * @brief 判断日志模块是否已经初始化完成。
     * @return true 表示日志模块已可用；false 表示还未初始化。
     */
    static bool isInitialized();

    /**
     * @brief 返回当前日志文件路径。
     * @return 若启用了文件日志，则返回日志文件绝对路径；否则返回空字符串。
     */
    static const std::string &logFilePath();

  private:
    AppLogger() = delete;
};

}  // namespace chatserver::infra::log

#define CHATSERVER_LOG_TRACE(component) LOG_TRACE << "[" << component << "] "
#define CHATSERVER_LOG_DEBUG(component) LOG_DEBUG << "[" << component << "] "
#define CHATSERVER_LOG_INFO(component) LOG_INFO << "[" << component << "] "
#define CHATSERVER_LOG_WARN(component) LOG_WARN << "[" << component << "] "
#define CHATSERVER_LOG_ERROR(component) LOG_ERROR << "[" << component << "] "
#define CHATSERVER_LOG_FATAL(component) LOG_FATAL << "[" << component << "] "

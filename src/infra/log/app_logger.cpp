#include "infra/log/app_logger.h"

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace chatserver::infra::log {
namespace {

/**
 * @brief 日志运行时配置。
 */
struct LogSettings
{
    std::string appName = "chatServer";
    bool enableConsole = true;
    bool enableFile = true;
    std::string directory = "../logs";
    std::string fileName = "chatserver.log";
    trantor::Logger::LogLevel logLevel = trantor::Logger::kInfo;
    bool displayLocalTime = true;
};

/**
 * @brief 日志模块运行时状态。
 */
struct LogState
{
    bool initialized = false;
    bool enableConsole = true;
    bool enableFile = false;
    std::string appName = "chatServer";
    std::string logFilePath;
    std::ofstream fileStream;
    std::mutex writeMutex;
};

LogState g_logState;

/**
 * @brief 判断 JSON 字段是否存在且不是 null。
 * @param object 当前 JSON 对象。
 * @param key 字段名。
 * @return true 表示字段存在；false 表示字段不存在或为 null。
 */
bool hasObjectMember(const Json::Value &object, const char *key)
{
    return object.isObject() && object.isMember(key) && !object[key].isNull();
}

/**
 * @brief 读取可选字符串字段。
 * @param object 当前 JSON 对象。
 * @param key 字段名。
 * @param out 成功时写入的结果。
 * @param fieldName 便于拼错误消息的字段路径。
 */
void readOptionalString(const Json::Value &object,
                        const char *key,
                        std::string &out,
                        const char *fieldName)
{
    if (!hasObjectMember(object, key))
    {
        return;
    }

    if (!object[key].isString())
    {
        throw std::runtime_error(std::string(fieldName) + " must be a string");
    }

    out = object[key].asString();
}

/**
 * @brief 读取可选布尔字段。
 * @param object 当前 JSON 对象。
 * @param key 字段名。
 * @param out 成功时写入的结果。
 * @param fieldName 便于拼错误消息的字段路径。
 */
void readOptionalBool(const Json::Value &object,
                      const char *key,
                      bool &out,
                      const char *fieldName)
{
    if (!hasObjectMember(object, key))
    {
        return;
    }

    if (!object[key].isBool())
    {
        throw std::runtime_error(std::string(fieldName) + " must be a boolean");
    }

    out = object[key].asBool();
}

/**
 * @brief 把日志级别字符串映射成 Trantor 日志级别枚举。
 * @param text 配置文件中的日志级别文本。
 * @return 对应的日志级别枚举。
 */
trantor::Logger::LogLevel parseLogLevel(const std::string &text)
{
    std::string upperText = text;
    std::transform(upperText.begin(),
                   upperText.end(),
                   upperText.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

    if (upperText == "TRACE")
    {
        return trantor::Logger::kTrace;
    }
    if (upperText == "DEBUG")
    {
        return trantor::Logger::kDebug;
    }
    if (upperText == "INFO")
    {
        return trantor::Logger::kInfo;
    }
    if (upperText == "WARN" || upperText == "WARNING")
    {
        return trantor::Logger::kWarn;
    }
    if (upperText == "ERROR")
    {
        return trantor::Logger::kError;
    }
    if (upperText == "FATAL")
    {
        return trantor::Logger::kFatal;
    }

    throw std::runtime_error("app.log.log_level contains unsupported value: " +
                             text);
}

/**
 * @brief 从 `app.json` 读取日志配置。
 * @param configPath 配置文件路径。
 * @return 解析后的日志配置。
 */
LogSettings loadSettings(const std::string &configPath)
{
    std::ifstream input(configPath);
    if (!input.is_open())
    {
        throw std::runtime_error("failed to open log config file: " + configPath);
    }

    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["allowComments"] = true;

    Json::Value root;
    std::string parseErrors;
    if (!Json::parseFromStream(builder, input, &root, &parseErrors))
    {
        throw std::runtime_error("failed to parse log config: " + parseErrors);
    }

    LogSettings settings;

    const Json::Value &appObject = root["app"];
    const Json::Value &appLogObject = appObject["log"];
    if (appLogObject.isObject())
    {
        if (hasObjectMember(appLogObject, "log_level"))
        {
            if (!appLogObject["log_level"].isString())
            {
                throw std::runtime_error("app.log.log_level must be a string");
            }
            settings.logLevel =
                parseLogLevel(appLogObject["log_level"].asString());
        }

        readOptionalBool(appLogObject,
                         "display_local_time",
                         settings.displayLocalTime,
                         "app.log.display_local_time");
    }

    const Json::Value &chatServerObject = root["chatserver"];
    const Json::Value &chatServerLogObject = chatServerObject["log"];
    if (chatServerLogObject.isObject())
    {
        readOptionalString(chatServerLogObject,
                           "app_name",
                           settings.appName,
                           "chatserver.log.app_name");
        readOptionalBool(chatServerLogObject,
                         "enable_console",
                         settings.enableConsole,
                         "chatserver.log.enable_console");
        readOptionalBool(chatServerLogObject,
                         "enable_file",
                         settings.enableFile,
                         "chatserver.log.enable_file");
        readOptionalString(chatServerLogObject,
                           "directory",
                           settings.directory,
                           "chatserver.log.directory");
        readOptionalString(chatServerLogObject,
                           "file_name",
                           settings.fileName,
                           "chatserver.log.file_name");
    }

    if (settings.appName.empty())
    {
        throw std::runtime_error("chatserver.log.app_name must not be empty");
    }

    if (settings.enableFile && settings.fileName.empty())
    {
        throw std::runtime_error("chatserver.log.file_name must not be empty");
    }

    return settings;
}

/**
 * @brief 根据配置路径解析最终日志文件路径。
 * @param configPath `app.json` 路径。
 * @param settings 当前日志配置。
 * @return 规范化后的日志文件绝对路径；若禁用文件日志则返回空路径。
 */
std::filesystem::path resolveLogFilePath(const std::string &configPath,
                                         const LogSettings &settings)
{
    if (!settings.enableFile)
    {
        return {};
    }

    const std::filesystem::path configDir =
        std::filesystem::path(configPath).parent_path();

    std::filesystem::path filePath(settings.fileName);
    if (filePath.is_absolute())
    {
        return filePath.lexically_normal();
    }

    std::filesystem::path directoryPath(settings.directory);
    if (directoryPath.empty())
    {
        directoryPath = configDir;
    }
    else if (directoryPath.is_relative())
    {
        directoryPath = configDir / directoryPath;
    }

    return (directoryPath / filePath).lexically_normal();
}

/**
 * @brief 把日志消息写到当前启用的输出目标。
 * @param message 已经由 Trantor 格式化完成的日志文本。
 * @param length 文本长度。
 */
void writeLogMessage(const char *message, const uint64_t length)
{
    std::lock_guard<std::mutex> lock(g_logState.writeMutex);
    const std::string prefix = "[" + g_logState.appName + "] ";

    if (g_logState.enableConsole)
    {
        std::cout.write(prefix.data(),
                        static_cast<std::streamsize>(prefix.size()));
        std::cout.write(message, static_cast<std::streamsize>(length));
    }

    if (g_logState.enableFile && g_logState.fileStream.is_open())
    {
        g_logState.fileStream.write(prefix.data(),
                                    static_cast<std::streamsize>(prefix.size()));
        g_logState.fileStream.write(message, static_cast<std::streamsize>(length));
    }
}

/**
 * @brief 刷新当前启用的日志输出目标。
 */
void flushLogOutputs()
{
    std::lock_guard<std::mutex> lock(g_logState.writeMutex);

    if (g_logState.enableConsole)
    {
        std::cout.flush();
    }

    if (g_logState.enableFile && g_logState.fileStream.is_open())
    {
        g_logState.fileStream.flush();
    }
}

}  // namespace

void AppLogger::initialize(const std::string &configPath)
{
    if (g_logState.initialized)
    {
        return;
    }

    const LogSettings settings = loadSettings(configPath);
    const std::filesystem::path logFilePath =
        resolveLogFilePath(configPath, settings);

    if (settings.enableFile)
    {
        const auto parentDirectory = logFilePath.parent_path();
        if (!parentDirectory.empty())
        {
            std::error_code errorCode;
            std::filesystem::create_directories(parentDirectory, errorCode);
            if (errorCode)
            {
                throw std::runtime_error("failed to create log directory: " +
                                         parentDirectory.string() + ", error: " +
                                         errorCode.message());
            }
        }

        g_logState.fileStream.open(logFilePath, std::ios::app | std::ios::out);
        if (!g_logState.fileStream.is_open())
        {
            throw std::runtime_error("failed to open log file: " +
                                     logFilePath.string());
        }
        g_logState.logFilePath = logFilePath.string();
    }

    g_logState.appName = settings.appName;
    g_logState.enableConsole = settings.enableConsole;
    g_logState.enableFile = settings.enableFile;

    trantor::Logger::setLogLevel(settings.logLevel);
    trantor::Logger::setDisplayLocalTime(settings.displayLocalTime);
    trantor::Logger::setOutputFunction(writeLogMessage, flushLogOutputs);

    g_logState.initialized = true;
}

bool AppLogger::isInitialized()
{
    return g_logState.initialized;
}

const std::string &AppLogger::logFilePath()
{
    return g_logState.logFilePath;
}

}  // namespace chatserver::infra::log

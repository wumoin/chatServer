#include "storage/local_storage.h"

#include <drogon/drogon.h>
#include <json/json.h>

#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

// LocalStorage 是 FileStorage 的本地磁盘实现。
// 它统一负责：
// - 从 app.json 读取存储目录
// - 生成 storage key
// - 安全落盘
// - 按 storage key 反查真实文件
//
// 上层 service 只关心“保存什么文件”，不需要自己拼接磁盘路径。
namespace chatserver::storage {
namespace {

bool hasObjectMember(const Json::Value &object, const char *key)
{
    return object.isObject() && object.isMember(key) && !object[key].isNull();
}

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

std::filesystem::path resolveAgainstConfigDirectory(
    const std::filesystem::path &configDir,
    const std::string &rawPath)
{
    std::filesystem::path configuredPath(rawPath);
    if (configuredPath.empty())
    {
        return configDir;
    }

    if (configuredPath.is_absolute())
    {
        return configuredPath.lexically_normal();
    }

    return (configDir / configuredPath).lexically_normal();
}

bool isPathInsideRoot(const std::filesystem::path &path,
                      const std::filesystem::path &root)
{
    const auto normalizedPath = path.lexically_normal();
    const auto normalizedRoot = root.lexically_normal();

    auto rootIt = normalizedRoot.begin();
    auto pathIt = normalizedPath.begin();
    for (; rootIt != normalizedRoot.end(); ++rootIt, ++pathIt)
    {
        if (pathIt == normalizedPath.end() || *rootIt != *pathIt)
        {
            return false;
        }
    }
    return true;
}

LocalStorageSettings loadSettings(const std::string &configPath)
{
    // 本地存储目录既可以写绝对路径，也可以写相对 app.json 的路径。
    // 解析完成后统一折算成规范化绝对路径，减少运行时歧义。
    std::ifstream input(configPath);
    if (!input.is_open())
    {
        throw std::runtime_error("failed to open storage config file: " +
                                 configPath);
    }

    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["allowComments"] = true;

    Json::Value root;
    std::string errors;
    if (!Json::parseFromStream(builder, input, &root, &errors))
    {
        throw std::runtime_error("failed to parse storage config: " + errors);
    }

    const Json::Value &chatServerObject = root["chatserver"];
    const Json::Value &storageObject = chatServerObject["storage"];

    std::string provider = "local";
    if (storageObject.isObject())
    {
        readOptionalString(storageObject,
                           "provider",
                           provider,
                           "chatserver.storage.provider");
    }

    if (provider != "local")
    {
        throw std::runtime_error(
            "chatserver.storage.provider currently only supports 'local'");
    }

    std::string rootDirText = "../uploads";
    std::string tmpDirText = "tmp";
    std::string attachmentsDirText = "files";
    std::string avatarsDirText = "avatars";

    const Json::Value &localObject = storageObject["local"];
    if (localObject.isObject())
    {
        readOptionalString(localObject,
                           "root_dir",
                           rootDirText,
                           "chatserver.storage.local.root_dir");
        readOptionalString(localObject,
                           "tmp_dir",
                           tmpDirText,
                           "chatserver.storage.local.tmp_dir");
        readOptionalString(localObject,
                           "attachments_dir",
                           attachmentsDirText,
                           "chatserver.storage.local.attachments_dir");
        readOptionalString(localObject,
                           "avatars_dir",
                           avatarsDirText,
                           "chatserver.storage.local.avatars_dir");
    }

    if (rootDirText.empty())
    {
        throw std::runtime_error(
            "chatserver.storage.local.root_dir must not be empty");
    }

    const auto configDir = std::filesystem::path(configPath).parent_path();
    LocalStorageSettings settings;
    settings.rootDir = resolveAgainstConfigDirectory(configDir, rootDirText);
    settings.temporaryDir =
        resolveAgainstConfigDirectory(settings.rootDir, tmpDirText);
    settings.attachmentsDir =
        resolveAgainstConfigDirectory(settings.rootDir, attachmentsDirText);
    settings.avatarsDir =
        resolveAgainstConfigDirectory(settings.rootDir, avatarsDirText);

    if (!isPathInsideRoot(settings.temporaryDir, settings.rootDir) ||
        !isPathInsideRoot(settings.attachmentsDir, settings.rootDir) ||
        !isPathInsideRoot(settings.avatarsDir, settings.rootDir))
    {
        throw std::runtime_error(
            "local storage directories must stay inside chatserver.storage.local.root_dir");
    }

    return settings;
}

std::string relativeBaseSegment(const std::filesystem::path &baseDir,
                                const std::filesystem::path &rootDir)
{
    const auto relative = baseDir.lexically_relative(rootDir);
    if (relative.empty() || relative == ".")
    {
        throw std::runtime_error(
            "local storage base directory must be under root_dir");
    }
    return relative.generic_string();
}

std::string normalizeExtension(const std::string &fileName)
{
    const auto extension = std::filesystem::path(fileName).extension().string();
    if (extension.empty() || extension.size() > 16)
    {
        return {};
    }

    std::string normalized;
    normalized.reserve(extension.size());
    for (const unsigned char ch : extension)
    {
        if (std::isalnum(ch) || ch == '.')
        {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }

    if (normalized.empty() || normalized == ".")
    {
        return {};
    }

    return normalized;
}

std::string generateStorageKey(FileCategory category,
                               const std::filesystem::path &rootDir,
                               const std::filesystem::path &baseDir,
                               const std::string &preferredKey,
                               const std::string &originalFileName)
{
    if (!preferredKey.empty())
    {
        // 显式指定 storage key 时也要做一次安全校验，防止把对象写出 root_dir。
        const auto normalized = std::filesystem::path(preferredKey).lexically_normal();
        if (normalized.is_absolute())
        {
            throw std::runtime_error("preferredStorageKey must be a relative path");
        }

        const auto normalizedText = normalized.generic_string();
        if (normalizedText.find("..") != std::string::npos)
        {
            throw std::runtime_error(
                "preferredStorageKey must not contain parent path segments");
        }

        return normalizedText;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream dateStream;
    dateStream << std::put_time(&localTime, "%Y/%m/%d");

    const std::string baseSegment = relativeBaseSegment(baseDir, rootDir);
    const std::string categoryPrefix =
        category == FileCategory::kAvatar
            ? "avatar"
            : (category == FileCategory::kTemporary ? "tmp" : "file");
    const std::string extension = normalizeExtension(originalFileName);
    const std::string objectId =
        categoryPrefix + "_" +
        drogon::utils::getUuid(false).substr(0, 24) + extension;

    return (std::filesystem::path(baseSegment) / dateStream.str() / objectId)
        .generic_string();
}

StoredFileInfo buildStoredFileInfo(const SaveFileRequest &request,
                                   const LocalStorageSettings &settings,
                                   const std::string &storageKey,
                                   const std::filesystem::path &absolutePath,
                                   const std::uint64_t size)
{
    StoredFileInfo info;
    info.category = request.category;
    info.storageKey = storageKey;
    info.relativePath =
        absolutePath.lexically_relative(settings.rootDir).generic_string();
    info.absolutePath = absolutePath.string();
    info.originalFileName = request.originalFileName;
    info.contentType = request.contentType;
    info.size = size;
    return info;
}

}  // namespace

std::shared_ptr<LocalStorage> LocalStorage::createFromConfig(
    const std::string &configPath)
{
    return std::make_shared<LocalStorage>(loadSettings(configPath));
}

LocalStorage::LocalStorage(LocalStorageSettings settings)
    : settings_(std::move(settings))
{
    ensureDirectories();
}

std::string LocalStorage::providerName() const
{
    return "local";
}

std::string LocalStorage::debugDescription() const
{
    std::ostringstream stream;
    stream << "root=" << settings_.rootDir.string()
           << ", tmp=" << settings_.temporaryDir.string()
           << ", attachments=" << settings_.attachmentsDir.string()
           << ", avatars=" << settings_.avatarsDir.string();
    return stream.str();
}

StoredFileInfo LocalStorage::save(const SaveFileRequest &request,
                                  const std::string &content)
{
    const auto &baseDir = baseDirectoryFor(request.category);
    const std::string storageKey = generateStorageKey(request.category,
                                                      settings_.rootDir,
                                                      baseDir,
                                                      request.preferredStorageKey,
                                                      request.originalFileName);
    const auto absolutePath = resolveAbsolutePath(storageKey);
    const auto parentDir = absolutePath.parent_path();

    std::error_code errorCode;
    std::filesystem::create_directories(parentDir, errorCode);
    if (errorCode)
    {
        throw std::runtime_error("failed to create storage directory: " +
                                 parentDir.string() + ", error: " +
                                 errorCode.message());
    }

    const auto tempPath = absolutePath.string() + ".part";
    {
        // 先写 .part 临时文件，再 rename 成正式文件，避免中途失败留下半写入对象。
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("failed to open local storage file: " +
                                     tempPath);
        }
        output.write(content.data(),
                     static_cast<std::streamsize>(content.size()));
        if (!output.good())
        {
            throw std::runtime_error("failed to write local storage file: " +
                                     tempPath);
        }
    }

    std::filesystem::rename(tempPath, absolutePath, errorCode);
    if (errorCode)
    {
        std::filesystem::remove(tempPath);
        throw std::runtime_error("failed to finalize local storage file: " +
                                 absolutePath.string() + ", error: " +
                                 errorCode.message());
    }

    return buildStoredFileInfo(request,
                               settings_,
                               storageKey,
                               absolutePath,
                               static_cast<std::uint64_t>(content.size()));
}

StoredFileInfo LocalStorage::saveFromFile(const SaveFileRequest &request,
                                          const std::filesystem::path &sourcePath,
                                          const bool moveSource)
{
    if (sourcePath.empty())
    {
        throw std::runtime_error("sourcePath must not be empty");
    }

    const auto normalizedSourcePath = sourcePath.lexically_normal();
    std::error_code errorCode;
    if (!std::filesystem::exists(normalizedSourcePath, errorCode) ||
        errorCode)
    {
        throw std::runtime_error("source file does not exist: " +
                                 normalizedSourcePath.string());
    }

    const auto &baseDir = baseDirectoryFor(request.category);
    const std::string storageKey = generateStorageKey(request.category,
                                                      settings_.rootDir,
                                                      baseDir,
                                                      request.preferredStorageKey,
                                                      request.originalFileName);
    const auto absolutePath = resolveAbsolutePath(storageKey);
    const auto parentDir = absolutePath.parent_path();

    std::filesystem::create_directories(parentDir, errorCode);
    if (errorCode)
    {
        throw std::runtime_error("failed to create storage directory: " +
                                 parentDir.string() + ", error: " +
                                 errorCode.message());
    }

    const auto tempPath = absolutePath.string() + ".part";
    std::filesystem::remove(tempPath, errorCode);
    errorCode.clear();

    if (moveSource)
    {
        // 优先尝试 rename，让“流式写好的 staging file -> 正式对象”尽量走移动，
        // 避免对超大文件再额外做一整次用户态复制。
        std::filesystem::rename(normalizedSourcePath, tempPath, errorCode);
        if (errorCode)
        {
            errorCode.clear();
            std::filesystem::copy_file(normalizedSourcePath,
                                       tempPath,
                                       std::filesystem::copy_options::overwrite_existing,
                                       errorCode);
            if (errorCode)
            {
                throw std::runtime_error("failed to import local storage file: " +
                                         normalizedSourcePath.string() +
                                         ", error: " + errorCode.message());
            }

            std::filesystem::remove(normalizedSourcePath, errorCode);
            if (errorCode)
            {
                std::filesystem::remove(tempPath);
                throw std::runtime_error("failed to remove staged source file: " +
                                         normalizedSourcePath.string() +
                                         ", error: " + errorCode.message());
            }
        }
    }
    else
    {
        // 正式附件确认阶段仍需保留临时上传对象，便于后续消息落库失败时重试，
        // 因此这里显式走复制语义，不移动源文件。
        std::filesystem::copy_file(normalizedSourcePath,
                                   tempPath,
                                   std::filesystem::copy_options::overwrite_existing,
                                   errorCode);
        if (errorCode)
        {
            throw std::runtime_error("failed to copy local storage file: " +
                                     normalizedSourcePath.string() +
                                     ", error: " + errorCode.message());
        }
    }

    std::filesystem::rename(tempPath, absolutePath, errorCode);
    if (errorCode)
    {
        std::filesystem::remove(tempPath);
        throw std::runtime_error("failed to finalize local storage file: " +
                                 absolutePath.string() + ", error: " +
                                 errorCode.message());
    }

    const auto size = std::filesystem::file_size(absolutePath, errorCode);
    if (errorCode)
    {
        throw std::runtime_error("failed to stat local storage file: " +
                                 absolutePath.string() + ", error: " +
                                 errorCode.message());
    }

    return buildStoredFileInfo(request, settings_, storageKey, absolutePath, size);
}

std::string LocalStorage::read(const std::string &storageKey) const
{
    const auto absolutePath = resolveAbsolutePath(storageKey);
    std::ifstream input(absolutePath, std::ios::binary);
    if (!input.is_open())
    {
        throw std::runtime_error("failed to open local storage file: " +
                                 absolutePath.string());
    }

    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::filesystem::path LocalStorage::resolveAbsolutePath(
    const std::string &storageKey) const
{
    if (storageKey.empty())
    {
        throw std::runtime_error("storageKey must not be empty");
    }

    const auto normalizedKey =
        std::filesystem::path(storageKey).lexically_normal();
    if (normalizedKey.is_absolute())
    {
        throw std::runtime_error("storageKey must be a relative path");
    }

    const auto normalizedText = normalizedKey.generic_string();
    if (normalizedText.find("..") != std::string::npos)
    {
        throw std::runtime_error(
            "storageKey must not contain parent path segments");
    }

    const auto resolvedPath =
        (settings_.rootDir / normalizedKey).lexically_normal();
    // 即使前面已经过滤过 ".."，这里仍再做一次最终“必须留在 root_dir 内”的守卫。
    if (!isPathInsideRoot(resolvedPath, settings_.rootDir))
    {
        throw std::runtime_error("resolved local storage path escaped root_dir");
    }
    return resolvedPath;
}

bool LocalStorage::exists(const std::string &storageKey) const
{
    return std::filesystem::exists(resolveAbsolutePath(storageKey));
}

void LocalStorage::remove(const std::string &storageKey)
{
    std::error_code errorCode;
    std::filesystem::remove(resolveAbsolutePath(storageKey), errorCode);
    if (errorCode)
    {
        throw std::runtime_error("failed to remove local storage file: " +
                                 storageKey + ", error: " +
                                 errorCode.message());
    }
}

const std::filesystem::path &LocalStorage::rootDir() const
{
    return settings_.rootDir;
}

const std::filesystem::path &LocalStorage::temporaryDir() const
{
    return settings_.temporaryDir;
}

const std::filesystem::path &LocalStorage::attachmentsDir() const
{
    return settings_.attachmentsDir;
}

const std::filesystem::path &LocalStorage::avatarsDir() const
{
    return settings_.avatarsDir;
}

const std::filesystem::path &LocalStorage::baseDirectoryFor(
    const FileCategory category) const
{
    switch (category)
    {
    case FileCategory::kAvatar:
        return settings_.avatarsDir;
    case FileCategory::kTemporary:
        return settings_.temporaryDir;
    case FileCategory::kAttachment:
    default:
        return settings_.attachmentsDir;
    }
}

void LocalStorage::ensureDirectories()
{
    for (const auto *path : {&settings_.rootDir,
                             &settings_.temporaryDir,
                             &settings_.attachmentsDir,
                             &settings_.avatarsDir})
    {
        std::error_code errorCode;
        std::filesystem::create_directories(*path, errorCode);
        if (errorCode)
        {
            throw std::runtime_error("failed to create local storage directory: " +
                                     path->string() + ", error: " +
                                     errorCode.message());
        }
    }
}

}  // namespace chatserver::storage

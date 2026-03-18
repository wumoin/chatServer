#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace chatserver::storage {

/**
 * @brief 文件逻辑分类。
 *
 * 当前先按业务用途区分三类：
 * 1) avatar：用户头像；
 * 2) attachment：聊天过程中的图片、文件等附件；
 * 3) temporary：上传中的临时对象。
 */
enum class FileCategory
{
    kAvatar,
    kAttachment,
    kTemporary,
};

/**
 * @brief 保存文件时的统一输入参数。
 */
struct SaveFileRequest
{
    FileCategory category{FileCategory::kAttachment};
    std::string originalFileName;
    std::string contentType;
    std::string preferredStorageKey;
};

/**
 * @brief 文件保存成功后的统一结果。
 */
struct StoredFileInfo
{
    FileCategory category{FileCategory::kAttachment};
    std::string storageKey;
    std::string relativePath;
    std::string absolutePath;
    std::string originalFileName;
    std::string contentType;
    std::uint64_t size{0};
};

/**
 * @brief 统一文件存储抽象。
 *
 * 这一层只定义“上层业务需要哪些能力”，不关心底层到底是本地磁盘还是对象存储。
 * 后续无论切到 MinIO 还是其它对象存储，上层 `service / controller / repository`
 * 都只依赖这里的接口。
 */
class FileStorage
{
  public:
    virtual ~FileStorage() = default;

    /**
     * @brief 返回当前存储实现名称。
     * @return 例如 `local`、`minio`。
     */
    virtual std::string providerName() const = 0;

    /**
     * @brief 返回适合启动日志输出的存储描述文本。
     * @return 当前存储实现的简短描述。
     */
    virtual std::string debugDescription() const = 0;

    /**
     * @brief 保存一段完整文件内容。
     * @param request 文件保存请求。
     * @param content 文件二进制内容。
     * @return 保存成功后的统一结果。
     */
    virtual StoredFileInfo save(const SaveFileRequest &request,
                                const std::string &content) = 0;

    /**
     * @brief 读取某个存储 key 对应的完整文件内容。
     * @param storageKey 存储 key。
     * @return 文件的完整二进制内容。
     */
    virtual std::string read(const std::string &storageKey) const = 0;

    /**
     * @brief 解析某个存储 key 对应的绝对路径。
     * @param storageKey 存储 key。
     * @return 当前实现下的绝对定位路径。
     */
    virtual std::filesystem::path resolveAbsolutePath(
        const std::string &storageKey) const = 0;

    /**
     * @brief 判断某个存储 key 是否存在。
     * @param storageKey 存储 key。
     * @return true 表示对象存在；false 表示不存在。
     */
    virtual bool exists(const std::string &storageKey) const = 0;

    /**
     * @brief 删除某个存储 key 对应的对象。
     * @param storageKey 存储 key。
     */
    virtual void remove(const std::string &storageKey) = 0;
};

/**
 * @brief 默认文件存储注册表。
 *
 * 当前阶段先用一个全局默认实例承接文件存储能力，
 * 让后续 controller / service 可以不必自己管理存储实现的生命周期。
 */
class StorageRegistry
{
  public:
    /**
     * @brief 注册默认文件存储实例。
     * @param storage 默认文件存储实现。
     */
    static void setDefaultStorage(std::shared_ptr<FileStorage> storage);

    /**
     * @brief 判断默认文件存储是否已经注册。
     * @return true 表示已注册；false 表示尚未注册。
     */
    static bool hasDefaultStorage();

    /**
     * @brief 读取默认文件存储实例。
     * @return 默认文件存储共享指针。
     */
    static std::shared_ptr<FileStorage> defaultStorage();

  private:
    StorageRegistry() = delete;
};

}  // namespace chatserver::storage

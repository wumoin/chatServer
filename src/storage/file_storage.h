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
    // 调用方可显式指定 storage key；为空时由底层存储自行生成。
    std::string preferredStorageKey;
};

/**
 * @brief 文件保存成功后的统一结果。
 */
struct StoredFileInfo
{
    FileCategory category{FileCategory::kAttachment};
    // storageKey 是上层长期保存到数据库里的稳定引用。
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
     * @brief 从一个已经存在于本地磁盘上的源文件导入对象。
     *
     * 这个接口主要服务于“大文件先流式写到 staging file，再统一导入存储层”
     * 的场景。相比 `save(..., content)`，它不要求调用方先把整份文件拼成一块
     * 内存缓冲，因此更适合大附件上传和后续正式附件确认。
     *
     * @param request 文件保存请求。
     * @param sourcePath 已存在的本地源文件路径。
     * @param moveSource true 表示导入成功后尽量移动并删除源文件；
     *        false 表示保留源文件，仅复制一份到目标存储路径。
     * @return 保存成功后的统一结果。
     */
    virtual StoredFileInfo saveFromFile(const SaveFileRequest &request,
                                        const std::filesystem::path &sourcePath,
                                        bool moveSource) = 0;

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
 * 它更像一个启动期装配好的 service locator，而不是业务态可频繁切换的配置中心。
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

#pragma once

#include "storage/file_storage.h"

#include <filesystem>
#include <memory>
#include <string>

namespace chatserver::storage {

/**
 * @brief 本地磁盘存储配置。
 */
struct LocalStorageSettings
{
    std::filesystem::path rootDir;
    std::filesystem::path temporaryDir;
    std::filesystem::path attachmentsDir;
    std::filesystem::path avatarsDir;
};

/**
 * @brief 本地磁盘存储实现。
 *
 * 当前职责：
 * 1) 从 `chatserver.storage.local` 读取目录配置；
 * 2) 自动创建上传根目录、临时目录、附件目录和头像目录；
 * 3) 负责把文件写到本地磁盘并返回统一的存储结果。
 */
class LocalStorage final : public FileStorage
{
  public:
    /**
     * @brief 从 `app.json` 创建本地存储实例。
     * @param configPath 配置文件绝对路径。
     * @return 已完成目录校验与创建的本地存储实例。
     */
    static std::shared_ptr<LocalStorage> createFromConfig(
        const std::string &configPath);

    /**
     * @brief 使用显式配置构造本地存储。
     * @param settings 已解析完成的本地目录配置。
     */
    explicit LocalStorage(LocalStorageSettings settings);

    /**
     * @brief 返回存储实现名称。
     * @return 固定返回 `local`。
     */
    std::string providerName() const override;

    /**
     * @brief 返回适合启动日志输出的存储描述文本。
     * @return 当前本地根目录及关键子目录说明。
     */
    std::string debugDescription() const override;

    /**
     * @brief 把完整文件内容写入本地磁盘。
     * @param request 文件保存请求。
     * @param content 文件二进制内容。
     * @return 保存成功后的统一结果。
     */
    StoredFileInfo save(const SaveFileRequest &request,
                        const std::string &content) override;

    /**
     * @brief 读取某个存储 key 对应的本地文件完整内容。
     * @param storageKey 存储 key。
     * @return 文件二进制内容。
     */
    std::string read(const std::string &storageKey) const override;

    /**
     * @brief 解析某个存储 key 对应的本地绝对路径。
     * @param storageKey 存储 key。
     * @return 规范化后的本地绝对路径。
     */
    std::filesystem::path resolveAbsolutePath(
        const std::string &storageKey) const override;

    /**
     * @brief 判断某个存储 key 对应的本地文件是否存在。
     * @param storageKey 存储 key。
     * @return true 表示存在；false 表示不存在。
     */
    bool exists(const std::string &storageKey) const override;

    /**
     * @brief 删除某个存储 key 对应的本地文件。
     * @param storageKey 存储 key。
     */
    void remove(const std::string &storageKey) override;

    /**
     * @brief 返回当前根目录。
     * @return 根目录绝对路径。
     */
    const std::filesystem::path &rootDir() const;

    /**
     * @brief 返回当前临时目录。
     * @return 临时目录绝对路径。
     */
    const std::filesystem::path &temporaryDir() const;

    /**
     * @brief 返回当前附件目录。
     * @return 附件目录绝对路径。
     */
    const std::filesystem::path &attachmentsDir() const;

    /**
     * @brief 返回当前头像目录。
     * @return 头像目录绝对路径。
     */
    const std::filesystem::path &avatarsDir() const;

  private:
    /**
     * @brief 按文件分类返回对应基目录。
     * @param category 文件逻辑分类。
     * @return 本地基目录绝对路径。
     */
    const std::filesystem::path &baseDirectoryFor(FileCategory category) const;

    /**
     * @brief 确保本地目录结构已经创建完成。
     */
    void ensureDirectories();

    LocalStorageSettings settings_;
};

}  // namespace chatserver::storage

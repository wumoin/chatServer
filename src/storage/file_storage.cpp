#include "storage/file_storage.h"

#include <mutex>
#include <stdexcept>

// StorageRegistry 为文件存储提供一个统一默认入口。
// 当前项目阶段只有一个默认存储实例，但通过这层注册表，业务代码不需要知道
// 底层到底是 LocalStorage 还是未来的对象存储实现。
namespace chatserver::storage {
namespace {

std::mutex g_storageMutex;
std::shared_ptr<FileStorage> g_defaultStorage;

}  // namespace

void StorageRegistry::setDefaultStorage(std::shared_ptr<FileStorage> storage)
{
    if (!storage)
    {
        throw std::runtime_error("default file storage must not be null");
    }

    std::lock_guard<std::mutex> lock(g_storageMutex);
    g_defaultStorage = std::move(storage);
}

bool StorageRegistry::hasDefaultStorage()
{
    std::lock_guard<std::mutex> lock(g_storageMutex);
    return static_cast<bool>(g_defaultStorage);
}

std::shared_ptr<FileStorage> StorageRegistry::defaultStorage()
{
    std::lock_guard<std::mutex> lock(g_storageMutex);
    if (!g_defaultStorage)
    {
        throw std::runtime_error("default file storage has not been initialized");
    }
    return g_defaultStorage;
}

}  // namespace chatserver::storage

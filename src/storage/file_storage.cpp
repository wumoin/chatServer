#include "storage/file_storage.h"

#include <mutex>
#include <stdexcept>

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

#pragma once

#include "storage/file_storage.h"

namespace chatserver::storage {

/**
 * @brief MinIO 存储实现占位类型。
 *
 * 当前阶段尚未真正接入 MinIO。
 * 这个头文件只负责把“未来会有对象存储实现”这件事明确落到目录结构上，
 * 后续正式接入时再补充完整实现。
 */
class MinioStorage;

}  // namespace chatserver::storage

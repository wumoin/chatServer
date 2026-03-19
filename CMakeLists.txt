cmake_minimum_required(VERSION 3.16)

project(chatServer LANGUAGES C CXX)

# 基础编译选项：
# - 当前服务端脚手架统一使用 C++17；
# - 导出 compile_commands.json，便于 clangd、clang-tidy 等工具工作。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 注册 / 登录链路需要 bcrypt 哈希能力。
# 当前优先复用系统 libcrypt，而不是额外再引一套密码学第三方库。
find_library(CRYPT_LIBRARY crypt)
if(NOT CRYPT_LIBRARY)
    message(FATAL_ERROR
        "System libcrypt was not found. "
        "The auth module requires bcrypt support via crypt(3).")
endif()

# 第三方目录约定：
# 所有 vendored 依赖都放在 chatServer/3rdparty 下，
# 并在当前构建目录中生成各自独立的 out-of-source build 产物。
set(CHATSERVER_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty")
set(DROGON_SOURCE_DIR "${CHATSERVER_THIRDPARTY_DIR}/drogon")
set(DROGON_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/drogon")
set(HIREDIS_SOURCE_DIR "${CHATSERVER_THIRDPARTY_DIR}/hiredis")
set(HIREDIS_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/hiredis")
set(JSONCPP_SOURCE_DIR "${CHATSERVER_THIRDPARTY_DIR}/jsoncpp")
set(JSONCPP_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/jsoncpp")

# 启动配置阶段前先校验 vendored 源码是否存在，
# 这样可以在目录不完整时尽早给出明确错误，而不是等到 add_subdirectory 才失败。
if(NOT EXISTS "${DROGON_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Vendored Drogon source was not found at ${DROGON_SOURCE_DIR}. "
        "Populate chatServer/3rdparty/drogon first.")
endif()

if(NOT EXISTS "${JSONCPP_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Vendored JsonCpp source was not found at ${JSONCPP_SOURCE_DIR}. "
        "Populate chatServer/3rdparty/jsoncpp first.")
endif()

if(NOT EXISTS "${HIREDIS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Vendored hiredis source was not found at ${HIREDIS_SOURCE_DIR}. "
        "Populate chatServer/3rdparty/hiredis first.")
endif()

# 当前阶段需要把 PostgreSQL / Redis 这两条底座链路真正打通，因此这里显式打开：
# - ORM / PostgreSQL：让 Drogon 可以创建数据库客户端并提供异步查询能力；
# - Redis：让 Drogon 可以创建 redis client，并支撑在线状态、缓存和健康检查探测。
# 同时继续关闭：
# - drogon_ctl / examples：避免无关目标拉长构建链路；
# - MySQL / SQLite：当前项目暂不接入；
# - Brotli / yaml 配置：当前脚手架尚未使用。
# 另外统一强制静态库构建，减少部署阶段对动态库查找路径的依赖。
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(BUILD_CTL OFF CACHE BOOL "Build drogon_ctl" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build Drogon examples" FORCE)
set(BUILD_ORM ON CACHE BOOL "Build Drogon ORM" FORCE)
set(BUILD_POSTGRESQL ON CACHE BOOL "Build Drogon PostgreSQL support" FORCE)
set(BUILD_MYSQL OFF CACHE BOOL "Build Drogon MySQL support" FORCE)
set(BUILD_SQLITE OFF CACHE BOOL "Build Drogon SQLite support" FORCE)
set(BUILD_REDIS ON CACHE BOOL "Build Drogon Redis support" FORCE)
set(BUILD_BROTLI OFF CACHE BOOL "Build Brotli support" FORCE)
set(BUILD_YAML_CONFIG OFF CACHE BOOL "Build yaml-cpp config support" FORCE)

# JsonCpp 当前只承担 Drogon 所需的 JSON 能力，因此同样裁剪为最小静态库构建。
# 这里关闭测试、示例、安装和包导出，避免无关目标污染脚手架工程。
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "Build JsonCpp tests" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "Run JsonCpp tests post build" FORCE)
set(JSONCPP_WITH_EXAMPLE OFF CACHE BOOL "Build JsonCpp examples" FORCE)
set(JSONCPP_WITH_INSTALL OFF CACHE BOOL "Install JsonCpp artifacts" FORCE)
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "Generate JsonCpp pkg-config" FORCE)
set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "Generate JsonCpp CMake package" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "Build JsonCpp static library" FORCE)
set(BUILD_OBJECT_LIBS OFF CACHE BOOL "Build JsonCpp object library" FORCE)

# 先构建 vendored JsonCpp，再让 Drogon 在同一棵 CMake 工程树里复用它。
add_subdirectory("${JSONCPP_SOURCE_DIR}" "${JSONCPP_BINARY_DIR}")

# hiredis 当前只作为 Drogon Redis 支持的底层 C 客户端，因此直接在本工程里编一个最小静态库：
# - 不启用 hiredis 自带测试 / 示例目标；
# - 不引入 SSL 变体；
# - 只保留 Drogon Redis 路径真正需要的核心源文件。
add_library(hiredis STATIC
    "${HIREDIS_SOURCE_DIR}/alloc.c"
    "${HIREDIS_SOURCE_DIR}/async.c"
    "${HIREDIS_SOURCE_DIR}/hiredis.c"
    "${HIREDIS_SOURCE_DIR}/net.c"
    "${HIREDIS_SOURCE_DIR}/read.c"
    "${HIREDIS_SOURCE_DIR}/sds.c"
    "${HIREDIS_SOURCE_DIR}/sockcompat.c"
)
set_target_properties(hiredis PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)
target_include_directories(hiredis
    PUBLIC "${CHATSERVER_THIRDPARTY_DIR}"
    PRIVATE "${HIREDIS_SOURCE_DIR}")

# Drogon 仍然通过 find_package(Jsoncpp) 的老接口查找 JsonCpp。
# 这里提前把 include 路径和目标名喂给它，避免再依赖系统安装包。
set(JSONCPP_INCLUDE_DIRS "${JSONCPP_SOURCE_DIR}/include")
set(JSONCPP_LIBRARIES jsoncpp_static)

# Drogon 对 hiredis 也仍然走 find_package(Hiredis)。
# hiredis 源码目录本身就是 3rdparty/hiredis，因此只要把 3rdparty 根目录作为 include path，
# Drogon 内部的 <hiredis/hiredis.h> 就能稳定命中 vendored 头文件。
set(HIREDIS_INCLUDE_DIRS "${CHATSERVER_THIRDPARTY_DIR}")
set(HIREDIS_LIBRARIES hiredis)

# 接入 Drogon 本体。它会继续在自己的子目录里构建 trantor 等依赖。
add_subdirectory("${DROGON_SOURCE_DIR}" "${DROGON_BINARY_DIR}")

# 当前服务端入口已经拆出最小 application 层：
# - main.cpp 只负责进程入口和异常兜底；
# - src/app/application.cpp 负责加载配置、注册健康检查，并探测 PostgreSQL / Redis 状态。
add_executable(chatServer
    src/infra/id/id_generator.cpp
    src/infra/log/app_logger.cpp
    src/infra/security/password_hasher.cpp
    src/infra/security/token_provider.cpp
    src/storage/file_storage.cpp
    src/storage/local_storage.cpp
    src/repository/conversation_repository.cpp
    src/repository/device_session_repository.cpp
    src/repository/friend_repository.cpp
    src/repository/user_repository.cpp
    src/service/avatar_service.cpp
    src/service/auth_service.cpp
    src/service/conversation_service.cpp
    src/service/friend_service.cpp
    src/service/user_service.cpp
    src/app/application.cpp
    src/transport/http/auth_controller.cpp
    src/transport/http/conversation_controller.cpp
    src/transport/http/friend_controller.cpp
    src/transport/http/user_controller.cpp
    src/main.cpp
)

# Drogon 主库已经聚合了当前脚手架需要的 HTTP 服务器能力，
# chatServer 只需直接链接 drogon 目标即可。
target_link_libraries(chatServer PRIVATE drogon "${CRYPT_LIBRARY}")

# 统一把 src/ 作为当前目标的私有头文件搜索路径，
# 这样 main.cpp 可以直接包含 "app/application.h" 等项目内头文件。
target_include_directories(chatServer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# 开发阶段统一从源码树读取 app.json。
# 这里把默认配置文件的绝对路径编译进程序，避免从不同工作目录启动时找不到配置文件。
target_compile_definitions(
    chatServer
    PRIVATE CHATSERVER_DEFAULT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/config/app.json")

# Windows 下提前关闭常见宏污染，避免未来引入更多头文件时出现 min/max 冲突。
if(WIN32)
    target_compile_definitions(chatServer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

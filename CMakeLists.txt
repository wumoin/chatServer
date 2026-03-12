cmake_minimum_required(VERSION 3.16)

project(chatServer LANGUAGES CXX)

# 基础编译选项：
# - 当前服务端脚手架统一使用 C++17；
# - 导出 compile_commands.json，便于 clangd、clang-tidy 等工具工作。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 第三方目录约定：
# 所有 vendored 依赖都放在 chatServer/3rdparty 下，
# 并在当前构建目录中生成各自独立的 out-of-source build 产物。
set(CHATSERVER_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty")
set(DROGON_SOURCE_DIR "${CHATSERVER_THIRDPARTY_DIR}/drogon")
set(DROGON_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/drogon")
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

# 当前阶段只需要“能启动的服务端骨架”，不需要把 Drogon 的附带工具和示例也编进来。
# 因此这里显式关闭：
# - drogon_ctl / examples：避免无关目标拉长构建链路；
# - ORM 和各类数据库后端：等 infra/db 真正落地后再打开；
# - Brotli / yaml 配置：当前脚手架尚未使用。
# 另外统一强制静态库构建，减少部署阶段对动态库查找路径的依赖。
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(BUILD_CTL OFF CACHE BOOL "Build drogon_ctl" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build Drogon examples" FORCE)
set(BUILD_ORM OFF CACHE BOOL "Build Drogon ORM" FORCE)
set(BUILD_POSTGRESQL OFF CACHE BOOL "Build Drogon PostgreSQL support" FORCE)
set(BUILD_MYSQL OFF CACHE BOOL "Build Drogon MySQL support" FORCE)
set(BUILD_SQLITE OFF CACHE BOOL "Build Drogon SQLite support" FORCE)
set(BUILD_REDIS OFF CACHE BOOL "Build Drogon Redis support" FORCE)
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

# Drogon 仍然通过 find_package(Jsoncpp) 的老接口查找 JsonCpp。
# 这里提前把 include 路径和目标名喂给它，避免再依赖系统安装包。
set(JSONCPP_INCLUDE_DIRS "${JSONCPP_SOURCE_DIR}/include")
set(JSONCPP_LIBRARIES jsoncpp_static)

# 接入 Drogon 本体。它会继续在自己的子目录里构建 trantor 等依赖。
add_subdirectory("${DROGON_SOURCE_DIR}" "${DROGON_BINARY_DIR}")

# 当前服务端可执行文件只包含最小入口 main.cpp。
# 后续当 app/、transport/、service/ 等目录落地后，再逐步扩展源文件列表。
add_executable(chatServer
    src/main.cpp
)

# Drogon 主库已经聚合了当前脚手架需要的 HTTP 服务器能力，
# chatServer 只需直接链接 drogon 目标即可。
target_link_libraries(chatServer PRIVATE drogon)

# Windows 下提前关闭常见宏污染，避免未来引入更多头文件时出现 min/max 冲突。
if(WIN32)
    target_compile_definitions(chatServer PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

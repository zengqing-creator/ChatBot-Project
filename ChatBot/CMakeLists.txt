cmake_minimum_required(VERSION 3.21)
project(ChatBot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 项目根目录
set(PROJECT_ROOT ${CMAKE_SOURCE_DIR})
set(INCLUDE_DIR ${PROJECT_ROOT}/include)

# 头文件搜索路径
include_directories(${INCLUDE_DIR})

# 编译定义
if(WIN32)
    add_compile_definitions(WIN32_WINNT=0x0A00 WINVER=0x0A00)
    add_compile_definitions(_WIN32_WINNT_WIN10=0x0A00)
    add_compile_definitions(SQLITE_THREADSAFE=1)
endif()

# 启用 httplib 的 SSL 支持
add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT=1)

# 查找 OpenSSL
find_package(OpenSSL REQUIRED)

# 源文件列表
set(SOURCES
    src/main.cpp
    src/SQLiteDB.cpp
    src/Conversation.cpp
    src/utils.cpp
    src/sqlite3.c
)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# 生成可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32
        crypt32
        user32
        advapi32
        bcrypt
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 构建后拷贝运行期依赖到 exe 同级
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_ROOT}/src/index.html"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "拷贝 index.html 到输出目录"
)
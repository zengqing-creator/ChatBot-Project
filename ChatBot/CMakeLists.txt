cmake_minimum_required(VERSION 3.21)
project(ChatBot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT_ROOT ${CMAKE_SOURCE_DIR})
set(INCLUDE_DIR ${PROJECT_ROOT}/include)

include_directories(${INCLUDE_DIR})

if(WIN32)
    add_compile_definitions(WIN32_WINNT=0x0A00 WINVER=0x0A00)
    add_compile_definitions(_WIN32_WINNT_WIN10=0x0A00)
    add_compile_definitions(SQLITE_THREADSAFE=1)
endif()

add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT=1)

# FAISS/MKL路径
set(FAISS_ROOT "C:/dev/faiss-install")
set(MKL_ROOT   "C:/Program Files (x86)/Intel/oneAPI/mkl/2025.3")

find_package(OpenSSL REQUIRED)

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

# 创建目标
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${FAISS_ROOT}/include
)

# 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${FAISS_ROOT}/lib/faiss.lib
    ${MKL_ROOT}/lib/mkl_intel_lp64.lib
    ${MKL_ROOT}/lib/mkl_sequential.lib
    ${MKL_ROOT}/lib/mkl_core.lib
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ws2_32 crypt32 user32 advapi32 bcrypt
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_ROOT}/src/index.html"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_ROOT}/faiss.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "拷贝 index.html 和 faiss.dll"
)
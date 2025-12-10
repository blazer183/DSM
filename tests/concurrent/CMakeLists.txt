cmake_minimum_required(VERSION 3.10)
project(dsm_concurrent_test)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)

# 1. 包含头文件路径
# 根据你的实际路径调整，确保能找到 "dsm.h", "os/page_table.h" 等
include_directories(
    ${CMAKE_SOURCE_DIR}/../../include 
    ${CMAKE_SOURCE_DIR}/.             # 当前目录
)

# 2. 你的核心源代码 (被测试的目标)
# 注意：这里只包含你负责的并发部分，不包含队友的 dsm_init 等
set(CORE_SOURCES
    ../../src/concurrent/concurrent_daemon.cpp
    ../../src/concurrent/concurrent_page.cpp
    ../../src/concurrent/concurrent_lock.cpp
    ../../src/concurrent/concurrent_misc.cpp
    ../../src/concurrent/concurrent_join.cpp
    ../../src/network/connection.cpp
    # 如果 rio_readn 在单独的文件里，记得加上，比如 ../../src/common/rio.cpp
)

# 3. 测试辅助代码 (模拟环境 + 客户端发包工具)
set(TEST_SOURCES
    main_test.cpp
    mock_env.cpp
    client_utils.cpp
)

# 4. 生成可执行文件
add_executable(dsm_test ${CORE_SOURCES} ${TEST_SOURCES})

# 5. 链接线程库 (pthread)
target_link_libraries(dsm_test pthread)
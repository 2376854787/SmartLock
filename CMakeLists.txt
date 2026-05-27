cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
#c 语言最低标准
set(CMAKE_C_STANDARD 11)
#如果编译器不支持 C11，则直接报错停止构建。
set(CMAKE_C_STANDARD_REQUIRED ON)
#允许使用编译器特定的扩展
set(CMAKE_C_EXTENSIONS ON)
#手动指定 C 编译器和对象转换工具
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)

set(CMAKE_SIZE arm-none-eabi-size)

# Define the build type
#设置默认编译类型为 Debug
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif ()

# Set the project name
# 设置变量名为 SmartLock 并正式定义 CMake 工程。
set(CMAKE_PROJECT_NAME SmartLock)
# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable compile command to ease indexing with e.g. clangd
#生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
# Enable CMake support for ASM and C languages
#包含 C 代码 和 汇编代码
enable_language(C ASM)

# 1. 自动搜索 BSP 目录下所有的 .c 文件
file(GLOB_RECURSE BSP_SRCS CONFIGURE_DEPENDS "Drivers/BSP/*.c")
list(REMOVE_ITEM BSP_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/lcd/lcd_ex.c"
)
# 2. 自动搜索 components 目录下所有的 .c 文件
file(GLOB_RECURSE COMPONENT_SRCS CONFIGURE_DEPENDS "components/*.c")

# 3. 自动搜索 Application 目录下所有的 .c 文件
file(GLOB_RECURSE APP_SRCS CONFIGURE_DEPENDS "Application/*.c")

# 4. 自动搜索 platform 目录下所有的 .c 文件
file(GLOB_RECURSE PLATFORM_SRCS CONFIGURE_DEPENDS "platform/*.c")

# 5. 自动搜索 Middlewares_cus 目录下所有的 .c 文件
file(GLOB_RECURSE MIDDLEWARES_SRCS CONFIGURE_DEPENDS "Middlewares_cus/*.c")

file(GLOB_RECURSE SYSTEM_VIEW_SRCS CONFIGURE_DEPENDS
        "SystemView/*.c"
        "SystemView/*.S"
        "SystemView/*.s"
)
list(REMOVE_ITEM SYSTEM_VIEW_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/SystemView/RTT/Syscalls/SEGGER_RTT_Syscalls_GCC.c"
)
# Create an executable object type
# 定义最终生成的二进制文件对象
add_executable(${CMAKE_PROJECT_NAME}
        ${BSP_SRCS}
        ${COMPONENT_SRCS}
        ${APP_SRCS}
        ${PLATFORM_SRCS}
        ${MIDDLEWARES_SRCS}
        ${SYSTEM_VIEW_SRCS}


)

# Add STM32CubeMX generated sources
#引入子模块
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
        # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        # Add user sources here
)
file(GLOB_RECURSE AUTO_HEADER_FILES CONFIGURE_DEPENDS
        "Drivers/BSP/*.h"
        "components/*.h"
        "Application/*.h"
        "platform/*.h"
        "Middlewares_cus/LVGL/port/*.h"
        "config/*.h"
        "SystemView/*.h"
)
# 2. 准备一个空的小篮子，用来装目录
set(AUTO_INCLUDE_DIRS "")
# 3. 遍历找到的每一个 .h 文件，把它们所在的文件夹扒出来放进篮子里
foreach (HEADER_FILE ${AUTO_HEADER_FILES})
    get_filename_component(HEADER_DIR ${HEADER_FILE} DIRECTORY)
    list(APPEND AUTO_INCLUDE_DIRS ${HEADER_DIR})
endforeach ()

# 4. 去重并执行黑名单过滤
if (AUTO_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES AUTO_INCLUDE_DIRS)

    set(BLACKLIST_KEYWORDS "tests?|docs?|simulator|nuttx|windows|wayland|sdl|x11|qnx|uefi|micropython")

    # 清剿
    list(FILTER AUTO_INCLUDE_DIRS EXCLUDE REGEX "${BLACKLIST_KEYWORDS}")
endif ()
# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        # Add user defined include paths
        ${AUTO_INCLUDE_DIRS}
        Middlewares_cus/LVGL


)
if(TARGET FreeRTOS)
    target_include_directories(FreeRTOS PUBLIC ${AUTO_INCLUDE_DIRS})
endif()
# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        # Add user defined symbols
        # 解决：某些工程未从 device header 导入 __MPU_PRESENT，导致 core_mpu 被编译为 no-op。
        # 若你确认当前芯片具备 MPU（如 STM32F497），开启该兜底宏即可。
        CORE_MPU_ASSUME_PRESENT=1
)

target_compile_definitions(stm32cubemx INTERFACE
        USER_VECT_TAB_ADDRESS
        VECT_TAB_OFFSET=0x20400
)
# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
# 链接库
target_link_libraries(${CMAKE_PROJECT_NAME}
        stm32cubemx
        # Add user defined libraries
)
#链接选项
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        ## 启用浮点数打印
        #        -Wl,-u,_printf_float
        #        -Wl,-u,_scanf_float
        #禁用内存分配
        -Wl,--wrap=malloc
        -Wl,--wrap=free
        -Wl,--wrap=realloc
        -Wl,--wrap=calloc
        -Wl,--wrap=alloca
)
# 为 Debug 模式定义 DEBUG_MODE 宏
# 为 Release 模式定义 RELEASE_MODE 宏
add_compile_definitions(
        #生成器表达式
        $<$<CONFIG:Debug>:DEBUG_MODE>
        $<$<CONFIG:Release>:RELEASE_MODE>
)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"
        AND "$ENV{ENABLE_ANALYZE}" STREQUAL "1")
    message(STATUS "当前为 GCC || Clang 环境, 已开启堆栈和调用图生成！")
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
            # 1. 生成分析文件
            -fstack-usage    # 生成 .su 文件
            -fdump-ipa-cgraph # 导出调用图
    )
endif ()
# 检测是否是 GCC 或 Clang 编译器
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "当前为 GCC || Clang 环境, 已开启调试优化、禁止动态栈分配!")
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
            # 2. 调试稳定性
            -fno-omit-frame-pointer  # 保留帧指针
            -fno-optimize-sibling-calls # 禁用尾调用优化

            # 3. 堵死动态栈分配
            -Wvla -Werror=vla        # 严禁变长数组
            -Walloca -Werror=alloca  # 严禁 alloca()
    )
endif ()
# --- 生成 .bin 和 .hex 文件 ---
if (CMAKE_OBJCOPY)
    message(STATUS "找到 objcopy: ${CMAKE_OBJCOPY} 用于生成bin && hex文件!")
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    )

    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    )

    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "=>内存占用大小:"
    )

else ()
    message(WARNING "CMAKE_OBJCOPY 没有设置 .bin and .hex 文件将不会被生成")
endif ()

# --- Stack/Recursion audit target ---
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    message("已找到Python3 环境准备递归审计及最坏情况堆栈！")
    add_custom_target(audit-stack
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/audit_stack.py
            --build-dir ${CMAKE_BINARY_DIR}
            --require-entry
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "递归审计及最坏情况堆栈路径中... (.cgraph + .su)"
    )
else ()
    message(WARNING "找不到 Python3 解释器; 审计栈目标将不可用.")
endif ()

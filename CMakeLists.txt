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

# Create an executable object type
# 定义最终生成的二进制文件对象
add_executable(${CMAKE_PROJECT_NAME}
        components/ring_buffer/RingBuffer.c
        components/memory_allocation/MemoryAllocation.c
        components/hfsm/HFSM.c
        Drivers/BSP/Keys/KEY.c
        Drivers/BSP/lcd/lcd.c
        Drivers/BSP/ESP01s/ESP01S.c
        Application/Src/wifi_mqtt_task.c
        Application/Src/mqtt_at_task.c
        Drivers/BSP/Beep/Beep.c
        Drivers/BSP/Light_Sensor/LightSeneor.c
        Application/Src/Light_Sensor_task.c
        Drivers/System/Src/My_delay.c
        Application/Src/water_adc.c
        Application/Src/Usart1_manage.c
        components/log/log.c
        components/AT/AT.c
        components/AT/AT_Core_Task.c
        components/AT/AT_UartMap.c
        components/log/log_port.c
        components/osal/osal_cmsis2.c
        components/soft_timer/src/soft_timer.c
        components/auto_init/auto_init.c
        platform/STM32/ports/hal_gpio_port.c
        platform/STM32/ports/hal_uart_port.c
        platform/STM32/ports/hal_time_port.c
        components/core_base/assert/assert_cus.c
        components/hal/hal_gpio.c
        components/board/Src/board_gpio_map.c
        example/GPIO/ex_hal_gpio.c
        components/core_base/no_malloc_wrap.c
        components/hal/hal_uart.c
        platform/STM32/series/h7/stm32_uart_series_h7.c
        components/board/explorer/board_uart_map.c
        components/printf/printf.c
        platform/STM32/ports/bsp_assert_port.c
        components/core_mpu/src/core_mpu.c
        platform/STM32/common/core_mpu_port.c
        components/CRC/CRC16/crc16.c
        components/memory_pool/memory_pool1/memory_pool1.c
        Drivers/BSP/bh1750/bh1750.c
        components/core_base/stack/stack_monitor.c
        components/core_base/blackbox/blackbox_record.c
        components/ports/arch/cortexm/fault_capture_cm.c
        components/core_base/fault_capture/fault_capture.c
        components/core_base/ramfunc/ramfunc.c
        platform/STM32/ports/vectortable_to_ram.c
        platform/STM32/ports/memory_pool1_port.c
        components/perf/perf.c
        platform/STM32/ports/blackbox_port.c
        components/eventbus/include/eb_module_id.h
        components/eventbus/include/eb_event_id.h
        components/eventbus/include/eb_config.h
        components/eventbus/include/eb_types.h
        components/eventbus/include/eb_api.h
        components/eventbus/include/eb_port.h
        components/eventbus/src/eb_core.c
        components/eventbus/include/eb_sub.h
        components/eventbus/src/eb_sub.c
        components/eventbus/src/eb_dispatch.c
        platform/STM32/ports/event_port.c
        components/eventbus/include/eb_eventdef.h
        components/eventbus/src/eb_eventdef.c


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

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        # Add user defined include paths
        components/hfsm
        components/memory_allocation
        components/ring_buffer
        Drivers/BSP/Keys
        Drivers/BSP/lcd
        Drivers/BSP/ESP01s
        Application/Inc
        Drivers/BSP/Beep
        Drivers/BSP/Light_Sensor
        Drivers/System/Inc
        components/log
        components/AT
        components/core_base
        components/osal
        components/hal/include
        components/board/Include
        platform/STM32/include
        components/printf
        components/core_mpu/include
        platform/STM32/common
        components/CRC/CRC16
        Drivers/BSP/bh1750
        components/core_base/assert
        components/core_base/blackbox
        components/core_base/compiler
        components/core_base/stack
        components/core_base/utils
        components/core_base/fault_capture
        components/ports/arch/cortexm
        components/core_base/barrier
        components/memory_pool/memory_pool1
        components/perf
        components/auto_init
        components/eventbus/include

)

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
# 检测是否是 GCC 或 Clang 编译器
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # 修正：移除 .elf 后缀，确保与 add_executable 中定义的名称一致
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
            # 1. 生成分析文件
            -fstack-usage            # 生成 .su 文件 (Stack Usage)
            -fdump-ipa-cgraph        # 导出调用图 (GCC 特有)

            # 2. 调试稳定性
            -fno-omit-frame-pointer  # 保留帧指针
            -fno-optimize-sibling-calls # 禁用尾调用优化

            # 3. 堵死动态栈分配 (安全红线)
            -Wvla -Werror=vla        # 严禁变长数组
            -Walloca -Werror=alloca  # 严禁 alloca()
    )
endif()
# --- 生成 .bin 和 .hex 文件 ---
if (CMAKE_OBJCOPY)
    message(STATUS "Found objcopy: ${CMAKE_OBJCOPY}")
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            # 使用 ${CMAKE_PROJECT_NAME} 作为目标名
            COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>.bin
            COMMENT "Generating .bin file")

    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>.hex
            COMMENT "Generating .hex file")

    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            COMMENT "Printing size information")
else ()
    message(WARNING "CMAKE_OBJCOPY is not set. .bin and .hex files will not be generated.")
endif ()

# --- Stack/Recursion audit target ---
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_custom_target(audit-stack
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/audit_stack.py
            --build-dir ${CMAKE_BINARY_DIR}
            --require-entry
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Auditing recursion and worst-case stack paths (.cgraph + .su)"
    )
else ()
    message(WARNING "Python3 interpreter not found; audit-stack target will be unavailable.")
endif ()

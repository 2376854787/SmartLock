cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)


# Define the build type
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif ()

# Set the project name
set(CMAKE_PROJECT_NAME SmartLock)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
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
        components/auto_init/src/auto_init.c
        platform/STM32/ports/hal_gpio_port.c
        platform/STM32/ports/hal_uart_port.c
        platform/STM32/ports/hal_time_port.c
        components/core_base/assert_cus.c
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

)

# Add STM32CubeMX generated sources
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

)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        # Add user defined symbols
        # 解决：某些工程未从 device header 导入 __MPU_PRESENT，导致 core_mpu 被编译为 no-op。
        # 若你确认当前芯片具备 MPU（如 STM32F497），开启该兜底宏即可。
        CORE_MPU_ASSUME_PRESENT=1
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
        stm32cubemx

        # Add user defined libraries
)
## 启用浮点数打印
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
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
        $<$<CONFIG:Debug>:DEBUG_MODE>
        $<$<CONFIG:Release>:RELEASE_MODE>
)
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

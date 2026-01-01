cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Force ARM GCC toolchain
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)

# Set CMAKE_AR for static library creation
set(CMAKE_AR arm-none-eabi-ar)
set(CMAKE_RANLIB arm-none-eabi-ranlib)

# Disable compiler checks
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)


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

# ARM Cortex-M4F specific flags
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS} -Wall -Wextra -pedantic -fdata-sections -ffunction-sections")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -DNDEBUG")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}")

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
        Drivers/BSP/as608/ThirdParty/LibDriver_AS608/driver_as608.c
        Drivers/BSP/as608/Core/Src/as608_port.c
        Drivers/BSP/as608/Core/Src/as608_service.c
        Application/Src/as608_test_task.c
        Application/Src/rc522_my_test_task.c
        Drivers/BSP/rc522/rc522_my.c
        components/log/log_port.c
        Drivers/BSP/lvgl_port/lvgl_port.c
        Application/Src/lock_data.c
        Application/Src/ui_lock.c
        Application/Src/lvgl_task.c
        Middlewares/LVGL/src/core/lv_disp.c
        Middlewares/LVGL/src/core/lv_group.c
        Middlewares/LVGL/src/core/lv_indev.c
        Middlewares/LVGL/src/core/lv_indev_scroll.c
        Middlewares/LVGL/src/core/lv_obj.c
        Middlewares/LVGL/src/core/lv_obj_class.c
        Middlewares/LVGL/src/core/lv_obj_draw.c
        Middlewares/LVGL/src/core/lv_obj_pos.c
        Middlewares/LVGL/src/core/lv_obj_scroll.c
        Middlewares/LVGL/src/core/lv_obj_style.c
        Middlewares/LVGL/src/core/lv_obj_style_gen.c
        Middlewares/LVGL/src/core/lv_obj_tree.c
        Middlewares/LVGL/src/core/lv_event.c
        Middlewares/LVGL/src/core/lv_refr.c
        Middlewares/LVGL/src/core/lv_theme.c
        Middlewares/LVGL/src/draw/lv_draw_arc.c
        Middlewares/LVGL/src/draw/lv_draw.c
        Middlewares/LVGL/src/draw/lv_draw_img.c
        Middlewares/LVGL/src/draw/lv_draw_label.c
        Middlewares/LVGL/src/draw/lv_draw_line.c
        Middlewares/LVGL/src/draw/lv_draw_mask.c
        Middlewares/LVGL/src/draw/lv_draw_rect.c
        Middlewares/LVGL/src/draw/lv_draw_transform.c
        Middlewares/LVGL/src/draw/lv_draw_layer.c
        Middlewares/LVGL/src/draw/lv_draw_triangle.c
        Middlewares/LVGL/src/draw/lv_img_buf.c
        Middlewares/LVGL/src/draw/lv_img_cache.c
        Middlewares/LVGL/src/draw/lv_img_decoder.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_arc.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_blend.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_dither.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_gradient.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_img.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_letter.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_line.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_polygon.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_rect.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_transform.c
        Middlewares/LVGL/src/draw/sw/lv_draw_sw_layer.c
        Middlewares/LVGL/src/hal/lv_hal_disp.c
        Middlewares/LVGL/src/hal/lv_hal_indev.c
        Middlewares/LVGL/src/hal/lv_hal_tick.c
        Middlewares/LVGL/src/misc/lv_anim.c
        Middlewares/LVGL/src/misc/lv_anim_timeline.c
        Middlewares/LVGL/src/misc/lv_area.c
        Middlewares/LVGL/src/misc/lv_async.c
        Middlewares/LVGL/src/misc/lv_bidi.c
        Middlewares/LVGL/src/misc/lv_color.c
        Middlewares/LVGL/src/misc/lv_fs.c
        Middlewares/LVGL/src/misc/lv_gc.c
        Middlewares/LVGL/src/misc/lv_ll.c
        Middlewares/LVGL/src/misc/lv_log.c
        Middlewares/LVGL/src/misc/lv_lru.c
        Middlewares/LVGL/src/misc/lv_math.c
        Middlewares/LVGL/src/misc/lv_mem.c
        Middlewares/LVGL/src/misc/lv_printf.c
        Middlewares/LVGL/src/misc/lv_style.c
        Middlewares/LVGL/src/misc/lv_style_gen.c
        Middlewares/LVGL/src/misc/lv_timer.c
        Middlewares/LVGL/src/misc/lv_tlsf.c
        Middlewares/LVGL/src/misc/lv_txt.c
        Middlewares/LVGL/src/misc/lv_txt_ap.c
        Middlewares/LVGL/src/misc/lv_utils.c
        Middlewares/LVGL/src/font/lv_font.c
        Middlewares/LVGL/src/font/lv_font_fmt_txt.c
        Middlewares/LVGL/src/font/lv_font_loader.c
        Middlewares/LVGL/src/font/lv_font_montserrat_14.c
        Middlewares/LVGL/src/font/lv_font_montserrat_16.c
        # NOTE: This project lists LVGL sources manually. When enabling a font in `lv_conf.h`,
        # also add its corresponding `lv_font_montserrat_XX.c` here, otherwise it will compile
        # but fail at link-time with missing font symbols.
        Middlewares/LVGL/src/font/lv_font_montserrat_20.c
        Middlewares/LVGL/src/font/lv_font_montserrat_48.c
        Middlewares/LVGL/src/extra/lv_extra.c
        Middlewares/LVGL/src/extra/layouts/flex/lv_flex.c
        Middlewares/LVGL/src/extra/layouts/grid/lv_grid.c
        Middlewares/LVGL/src/extra/themes/default/lv_theme_default.c
        Middlewares/LVGL/src/widgets/lv_arc.c
        Middlewares/LVGL/src/widgets/lv_bar.c
        Middlewares/LVGL/src/widgets/lv_btn.c
        Middlewares/LVGL/src/widgets/lv_btnmatrix.c
        Middlewares/LVGL/src/widgets/lv_canvas.c
        Middlewares/LVGL/src/widgets/lv_checkbox.c
        Middlewares/LVGL/src/widgets/lv_dropdown.c
        Middlewares/LVGL/src/widgets/lv_img.c
        Middlewares/LVGL/src/widgets/lv_label.c
        Middlewares/LVGL/src/widgets/lv_line.c
        Middlewares/LVGL/src/widgets/lv_roller.c
        Middlewares/LVGL/src/widgets/lv_slider.c
        Middlewares/LVGL/src/widgets/lv_switch.c
        Middlewares/LVGL/src/widgets/lv_table.c
        Middlewares/LVGL/src/widgets/lv_textarea.c
        Middlewares/LVGL/src/extra/widgets/animimg/lv_animimg.c
        Middlewares/LVGL/src/extra/widgets/calendar/lv_calendar.c
        Middlewares/LVGL/src/extra/widgets/calendar/lv_calendar_header_arrow.c
        Middlewares/LVGL/src/extra/widgets/calendar/lv_calendar_header_dropdown.c
        Middlewares/LVGL/src/extra/widgets/chart/lv_chart.c
        Middlewares/LVGL/src/extra/widgets/colorwheel/lv_colorwheel.c
        Middlewares/LVGL/src/extra/widgets/imgbtn/lv_imgbtn.c
        Middlewares/LVGL/src/extra/widgets/keyboard/lv_keyboard.c
        Middlewares/LVGL/src/extra/widgets/led/lv_led.c
        Middlewares/LVGL/src/extra/widgets/list/lv_list.c
        Middlewares/LVGL/src/extra/widgets/menu/lv_menu.c
        Middlewares/LVGL/src/extra/widgets/meter/lv_meter.c
        Middlewares/LVGL/src/extra/widgets/msgbox/lv_msgbox.c
        Middlewares/LVGL/src/extra/widgets/spinbox/lv_spinbox.c
        Middlewares/LVGL/src/extra/widgets/spinner/lv_spinner.c
        Middlewares/LVGL/src/extra/widgets/tabview/lv_tabview.c
        Middlewares/LVGL/src/extra/widgets/tileview/lv_tileview.c
        Middlewares/LVGL/src/extra/widgets/win/lv_win.c
        Drivers/BSP/rc522/rc522_my.c
        Drivers/BSP/touch/touch_port.c
        Drivers/BSP/touch/ctiic.c
        Drivers/BSP/touch/gt9xxx.c
        Drivers/BSP/touch/touch.c
        Drivers/BSP/touch/gt9147_compat.c
        components/osal/osal_cmsis2.c
        components/core_base/assert_cus.c
        platform/STM32/ports/hal_time_port.c

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
        Drivers/BSP/gt9147
        Drivers/BSP/rc522
        Drivers/BSP/ESP01s
        Application/Inc
        Drivers/BSP/Beep
        Drivers/BSP/Light_Sensor
        Drivers/System/Inc
        components/log
        components/AT
        components/core_base
        Drivers/BSP/as608/Core/Inc
        Drivers/BSP/as608/ThirdParty/LibDriver_AS608
        Drivers/BSP/lvgl_port
        Middlewares/LVGL
        Middlewares/LVGL/src
        Middlewares/LVGL/src/core
        Middlewares/LVGL/src/draw
        Middlewares/LVGL/src/draw/sw
        Middlewares/LVGL/src/hal
        Middlewares/LVGL/src/misc
        Middlewares/LVGL/src/font
        Middlewares/LVGL/src/widgets
        Middlewares/LVGL/src/extra
        Middlewares/LVGL/src/extra/themes
        Middlewares/LVGL/src/extra/widgets
        Middlewares/LVGL/src/extra/layouts
        Middlewares/LVGL/src/extra/layouts/flex
        Middlewares/LVGL/src/extra/layouts/grid
        Middlewares/LVGL/src/extra/widgets/animimg
        Middlewares/LVGL/src/extra/widgets/calendar
        Middlewares/LVGL/src/extra/widgets/chart
        Middlewares/LVGL/src/extra/widgets/colorwheel
        Middlewares/LVGL/src/extra/widgets/imgbtn
        Middlewares/LVGL/src/extra/widgets/keyboard
        Middlewares/LVGL/src/extra/widgets/led
        Middlewares/LVGL/src/extra/widgets/list
        Middlewares/LVGL/src/extra/widgets/menu
        Middlewares/LVGL/src/extra/widgets/meter
        Middlewares/LVGL/src/extra/widgets/msgbox
        Middlewares/LVGL/src/extra/widgets/spinbox
        Middlewares/LVGL/src/extra/widgets/spinner
        Middlewares/LVGL/src/extra/widgets/tabview
        Middlewares/LVGL/src/extra/widgets/tileview
        Middlewares/LVGL/src/extra/widgets/win
        Drivers/BSP/touch
        components/osal
        components/hal/include


)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
        stm32cubemx
        m

        # Add user defined libraries
)
## Add linker script and enable floating point printing
#target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
#        -T${CMAKE_SOURCE_DIR}/STM32F407XX_FLASH.ld
#        -Wl,-Map=${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map
#        -Wl,--gc-sections
#        -Wl,-u,_printf_float
#        -Wl,-u,_scanf_float
#)


## Add linker script and enable floating point printing
#target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
#        -Wl,-u,_printf_float
#        -Wl,-u,_scanf_float
#)
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

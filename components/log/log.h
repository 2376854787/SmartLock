//
// Created by yan on 2025/12/6.
//

#ifndef SMARTCLOCK_LOG_H
#define SMARTCLOCK_LOG_H
#include <stdint.h>
/* ================= 配置区域 ================= */
/* 日志控制宏可以控制调试信息的输出 全局*/
#define G_LOG_ENABLE        1
/* 日志输出颜色使能 */
#define LOG_COLOR_ENABLE    1
/*  启用异步缓冲(RTOS Task发送)  0: 使用阻塞发送 */
#define LOG_ASYNC_ENABLE    1
/* 定义日志缓冲区的总大小 (仅在异步模式下有效) */
#define LOG_RB_SIZE         2048
/* HEX一行打印的字节数 */
#define LOG_HEX_BYTES_PER_LINE     16


/* 日志等级定义 */
typedef enum {
    LOG_LEVEL_OFF = 0, // 关闭
    LOG_LEVEL_ERROR, // 错误 (红色)
    LOG_LEVEL_WARN, // 警告 (黄色)
    LOG_LEVEL_INFO, // 信息 (绿色)
    LOG_LEVEL_DEBUG, // 调试 (蓝色)
    LOG_LEVEL_ALL // 全部
} LogLevel_t;

/* 发送函数抽象 */
typedef int (*log_send_async_fn_t)(const uint8_t *data, uint16_t len, void *user);

typedef struct {
    log_send_async_fn_t send_async; // 启动发送（最终要DMA/IT非阻塞）
    void *user; // 例如 UART_HandleTypeDef*
} log_backend_t;


/* 设置当前系统的过滤等级 (小于此等级的日志不会打印) */
// 这里默认设为 DEBUG，开发完后可以改成 INFO 或 ERROR
#ifndef LOG_CURRENT_LEVEL
#define LOG_CURRENT_LEVEL   LOG_LEVEL_ALL
#endif


/* 核心日志输出文件 */
void Log_Printf(LogLevel_t level, const char *file, int line, const char *tag, const char *fmt, ...);

void Log_Hexdump(LogLevel_t level, const char *file, int line, const char *tag, const void *buf, uint32_t len);

/* 初始化日志系统 (RTOS模式下必须先调用) */
void Log_Init(void);

/* 发送函数抽象 */
void Log_SetBackend(log_backend_t b);

/* ================= 宏定义封装  ================= */

#if  (G_LOG_ENABLE==1)

/* ERROR: 严重错误 */
#define LOG_E(tag, fmt, ...) \
Log_Printf(LOG_LEVEL_ERROR, __FILE__, __LINE__, tag, fmt, ##__VA_ARGS__)

/* WARN: 警告，不影响运行但需注意 */
#define LOG_W(tag, fmt, ...) \
Log_Printf(LOG_LEVEL_WARN, __FILE__, __LINE__, tag, fmt, ##__VA_ARGS__)

/* INFO: 关键流程信息 */
#define LOG_I(tag, fmt, ...) \
Log_Printf(LOG_LEVEL_INFO, __FILE__, __LINE__, tag, fmt, ##__VA_ARGS__)

/* DEBUG: 调试数据，发布时可关闭 */
#define LOG_D(tag, fmt, ...) \
Log_Printf(LOG_LEVEL_DEBUG, __FILE__, __LINE__, tag, fmt, ##__VA_ARGS__)
/* 16进制输出信息 */
#define LOG_HEX(tag, level, buf, len) \
Log_Hexdump((level), __FILE__, __LINE__, (tag), (buf), (uint32_t)(len))
#else
/* 如果关闭日志，这些宏为空，编译时直接优化掉，不占空间 */
#define LOG_E(tag, fmt, ...)
#define LOG_W(tag, fmt, ...)
#define LOG_I(tag, fmt, ...)
#define LOG_D(tag, fmt, ...)
#define LOG_HEX(tag, level, buf, len)
#endif

#endif //SMARTCLOCK_LOG_H

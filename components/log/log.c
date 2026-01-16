#include "APP_config.h"
/* 全局配置宏开关 */
#ifdef ENABLE_LOG_SYSTEM
#include <stdarg.h> /* 用于处理可变参数 va_list */
#include <stdio.h>  /* 用于 snprintf, vsnprintf */
#include <string.h> /* 用于 strrchr, memcpy */

#include "log.h"  /* 确保文件名与头文件一致 */
#include "main.h" /* 包含HAL库定义 */

/* 引入 CMSIS-OS2 和 RingBuffer */
#include "MemoryAllocation.h"
#include "RingBuffer.h"
#include "osal.h"
#include "ret_code.h"

#define snprintf_my sniprintf
#define vsnprintf_my vsniprintf
/* ================= 宏定义与配置 ================= */

/* 颜色代码 (ANSI Escape Code) - 用于串口终端显示颜色 */
#if LOG_COLOR_ENABLE
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_RESET "\033[0m"
#else
#define COLOR_RED ""
#define COLOR_GREEN ""
#define COLOR_YELLOW ""
#define COLOR_BLUE ""
#define COLOR_RESET ""
#endif

/* 单条日志最大长度 (字节) */
#define LOG_LINE_MAX 256

/* 定义用于唤醒后台任务的事件标志位 (任意未使用的位即可) */
#define LOG_TASK_FLAG 0x0001
#define LOG_TX_DONE_FLAG 0x0002

/* 将单字节写入环形缓冲区 */
static void Log_PushBytes_NoBlock(const uint8_t *data, uint16_t len);

/* ================= 外部依赖 ================= */

/* ================= 变量定义 ================= */
static log_backend_t s_log_backend = {
    .send_async = NULL,
    .user       = NULL,
};
static bool s_log_backend_ready = 0;
/* 互斥量句柄：用于保护日志缓冲区，防止多线程同时写入导致乱码 */
static osal_mutex_t s_logMutex  = NULL;

#if LOG_ASYNC_ENABLE
/* 异步模式资源 */
static RingBuffer s_logRB; /* 环形缓冲区实例 */

/* 后台发送任务的线程 ID */
static osal_thread_t s_logTaskHandle = NULL;

#else
/* 同步模式下的互斥量属性 */
static const osMutexAttr_t logMutex_attr = {"LogMutex", osMutexRecursive, NULL, 0};
#endif

/* ================= 内部任务实现 ================= */
/**
 * @brief 判断是否初始化发送端
 * @return
 */
static inline uint8_t Log_BackendReady(void) {
    return s_log_backend_ready;
}

/**
 * @brief 查询返回后端配置
 * @return
 */
static inline const log_backend_t *Log_GetBackend(void) {
    return &s_log_backend;
}
#if LOG_ASYNC_ENABLE
/**
 * @brief 日志后台处理任务
 * @note  负责从 RingBuffer 取出数据并通过串口发送
 */
void Log_Task_Entry(void *argument) {
    static uint8_t send_buf[128];
    uint32_t read_len;

    for (;;) {
        /* 等待：有新日志 或 上一笔发送完成（都可能触发继续flush） */
        (void)OSAL_thread_flags_wait(LOG_TASK_FLAG | LOG_TX_DONE_FLAG, OSAL_FLAGS_WAIT_ANY,
                                     OSAL_WAIT_FOREVER);

        for (;;) {
            read_len = sizeof(send_buf);

            if (!ret_is_ok(ReadRingBuffer(&s_logRB, send_buf, &read_len, true)) || read_len == 0) {
                break; /* 空了 */
            }

            if (!Log_BackendReady()) {
                /* 后端未就绪：丢弃 */
                printf("LOG发送端待就位！！！\r\n");
                continue;
            }

            const log_backend_t *b = Log_GetBackend();

            /* 启动发送：若忙则等待完成再重试 */
            int rc;
            do {
                rc = b->send_async(send_buf, (uint16_t)read_len, b->user);
                if (rc == RET_E_BUSY) {
                    printf("LOG发送BUSY！！！\r\n");
                    (void)OSAL_thread_flags_wait(LOG_TX_DONE_FLAG, OSAL_FLAGS_WAIT_ANY,
                                                 OSAL_WAIT_FOREVER);
                }
            } while (rc == RET_E_BUSY);

            /* 启动成功后必须等这笔发送完成，才能复用 send_buf 读下一段 */
            (void)OSAL_thread_flags_wait(LOG_TX_DONE_FLAG, OSAL_FLAGS_WAIT_ANY, OSAL_WAIT_FOREVER);
        }
    }
}

#endif

/* ================= API 接口实现 ================= */

/**
 * @brief 初始化日志系统
 * @note  需要在 main.c 中系统调度开启前，或者第一个任务中调用
 */
void Log_Init(void) {
    /* 1. 创建互斥量 (如果尚未创建) */
    if (s_logMutex == NULL) {
        OSAL_mutex_create(&s_logMutex, "LogMutex", true, true);
    }

#if LOG_ASYNC_ENABLE
    /* 2. 初始化 RingBuffer */
    /* 假设 CreateRingBuffer 内部使用了 static_alloc 或 malloc */
    if (ret_is_err(CreateRingBuffer(&s_logRB, LOG_RB_SIZE))) {
        LOG_E("AT", "s_logRB 环形缓冲区分配失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, LOG_RB_SIZE, query_remain_size());
    /* 3. 创建后台发送任务 */
    const osal_thread_attr_t log_attr = {
        .name       = "LogTask",
        .stack_size = 128 * 4,
        .priority   = OSAL_PRIO_LOW,
    };

    /* 创建线程并保存句柄 */
    OSAL_thread_create(&s_logTaskHandle, Log_Task_Entry, NULL, &log_attr);
#endif
}

/**
 * @brief 核心日志打印函数
 */
void Log_Printf(LogLevel_t level, const char *file, int line, const char *tag, const char *fmt,
                ...) {
    /* 1. 过滤低等级日志 */
    if (level > LOG_CURRENT_LEVEL) return;

    const bool in_isr = (__get_IPSR() != 0);

    /* 2. 获取互斥锁 (保护静态缓冲区 static char log_buf) */
    /* 只有当内核运行中时才需要锁，初始化阶段单线程运行不需要锁 */
    if (OSAL_kernel_is_running() && !in_isr) {
        if (s_logMutex != NULL) {
            /* 等待获取锁，超时时间设为最大 */
            OSAL_mutex_lock(s_logMutex, OSAL_WAIT_FOREVER);
        }
    }

    /* 待优化 静态ISR 和任务抢占的问题 */
    char log_buf[LOG_LINE_MAX];

    /* ================= 格式化阶段 ================= */

    /* 获取系统滴答数 */
    const uint32_t tick = HAL_GetTick();

    /* 设置颜色与等级字符 */
    const char *color   = "";
    char level_char     = ' ';

    /* 判断使用的日志 */
    switch (level) {
        case LOG_LEVEL_ERROR:
            color      = COLOR_RED;
            level_char = 'E';
            break;
        case LOG_LEVEL_WARN:
            color      = COLOR_YELLOW;
            level_char = 'W';
            break;
        case LOG_LEVEL_INFO:
            color      = COLOR_GREEN;
            level_char = 'I';
            break;
        case LOG_LEVEL_DEBUG:
            color      = COLOR_BLUE;
            level_char = 'D';
            break;
        default:
            break;
    }

    /* 文件名简化处理：去除路径，只保留文件名 */
    const char *short_file = strrchr(file, '/');
    if (!short_file) short_file = strrchr(file, '\\');
    short_file         = short_file ? short_file + 1 : file;

    /* 3. 拼装日志头: [Tick] L/TAG: */
    const int head_len = snprintf_my(log_buf, LOG_LINE_MAX, "%s[%lu] %c/%s %s:%d: ", color, tick,
                                     level_char, tag, short_file, line);

    /* 4. 拼装用户内容 (处理可变参数) */
    va_list args;
    va_start(args, fmt);
    /* vsnprintf 会自动处理缓冲区长度限制，防止溢出 */
    const int content_len = vsnprintf_my(log_buf + head_len, LOG_LINE_MAX - head_len, fmt, args);
    va_end(args);

    /* 计算当前总长度 */
    int total_len = head_len + content_len;

    /* 5. 拼装尾部 (颜色复位 + 换行) */
    /* 预留 6 字节给尾部字符，如果不够则截断内容 */
    if (total_len + 6 > LOG_LINE_MAX) total_len = LOG_LINE_MAX - 6;

    const int tail_len =
        snprintf_my(log_buf + total_len, LOG_LINE_MAX - total_len, "%s\r\n", COLOR_RESET);
    total_len += tail_len;

    /* ================= 发送阶段 ================= */

#if LOG_ASYNC_ENABLE
    /* 异步模式：判断内核是否正在运行 */
    if (__get_IPSR() == 0) {
        if (OSAL_kernel_is_running()) {
            /* 尝试写入 RingBuffer */
            uint32_t write_len = total_len;

            /* 这里的 false 表示如果空间不足不写入全部丢弃 (或根据 RingBuffer 实现策略) */
            if (ret_is_ok(WriteRingBuffer(&s_logRB, (uint8_t *)log_buf, &write_len, false))) {
                /* 写入成功，设置标志位唤醒后台任务 */
                if (s_logTaskHandle != NULL) {
                    /* OSAL_thread_flags_set 在 CMSIS-OS2 (STM32实现) 中通常是 ISR 安全的 */
                    /* 它内部会自动判断是否在中断中，并调用对应的 FreeRTOS FromISR 函数 */
                    OSAL_thread_flags_set(s_logTaskHandle, LOG_TASK_FLAG);
                }
            } else {
                /* 缓冲区已满：可以选择丢弃，或者在此处强制改为阻塞发送(会影响实时性) */
                printf("缓冲区已满！！！%s \r\n", log_buf);
            }
        } else {
            /* 如果调度器没启动 (例如在 Log_Init 前使用)，强制使用同步发送 */
            printf("RTOS调度器没启动！！！%s \r\n", log_buf);
        }
    } /* if (__get_IPSR() == 0) */
    else {
        /* 警告需要锁/阻塞代码 被上层尝试调用 */
        char buffer[128];
        uint32_t t_len =
            sniprintf(buffer, 128, "%s[%lu] %c/%s %s:%d:  %s \r\n %s", COLOR_RED, tick, 'E', "LOG",
                      short_file, line, "该代码尝试在中断调用有锁的代码！", COLOR_RESET);

        uint32_t write_len = total_len;
        /* 发送数据到缓冲区 提示 */
        if (ret_is_ok(WriteRingBufferFromISR(&s_logRB, (uint8_t *)buffer, &t_len, false))) {
            /* 写入成功，设置标志位唤醒后台任务 */
            if (s_logTaskHandle != NULL) {
                /* OSAL_thread_flags_set 在 CMSIS-OS2 (STM32实现) 中通常是 ISR 安全的 */
                /* 它内部会自动判断是否在中断中，并调用对应的 FreeRTOS FromISR 函数 */
                OSAL_thread_flags_set(s_logTaskHandle, LOG_TASK_FLAG);
            }
        } else {
            /* 缓冲区已满：可以选择丢弃，或者在此处强制改为阻塞发送(会影响实时性) */
            printf("缓冲区已满！！！%s \r\n", log_buf);
        }

        /* 发送数据到缓冲区 日志 */
        if (ret_is_ok(WriteRingBufferFromISR(&s_logRB, (uint8_t *)log_buf, &write_len, false))) {
            /* 写入成功，设置标志位唤醒后台任务 */
            if (s_logTaskHandle != NULL) {
                /* OSAL_thread_flags_set 在 CMSIS-OS2 (STM32实现) 中通常是 ISR 安全的 */
                /* 它内部会自动判断是否在中断中，并调用对应的 FreeRTOS FromISR 函数 */
                OSAL_thread_flags_set(s_logTaskHandle, LOG_TASK_FLAG);
            }
        } else {
            /* 缓冲区已满：可以选择丢弃，或者在此处强制改为阻塞发送(会影响实时性) */
            /* 采用阻塞式的代码发生 */
            printf("缓冲区已满！！！%s \r\n", log_buf);
        }
    }

#else
    /* 同步模式：直接阻塞发送 */
    printf("%s", log_buf);
#endif

    /* 6. 释放互斥锁 */
    if (OSAL_kernel_is_running()) {
        if (s_logMutex != NULL) {
            OSAL_mutex_unlock(s_logMutex);
        }
    }
}

/**
 * @brief 将数据传入环形缓冲区通知唤醒发送任务
 * @param data 数据源
 * @param len  数据长度
 */
static void Log_PushBytes_NoBlock(const uint8_t *data, uint16_t len) {
#if LOG_ASYNC_ENABLE
    if (OSAL_kernel_is_running()) {
        uint32_t write_len = (uint32_t)len;
        (void)WriteRingBufferFromISR(&s_logRB, (uint8_t *)data, &write_len, false);

        if (s_logTaskHandle != NULL) {
            (void)OSAL_thread_flags_set(s_logTaskHandle, LOG_TASK_FLAG);
        }
    }
#else
    (void)data;
    (void)len;
#endif
}

/**
 * @brief HEX版本的日志打印输出
 * @param level 日志等级
 * @param file  触发文件
 * @param line  触发行数
 * @param tag   tag
 * @param buf   数据源缓冲区
 * @param len   数据长度
 * @note  buf为中文时ASC显示为...
 */
void Log_Hexdump(LogLevel_t level, const char *file, int line, const char *tag, const void *buf,
                 uint32_t len) {
    /* 1、检查日志等级 */
    if (level > LOG_CURRENT_LEVEL) return;

    /* 2、获取数据源的指针 */
    const uint8_t *p    = (const uint8_t *)buf;

    /* 3、获取时间戳、根据日志等级获取输出颜色 */
    const uint32_t tick = HAL_GetTick();
    const char *color   = "";
    char level_char     = ' ';
    switch (level) {
        case LOG_LEVEL_ERROR:
            color      = COLOR_RED;
            level_char = 'E';
            break;
        case LOG_LEVEL_WARN:
            color      = COLOR_YELLOW;
            level_char = 'W';
            break;
        case LOG_LEVEL_INFO:
            color      = COLOR_GREEN;
            level_char = 'I';
            break;
        case LOG_LEVEL_DEBUG:
            color      = COLOR_BLUE;
            level_char = 'D';
            break;
        default:
            break;
    }

    /* 4、获取文件名 */
    const char *short_file = strrchr(file, '/');
    if (!short_file) short_file = strrchr(file, '\\');
    short_file        = short_file ? short_file + 1 : file;

    /* 5、判断是否在中断中执行 */
    const bool in_isr = OSAL_in_isr();

    /* --- [收敛：定义输出宏，用于处理中断和非中断的不同路径] --- */
#if LOG_ASYNC_ENABLE

#define OUTPUT_LOG_LINE(ptr, length)                                                  \
    do {                                                                              \
        if (in_isr) {                                                                 \
            Log_PushBytes_NoBlock((const uint8_t *)(ptr), (uint16_t)(length));        \
        } else {                                                                      \
            if (OSAL_kernel_is_running()) {                                           \
                uint32_t write_len = (uint32_t)(length);                              \
                (void)WriteRingBuffer(&s_logRB, (uint8_t *)(ptr), &write_len, false); \
                if (write_len > 0 && s_logTaskHandle != NULL) {                       \
                    (void)OSAL_thread_flags_set(s_logTaskHandle, LOG_TASK_FLAG);      \
                }                                                                     \
            } else {                                                                  \
                printf("HEX打印缓冲区已满！！！%s \r\n", ptr);                        \
            }                                                                         \
        }                                                                             \
    } while (0)

#else

#define OUTPUT_LOG_LINE(ptr, length)                                           \
    do {                                                                       \
        if (in_isr) {                                                          \
            Log_PushBytes_NoBlock((const uint8_t *)(ptr), (uint16_t)(length)); \
        } else {                                                               \
            Hardware_Send((uint8_t *)(ptr), (uint16_t)(length));               \
        }                                                                      \
    } while (0)

#endif

    /* 6、非中断模式下尝试获取互斥锁 */
    if (!in_isr && OSAL_kernel_is_running() && s_logMutex != NULL) {
        OSAL_mutex_lock(s_logMutex, OSAL_WAIT_FOREVER);
    }

    /* 7、处理空数据或无效参数 */
    if (p == NULL || len == 0) {
        char line_buf[128];
        const int n = sniprintf(line_buf, sizeof(line_buf), "%s[%lu] %c/%s %s:%d: HEX len=0%s\r\n",
                                color, tick, level_char, tag, short_file, line, COLOR_RESET);
        if (n > 0) OUTPUT_LOG_LINE(line_buf, n);
    } else {
        /* 8、开始循环拼装 HEX Dump 主体 */
        static const char HEX[] = "0123456789ABCDEF";
        for (uint32_t off = 0; off < len; off += LOG_HEX_BYTES_PER_LINE) {
            char line_buf[LOG_LINE_MAX];  // 局部缓冲区
            const size_t cap          = sizeof(line_buf);
            const size_t tail_reserve = strlen(COLOR_RESET) + 2 /*\r\n*/ + 1 /*\0*/;
            const size_t limit        = (cap > tail_reserve) ? (cap - tail_reserve) : 0;
            size_t pos                = 0;
            if (limit == 0) {
                // line_buf 太小，无法保证尾部，直接输出简版
                char small[96];
                int n = snprintf_my(small, sizeof(small),
                                    "%s[%lu] %c/%s %s:%d: HEX buf too small%s\r\n", color, tick,
                                    level_char, tag, short_file, line, COLOR_RESET);
                if (n > 0) OUTPUT_LOG_LINE(small, n);
                break;  // 或 return
            }

            /* 拼装头部 */
            const int head =
                snprintf_my(line_buf, sizeof(line_buf), "%s[%lu] %c/%s %s:%d: %08lX: ", color, tick,
                            level_char, tag, short_file, line, (unsigned long)off);
            if (head < 0) continue;
            pos = (size_t)head;
            if (pos > limit) pos = limit;

            /* 拼装 HEX 主体 */
            for (uint16_t i = 0; i < LOG_HEX_BYTES_PER_LINE; i++) {
                /* 至少能够装下一个16进制数 */
                if (pos + 3 > limit) break;
                /* 防止越界读取 */
                if (off + i < len) {
                    const uint8_t b = p[off + i];
                    line_buf[pos++] = HEX[(b >> 4) & 0x0F];
                    line_buf[pos++] = HEX[b & 0x0F];
                    line_buf[pos++] = ' ';
                } else {
                    line_buf[pos++] = ' ';
                    line_buf[pos++] = ' ';
                    line_buf[pos++] = ' ';
                }
                /* 每 8 字节额外空格 */
                if (i == 7 && pos + 1 < limit) line_buf[pos++] = ' ';
            }

            /* ASCII 区 */
            if (pos + 1 < limit) line_buf[pos++] = '|';
            for (uint16_t i = 0; i < LOG_HEX_BYTES_PER_LINE; i++) {
                if (pos + 1 > limit) break;
                if (off + i < len) {
                    const uint8_t c = p[off + i];
                    line_buf[pos++] = (c >= 32 && c <= 126) ? (char)c : '.';
                } else {
                    line_buf[pos++] = ' ';
                }
            }
            if (pos + 1 <= limit) line_buf[pos++] = '|';

            /* 尾部：颜色重置与换行 */
            const int tail = snprintf_my(line_buf + pos, cap - pos, "%s\r\n", COLOR_RESET);
            if (tail > 0) pos += (size_t)tail;
            if (pos < cap) line_buf[pos] = '\0';
            /* 调用收敛后的输出逻辑 */
            OUTPUT_LOG_LINE(line_buf, pos);
        }
    }

    /* 9、释放互斥锁 */
    if (!in_isr && OSAL_kernel_is_running() && s_logMutex != NULL) {
        OSAL_mutex_unlock(s_logMutex);
    }

#undef OUTPUT_LOG_LINE  // 宏仅在该函数内有效
}

/**
 * @brief 注册日志后端（UART/RTT/USB/Flash 等）
 * @note  必须在 Log_Init() 之前调用；一般只调用一次
 */
void Log_SetBackend(log_backend_t b) {
    // 若传入无效后端，则清空并标记未就绪
    if (b.send_async == NULL) {
        memset(&s_log_backend, 0, sizeof(s_log_backend));
        s_log_backend_ready = 0;
        return;
    }

    /* 直接拷贝保存 */
    s_log_backend       = b;
    s_log_backend_ready = 1;
}

/**
 * @brief 通知任务消息存储入缓冲区完成
 */
void Log_OnTxDoneISR(void) {
#if LOG_ASYNC_ENABLE
    if (s_logTaskHandle != NULL) {
        (void)OSAL_thread_flags_set(s_logTaskHandle, LOG_TX_DONE_FLAG);
    }
#endif
}

#endif

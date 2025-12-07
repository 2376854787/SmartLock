#include "log.h"   /* 确保文件名与头文件一致 */
#include "main.h"       /* 包含HAL库定义 */
#include <stdarg.h>     /* 用于处理可变参数 va_list */
#include <stdio.h>      /* 用于 snprintf, vsnprintf */
#include <string.h>     /* 用于 strrchr, memcpy */

/* 引入 CMSIS-OS2 和 RingBuffer */
#include "cmsis_os2.h"
#include "RingBuffer.h"

/* ================= 宏定义与配置 ================= */

/* 颜色代码 (ANSI Escape Code) - 用于串口终端显示颜色 */
#if LOG_COLOR_ENABLE
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_RESET   "\033[0m"
#else
#define COLOR_RED     ""
#define COLOR_GREEN   ""
#define COLOR_YELLOW  ""
#define COLOR_BLUE    ""
#define COLOR_RESET   ""
#endif

/* 单条日志最大长度 (字节) */
#define LOG_LINE_MAX  256

/* 定义用于唤醒后台任务的事件标志位 (任意未使用的位即可) */
#define LOG_TASK_FLAG 0x0001

/* ================= 外部依赖 ================= */

/* 引入串口句柄，用于硬件发送 */
extern UART_HandleTypeDef huart1;

/* 硬件层发送函数的封装 */
static void Hardware_Send(const uint8_t *data, const uint16_t len) {
    /* 使用 HAL 库阻塞式发送，超时时间 100ms */
    HAL_UART_Transmit(&huart1, (uint8_t *) data, len, 100);
}

/* ================= 变量定义 ================= */

/* 互斥量句柄：用于保护日志缓冲区，防止多线程同时写入导致乱码 */
static osMutexId_t s_logMutex = NULL;

#if LOG_ASYNC_ENABLE
/* 异步模式资源 */
static RingBuffer s_logRB; /* 环形缓冲区实例 */

/* 后台发送任务的线程 ID */
static osThreadId_t s_logTaskHandle = NULL;

/* 互斥量属性 (CMSIS v2 需要) */
static const osMutexAttr_t logMutex_attr = {
    "LogMutex",
    osMutexRecursive | osMutexPrioInherit, /* 推荐使用递归锁和优先级继承 */
    NULL,
    0
};
#else
/* 同步模式下的互斥量属性 */
static const osMutexAttr_t logMutex_attr = {"LogMutex", osMutexRecursive, NULL, 0};
#endif

/* ================= 内部任务实现 ================= */

#if LOG_ASYNC_ENABLE
/**
 * @brief 日志后台处理任务
 * @note  负责从 RingBuffer 取出数据并通过串口发送
 */
void Log_Task_Entry(void *argument) {
    uint8_t send_buf[64]; /* 临时发送缓存，减少对 RingBuffer 的锁占用时间 */
    uint16_t read_len;
    for (;;) {
        /* 1. 等待事件标志位 */
        /* osFlagsWaitAny: 等待任意标志位，osWaitForever: 永久阻塞直到被唤醒 (也可设为 1000ms 超时) */
        osThreadFlagsWait(LOG_TASK_FLAG, osFlagsWaitAny, 1000);

        /* 2. 循环读取缓冲区直到为空 */
        do {
            read_len = sizeof(send_buf);

            /* 从 RingBuffer 读取数据 */
            if (ReadRingBuffer(&s_logRB, send_buf, &read_len, false) == true) {
                /* 3. 调用硬件发送 (低优先级任务可以阻塞) */
                Hardware_Send(send_buf, read_len);
            } else {
                /* 读取失败或为空，退出循环 */
                read_len = 0;
            }
        } while (read_len > 0);
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
        s_logMutex = osMutexNew(&logMutex_attr);
    }

#if LOG_ASYNC_ENABLE
    /* 2. 初始化 RingBuffer */
    /* 假设 CreateRingBuffer 内部使用了 static_alloc 或 malloc */
    CreateRingBuffer(&s_logRB, LOG_RB_SIZE);

    /* 3. 创建后台发送任务 */
    const osThreadAttr_t logTask_attributes = {
        .name = "LogTask",
        .stack_size = 128 * 4, /* 栈大小，根据实际情况调整 */
        .priority = (osPriority_t) osPriorityLow, /* 低优先级，不影响业务 */
    };

    /* 创建线程并保存句柄 */
    s_logTaskHandle = osThreadNew(Log_Task_Entry, NULL, &logTask_attributes);
#endif
}

/**
 * @brief 核心日志打印函数
 */
void Log_Printf(LogLevel_t level, const char *file, int line, const char *tag, const char *fmt, ...) {
    /* 1. 过滤低等级日志 */
    if (level > LOG_CURRENT_LEVEL) return;

    /* 2. 获取互斥锁 (保护静态缓冲区 static char log_buf) */
    /* 只有当内核运行中时才需要锁，初始化阶段单线程运行不需要锁 */
    if (osKernelGetState() == osKernelRunning) {
        if (s_logMutex != NULL) {
            /* 等待获取锁，超时时间设为最大 */
            osMutexAcquire(s_logMutex, osWaitForever);
        }
    }

    /* 共享缓冲区 (放在静态区减少栈溢出风险) */
    static char log_buf[LOG_LINE_MAX];

    /* ================= 格式化阶段 ================= */

    /* 获取系统滴答数 */
    const uint32_t tick = HAL_GetTick();

    /* 设置颜色与等级字符 */
    const char *color = "";
    char level_char = ' ';

    /* 判断使用的日志 */
    switch (level) {
        case LOG_LEVEL_ERROR: color = COLOR_RED;
            level_char = 'E';
            break;
        case LOG_LEVEL_WARN: color = COLOR_YELLOW;
            level_char = 'W';
            break;
        case LOG_LEVEL_INFO: color = COLOR_GREEN;
            level_char = 'I';
            break;
        case LOG_LEVEL_DEBUG: color = COLOR_BLUE;
            level_char = 'D';
            break;
        default: break;
    }

    /* 文件名简化处理：去除路径，只保留文件名 */
    const char *short_file = strrchr(file, '/');
    if (!short_file) short_file = strrchr(file, '\\');
    short_file = short_file ? short_file + 1 : file;

    /* 3. 拼装日志头: [Tick] L/TAG: */
    const int head_len = snprintf(log_buf, LOG_LINE_MAX, "%s[%lu] %c/%s: ", color, tick, level_char, tag);

    /* 4. 拼装用户内容 (处理可变参数) */
    va_list args;
    va_start(args, fmt);
    /* vsnprintf 会自动处理缓冲区长度限制，防止溢出 */
    const int content_len = vsnprintf(log_buf + head_len, LOG_LINE_MAX - head_len, fmt, args);
    va_end(args);

    /* 计算当前总长度 */
    int total_len = head_len + content_len;

    /* 5. 拼装尾部 (颜色复位 + 换行) */
    /* 预留 6 字节给尾部字符，如果不够则截断内容 */
    if (total_len + 6 > LOG_LINE_MAX) total_len = LOG_LINE_MAX - 6;

    const int tail_len = snprintf(log_buf + total_len, LOG_LINE_MAX - total_len, "%s\r\n", COLOR_RESET);
    total_len += tail_len;

    /* ================= 发送阶段 ================= */

#if LOG_ASYNC_ENABLE
    /* 异步模式：判断内核是否正在运行 */
    if (osKernelGetState() == osKernelRunning) {
        /* 尝试写入 RingBuffer */
        uint16_t write_len = total_len;

        /* 这里的 false 表示如果空间不足不写入全部丢弃 (或根据 RingBuffer 实现策略) */
        if (WriteRingBuffer(&s_logRB, (uint8_t *) log_buf, &write_len, false)) {
            /* 写入成功，设置标志位唤醒后台任务 */
            if (s_logTaskHandle != NULL) {
                /* osThreadFlagsSet 在 CMSIS-OS2 (STM32实现) 中通常是 ISR 安全的 */
                /* 它内部会自动判断是否在中断中，并调用对应的 FreeRTOS FromISR 函数 */
                osThreadFlagsSet(s_logTaskHandle, LOG_TASK_FLAG);
            }
        } else {
            /* 缓冲区已满：可以选择丢弃，或者在此处强制改为阻塞发送(会影响实时性) */
        }
    } else {
        /* 如果调度器没启动 (例如在 Log_Init 前使用)，强制使用同步发送 */
        Hardware_Send((uint8_t *) log_buf, total_len);
    }

#else
    /* 同步模式：直接阻塞发送 */
    Hardware_Send((uint8_t *) log_buf, total_len);
#endif

    /* 6. 释放互斥锁 */
    if (osKernelGetState() == osKernelRunning) {
        if (s_logMutex != NULL) {
            osMutexRelease(s_logMutex);
        }
    }
}

#include "AT_Core_Task.h"
#include "AT.h" // 包含定义 g_AT_Manager 类型的头文件

#define AT_FLAG_RX   (1u << 0)

/* 任务句柄 */
osThreadId_t AT_Core_Task_Handle = NULL;

/* 引用外部变量 */
extern AT_Manager_t g_AT_Manager;


/**
 * @brief AT核心任务
 * @param argument 参数
 * @note 负责接收通知然后处理接收的数据
 */
void AT_Core_Task(void *argument) {
    for (;;) {
        /* 
         * 等待通知
         * 如果不需要处理超时（例如AT命令超时由硬件定时器或独立的Tick处理），
         * 建议使用 osWaitForever，这样没数据时完全挂起，极度省电。
         */
        const uint32_t flags = osThreadFlagsWait(
            AT_FLAG_RX, /* 只关心接收标志 */
            osFlagsWaitAny, /* 任意匹配 */
            osWaitForever /* 死等，直到有数据才唤醒 (或改为 1000 处理超时) */
        );

        /* 1. 检查是否出错 (最高位为1通常代表错误，如 osErrorTimeout) */
        if (flags & 0x80000000) {
            continue; /* 忽略错误或超时 */
        }

        /* 2. 处理接收事件 */
        if (flags & AT_FLAG_RX) {
            /*
            * 这里只在有数据时进入
            * 建议：可以在 AT_Core_Process 内部加一个 while 循环，
            * 只要 RingBuffer 不为空就一直处理，处理完再退出，提高效率
            */
            AT_Core_Process();
        }
    }
}

/**
 * @brief 初初始化AT_回调处理任务
 */
void at_core_task_init() {
    const osThreadAttr_t AT_Task_attributes = {
        .name = "AT_Core_Task",
        .stack_size = 128 * 6,
        .priority = (osPriority_t) osPriorityNormal, /*  Normal，以免被低优先级日志阻塞 */
    };

    /* 1. 创建任务 */
    AT_Core_Task_Handle = osThreadNew(AT_Core_Task, NULL, &AT_Task_attributes);

    /* 2.立即赋值句柄，防止中断竞争 */
    if (AT_Core_Task_Handle != NULL) {
        g_AT_Manager.core_task = AT_Core_Task_Handle;
    } else {
        LOG_E("AT_Task", "Task Create Failed!");
    }
}

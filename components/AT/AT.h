//
// Created by yan on 2025/12/7.
//

#ifndef SMARTCLOCK_AT_H
#define SMARTCLOCK_AT_H
#include "HFSM.h"
#include "RingBuffer.h"
#include "stm32f4xx_hal.h"
/* 1: 启用RTOS模式(信号量/互斥锁)  0: 启用裸机模式(轮询) */
#ifndef AT_RTOS_ENABLE
#define AT_RTOS_ENABLE 1 /*是否启用了RTOS*/
#endif
/* 2、=阻塞发送(HAL_UART_Transmit)  1=DMA发送(HAL_UART_Transmit_DMA)*/
#ifndef AT_TX_USE_DMA
#define AT_TX_USE_DMA 1
#endif
/* 核心任务任务通知唤醒 */
#define AT_FLAG_RX (1u << 0)
#define AT_FLAG_TX (1u << 1)
#define AT_FLAG_TXDONE (1u << 2)
/* AT指令超时设置 */
#define AT_RX_RB_SIZE 1024      /* AT接收环形缓冲区大小 最好为2的幂*/
#define AT_LEN_RB_SIZE 64       /* 长度缓冲区: 存每行的长度 (存32行足够了, 32*2byte=64) */
#define AT_DMA_BUF_SIZE 256     /* DMA 接收缓冲区 */
#define AT_LINE_MAX_LEN 256     /* 单行回复最大长度 */
#define AT_CMD_TIMEOUT_DEF 5000 /* 默认超时时间 5s */
#define AT_MAX_PENDING 16       /* 同同一个串口最大排队的命令数 */
#define AT_CMD_MAX_LEN 128      /* 命令缓存长度  */
#define AT_EXPECT_MAX_LEN 64    /* expect 缓存长度 */

/* 根据模式引入头文件 */
#if AT_RTOS_ENABLE
#include "osal.h"
#endif
/* 向前声明 */
typedef struct AT_Manager_t AT_Manager_t;

typedef void (*AT_UrcCb)(AT_Manager_t *mgr, const char *line, void *user);

typedef bool (*HW_Send)(AT_Manager_t *mgr, const uint8_t *data, uint16_t len);

/* ================= 枚举定义 ================= */
/* AT命令执行返回的结果 */
typedef enum {
    AT_RESP_OK = 0,  /* 收到了期待的回复 */
    AT_RESP_ERROR,   /*收到了“Error” */
    AT_RESP_TIMEOUT, /* 系统超时没有回复 */
    AT_RESP_BUSY,    /* 系统忙 */
    AT_RESP_WAITING  /* (内部状态) 正在等待中 */
} AT_Resp_t;

/* 内部事件 ID （用于驱动 HFSM）*/
typedef enum {
    AT_EVT_NONE = 0,
    AT_EVT_SEND,    /* [操作] 请求发送 */
    AT_EVT_RX_LINE, /* [中断/轮询] 收到了一行完整数据 */
    AT_EVT_TIMEOUT, /* [Tick] 定时器超时 */
} AT_EventID_t;

/* 串口发送是否采用DMA */
typedef enum { AT_TX_BLOCK = 0, AT_TX_DMA = 1 } AT_TxMode;

/**
 * @brief AT 命令对象（一次请求-响应会话的载体）
 *
 * 该结构体用于描述“发送一条 AT 命令并等待其终止响应”的完整生命周期：
 * - 上层填充：命令文本、期望匹配串、超时
 * - 核心层回填：最终结果 result
 * - 同步方式：RTOS 用信号量等待；裸机用完成标志轮询等待
 *
 * 注意：
 * - cmd_buf / expect_buf 采用“拷贝式存储”，避免上层传入栈内存导致悬空。
 * - result / is_finished 常在核心任务与上层调用线程之间共享，因此以 volatile 标记可见性。
 */
typedef struct {
    /* ===========================
     * 1) 请求参数（由调用者写入）
     * =========================== */

    /** 要发送的 AT 命令文本（必须是 '\0' 结尾的字符串） */
    char cmd_buf[AT_CMD_MAX_LEN];

    /**
     * 期望命中的响应关键字（'\0' 结尾字符串）
     * - 为空/NULL 语义：表示使用默认成功终止条件（例如 "OK"）
     * - 非空：表示以该子串命中作为成功终止（例如 "+CGREG: 1,1"）
     */
    char expect_buf[AT_EXPECT_MAX_LEN];

    /** 命令级超时（毫秒），从“命令发出/会话开始”起计算 */
    uint32_t timeout_ms;

    /* ===========================
     * 2) 运行结果（由核心层回填）
     * =========================== */

    /**
     * 最终结果（核心解析层写入，上层读取）
     * 典型取值：WAITING / OK / ERROR / TIMEOUT / BUSY 等
     */
    volatile AT_Resp_t result;

    /* ===========================
     * 3) 完成通知机制（RTOS/裸机双模）
     * =========================== */
#if AT_RTOS_ENABLE
    /**
     * RTOS 模式：完成信号量
     * - 核心层在命令“终止”时释放信号量
     * - 上层阻塞等待该信号量以获得同步语义
     */
    osal_sem_t done_sem;
#else
    /**
     * 裸机模式：完成标志
     * - 核心层在命令“终止”时置位
     * - 上层采用 while 轮询等待（注意需配合超时/喂狗策略）
     */
    volatile bool is_finished;
#endif

    /**
     * 对象池占用标志（对象生命周期管理用）
     * - 1：对象已分配/正在使用
     * - 0：对象空闲，可再次分配
     *
     * 通常与“对象池管理策略（栈/链表/位图）”配合使用。
     */
    volatile uint8_t in_use;

} AT_Command_t;

/**
 * @brief AT 管理器（一个串口实例/一个 AT 会话域的运行上下文）
 *
 * 管理器负责协调如下职责：
 * 1) 底层输入输出：串口句柄、发送函数、DMA 接收缓冲
 * 2) 接收路径：DMA -> 回调搬运 -> 字节环形缓冲；以及“行边界/长度”队列
 * 3) 解析与分流：按行取出，区分“命令响应”与“URC 异步通知”
 * 4) 命令调度：命令队列、单活动命令会话、超时裁决
 * 5) 双模：RTOS 下用消息队列/任务/互斥；裸机下用简化忙锁
 *
 * 线程模型（RTOS）：
 * - ISR/回调：只做增量搬运 + 行边界打点 + 事件通知，不做字符串解析
 * - 核心任务：消费行、执行匹配、驱动命令会话完成、路由 URC
 */
typedef struct AT_Manager_t {
    /* =========================================================
     * 1) 状态机（可选：用于扩展 payload 模式、恢复模式等）
     * ========================================================= */

    /**
     * 解析/会话状态机
     * - 可用于描述：IDLE / WAIT_RESP / WAIT_PROMPT / SEND_PAYLOAD / RECOVERY ...
     * - 目前可作为扩展接口：将“复杂模式”显式状态化，避免散落 if/else
     */
    StateMachine fsm;

    /* =========================================================
     * 2) 硬件与底层资源
     * ========================================================= */

    /**
     * 接收字节环形缓冲（Byte Ring Buffer）
     * - 写入侧：ISR/接收回调（增量搬运）
     * - 读取侧：核心任务（按行长度读取）
     *
     * 注意：若 RingBuffer 内部不自带存储区，则需在外部注入静态内存。
     */
    RingBuffer rx_rb;

    /**
     * 硬件发送函数指针（可切换阻塞/中断/DMA 实现）
     * - 由核心任务调用以发送命令文本或 payload
     * - 需满足线程安全与重入约束（通常只由核心任务单线程调用）
     */
    HW_Send hw_send;

    /** 串口句柄（HAL/驱动层句柄），用于 DMA 状态查询、回调映射等 */
    UART_HandleTypeDef *uart;

    /* =========================================================
     * 3) 解析与接收相关缓存
     * ========================================================= */

    /**
     * 线性行缓存（Line Buffer）
     * - 核心任务从 rx_rb 中读取一行后存放于此，再做字符串匹配/路由
     * - 该缓存为“任务上下文私有”，原则上不应在 ISR 中写
     */
    uint8_t line_buf[AT_LINE_MAX_LEN];

    /**
     * DMA 循环接收缓冲（DMA Rx Circular Buffer）
     * - DMA 写入侧：硬件/驱动
     * - 消费侧：接收回调根据 DMA 写指针增量取出新数据
     */
    uint8_t dma_rx_arr[AT_DMA_BUF_SIZE];

    /**
     * 行处理索引（可用于行缓存消费过程中的游标）
     * - 若仅作为调试/统计，可考虑后续收敛
     * - volatile：可能在不同上下文读取（建议统一由核心任务维护）
     */
    volatile uint16_t line_idx;

    /**
     * ISR 侧“当前行累计长度”
     * - 接收回调逐字节统计：当前行写入了多少字节
     * - 遇到行结束符/提示符时，将该长度写入“行长度队列”，并清零重新累计
     */
    volatile uint16_t isr_line_len;

    /**
     * 行长度环形缓冲（Length Queue）
     * - 存放每一行的长度（uint16）
     * - 写入侧：ISR/接收回调（边界打点）
     * - 读取侧：核心任务（按长度从 rx_rb 取出完整行）
     *
     * 该队列的存在使核心任务无需再扫描分隔符，直接按长度取行。
     */
    RingBuffer msg_len_rb;

    /**
     * DMA 增量处理游标：记录上次处理到的 DMA 写入位置
     * - 用于处理 DMA 环形回卷：计算“新增字节段”
     */
    volatile uint16_t last_pos;

    /**
     * 接收溢出标志
     * - 当 rx_rb 或 msg_len_rb 写入失败（空间不足）时置位
     * - 核心任务应执行“止血策略”：清空缓冲、记录统计、必要时进入恢复状态
     */
    volatile bool rx_overflow;

    /** URC 回调的用户上下文指针（透传给 urc_cb） */
    void *urc_user;

    /**
     * URC 回调（异步通知处理）
     * - 当收到的行不属于任何活动命令会话时，调用该回调
     * - 建议 URC 回调只做轻量解析与投递，避免阻塞核心任务
     */
    AT_UrcCb urc_cb;

    /* =========================================================
     * 4) 命令会话运行时状态（单活动命令）
     * ========================================================= */

    /**
     * 当前活动命令对象指针
     * - 非 NULL：表示当前正在等待该命令的终止响应
     * - NULL：表示当前无活动会话，可从命令队列取新命令发送
     */
    AT_Command_t *curr_cmd;

    /**
     * 当前命令会话开始时刻（tick）
     * - 用于统计时延/日志
     * - 与 deadline tick 配合用于超时裁决
     */
    volatile uint32_t req_start_tick;

#if AT_RTOS_ENABLE
    /* =========================================================
     * 5) 发送侧（DMA/异步发送）辅助状态（RTOS 模式）
     * ========================================================= */

    /** 发送完成信号量：DMA 发送完成回调释放，用于同步/推进队列 */
    osal_sem_t tx_done_sem;

    /**
     * 发送忙标志（保护/诊断）
     * - 1：发送通道繁忙（DMA 未完成）
     * - 0：可发
     * 通常由发送启动与发送完成回调维护
     */
    volatile uint8_t tx_busy;

    /**
     * 发送错误标志
     * - 在发送错误回调中置位
     * - 核心任务读取后应进行错误处置（结束命令/重试/恢复）
     */
    volatile uint8_t tx_error;

    /**
     * 发送模式选择
     * - 运行期选择：阻塞发送 / DMA 发送 / 其他
     * - 影响 hw_send 的具体路径或策略
     */
    AT_TxMode tx_mode;
#endif

    /* =========================================================
     * 6) 线程与同步（双模兼容）
     * ========================================================= */
#if AT_RTOS_ENABLE

    /**
     * 命令队列（元素为命令对象指针）
     * - 写入侧：上层提交命令
     * - 读取侧：核心任务取出并发送
     */
    osal_msgq_t cmd_q;

    /** 核心任务句柄：负责解析、分流、调度与超时裁决 */
    osal_thread_t core_task;

    /** 对象池互斥：保护对象池分配/归还操作，避免多线程并发破坏 */
    osal_mutex_t pool_mutex;

    /**
     * 命令对象池（静态分配，避免动态内存）
     * - cmd_pool：实际对象存储
     * - free_stack：空闲索引栈（LIFO），用于快速分配/归还
     * - free_top：当前栈顶
     */
    AT_Command_t cmd_pool[AT_MAX_PENDING];
    uint16_t free_stack[AT_MAX_PENDING];
    uint16_t free_top;

    /**
     * 当前活动命令的超时点（tick）
     * - req_start_tick + timeout 转换而来
     * - 核心任务定期检查 now >= deadline 来裁决 TIMEOUT
     */
    uint32_t curr_deadline_tick;

#else
    /**
     * 裸机模式忙锁（简化串行化）
     * - true：已有命令在执行/等待响应
     * - false：可发新命令
     *
     * 注意：裸机模式下仍需考虑 ISR 与主循环之间的可见性与临界区保护。
     */
    bool is_locked;
#endif

} AT_Manager_t;

/* ================= API 声明 ================= */

/**
 * @brief 初始化 AT 核心框架
 * @param at_manager
 * @param uart 串口句柄
 * @param hw_send 硬件串口发送函数指针
 */
void AT_Core_Init(AT_Manager_t *at_manager, UART_HandleTypeDef *uart, HW_Send hw_send);

/**
 * @brief 接收数据回调 (放入串口接收中断)
 * @param at_manager AT设备句柄
 * @param huart      串口句柄
 * @param Size       接收的大小
 */
void AT_Core_RxCallback(AT_Manager_t *at_manager, const UART_HandleTypeDef *huart, uint16_t Size);

/**
 * @brief 核心轮询/处理函数
 * @note  RTOS模式: 放入 AT_Task 中运行
 * @note  裸机模式: 放入 main while(1) 中运行
 */
void AT_Core_Process(AT_Manager_t *at_manager);

/**
 * @brief 发送 AT 指令并等待结果 (阻塞式接口)
 * @param at_manager AT设备句柄
 * @param cmd 指令内容，如 "AT"
 * @param expect 期望回复，如 "OK" (NULL表示只等默认OK)
 * @param timeout_ms 超时时间(ms)
 * @return 执行结果
 */
AT_Resp_t AT_SendCmd(AT_Manager_t *at_manager, const char *cmd, const char *expect,
                     uint32_t timeout_ms);

/**
 * @brief 将ms转换为心跳
 * @param ms 需要转换为心跳的ms
 * @return 返回心跳
 */
uint32_t AT_MsToTicks(uint32_t ms);

/**
 * @brief   返回当前句柄当前执行对象的进度状态
 * @param h AT设备句柄
 * @return 对象的进度状态
 */
AT_Resp_t AT_Poll(AT_Command_t *h);

/**
 *
 * @param mgr AT设备句柄
 * @param cb  绑定的URC回调函数
 * @param user 传递的上下文
 */
void AT_SetUrcHandler(AT_Manager_t *mgr, AT_UrcCb cb, void *user);

/**
 * @brief 获取空闲对象装填参数后返回
 * @param mgr AT句柄
 * @param cmd 发送的AT命令
 * @param expect 期待返回中应该有的字符串
 * @param timeout_ms 超时时间
 * @return 返回一个装填好的命令对象指针
 */
AT_Command_t *AT_Submit(AT_Manager_t *mgr, const char *cmd, const char *expect,
                        uint32_t timeout_ms);

/**
 * @brief 获取空闲对象装填参数后返回
 * @param mgr AT句柄
 * @param cmd 发送的AT命令
 * @param expect 期待返回中应该有的字符串
 * @param timeout_ms 超时时间
 * @return 返回一个装填好的命令对象指针
 * @note  非阻塞版
 */
AT_Command_t *AT_SendAsync(AT_Manager_t *mgr, const char *cmd, const char *expect,
                           uint32_t timeout_ms);

/**
 * @brief 获取信号量确保发送后被任务唤醒
 * @param sem 需要被获取的信号量
 */
void AT_SemDrain(osal_sem_t sem);

/**
 * @brief 根据波特率和发送的数据长度计算需要的时间
 * @param mgr AT设备句柄
 * @param len 发送数据的长度
 * @return 返回发送数据需要的数据时间
 */
uint32_t AT_TxTimeoutMs(AT_Manager_t *mgr, uint16_t len);

/**
 * @brief 更改具体AT设备的发送模式
 * @param mgr AT设备句柄
 * @param mode 设定的模式
 * @note 默认根据全局宏定义现在是 开启DMA
 */
void AT_SetTxMode(AT_Manager_t *mgr, AT_TxMode mode);

#endif  // SMARTCLOCK_AT_H

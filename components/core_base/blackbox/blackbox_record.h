#ifndef SMARTLOCK_BLACK_BOX_RECORDE_H
#define SMARTLOCK_BLACK_BOX_RECORDE_H
#include <stdint.h>

#define BLACK_BOX_MAGIC   0xB10C1B00u
#define BLACK_BOX_VERSION 0x00010001u
/* 重启原因枚举 */
typedef enum {
    BB_RESET_UNKNOW = 0,
    BB_RESET_ASSERT,     /* 断言失败 */
    BB_RESET_HARDFAULT,  /* 硬错误 */
    BB_RESET_MEMMANAGE,  /* 内存管理错误 MPU */
    BB_RESET_BUSFAULT,   /* 总线错误 */
    BB_RESET_USAGEFAULT, /* 用法错误 */
    BB_RESET_POR,        /* Power-On Reset */
    BB_RESET_PIN,        /* Reset按钮 */
    BB_RESET_SOFT,       /* 软件复位 */
    BB_RESET_IWDG,
    BB_RESET_WWDG,
    BB_RESET_BOR,  /* Brown-Out Reset 欠压复位*/
    BB_RESET_LPWR, /* Low Power 低功耗唤醒复位*/
} bb_reset_reason_t;
/* 崩溃原因枚举 */
typedef enum {
    BB_CRASH_NONE = 0,
    BB_CRASH_ASSERT,     /* 断言失败 */
    BB_CRASH_HARDFAULT,  /* 硬错误 */
    BB_CRASH_MEMMANAGE,  /* 内存管理错误 MPU */
    BB_CRASH_BUSFAULT,   /* 总线错误 */
    BB_CRASH_USAGEFAULT, /* 用法错误 */
} bb_crash_type_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t boot_count;
    /* 复位原因 启动阶段写入*/
    uint32_t reset_reason;
    /* 崩溃信息 */
    uint32_t crash_type;
    uint32_t pc;
    uint32_t lr;
    uint32_t sp;
    uint32_t psr; /* 中断状态、指令集状态 */
    /* Fault 诊断寄存器 Cortex-M HardFault MemManage BusFault UsageFault*/
    uint32_t cfsr;  /* 配置错误状态寄存器 指出是除零、未定义指令还是总线错误*/
    uint32_t hfsr;  /* 硬错误状态寄存器  指出是否是因为处理其他异常时又发生了异常 */
    uint32_t dfsr;  /* 调试故障状态 */
    uint32_t mmfar; /* 内存管理错误地址寄存器 如果发生了内存访问违规，这里记录了尝试访问的非法地址*/
    uint32_t bfar;  /* 发生总线错误（如访问不存在的外设）时的非法地址　*/
    uint32_t afsr;  /* 辅助故障状态寄存器 可能为空看具体芯片 */

    /* 关键状态机快照 */
    uint32_t fsm_ptr;   /* 当前运行的层次状态机 */
    uint32_t state_ptr; /* 状态机所属的状态 */

    /* 确定性监控；最大关中断时间 */
    uint32_t max_crit_us;

    /* 栈最小剩余 字节数 */
    uint32_t min_stack_free;

    /* 追溯 */
    uint32_t build_id; /* git commit 哈希前4字节 */
} blackbox_record_t;

const blackbox_record_t* BB_Get(void);
void BB_Clear(void);
void BB_ClearCrashInfo(void); /* 仅清除崩溃信息，上报日志后调用 */
/* 启动调用 读取并清除 MCU reset flags,写入 reset_reason */
void BB_OnBootUpdateResetReason(void);
/* Assert/Fault 调用 写入crash 上下文 */
void BB_RecordAssert(uint32_t pc, uint32_t lr, uint32_t sp, uint32_t psr);
void BB_RecordFault(bb_crash_type_t type, uint32_t pc, uint32_t lr, uint32_t sp, uint32_t psr,
                    uint32_t cfsr, uint32_t hfsr, uint32_t dfsr, uint32_t mmfar, uint32_t bfsr,
                    uint32_t afsr);
/* HFSM 由 Transition 钩子更新 */
void BB_RecordFsm(uint32_t fsm_ptr, uint32_t state_ptr);
/* 中断时间/栈剩余字节数 */
void BB_UpdateMaxCriUs(uint32_t us);
void BB_UpdateMinStackFree(uint32_t bytes);
void BB_Info_Printf();
#endif  // SMARTLOCK_BLACK_BOX_RECORDE_H

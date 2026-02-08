#ifndef SMARTLOCK_EB_PORT_H
#define SMARTLOCK_EB_PORT_H
#include <stdint.h>

/* 将事件投递到 mailbox，成功返回 true，失败（满）返回 false */
bool eb_port_mailbox_push(void* mailbox, const eb_event_t* ev);
bool eb_port_mailbox_pop(void* mailbox, eb_event_t* out); // 模块Task用
/* 进入/退出临界区：先用关中断/OS critical，后续可换更细粒度 */
uint32_t eb_port_enter_critical(void);
void eb_port_exit_critical(uint32_t primask);

/* 时间戳：ms 或 us 都行，但要单调递增 */
uint32_t eb_port_timestamp(void);

/* 可选：断言失败处理 */
void eb_port_panic(const char* msg);
#endif  // SMARTLOCK_EB_PORT_H

#ifndef SMARTLOCK_EB_FREEZE_H
#define SMARTLOCK_EB_FREEZE_H
#include <stdbool.h>

/* 进入调度前调用一次：冻结订阅表，运行期只读 */
void eb_freeze(void);

/* 查询是否已冻结（Debug/断言用） */
bool eb_is_frozen(void);
#endif  // SMARTLOCK_EB_FREEZE_H

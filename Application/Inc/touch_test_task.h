#ifndef SMARTLOCK_TOUCH_TEST_TASK_H
#define SMARTLOCK_TOUCH_TEST_TASK_H

/**
 * @brief GT911 触摸测试任务入口
 * @param argument 未使用
 *
 * 功能: 初始化 GT911 → 轮询读取触摸数据 → LOG 输出坐标
 */
void StartTouchTestTask(void* argument);

#endif /* SMARTLOCK_TOUCH_TEST_TASK_H */

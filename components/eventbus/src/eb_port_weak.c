#include "eb_port.h"

/* 默认弱实现：平台未实现时避免链接失败。
 * 注意：更高精度、更可信的实现应该由平台层覆盖（提供同名非 weak 函数）。
 */

/* TODO: 当前 weak 实现精度不足（ms*1000），Budget Police 的 p50/p95/p99 在 <1ms 粒度下无意义。
 *       平台层应使用 DWT Cycle Counter 或硬件 TIM 提供真正的 µs 级单调递增时间戳。
 *       实现路径参考：
 *         CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
 *         DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
 *         return DWT->CYCCNT / (SystemCoreClock / 1000000u);
 */
__attribute__((weak)) uint32_t eb_port_timestamp_us(void) {
    /* 退化：ms -> us */
    return eb_port_timestamp() * 1000u;
}

__attribute__((weak)) eb_reset_reason_t eb_port_read_reset_reason_and_clear(void) {
    return EB_RESET_UNKNOWN;
}

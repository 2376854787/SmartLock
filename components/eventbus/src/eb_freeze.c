#include "eb_freeze.h"

#include "assert_cus.h"
#include "eb_config.h"
#include "eb_port.h"
static volatile uint8_t g_frozen = 0u;

void eb_freeze(void) {
#if EB_CFG_STATIC_FREEZE
    uint32_t state;
    eb_port_enter_critical(&state);
    g_frozen = 1u;
    eb_port_exit_critical(state);
    CORE_ASSERT(g_frozen == 1u);
#else
    /* 未启用 Freeze：保持 0 */
#endif
}

bool eb_is_frozen(void) {
#if EB_CFG_STATIC_FREEZE
    return (g_frozen != 0u);
#else
    return false;
#endif
}

#include "hal_error_catch.h"

#include "hal_uart.h"
#include "log.h"
__attribute__((weak)) void hal_uart_on_port_error(ret_code_t rc_port, const char* api,
                                                  hal_uart_id_t id, uint32_t arg0,
                                                  uint32_t arg1) {
    (void)rc_port;
    (void)api;
    (void)id;
    (void)arg0;
    (void)arg1;
    LOG_E("port", "port:%d, api:%s, uart_id:%d", rc_port, api, id);
}

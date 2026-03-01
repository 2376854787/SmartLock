#include "hal_uart.h"

#include <stddef.h>
#include <stdio.h>

#include "APP_config.h"
#include "assert_cus.h"
#include "hal_uart_port.h"
#include "osal.h"
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)
#include "log.h"
#endif

/* ---------- HAL UART 统一错误码构造 ---------- */
#define UART_HAL_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_STATE(reason_)   RET_MAKE_STATE(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_IO(reason_)      RET_MAKE_IO(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_RES(reason_)     RET_MAKE_RESOURCE(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_DATA(reason_)    RET_MAKE_DATA(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_FATAL(reason_)   RET_MAKE_FATAL(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))

#if (defined(CFG_FEAT_HAL_UART) && (CFG_FEAT_HAL_UART == 1))

/**
 * @brief port 层错误上报钩子（默认弱实现，可在外部重写）
 * @param rc_port port 层原始错误码
 * @param rc_hal  映射后的 HAL 错误码
 * @param api     触发错误的 HAL API 名称
 * @param arg0    辅助参数0（调用点上下文）
 * @param arg1    辅助参数1（调用点上下文）
 * @note 是否在 ISR 中打印由 CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR 决定
 */
__attribute__((weak)) void hal_uart_on_port_error(ret_code_t rc_port, ret_code_t rc_hal,
                                                  const char* api, uint32_t arg0,
                                                  uint32_t arg1) {
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_UART_LOG_PORT_ERR) && (CFG_PARAM_UART_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        printf("HAL_UART  api:%s port:0x%08lX->hal:0x%08lX arg0:%lu arg1:%lu",
              (api != NULL) ? api : "unknown", (unsigned long)rc_port, (unsigned long)rc_hal,
              (unsigned long)arg0, (unsigned long)arg1);
    }
#else
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
#endif
}

/**
 * @brief 将 port 层错误码映射为 HAL UART 统一错误码
 * @param rc_port port 层返回值
 * @param api     触发映射的 HAL API 名称
 * @param arg0    辅助参数0（用于日志定位）
 * @param arg1    辅助参数1（用于日志定位）
 * @return 映射后的 HAL 错误码；当 rc_port 为成功时返回 RET_OK
 */
static inline ret_code_t uart_map_port_to_hal(ret_code_t rc_port, const char* api, uint32_t arg0,
                                              uint32_t arg1) {
    if (ret_is_ok(rc_port)) return RET_OK;

    ret_code_t rc_hal = UART_HAL_IO(RET_R_IO);

    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_port, RET_R_NULL_PTR))
            rc_hal = UART_HAL_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc_port, RET_R_RANGE_ERR))
            rc_hal = UART_HAL_PARAM(RET_R_RANGE_ERR);
        else if (ret_is_reason(rc_port, RET_R_UNSUPPORTED))
            rc_hal = UART_HAL_PARAM(RET_R_UNSUPPORTED);
        else
            rc_hal = UART_HAL_PARAM(RET_R_INVALID_ARG);
    } else if (ret_is_class(rc_port, RET_CLASS_TIMEOUT)) {
        rc_hal = UART_HAL_TIMEOUT(RET_R_TIMEOUT);
    } else if (ret_is_class(rc_port, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_port, RET_R_QUEUE_FULL))
            rc_hal = UART_HAL_RES(RET_R_QUEUE_FULL);
        else if (ret_is_reason(rc_port, RET_R_BUFFER_FULL))
            rc_hal = UART_HAL_RES(RET_R_BUFFER_FULL);
        else if (ret_is_reason(rc_port, RET_R_NO_MEM))
            rc_hal = UART_HAL_RES(RET_R_NO_MEM);
        else
            rc_hal = UART_HAL_RES(RET_R_NO_RESOURCE);
    } else if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_port, RET_R_BUSY))
            rc_hal = UART_HAL_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc_port, RET_R_NOT_READY))
            rc_hal = UART_HAL_STATE(RET_R_NOT_READY);
        else
            rc_hal = UART_HAL_STATE(RET_R_STATE_ERR);
    } else if (ret_is_class(rc_port, RET_CLASS_DATA)) {
        if (ret_is_reason(rc_port, RET_R_CRC))
            rc_hal = UART_HAL_DATA(RET_R_CRC);
        else if (ret_is_reason(rc_port, RET_R_DATA_NOT_ENOUGH))
            rc_hal = UART_HAL_DATA(RET_R_DATA_NOT_ENOUGH);
        else if (ret_is_reason(rc_port, RET_R_DATA_OVERFLOW))
            rc_hal = UART_HAL_DATA(RET_R_DATA_OVERFLOW);
        else
            rc_hal = UART_HAL_DATA(RET_R_PARSE_ERR);
    } else if (ret_is_class(rc_port, RET_CLASS_FATAL)) {
        rc_hal = UART_HAL_FATAL(RET_R_PANIC);
    }

    hal_uart_on_port_error(rc_port, rc_hal, api, arg0, arg1);
    return rc_hal;
}

/**
 * @brief 初始化 UART 句柄并完成板级资源绑定
 * @param id   板级 UART 编号
 * @param cfg  UART 配置
 * @param out  返回 UART 句柄
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_init(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    ASSERT_PARAM((cfg != NULL) && (out != NULL));
    REQUIRE_RET((cfg != NULL) && (out != NULL), UART_HAL_PARAM(RET_R_INVALID_ARG));
    *out = NULL;

    const ret_code_t rc = hal_uart_port_init(id, cfg, out);
    if (ret_is_err(rc)) return uart_map_port_to_hal(rc, "hal_uart_init", (uint32_t)id, 0u);

    if (*out == NULL) return UART_HAL_STATE(RET_R_STATE_ERR);
    return RET_OK;
}

/**
 * @brief 反初始化 UART 句柄并释放对应资源
 * @param h UART 句柄
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_deinit(hal_uart_t* h) {
    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, UART_HAL_PARAM(RET_R_INVALID_ARG));

    const ret_code_t rc = hal_uart_port_deinit(h);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_deinit", (uint32_t)hal_uart_port_get_id(h), 0u);
    return RET_OK;
}

/**
 * @brief 启动 UART 接收路径（通常为 DMA/中断接收）
 * @param h UART 句柄
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_rx_start(hal_uart_t* h) {
    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, UART_HAL_PARAM(RET_R_INVALID_ARG));

    const ret_code_t rc = hal_uart_port_rx_start(h);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_rx_start", (uint32_t)hal_uart_port_get_id(h), 0u);
    return RET_OK;
}

/**
 * @brief 异步发送数据
 * @param h   UART 句柄
 * @param buf 发送缓存地址
 * @param len 发送长度（字节）
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len) {
    ASSERT_PARAM((h != NULL) && (buf != NULL) && (len != 0u));
    REQUIRE_RET((h != NULL) && (buf != NULL) && (len != 0u), UART_HAL_PARAM(RET_R_INVALID_ARG));

    const ret_code_t rc = hal_uart_port_send_async(h, buf, len);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_send_async", (uint32_t)hal_uart_port_get_id(h),
                                    len);
    return RET_OK;
}

/**
 * @brief 从接收缓冲区拷贝读取数据
 * @param h     UART 句柄
 * @param out   输出缓冲区
 * @param want  期望读取字节数
 * @param nread 实际读取字节数
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread) {
    if (nread) *nread = 0u;
    ASSERT_PARAM((h != NULL) && (out != NULL) && (nread != NULL));
    ASSERT_PARAM(want != 0u);
    REQUIRE_RET((h != NULL) && (out != NULL) && (nread != NULL), UART_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(want != 0u, UART_HAL_PARAM(RET_R_RANGE_ERR));

    const ret_code_t rc = hal_uart_port_read(h, out, want, nread);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_read", (uint32_t)hal_uart_port_get_id(h), want);
    return RET_OK;
}

/**
 * @brief 申请接收环形缓冲区可读窗口（零拷贝读取）
 * @param h     UART 句柄
 * @param want  期望读取字节数；由 port 层决定最终授予大小
 * @param out   返回可读窗口
 * @param nread 实际可读字节数
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                 uint32_t* nread) {
    if (nread) *nread = 0u;
    ASSERT_PARAM((h != NULL) && (out != NULL) && (nread != NULL));
    REQUIRE_RET((h != NULL) && (out != NULL) && (nread != NULL), UART_HAL_PARAM(RET_R_INVALID_ARG));

    const ret_code_t rc = hal_uart_port_read_reserve(h, want, out, nread);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_read_reserve", (uint32_t)hal_uart_port_get_id(h),
                                    want);
    return RET_OK;
}

/**
 * @brief 提交零拷贝读取后已消费的字节数
 * @param h     UART 句柄
 * @param nread 已消费字节数
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_read_commit(hal_uart_t* h, uint32_t nread) {
    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, UART_HAL_PARAM(RET_R_INVALID_ARG));

    const ret_code_t rc = hal_uart_port_read_commit(h, nread);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_read_commit", (uint32_t)hal_uart_port_get_id(h),
                                    nread);
    return RET_OK;
}

/**
 * @brief 注册 UART 事件回调
 * @param h    UART 句柄
 * @param cb   事件回调
 * @param user 用户上下文
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, UART_HAL_PARAM(RET_R_INVALID_ARG));

    const ret_code_t rc = hal_uart_port_set_evt_cb(h, cb, user);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_set_evt_cb", (uint32_t)hal_uart_port_get_id(h),
                                    0u);
    return RET_OK;
}

ret_code_t hal_uart_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    return hal_uart_init(id, cfg, out);
}

ret_code_t hal_uart_close(hal_uart_t* h) {
    return hal_uart_deinit(h);
}

#else /* !CFG_FEAT_HAL_UART */

/* 功能关闭时统一返回 UNSUPPORTED，保证上层可编译链接 */
ret_code_t hal_uart_init(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    (void)id;
    (void)cfg;
    (void)out;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_deinit(hal_uart_t* h) {
    (void)h;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_rx_start(hal_uart_t* h) {
    (void)h;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len) {
    (void)h;
    (void)buf;
    (void)len;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread) {
    (void)h;
    (void)out;
    (void)want;
    if (nread) *nread = 0u;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                 uint32_t* nread) {
    (void)h;
    (void)want;
    (void)out;
    if (nread) *nread = 0u;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_read_commit(hal_uart_t* h, uint32_t nread) {
    (void)h;
    (void)nread;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    (void)h;
    (void)cb;
    (void)user;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    return hal_uart_init(id, cfg, out);
}

ret_code_t hal_uart_close(hal_uart_t* h) {
    return hal_uart_deinit(h);
}

#endif /* CFG_FEAT_HAL_UART */

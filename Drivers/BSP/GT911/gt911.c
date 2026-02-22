/**
 * @file    gt911.c
 * @brief   GT911 电容触控驱动实现
 *
 * GT911 寄存器地图 (关键部分):
 *   0x8140 ~ 0x8143  Product ID (4 字节 ASCII，例如 "911\0")
 *   0x8144 ~ 0x8145  Firmware Version
 *   0x8146 ~ 0x8147  X Resolution
 *   0x8148 ~ 0x8149  Y Resolution
 *   0x814E           Touch Status  (bit7=buffer_ready, bit3:0=touch_count)
 *   0x814F ~ 0x8176  Touch Point Data (每触点 8 字节)
 *
 * 每个触点数据格式 (8 字节):
 *   [0]     Track ID
 *   [1][2]  X 坐标 (little-endian)
 *   [3][4]  Y 坐标 (little-endian)
 *   [5][6]  Size    (little-endian)
 *   [7]     Reserved
 */

#include "gt911.h"

#include "APP_config.h"

#if defined(CFG_FEAT_GT911) && (CFG_FEAT_GT911==1)

#include <string.h>

#include "hal_time.h"

/* ======================== 寄存器地址 ======================== */

#define GT911_REG_PRODUCT_ID 0x8140u
#define GT911_REG_FW_VERSION 0x8144u
#define GT911_REG_X_RES      0x8146u
#define GT911_REG_Y_RES      0x8148u
#define GT911_REG_CONFIG     0x8047u
#define GT911_REG_CHECK_SUM  0x80FFu
#define GT911_REG_STATUS     0x814Eu
#define GT911_REG_POINT_BASE 0x814Fu

#define GT911_CFG_SIZE 186u /* 0x8047 ~ 0x8100 */

/** 每个触点占用的字节数 */
#define GT911_POINT_SIZE 8u

/* ======================== 错误码 ======================== */

#define GT911_RET(cls_, reason_) \
    RET_MAKE(RET_MOD_PORT, RET_SUB_PORT_DRIVER, RET_CODE_MAKE((cls_), (reason_)))

/* ======================== 内部函数 ======================== */

/**
 * @brief  执行 GT911 硬件复位 + I2C 地址选择时序
 *
 * 时序说明:
 *   1. INT 输出目标地址对应的电平 (LOW → 0x5D, HIGH → 0x14)
 *   2. RST 拉低 ≥ 10ms
 *   3. RST 释放
 *   4. 延时 ≥ 50ms 等待 GT911 启动
 *   5. INT 切换为浮空输入（准备接收中断）
 */
static ret_code_t gt911_hw_reset(const gt911_dev_t* dev) {
    /* 配置 INT 为推挽输出，用于地址选择 */
    const hal_gpio_cfg_t int_out_cfg = {
        .dir           = HAL_GPIO_DIR_OUT,
        .out_type      = HAL_GPIO_OUT_PP,
        .pull          = HAL_GPIO_PULL_NONE,
        .speed         = HAL_GPIO_SPEED_MEDIUM,
        .irq           = HAL_GPIO_IRQ_NONE,
        .alternate     = HAL_GPIO_AF_NONE,
        .default_level = HAL_GPIO_LEVEL_LOW,
    };
    ret_code_t rc = hal_gpio_config(dev->intr, &int_out_cfg);
    if (rc != RET_OK) return rc;

    /* 配置 RST 为推挽输出 */
    const hal_gpio_cfg_t rst_cfg = {
        .dir           = HAL_GPIO_DIR_OUT,
        .out_type      = HAL_GPIO_OUT_PP,
        .pull          = HAL_GPIO_PULL_NONE,
        .speed         = HAL_GPIO_SPEED_MEDIUM,
        .irq           = HAL_GPIO_IRQ_NONE,
        .alternate     = HAL_GPIO_AF_NONE,
        .default_level = HAL_GPIO_LEVEL_LOW,
    };
    rc = hal_gpio_config(dev->rst, &rst_cfg);
    if (rc != RET_OK) return rc;

    /* Step 1: INT 输出地址选择电平 */
    if (dev->addr == GT911_ADDR_LOW) {
        hal_gpio_write(dev->intr, HAL_GPIO_LEVEL_LOW); /* 0x5D */
    } else {
        hal_gpio_write(dev->intr, HAL_GPIO_LEVEL_HIGH); /* 0x14 */
    }

    /* Step 2: RST 拉低 ≥ 10ms */
    hal_gpio_write(dev->rst, HAL_GPIO_LEVEL_LOW);
    hal_time_delay_ms(20);

    /* Step 3: RST 释放 */
    hal_gpio_write(dev->rst, HAL_GPIO_LEVEL_HIGH);

    /* Step 4: 等待 GT911 启动 (≥ 50ms) */
    hal_time_delay_ms(60);

    /* Step 5: INT 切换为浮空输入 (准备接收触摸中断) */
    const hal_gpio_cfg_t int_in_cfg = {
        .dir           = HAL_GPIO_DIR_IN,
        .out_type      = HAL_GPIO_OUT_PP,
        .pull          = HAL_GPIO_PULL_NONE,
        .speed         = HAL_GPIO_SPEED_LOW,
        .irq           = HAL_GPIO_IRQ_NONE,
        .alternate     = HAL_GPIO_AF_NONE,
        .default_level = HAL_GPIO_LEVEL_LOW,
    };
    rc = hal_gpio_config(dev->intr, &int_in_cfg);

    return rc;
}

/**
 * @brief  更新配置中的刷新率
 * @param  dev   设备实例
 * @param  rate  周期 (ms), e.g. 10ms = 100Hz
 */
static ret_code_t gt911_update_refresh_rate(gt911_dev_t* dev, uint8_t rate) {
    if (rate == 0) return RET_OK; /* 保持默认 */

    uint8_t cfg[GT911_CFG_SIZE];

    /* 1. 读取当前完整配置 (186 字节: 0x8047 ~ 0x8100) */
    ret_code_t rc =
        soft_i2c_read_reg16(&dev->i2c, dev->addr, GT911_REG_CONFIG, cfg, GT911_CFG_SIZE);
    if (rc != RET_OK) return rc;

    /* 2. 修改刷新率 (偏移 = 0x804C - 0x8047 = 15) */
    /* 检查是否需变更，避免无效写 */
    if (cfg[15] == rate) return RET_OK;

    cfg[15]           = rate;

    /* 3. 重新计算校验和 */
    /* Checksum = (~(sum(0x8047...0x80FE)) + 1) & 0xFF */
    uint8_t checksum = 0;
    for (uint32_t i = 0; i < (GT911_CFG_SIZE - 2); ++i) {
        checksum += cfg[i];
    }
    checksum                = (~checksum) + 1;

    cfg[GT911_CFG_SIZE - 2] = checksum; /* 0x80FF */
    cfg[GT911_CFG_SIZE - 1] = 1;        /* 0x8100: Config Update Flag */

    /* 4. 写回配置 */
    /* GT911 要求写 Config 时要整块写入或分块写入，这里一次性写186字节 */
    rc = soft_i2c_write_reg16(&dev->i2c, dev->addr, GT911_REG_CONFIG, cfg, GT911_CFG_SIZE);

    return rc;
}

/* ======================== 公共 API ======================== */

ret_code_t gt911_init(gt911_dev_t* dev, const gt911_cfg_t* cfg) {
    if (!dev || !cfg) {
        return GT911_RET(RET_CLASS_PARAM, RET_R_NULL_PTR);
    }

    /* 保存配置 */
    dev->addr     = cfg->i2c_addr;
    dev->max_x    = cfg->max_x;
    dev->max_y    = cfg->max_y;

    /* 打开 RST / INT 引脚句柄 */
    ret_code_t rc = hal_gpio_open(&dev->rst, cfg->gpio_id_rst);
    if (rc != RET_OK) return rc;

    rc = hal_gpio_open(&dev->intr, cfg->gpio_id_int);
    if (rc != RET_OK) return rc;

    /* 硬件复位 + I2C 地址选择 */
    rc = gt911_hw_reset(dev);
    if (rc != RET_OK) return rc;

    /* 初始化 Soft I2C 总线 */
    const soft_i2c_cfg_t i2c_cfg = {
        .gpio_id_scl = cfg->gpio_id_scl,
        .gpio_id_sda = cfg->gpio_id_sda,
        .delay_us    = 1, /* ~400kHz，GT911 支持最高 400kHz */
    };
    rc = soft_i2c_init(&dev->i2c, &i2c_cfg);
    if (rc != RET_OK) return rc;

    /* 验证通信: 读 Product ID */
    char product_id[4] = {0};
    rc                 = gt911_read_product_id(dev, product_id, sizeof(product_id));
    if (rc != RET_OK) return rc;

    /* 检查 Product ID 是否包含 "911" 子串 */
    if (product_id[0] != '9' || product_id[1] != '1' || product_id[2] != '1') {
        return GT911_RET(RET_CLASS_IO, RET_R_HW_FAULT);
    }

    /* 更新刷新率 (如果配置了) */
    rc = gt911_update_refresh_rate(dev, cfg->refresh_rate);

    return rc;
}

ret_code_t gt911_read_touch(gt911_dev_t* dev, gt911_touch_data_t* out) {
    if (!dev || !out) {
        return GT911_RET(RET_CLASS_PARAM, RET_R_NULL_PTR);
    }

    memset(out, 0, sizeof(gt911_touch_data_t));

    /* 读取触摸状态寄存器 0x814E */
    uint8_t status = 0;
    ret_code_t rc  = soft_i2c_read_reg16(&dev->i2c, dev->addr, GT911_REG_STATUS, &status, 1);
    if (rc != RET_OK) return rc;

    /* bit7: buffer_ready 标志 */
    if (!(status & 0x80u)) {
        /* 数据未就绪，返回 0 触点 */
        return RET_OK;
    }

    /* bit3:0: 触点数量 */
    uint8_t touch_count = status & 0x0Fu;
    if (touch_count > GT911_MAX_TOUCH_POINTS) {
        touch_count = GT911_MAX_TOUCH_POINTS;
    }
    out->count = touch_count;

    /* 批量读取所有触点原始数据 */
    if (touch_count > 0) {
        uint8_t raw[GT911_MAX_TOUCH_POINTS * GT911_POINT_SIZE];
        const uint32_t read_len = (uint32_t)touch_count * GT911_POINT_SIZE;

        rc = soft_i2c_read_reg16(&dev->i2c, dev->addr, GT911_REG_POINT_BASE, raw, read_len);
        if (rc != RET_OK) {
            out->count = 0;
            /* 仍然需要清标志，否则 GT911 不会更新下一帧 */
            goto clear_flag;
        }

        /* 解析每个触点:
         *   [0]     Track ID
         *   [1][2]  X (little-endian)
         *   [3][4]  Y (little-endian)
         *   [5][6]  Size (little-endian)
         *   [7]     Reserved
         */
        for (uint8_t i = 0; i < touch_count; ++i) {
            const uint8_t* p    = &raw[i * GT911_POINT_SIZE];
            out->points[i].id   = p[0];
            out->points[i].x    = (uint16_t)((uint16_t)p[2] << 8u) | p[1];
            out->points[i].y    = (uint16_t)((uint16_t)p[4] << 8u) | p[3];
            out->points[i].size = (uint16_t)((uint16_t)p[6] << 8u) | p[5];
        }
    }

clear_flag:
    /* 清除 buffer_ready 标志: 向 0x814E 写入 0x00 */
    {
        const uint8_t zero = 0x00u;
        const ret_code_t rc2 =
            soft_i2c_write_reg16(&dev->i2c, dev->addr, GT911_REG_STATUS, &zero, 1);
        /* 如果前面读取成功但清标志失败，优先返回清标志的错误 */
        if (rc == RET_OK && rc2 != RET_OK) {
            rc = rc2;
        }
    }

    return rc;
}

ret_code_t gt911_read_product_id(gt911_dev_t* dev, char* id_buf, uint32_t buf_len) {
    if (!dev || !id_buf) {
        return GT911_RET(RET_CLASS_PARAM, RET_R_NULL_PTR);
    }
    if (buf_len < 4u) {
        return GT911_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    return soft_i2c_read_reg16(&dev->i2c, dev->addr, GT911_REG_PRODUCT_ID, (uint8_t*)id_buf, 4);
}

#else /* !ENABLE_GT911 */

ret_code_t gt911_init(gt911_dev_t* dev, const gt911_cfg_t* cfg) {
    (void)dev;
    (void)cfg;
    return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_DRIVER, RET_R_UNSUPPORTED);
}

ret_code_t gt911_read_touch(gt911_dev_t* dev, gt911_touch_data_t* out) {
    (void)dev;
    (void)out;
    return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_DRIVER, RET_R_UNSUPPORTED);
}

ret_code_t gt911_read_product_id(gt911_dev_t* dev, char* id_buf, uint32_t buf_len) {
    (void)dev;
    (void)id_buf;
    (void)buf_len;
    return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_DRIVER, RET_R_UNSUPPORTED);
}

#endif /* ENABLE_GT911 */

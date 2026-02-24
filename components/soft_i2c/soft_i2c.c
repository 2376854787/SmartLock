/**
 * @file    soft_i2c.c
 * @brief   软件 I2C  实现
 *
 * 原理:
 *   SCL/SDA 均配置为开漏+内部上拉模式。
 *   "拉低"  → hal_gpio_write(pin, LOW)   — FET 导通接地
 *   "释放"  → hal_gpio_write(pin, HIGH)  — FET 关断，由上拉电阻拉高
 *   "读取"  → hal_gpio_read(pin)         — 读 IDR 获取实际电平
 *
 * 时钟拉伸 :
 *   从机可在 SCL 低电平期间将 SCL 继续拉低，表示"请等待"。
 *   主机释放 SCL 后必须回读 SCL 引脚的实际电平，等到真正变 HIGH
 *   才能继续。本实现在每次释放 SCL 后调用 scl_wait_high() 轮询，
 *   超时则返回错误。
 *
 * 时序: 每个半周期插入 delay_us 微秒延时，典型值:
 *   100 kHz → delay_us = 5
 *   400 kHz → delay_us = 1 (受限于 GPIO 翻转速度和延时精度)
 */

#include "soft_i2c.h"

#include "APP_config.h"

/* soft_i2c 统一归口到 HAL/I2C 子模块。 */
#define SI2C_RET(cls_, reason_) \
    RET_MAKE(RET_MOD_HAL, RET_SUB_HAL_I2C, RET_CODE_MAKE((cls_), (reason_)))

#if (defined(CFG_FEAT_SOFT_I2C) && (CFG_FEAT_SOFT_I2C == 1)) && \
    (defined(CFG_FEAT_HAL_GPIO) && (CFG_FEAT_HAL_GPIO == 1))

#include "assert_cus.h"
#include "hal_time.h"
#include "log.h"

/* ======================== 内部宏 ======================== */

/** 释放引脚（开漏模式写 HIGH = 浮空，由上拉拉高） */
#define SCL_HIGH(bus) hal_gpio_write((bus)->scl, HAL_GPIO_LEVEL_HIGH)
#define SCL_LOW(bus)  hal_gpio_write((bus)->scl, HAL_GPIO_LEVEL_LOW)
#define SCL_READ(bus) hal_gpio_read((bus)->scl)
#define SDA_HIGH(bus) hal_gpio_write((bus)->sda, HAL_GPIO_LEVEL_HIGH)
#define SDA_LOW(bus)  hal_gpio_write((bus)->sda, HAL_GPIO_LEVEL_LOW)
#define SDA_READ(bus) hal_gpio_read((bus)->sda)

#define I2C_DELAY(bus) hal_time_delay_us((bus)->delay_us)

/** 时钟拉伸等待超时 (μs)，超过此时间认为从机异常 */
#define SCL_STRETCH_TIMEOUT_US 1000u

static inline ret_code_t si2c_lock(soft_i2c_t *bus) {
    CORE_ASSERT(bus != NULL);
    if ((bus == NULL) || (bus->mutex == NULL))
        return SI2C_RET(RET_CLASS_STATE, RET_R_NOT_READY);
    return OSAL_mutex_lock(bus->mutex, OSAL_WAIT_FOREVER);
}

static inline void si2c_unlock(soft_i2c_t *bus) {
    if ((bus != NULL) && (bus->mutex != NULL)) {
        (void)OSAL_mutex_unlock(bus->mutex);
    }
}

/* ======================== 时钟拉伸 ======================== */

/**
 * @brief  释放 SCL 后等待从机也释放 SCL（时钟拉伸处理）
 * @param  bus 句柄
 * @return true=SCL 已变 HIGH，false=超时（从机未释放）
 */
static bool scl_wait_high(const soft_i2c_t* bus) {
    SCL_HIGH(bus);

    /* 快速路径：大多数设备不做时钟拉伸，第一次读就是 HIGH */
    if (SCL_READ(bus) == HAL_GPIO_LEVEL_HIGH) {
        return true;
    }

    /* 慢路径：轮询等待，每次间隔 1μs */
    const uint32_t start = hal_get_tick_us32();
    while ((uint32_t)(hal_get_tick_us32() - start) < SCL_STRETCH_TIMEOUT_US) {
        if (SCL_READ(bus) == HAL_GPIO_LEVEL_HIGH) {
            return true;
        }
        hal_time_delay_us(1);
    }

    /* 超时：从机持续拉低 SCL */
    return false;
}

/* ======================== 底层 bit 操作 ======================== */

/**
 * @brief  发送 START 条件: SCL=HIGH 时 SDA 由 HIGH→LOW
 * @param  bus 句柄
 * @return true=成功，false=SCL 时钟拉伸超时
 */
static bool i2c_start(soft_i2c_t* bus) {
    /* 确保总线空闲: SDA=H, SCL=H */
    SDA_HIGH(bus);
    I2C_DELAY(bus);
    /* 释放 SCL 并等待时钟拉伸 */
    if (!scl_wait_high(bus)) return false;
    I2C_DELAY(bus);

    /* START: SDA 下降沿 */
    SDA_LOW(bus);
    I2C_DELAY(bus);
    /* 然后拉低 SCL 准备发数据 */
    SCL_LOW(bus);
    I2C_DELAY(bus);
    return true;
}

/**
 * @brief  发送 STOP 条件: SCL=HIGH 时 SDA 由 LOW→HIGH
 * @param  bus 句柄
 * @return true=成功，false=SCL 时钟拉伸超时
 */
static bool i2c_stop(soft_i2c_t* bus) {
    /* 确保 SDA=LOW */
    SDA_LOW(bus);
    I2C_DELAY(bus);
    /* 释放 SCL 并等待时钟拉伸 */
    if (!scl_wait_high(bus)) return false;
    I2C_DELAY(bus);
    /* STOP: SDA 上升沿 */
    SDA_HIGH(bus);
    I2C_DELAY(bus);
    return true;
}

/**
 * @brief  发送 1 字节，MSB first
 * @param  bus  句柄
 * @param  byte 一字节的数据
 * @return true=收到 ACK，false=收到 NACK 或时钟拉伸超时
 */
static bool i2c_write_byte(const soft_i2c_t* bus, uint8_t byte) {
    /* 发送 8 bit 数据 */
    for (uint8_t i = 0; i < 8u; ++i) {
        /* 取最高位 */
        if (byte & 0x80u) {
            SDA_HIGH(bus);
        } else {
            SDA_LOW(bus);
        }
        /* 左移 */
        byte <<= 1u;
        I2C_DELAY(bus);
        /* 释放 SCL 并等待时钟拉伸 */
        if (!scl_wait_high(bus)) return false;
        I2C_DELAY(bus); /* 从机在 SCL HIGH 期间采样 */
        SCL_LOW(bus);   /* 时钟下降沿 */
    }

    /* 释放 SDA，等从机拉低表示 ACK */
    SDA_HIGH(bus);
    I2C_DELAY(bus);
    /* 释放 SCL 并等待时钟拉伸（从机可能需要时间准备 ACK） */
    if (!scl_wait_high(bus)) return false;
    I2C_DELAY(bus);

    /* 读取 ACK: LOW=ACK, HIGH=NACK */
    const bool ack = (SDA_READ(bus) == HAL_GPIO_LEVEL_LOW);

    SCL_LOW(bus);
    I2C_DELAY(bus);

    return ack;
}

/**
 * @brief  读取 1 字节，MSB first
 * @param  bus  句柄
 * @param  ack  true=发送 ACK（还有后续字节），false=发送 NACK（最后一字节）
 * @param  out  输出读到的字节
 * @return true=成功，false=时钟拉伸超时
 */
static bool i2c_read_byte(const soft_i2c_t* bus, bool ack, uint8_t* out) {
    uint8_t byte = 0;
    /* 释放 SDA，让从机驱动 */
    SDA_HIGH(bus);
    for (uint8_t i = 0; i < 8u; ++i) {
        byte <<= 1u;

        I2C_DELAY(bus);
        /* 释放 SCL 并等待时钟拉伸 */
        if (!scl_wait_high(bus)) return false;
        I2C_DELAY(bus);

        if (SDA_READ(bus) == HAL_GPIO_LEVEL_HIGH) {
            byte |= 0x01u;
        }
        SCL_LOW(bus);
    }
    /* 发送 ACK 或 NACK */
    if (ack) {
        SDA_LOW(bus); /* ACK: SDA=LOW */
    } else {
        SDA_HIGH(bus); /* NACK: SDA=HIGH */
    }
    I2C_DELAY(bus);
    /* 释放 SCL 并等待时钟拉伸 */
    if (!scl_wait_high(bus)) return false;
    I2C_DELAY(bus);
    SCL_LOW(bus);
    I2C_DELAY(bus);

    /* 释放 SDA */
    SDA_HIGH(bus);

    *out = byte;
    return true;
}

/* ======================== 公共 API ======================== */
/**
 * @brief 平台无关初始化
 * @param bus 句柄
 * @param cfg 板级映射配置
 * @return 32位状态码
 */
ret_code_t soft_i2c_init(soft_i2c_t* bus, const soft_i2c_cfg_t* cfg) {
    ASSERT_PARAM((bus != NULL) && (cfg != NULL));
    REQUIRE_RET((bus != NULL) && (cfg != NULL), SI2C_RET(RET_CLASS_PARAM, RET_R_NULL_PTR));

    bus->scl   = NULL;
    bus->sda   = NULL;
    bus->mutex = NULL;

    bus->delay_us = cfg->delay_us;
    if (bus->delay_us == 0) {
        bus->delay_us = 2; /* 默认 ~250kHz */
    }

    /* 打开 GPIO 句柄 */
    ret_code_t rc = hal_gpio_open(&bus->scl, cfg->gpio_id_scl);
    if (rc != RET_OK) return rc;
    rc = hal_gpio_open(&bus->sda, cfg->gpio_id_sda);
    if (rc != RET_OK) {
        (void)hal_gpio_close(bus->scl);
        bus->scl = NULL;
        return rc;
    }

    /* 配置为开漏+上拉输出，默认高电平 */
    const hal_gpio_cfg_t gpio_cfg = {
        .dir           = HAL_GPIO_DIR_OUT,
        .out_type      = HAL_GPIO_OUT_OD,
        .pull          = HAL_GPIO_PULL_UP,
        .speed         = HAL_GPIO_SPEED_HIGH,
        .irq           = HAL_GPIO_IRQ_NONE,
        .alternate     = HAL_GPIO_AF_NONE,
        .default_level = HAL_GPIO_LEVEL_HIGH,
    };

    rc = hal_gpio_config(bus->scl, &gpio_cfg);
    if (rc != RET_OK) {
        (void)hal_gpio_close(bus->sda);
        (void)hal_gpio_close(bus->scl);
        bus->sda = NULL;
        bus->scl = NULL;
        return rc;
    }

    rc = hal_gpio_config(bus->sda, &gpio_cfg);
    if (rc != RET_OK) {
        (void)hal_gpio_close(bus->sda);
        (void)hal_gpio_close(bus->scl);
        bus->sda = NULL;
        bus->scl = NULL;
        return rc;
    }

    /* 释放总线 */
    SCL_HIGH(bus);
    SDA_HIGH(bus);
    I2C_DELAY(bus);

    /* 创建 RTOS 互斥锁（递归 + 优先级继承） */
    rc = OSAL_mutex_create(&bus->mutex, "si2c", true, true);
    if (rc != RET_OK) {
        (void)hal_gpio_close(bus->sda);
        (void)hal_gpio_close(bus->scl);
        bus->sda = NULL;
        bus->scl = NULL;
        return rc;
    }

    return RET_OK;
}
/**
 * @brief 往指定地址设备发送 len字节数据
 * @param bus 句柄
 * @param dev_addr 设备地址
 * @param data 数据
 * @param len 长度
 * @return 32位状态码
 */
ret_code_t soft_i2c_write(soft_i2c_t* bus, uint8_t dev_addr, const uint8_t* data, uint32_t len) {
    ASSERT_PARAM((bus != NULL) && ((data != NULL) || (len == 0u)));
    REQUIRE_RET((bus != NULL) && ((data != NULL) || (len == 0u)),
                SI2C_RET(RET_CLASS_PARAM, RET_R_NULL_PTR));
    ret_code_t rc = si2c_lock(bus);
    if (ret_is_err(rc)) return rc;

    ret_code_t ret = RET_OK;
    /* start */
    if (!i2c_start(bus)) {
        LOG_W("I2C", "I2C起始信号发送失败");
        ret = SI2C_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
        goto out;
    }

    /* 发送设备地址 + W(0) */
    if (!i2c_write_byte(bus, (uint8_t)(dev_addr << 1u))) {
        i2c_stop(bus);
        LOG_W("I2C", "I2C地址没有回复");
        ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
        goto out;
    }

    /* 发送数据 */
    for (uint32_t i = 0; i < len; ++i) {
        if (!i2c_write_byte(bus, data[i])) {
            i2c_stop(bus);
            LOG_W("I2C", "I2C读取数据失败");
            ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
            goto out;
        }
    }

    i2c_stop(bus);
out:
    si2c_unlock(bus);
    return ret;
}
/**
 * @brief 往指定地址设备读取 len 字节数据
 * @param bus 句柄
 * @param dev_addr 设备地址
 * @param data 数据接受地址
 * @param len 长度
 * @return 32位状态码
 * @noye 地址传入7位地址不加 读写控制位
 */
ret_code_t soft_i2c_read(soft_i2c_t* bus, uint8_t dev_addr, uint8_t* data, uint32_t len) {
    ASSERT_PARAM((bus != NULL) && ((data != NULL) || (len == 0u)));
    REQUIRE_RET((bus != NULL) && ((data != NULL) || (len == 0u)),
                SI2C_RET(RET_CLASS_PARAM, RET_R_NULL_PTR));
    ret_code_t rc = si2c_lock(bus);
    if (ret_is_err(rc)) return rc;
    ret_code_t ret = RET_OK;
    if (!i2c_start(bus)) {
        LOG_W("I2C", "I2C起始信号发送失败");
        ret = SI2C_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
        goto out;
    }
    /* 发送设备地址 + R(1) */
    if (!i2c_write_byte(bus, (uint8_t)((dev_addr << 1u) | 0x01u))) {
        i2c_stop(bus);
        LOG_W("I2C", "I2C地址没有回复");
        ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
        goto out;
    }
    /* 读取数据: 除最后一字节外均发 ACK */
    for (uint32_t i = 0; i < len; ++i) {
        const bool send_ack = (i < (len - 1u));
        if (!i2c_read_byte(bus, send_ack, &data[i])) {
            i2c_stop(bus);
            LOG_W("I2C", "I2C读取数据失败");
            ret = SI2C_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
            goto out;
        }
    }
    i2c_stop(bus);
out:
    si2c_unlock(bus);
    return ret;
}
/**
 * @brief 往指定设备的寄存器开始写数据
 * @param bus 句柄
 * @param dev_addr 设备地址
 * @param reg 寄存器地址
 * @param data 数据
 * @param len 长度
 * @return
 */
ret_code_t soft_i2c_write_reg16(soft_i2c_t* bus, uint8_t dev_addr, uint16_t reg,
                                const uint8_t* data, uint32_t len) {
    ASSERT_PARAM((bus != NULL) && ((data != NULL) || (len == 0u)));
    REQUIRE_RET((bus != NULL) && ((data != NULL) || (len == 0u)),
                SI2C_RET(RET_CLASS_PARAM, RET_R_NULL_PTR));
    ret_code_t rc = si2c_lock(bus);
    if (ret_is_err(rc)) return rc;
    ret_code_t ret = RET_OK;
    /* 1、start */
    if (!i2c_start(bus)) {
        LOG_W("I2C", "I2C起始信号发送失败");
        ret = SI2C_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
        goto out;
    }

    /* 2、发送设备地址 + W(0) */
    if (!i2c_write_byte(bus, (uint8_t)(dev_addr << 1u))) {
        i2c_stop(bus);
        ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
        goto out;
    }

    /* 3、发送 16-bit 寄存器地址 (大端: 高字节在前) */
    if (!i2c_write_byte(bus, (uint8_t)(reg >> 8u))) {
        i2c_stop(bus);
        ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
        goto out;
    }
    if (!i2c_write_byte(bus, (uint8_t)(reg & 0xFFu))) {
        i2c_stop(bus);
        ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
        goto out;
    }

    /* 发送数据 */
    for (uint32_t i = 0; i < len; ++i) {
        if (!i2c_write_byte(bus, data[i])) {
            i2c_stop(bus);
            ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
            goto out;
        }
    }

    i2c_stop(bus);
out:
    si2c_unlock(bus);
    return ret;
}
/**
 * @brief 从指定设备寄存器读取 len 字节数据
 * @param bus 句柄
 * @param dev_addr 设备地址
 * @param reg 寄存器地址
 * @param data 数据地址地址
 * @param len 长度
 * @return
 */
ret_code_t soft_i2c_read_reg16(soft_i2c_t* bus, uint8_t dev_addr, uint16_t reg, uint8_t* data,
                               uint32_t len) {
    ASSERT_PARAM((bus != NULL) && ((data != NULL) || (len == 0u)));
    REQUIRE_RET((bus != NULL) && ((data != NULL) || (len == 0u)),
                SI2C_RET(RET_CLASS_PARAM, RET_R_NULL_PTR));
    ret_code_t rc = si2c_lock(bus);
    if (ret_is_err(rc)) return rc;
    ret_code_t ret = RET_OK;

    /*  1、start */
    if (!i2c_start(bus)) {
        LOG_W("I2C", "I2C起始信号发送失败");
        ret = SI2C_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
        goto out;
    }
    /* 2、指定设备地址写 */
    if (!i2c_write_byte(bus, (uint8_t)(dev_addr << 1u))) {
        i2c_stop(bus);
        ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
        goto out;
    }
    /* 3、指定读取寄存器地址 */
    if (!i2c_write_byte(bus, (uint8_t)(reg >> 8u))) {
        i2c_stop(bus);
        ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
        goto out;
    }
    if (!i2c_write_byte(bus, (uint8_t)(reg & 0xFFu))) {
        i2c_stop(bus);
        ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
        goto out;
    }

    /* 4、start */
    if (!i2c_start(bus)) {
        ret = SI2C_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
        goto out;
    }
    /* 5、指定地址读 */
    if (!i2c_write_byte(bus, (uint8_t)((dev_addr << 1u) | 0x01u))) {
        i2c_stop(bus);
        ret = SI2C_RET(RET_CLASS_IO, RET_R_IO);
        goto out;
    }
    /* 6、读取数据 */
    for (uint32_t i = 0; i < len; ++i) {
        const bool send_ack = (i < (len - 1u));
        if (!i2c_read_byte(bus, send_ack, &data[i])) {
            i2c_stop(bus);
            ret = SI2C_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
            goto out;
        }
    }

    i2c_stop(bus);
out:
    si2c_unlock(bus);
    return ret;
}

#else /* !CFG_FEAT_SOFT_I2C */

/* 功能未启用时提供空壳实现 */
ret_code_t soft_i2c_init(soft_i2c_t* bus, const soft_i2c_cfg_t* cfg) {
    (void)bus;
    (void)cfg;
    return SI2C_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t soft_i2c_write(soft_i2c_t* bus, uint8_t dev_addr, const uint8_t* data, uint32_t len) {
    (void)bus;
    (void)dev_addr;
    (void)data;
    (void)len;
    return SI2C_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t soft_i2c_read(soft_i2c_t* bus, uint8_t dev_addr, uint8_t* data, uint32_t len) {
    (void)bus;
    (void)dev_addr;
    (void)data;
    (void)len;
    return SI2C_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t soft_i2c_write_reg16(soft_i2c_t* bus, uint8_t dev_addr, uint16_t reg,
                                const uint8_t* data, uint32_t len) {
    (void)bus;
    (void)dev_addr;
    (void)reg;
    (void)data;
    (void)len;
    return SI2C_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t soft_i2c_read_reg16(soft_i2c_t* bus, uint8_t dev_addr, uint16_t reg, uint8_t* data,
                               uint32_t len) {
    (void)bus;
    (void)dev_addr;
    (void)reg;
    (void)data;
    (void)len;
    return SI2C_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

#endif /* CFG_FEAT_SOFT_I2C */



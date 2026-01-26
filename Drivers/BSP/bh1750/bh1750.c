#include "bh1750.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bh1750_config.h"
#include "i2c.h"
#include "log.h"
#include "stm32f4xx_hal.h"
#define BH1750_ADDR ((BH1750_ADDR_GND) << 1)
/* 寄存器默认测量时间 */
#define MTreg_DEFAULT 69
/* 命令静态存储 */
static const uint8_t CMD_ARR[] = {
    BH1750_POWER_DOWN,      BH1750_POWER_ON,         BH1750_RESET,
    BH1750_CONT_HIRES_MODE, BH1750_CONT_HIRES_MODE2, BH1750_CONT_LORES_MODE,
    BH1750_ONE_HIRES_MODE,  BH1750_ONE_HIRES_MODE2,  BH1750_ONE_LORES_MODE};
/* 命令名称 */
static const char *const cmd_name[] = {"BH1750_POWER_DOWN",
                                       "BH1750_POWER_ON",
                                       "BH1750_RESET",
                                       "BH1750_CONT_HIRES_MODE",
                                       "BH1750_CONT_HIRES_MODE2",
                                       "BH1750_CONT_LORES_MODE",
                                       "BH1750_ONE_HIRES_MODE",
                                       "BH1750_ONE_HIRES_MODE2",
                                       "BH1750_ONE_LORES_MODE"};

/* 命令索引 */
typedef enum {
  POWER_DOWN = 0,
  POWER_ON,
  POWER_RESET,
  CONT_HIRES_MODE,
  CONT_HIRES_MODE2,
  CONT_LORES_MODE,
  ONE_HIRES_MODE,
  ONE_HIRES_MODE2,
  ONE_LORES_MODE
} CMD_IND;

/* 发送状态 */
typedef enum {
  BH1750_TX_IDLE = 0,
  BH1750_TX_BUSY,
  BH1750_TX_OK,
  BH1750_TX_ERR,
} bh1750_tx_state_t;
/* 运行时配置 */
typedef struct {
  volatile float precision;
  volatile uint16_t MTreg_Current;
} bh1750_config_t;
/* DMA发送全局状态结构体 */
typedef struct {
  volatile bh1750_tx_state_t tx_state;
  volatile uint32_t last_error;
  volatile uint8_t last_cmd;
} bh1750_ctx_t;
/* DMA发送全局状态 */
static bh1750_ctx_t g_bh1750 = {
    .tx_state = BH1750_TX_IDLE,
    .last_error = 0,
    .last_cmd = 0,
};

/* 全局配置默认值 */
static bh1750_config_t g_bh1750_config = {.precision = 1.0f,
                                          .MTreg_Current = MTreg_DEFAULT};
/**
 * @brief 发送
 * @param cmd 命令索引
 * @return 状态
 */
static inline HAL_StatusTypeDef BH1750_SendCommand_DMA(CMD_IND cmd) {
  /* 越界检查 */
  if ((unsigned)cmd >= (sizeof(CMD_ARR) / sizeof(CMD_ARR[0]))) {
    return HAL_ERROR;
  }
  /* 设置状态 */
  g_bh1750.last_cmd = CMD_ARR[cmd];
  g_bh1750.last_error = 0;
  g_bh1750.tx_state = BH1750_TX_BUSY;

  /* 发送命令 */
  const HAL_StatusTypeDef st = HAL_I2C_Master_Transmit_DMA(
      &hi2c1, BH1750_ADDR, (uint8_t *)&g_bh1750.last_cmd, 1);
  if (st != HAL_OK) {
    g_bh1750.tx_state = BH1750_TX_ERR;
    g_bh1750.last_error = hi2c1.ErrorCode;
  }
  return st;
}
/**
 * @brief 发送命令并检查发送函数是否成功
 * @param cmd 命令索引
 */
static inline void BH1750_SendAndCheck(CMD_IND cmd) {
  if (BH1750_SendCommand_DMA(cmd) != HAL_OK) {
    LOG_D("BH1750","发送失败\r\n");
  }
}
/**
 * @brief 查询返回当前命令全局状态
 * @return 返回命令发送的状态
 */
bh1750_tx_state_t BH1750_GetTxState(void) { return g_bh1750.tx_state; }
/**
 * @brief 发生错误查询返回状态错误码
 * @return 返回HAl库的错误码
 */
uint32_t BH1750_GetLastError(void) { return g_bh1750.last_error; }

/**
 * @brief 用于判断 BH1750_TX_BUSY 状态
 * @param timeout_ms 判断超时时间
 * @return 是否超时
 */
static bool BH1750_WaitTx(uint32_t timeout_ms) {
  const uint32_t t0 = HAL_GetTick();
  while (BH1750_GetTxState() == BH1750_TX_BUSY) {
    if (HAL_GetTick() - t0 > timeout_ms)
      return false;
  }
  return true;
}

/**
 *
 * @param index 处理命令发送过后回调返回的全局状态
 * @param isAuto 区分打印信息名称从哪里获取
 * @param name  手动填入的信息
 */
static inline bool send_command_callback(CMD_IND index, bool isAuto,
                                         const char *name) {
  /* 判断打印信息来源 */
  const char *tag = isAuto ? cmd_name[index] : name;
  /* NULL 检查 */
  if (!tag) {
    tag = "UNKNOWN";
  }

  /* 繁忙检查 */
  if (!BH1750_WaitTx(50)) {
    if (BH1750_GetTxState() == BH1750_TX_BUSY) {
      LOG_D("BH1750","%s命令发送超时(仍BUSY)\r\n", tag);
    }
    return false;
  }
  /* 根据返回状态 打印信息*/
  switch (BH1750_GetTxState()) {
  case BH1750_TX_OK:
    LOG_D("BH1750","%s命令发送成功\r\n", tag);
    return true;
    break;
  default:
    LOG_D("BH1750","%s命令发送失败 err=0x%08lx\r\n", tag, BH1750_GetLastError());
    return false;
  }
}

/**
 * @brief 初始化检查BH1750 是否成功连接
 */
void BH1750_Init(void) {
  if (HAL_I2C_IsDeviceReady(&hi2c1, BH1750_ADDR, 3, 10) != HAL_OK) {
    LOG_D("BH1750","BH1750响应失败\r\n");
    return;
  }
  BH1750_PowerOn();
  BH1750_Set_MTREG_H(MTreg_DEFAULT);
  BH1750_Set_MTREG_L(MTreg_DEFAULT);
  LOG_D("BH1750","BH1750响应成功\r\n");
}

/**
 * @brief 发送重置命令
 */
void BH1750_Reset(void) {
  BH1750_SendAndCheck(POWER_RESET);
  send_command_callback(POWER_RESET, 1, NULL);
}
/**
 * @brief 发送 关机命令
 */
void BH1750_PowerDown(void) {
  BH1750_SendAndCheck(POWER_DOWN);
  send_command_callback(POWER_DOWN, 1, NULL);
}
/**
 * @brief 发送上电命令
 */
void BH1750_PowerOn(void) {
  BH1750_SendAndCheck(POWER_ON);
  send_command_callback(POWER_ON, 1, NULL);
}
/**
 * @brief 设置为高精度连续采集模式
 * @note 1lx精度 120ms采集时间
 */
void BH1750_Set_CONT_HIRES_MODE(void) {
  BH1750_SendAndCheck(CONT_HIRES_MODE);
  send_command_callback(CONT_HIRES_MODE, 1, NULL);
  g_bh1750_config.precision = 1.0f;
}
/**
 * @brief 设置为高精度连续采集模式2
 * @note 0.5lx精度 120ms采集时间
 */
void BH1750_Set_CONT_HIRES_MODE2(void) {
  BH1750_SendAndCheck(CONT_HIRES_MODE2);
  send_command_callback(CONT_HIRES_MODE2, 1, NULL);
  g_bh1750_config.precision = 0.5f;
}
/**
 * @brief 持续低精度采集模式
 * @note 4lx精度 16ms采集时间
 */
void BH1750_Set_CONT_LORES_MODE(void) {
  BH1750_SendAndCheck(CONT_LORES_MODE);
  send_command_callback(CONT_LORES_MODE, 1, NULL);
  g_bh1750_config.precision = 4.0f;
}
/**
 * @brief 一次高精度连续采集模式
 * @note 1lx精度 120ms采集时间
 *       采集后会进入 PowerDown
 */
void BH1750_Set_ONE_HIRES_MODE(void) {
  BH1750_SendAndCheck(ONE_HIRES_MODE);
  send_command_callback(ONE_HIRES_MODE, 1, NULL);
  g_bh1750_config.precision = 1.0f;
}
/**
 * @brief 一次高精度连续采集模式2
 * @note 0.5lx精度 120ms采集时间
 *       采集后会进入 PowerDown
 */
void BH1750_Set_ONE_HIRES_MODE2(void) {
  BH1750_SendAndCheck(ONE_HIRES_MODE2);
  send_command_callback(ONE_HIRES_MODE2, 1, NULL);
  g_bh1750_config.precision = 0.5f;
}
/**
 * @brief 一次低精度采集模式
 * @note 4lx精度 16ms采集时间
 *       采集后会进入 PowerDown
 */
void BH1750_Set_ONE_LORES_MODE(void) {
  BH1750_SendAndCheck(ONE_LORES_MODE);
  send_command_callback(ONE_LORES_MODE, 1, NULL);
  g_bh1750_config.precision = 4.0f;
}

/**
 * @brief 设置采集时间的高3位
 * @param time 采集时间
 */
void BH1750_Set_MTREG_H(uint8_t time) {
  if (time < 31 || time > 254) {
    LOG_D("BH1750","MTreg out of range\r\n");
    return;
  }
  /* 更新全局状态 */
  g_bh1750.last_cmd = BH1750_CMD_MTREG_H(time);
  g_bh1750.last_error = 0;
  g_bh1750.tx_state = BH1750_TX_BUSY;

  const HAL_StatusTypeDef st = HAL_I2C_Master_Transmit_DMA(
      &hi2c1, BH1750_ADDR, (uint8_t *)&g_bh1750.last_cmd, 1);
  if (st != HAL_OK) {
    g_bh1750.tx_state = BH1750_TX_ERR;
    g_bh1750.last_error = hi2c1.ErrorCode;
    LOG_D("BH1750","设置采集时间命令发送失败H st=%d err=0x%08lx\r\n", (int)st,
           BH1750_GetLastError());
    return;
  }
  if (send_command_callback(ONE_LORES_MODE, 0, "BH1750_CMD_MTREG_H")) {
    /* 更新测量时间 */
    g_bh1750_config.MTreg_Current = time;
  }
}

/**
 * @brief 设置采集时间的低5位
 * @param time 采集时间
 */
void BH1750_Set_MTREG_L(uint8_t time) {
  if (time < 31 || time > 254) {
    LOG_D("BH1750","MTreg out of range\r\n");
    return;
  }

  /* 更新全局状态 */
  g_bh1750.last_cmd = BH1750_CMD_MTREG_L(time);
  g_bh1750.last_error = 0;
  g_bh1750.tx_state = BH1750_TX_BUSY;

  const HAL_StatusTypeDef st = HAL_I2C_Master_Transmit_DMA(
      &hi2c1, BH1750_ADDR, (uint8_t *)&g_bh1750.last_cmd, 1);
  if (st != HAL_OK) {
    g_bh1750.tx_state = BH1750_TX_ERR;
    g_bh1750.last_error = hi2c1.ErrorCode;
    LOG_D("BH1750","设置采集时间命令发送失败H st=%d err=0x%08lx\r\n", (int)st,
           BH1750_GetLastError());
    return;
  }

  if (send_command_callback(ONE_LORES_MODE, 0, "BH1750_CMD_MTREG_L")) {
    /* 更新测量时间 */
    g_bh1750_config.MTreg_Current = time;
  }
}

/**
 * @brief  获取光照LX
 * @return 光照LX
 * @note 低精度至高采集时间24ms 高精度180ms
 */
float BH1750_Get_LX(void) {
  uint8_t lx[2] = {0};
  HAL_I2C_Master_Receive(&hi2c1, BH1750_ADDR, lx, 2, 1000);
  return (float)(lx[0] << 8 | lx[1]) / 1.2f *
         ((float)69 / (float)g_bh1750_config.MTreg_Current) *
         g_bh1750_config.precision;
}
/* =========================================== HAL 库回调
 * =========================================== */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c) {
  if (hi2c->Instance == I2C1) {
    g_bh1750.tx_state = BH1750_TX_OK;
  }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
  if (hi2c->Instance == I2C1) {
    g_bh1750.last_error = hi2c->ErrorCode;
    g_bh1750.tx_state = BH1750_TX_ERR;
  }
}
#ifndef BH1750_BH1750_CONFIG_H
#define BH1750_BH1750_CONFIG_H

/* 运算工具 */
#define BH1750_GET_MASK(bits) (((1U) << (bits)) - 1U)
/* 模块命令宏定义 */
#define BH1750_POWER_DOWN 0x00 /* 关机 无活动状态*/
#define BH1750_POWER_ON 0x01   /* 开机 等待测量命令*/
#define BH1750_RESET 0x07      /* 重置 数据寄存器的值（非PowerDown 状态下）*/
#define BH1750_CONT_HIRES_MODE                                                 \
  0x10 /* 持续高精度采集模式 1lx精度 120ms采集时间 */
#define BH1750_CONT_HIRES_MODE2                                                \
  0x11 /* 持续高精度采集模式2 0.5lx精度 120ms采集时间 */
#define BH1750_CONT_LORES_MODE                                                 \
  0x13 /* 持续低精度采集模式 4lx精度 16ms采集时间 */
#define BH1750_ONE_HIRES_MODE                                                  \
  0x20 /* 单次高精度采集模式 1lx精度 120ms采集时间 自动关机*/
#define BH1750_ONE_HIRES_MODE2                                                 \
  0x21 /* 单次高精度采集模式 0.5lx精度 120ms采集时间 自动关机*/
#define BH1750_ONE_LORES_MODE                                                  \
  0x23 /* 单次低精度采集模式 4lx精度 16ms采集时间 自动关机*/
/**
 * 修改测量时间
 * MTreg 默认是 69 (0x45)
 * 范围是 31 ~ 254
 */
/* 高 3 位指令格式: 010000_MT[7:5] -> 0x40 | (MT >> 5) */
#define BH1750_CMD_MTREG_H(mt) (0x40 | (((mt) >> 5) & BH1750_GET_MASK(3)))
/* 低 5 位指令格式: 011_MT[4:0] -> 0x60 | (MT & 0x1F) */
#define BH1750_CMD_MTREG_L(mt) (0x60 | ((mt) & BH1750_GET_MASK(5)))

/* BH1750 7位地址 */
#define BH1750_ADDR_GND 0x23
#define BH1750_ADDR_VCC 0x5C


#endif

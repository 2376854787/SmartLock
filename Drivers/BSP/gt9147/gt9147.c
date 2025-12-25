#include "gt9147.h"
#include "main.h"
#include <stdio.h>

#define GT9147_REG_PRODUCT_ID   0x8140
#define GT9147_REG_STATUS       0x814E
#define GT9147_REG_FIRST_POINT  0x8150
#define GT9147_I2C_DELAY_CYCLES 800

static void gt9147_delay(void)
{
    for (volatile uint32_t i = 0; i < GT9147_I2C_DELAY_CYCLES; i++) {
        __NOP();
    }
}

static uint8_t gt9147_addr = GT9147_I2C_ADDR;

static void gt9147_scl_high(void);
static void gt9147_scl_low(void);
static void gt9147_sda_high(void);
static void gt9147_sda_low(void);
static void gt9147_i2c_start(void);
static void gt9147_i2c_stop(void);
static bool gt9147_i2c_write_byte(uint8_t data);

static void gt9147_bus_recover(void)
{
    gt9147_sda_high();
    for (uint8_t i = 0; i < 9; i++) {
        gt9147_scl_high();
        gt9147_delay();
        gt9147_scl_low();
        gt9147_delay();
    }
    gt9147_i2c_stop();
}

static bool gt9147_probe(uint8_t addr)
{
    bool ok;

    gt9147_i2c_start();
    ok = gt9147_i2c_write_byte((addr << 1) | 0x00);
    gt9147_i2c_stop();
    return ok;
}

static void gt9147_scl_high(void)
{
    HAL_GPIO_WritePin(GT9147_SCL_PORT, GT9147_SCL_PIN, GPIO_PIN_SET);
}

static void gt9147_scl_low(void)
{
    HAL_GPIO_WritePin(GT9147_SCL_PORT, GT9147_SCL_PIN, GPIO_PIN_RESET);
}

static void gt9147_sda_high(void)
{
    HAL_GPIO_WritePin(GT9147_SDA_PORT, GT9147_SDA_PIN, GPIO_PIN_SET);
}

static void gt9147_sda_low(void)
{
    HAL_GPIO_WritePin(GT9147_SDA_PORT, GT9147_SDA_PIN, GPIO_PIN_RESET);
}

static GPIO_PinState gt9147_sda_read(void)
{
    return HAL_GPIO_ReadPin(GT9147_SDA_PORT, GT9147_SDA_PIN);
}

static void gt9147_i2c_start(void)
{
    gt9147_sda_high();
    gt9147_scl_high();
    gt9147_delay();
    gt9147_sda_low();
    gt9147_delay();
    gt9147_scl_low();
}

static void gt9147_i2c_stop(void)
{
    gt9147_sda_low();
    gt9147_delay();
    gt9147_scl_high();
    gt9147_delay();
    gt9147_sda_high();
    gt9147_delay();
}

static bool gt9147_i2c_wait_ack(void)
{
    bool ack;

    gt9147_sda_high();
    gt9147_delay();
    gt9147_scl_high();
    gt9147_delay();
    ack = (gt9147_sda_read() == GPIO_PIN_RESET);
    gt9147_scl_low();
    return ack;
}

static bool gt9147_i2c_write_byte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) {
            gt9147_sda_high();
        } else {
            gt9147_sda_low();
        }
        gt9147_delay();
        gt9147_scl_high();
        gt9147_delay();
        gt9147_scl_low();
        data <<= 1;
    }
    return gt9147_i2c_wait_ack();
}

static uint8_t gt9147_i2c_read_byte(bool ack)
{
    uint8_t data = 0;

    gt9147_sda_high();
    for (uint8_t i = 0; i < 8; i++) {
        data <<= 1;
        gt9147_scl_high();
        gt9147_delay();
        if (gt9147_sda_read() == GPIO_PIN_SET) {
            data |= 0x01;
        }
        gt9147_scl_low();
        gt9147_delay();
    }

    if (ack) {
        gt9147_sda_low();
    } else {
        gt9147_sda_high();
    }
    gt9147_delay();
    gt9147_scl_high();
    gt9147_delay();
    gt9147_scl_low();
    gt9147_sda_high();

    return data;
}

static bool gt9147_write_reg(uint16_t reg, const uint8_t *buf, uint16_t len)
{
    gt9147_i2c_start();
    if (!gt9147_i2c_write_byte((gt9147_addr << 1) | 0x00)) {
        gt9147_i2c_stop();
        return false;
    }
    if (!gt9147_i2c_write_byte((uint8_t)(reg >> 8)) ||
        !gt9147_i2c_write_byte((uint8_t)(reg & 0xFF))) {
        gt9147_i2c_stop();
        return false;
    }
    for (uint16_t i = 0; i < len; i++) {
        if (!gt9147_i2c_write_byte(buf[i])) {
            gt9147_i2c_stop();
            return false;
        }
    }
    gt9147_i2c_stop();
    return true;
}

static bool gt9147_read_reg(uint16_t reg, uint8_t *buf, uint16_t len)
{
    gt9147_i2c_start();
    if (!gt9147_i2c_write_byte((gt9147_addr << 1) | 0x00)) {
        gt9147_i2c_stop();
        return false;
    }
    if (!gt9147_i2c_write_byte((uint8_t)(reg >> 8)) ||
        !gt9147_i2c_write_byte((uint8_t)(reg & 0xFF))) {
        gt9147_i2c_stop();
        return false;
    }
    gt9147_i2c_start();
    if (!gt9147_i2c_write_byte((gt9147_addr << 1) | 0x01)) {
        gt9147_i2c_stop();
        return false;
    }
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = gt9147_i2c_read_byte(i < (len - 1));
    }
    gt9147_i2c_stop();
    return true;
}

static void gt9147_gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin = GT9147_SCL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GT9147_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GT9147_SDA_PIN;
    HAL_GPIO_Init(GT9147_SDA_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GT9147_RST_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GT9147_RST_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GT9147_INT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GT9147_INT_PORT, &GPIO_InitStruct);

    gt9147_scl_high();
    gt9147_sda_high();
}

static void gt9147_hw_reset(uint8_t addr)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = GT9147_INT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GT9147_INT_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(GT9147_RST_PORT, GT9147_RST_PIN, GPIO_PIN_RESET);
    if (addr == 0x5D) {
        HAL_GPIO_WritePin(GT9147_INT_PORT, GT9147_INT_PIN, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(GT9147_INT_PORT, GT9147_INT_PIN, GPIO_PIN_RESET);
    }

    HAL_Delay(10);
    HAL_GPIO_WritePin(GT9147_RST_PORT, GT9147_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(50);

    GPIO_InitStruct.Pin = GT9147_INT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GT9147_INT_PORT, &GPIO_InitStruct);

    HAL_Delay(50);
}

static void gt9147_map_xy(uint16_t *x, uint16_t *y)
{
    uint16_t tx = *x;
    uint16_t ty = *y;

#if GT9147_SWAP_XY
    uint16_t t = tx;
    tx = ty;
    ty = t;
#endif

#if GT9147_INVERT_X
    if (tx < GT9147_MAX_WIDTH) {
        tx = (uint16_t)(GT9147_MAX_WIDTH - 1U - tx);
    }
#endif

#if GT9147_INVERT_Y
    if (ty < GT9147_MAX_HEIGHT) {
        ty = (uint16_t)(GT9147_MAX_HEIGHT - 1U - ty);
    }
#endif

    if (tx >= GT9147_MAX_WIDTH) {
        tx = (uint16_t)(GT9147_MAX_WIDTH - 1U);
    }
    if (ty >= GT9147_MAX_HEIGHT) {
        ty = (uint16_t)(GT9147_MAX_HEIGHT - 1U);
    }

    *x = tx;
    *y = ty;
}

bool gt9147_init(void)
{
    uint8_t id[4] = {0};
    uint8_t alt_addr = (gt9147_addr == 0x5D) ? 0x14 : 0x5D;
    bool ack0;
    bool ack1;

    gt9147_gpio_init();
    gt9147_bus_recover();

    ack0 = gt9147_probe(gt9147_addr);
    ack1 = gt9147_probe(alt_addr);
    printf("[GT9147] probe 0x%02X=%s 0x%02X=%s\r\n",
           gt9147_addr, ack0 ? "ACK" : "NACK",
           alt_addr, ack1 ? "ACK" : "NACK");

    gt9147_hw_reset(gt9147_addr);

    if (!gt9147_read_reg(GT9147_REG_PRODUCT_ID, id, sizeof(id)) ||
        (id[0] == 0x00U) || (id[0] == 0xFFU)) {
        printf("[GT9147] ID read failed at 0x%02X\r\n", gt9147_addr);
        gt9147_addr = alt_addr;
        gt9147_hw_reset(gt9147_addr);
        if (!gt9147_read_reg(GT9147_REG_PRODUCT_ID, id, sizeof(id)) ||
            (id[0] == 0x00U) || (id[0] == 0xFFU)) {
            printf("[GT9147] ID read failed at 0x%02X\r\n", gt9147_addr);
            return false;
        }
    }

    printf("[GT9147] ID=%c%c%c%c addr=0x%02X\r\n",
           (char)id[0], (char)id[1], (char)id[2], (char)id[3], gt9147_addr);

    return true;
}

bool gt9147_read_point(uint16_t *x, uint16_t *y, bool *pressed)
{
    uint8_t status = 0;
    uint8_t buf[8];
    uint8_t clear = 0;
    uint8_t points;

    if (!gt9147_read_reg(GT9147_REG_STATUS, &status, 1)) {
        return false;
    }

    if ((status & 0x80U) == 0U) {
        *pressed = false;
        return true;
    }

    points = status & 0x0FU;
    if (points == 0U || points > 5U) {
        *pressed = false;
        gt9147_write_reg(GT9147_REG_STATUS, &clear, 1);
        return true;
    }

    if (!gt9147_read_reg(GT9147_REG_FIRST_POINT, buf, sizeof(buf))) {
        return false;
    }

    /* buf[0] is track ID, coordinates start at buf[1] */
    *x = (uint16_t)(buf[1] | ((uint16_t)buf[2] << 8));
    *y = (uint16_t)(buf[3] | ((uint16_t)buf[4] << 8));
    gt9147_map_xy(x, y);

    *pressed = true;
    gt9147_write_reg(GT9147_REG_STATUS, &clear, 1);
    return true;
}

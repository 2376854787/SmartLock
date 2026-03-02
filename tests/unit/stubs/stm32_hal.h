#ifndef TEST_STM32_HAL_H
#define TEST_STM32_HAL_H

#ifndef __WEAK
#define __WEAK
#endif

typedef int IRQn_Type;

typedef struct {
    void *Instance;
    void *hdmatx;
    void *hdmarx;
    unsigned int ErrorCode;
} I2C_HandleTypeDef;

typedef struct {
    void *Instance;
    void *hdmatx;
    void *hdmarx;
    unsigned int ErrorCode;
} SPI_HandleTypeDef;

typedef struct {
    void *Parent;
} DMA_HandleTypeDef;

void HAL_SuspendTick(void);
void HAL_ResumeTick(void);

#endif /* TEST_STM32_HAL_H */

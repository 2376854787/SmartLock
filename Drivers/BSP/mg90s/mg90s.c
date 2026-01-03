
#include "mg90s.h"
#include "tim.h" /* Reuse generic TIM includes or HAL */

/*
 * Configuration:
 * TIM3, Channel 1, PC6
 * Timer Clock: Assuming 84MHz (APB1 x2)
 * Prescaler: 84-1 => 1MHz counter (1us tick)
 * Period: 20000-1 => 20ms (50Hz)
 */

static TIM_HandleTypeDef htim3_mg90s;

void MX_TIM3_Init_Custom(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* MSP Init directly here to avoid external dependency issues if not generated */
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /**TIM3 GPIO Configuration
    PC6     ------> TIM3_CH1
    */
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    htim3_mg90s.Instance = TIM3;
    htim3_mg90s.Init.Prescaler = 84 - 1; /* 1MHz */
    htim3_mg90s.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3_mg90s.Init.Period = 20000 - 1; /* 20ms */
    htim3_mg90s.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3_mg90s.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&htim3_mg90s) != HAL_OK)
    {
        Error_Handler();
    }
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim3_mg90s, &sClockSourceConfig) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_PWM_Init(&htim3_mg90s) != HAL_OK)
    {
        Error_Handler();
    }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3_mg90s, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* Config Channel 1 */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 1500; /* Initial middle position */
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3_mg90s, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }


}

void mg90s_init(void)
{
    //MX_TIM3_Init_Custom();
    // MX_TIM3_Init(); /* Already initialized in main.c */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    
    /* Initialize to Lock position */
    mg90s_lock();
}

void mg90s_lock(void)
{
    /* 0 degree -> 0.5ms = 500us */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 500);
}

void mg90s_unlock(void)
{
    /* 90 degree -> 1.5ms = 1500us */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 1500);
}

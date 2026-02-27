
#ifndef SMARTLOCK_BOARD_GPIO_IDS_H
#define SMARTLOCK_BOARD_GPIO_IDS_H
/* 定义板级 ID */
enum {
    HAL_GPIO_ID_CT_SCL = 0, /**< GT911 SCL  → PB0  */
    HAL_GPIO_ID_CT_SDA = 1, /**< GT911 SDA  → PF11 */
    HAL_GPIO_ID_CT_RST = 2, /**< GT911 RST  → PC13 */
    HAL_GPIO_ID_CT_INT = 3, /**< GT911 INT  → PB1  */
    HAL_GPIO_ID_RC522_CS  = 4, /**< RC522 CS   → PA4  */
    HAL_GPIO_ID_RC522_RST = 5, /**< RC522 RST  → PC4  */
    HAL_GPIO_ID_RC522_IRQ = 6, /**< RC522 IRQ  → PC5  */
};

#endif  // SMARTLOCK_BOARD_GPIO_IDS_H

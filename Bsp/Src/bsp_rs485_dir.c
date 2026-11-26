/**
 * @file bsp_rs485_dir.c
 * @brief 控制台 RS485 方向适配，支持硬件自动方向和经确认的 GPIO 手动方向。
 */

#include "bsp_rs485_dir.h"
#include "bsp_config.h"
#include "stm32h7xx_hal.h"

/* 宏定义与编译期检查 */

#if BSP_RS485_DIRECTION_MODE == BSP_RS485_MANUAL
#if BSP_RS485_MANUAL_WIRING_CONFIRMED != 1
#error Confirm automatic circuit isolation, voltage domain, pin and polarity before manual control
#endif
#if !defined(BSP_RS485_GPIO_PORT) || !defined(BSP_RS485_GPIO_PIN) ||                               \
    !defined(BSP_RS485_GPIO_CLOCK_ENABLE) || !defined(BSP_RS485_TX_LEVEL) ||                       \
    !defined(BSP_RS485_RX_LEVEL)
#error Manual mode requires explicit board GPIO and polarity definitions
#endif
typedef char BspRs485DirLevelsMustDiffer[(BSP_RS485_TX_LEVEL != BSP_RS485_RX_LEVEL) ? 1 : -1];
typedef char BspRs485DirExactlyOnePin
    [((BSP_RS485_GPIO_PIN != 0U) && ((BSP_RS485_GPIO_PIN & (BSP_RS485_GPIO_PIN - 1U)) == 0U)) ? 1
                                                                                              : -1];
#endif

/* 公共接口 */

void BspRs485Dir_Init(void)
{
#if BSP_RS485_DIRECTION_MODE == BSP_RS485_MANUAL
    GPIO_InitTypeDef gpio = {0};
    BSP_RS485_GPIO_CLOCK_ENABLE();
    /* 先设为接收电平，再切换引脚为输出，避免产生短暂发送状态。 */
    HAL_GPIO_WritePin(BSP_RS485_GPIO_PORT, BSP_RS485_GPIO_PIN, BSP_RS485_RX_LEVEL);
    gpio.Pin = BSP_RS485_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BSP_RS485_GPIO_PORT, &gpio);
#endif
}

void BspRs485Dir_Transmit(void)
{
#if BSP_RS485_DIRECTION_MODE == BSP_RS485_MANUAL
    HAL_GPIO_WritePin(BSP_RS485_GPIO_PORT, BSP_RS485_GPIO_PIN, BSP_RS485_TX_LEVEL);
#if BSP_RS485_SETUP_MS > 0
    HAL_Delay(BSP_RS485_SETUP_MS);
#endif
#endif
}

void BspRs485Dir_Receive(void)
{
#if BSP_RS485_DIRECTION_MODE == BSP_RS485_MANUAL
#if BSP_RS485_HOLD_MS > 0
    HAL_Delay(BSP_RS485_HOLD_MS);
#endif
    HAL_GPIO_WritePin(BSP_RS485_GPIO_PORT, BSP_RS485_GPIO_PIN, BSP_RS485_RX_LEVEL);
#endif
}

const char *BspRs485Dir_Name(void)
{
#if BSP_RS485_DIRECTION_MODE == BSP_RS485_MANUAL
    return "MANUAL_CONFIRMED";
#elif BSP_RS485_AUTO_ASSEMBLY_CONFIRMED == 1
    return "AUTO_CONFIRMED";
#else
    return "AUTO_UNCONFIRMED";
#endif
}

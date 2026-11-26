/**
 * @file bsp_uart_irq.c
 * @brief 统一 UART 回调分发，按外设转交接收、发送完成和错误事件。
 */

#include "bsp_dtu.h"
#include "bsp_weather_rs485.h"
#include "usart.h"

/* 中断与回调 */

/**
 * @brief 将 USART3、UART4 和 USART6 的接收完成分发给对应 BSP。
 * @param uart 触发回调的 UART 句柄。
 * @note 由 HAL 在中断上下文调用。
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart3)
    {
        BspWeatherRs485_RxCompleteIsr(BSP_WEATHER_RS485_MET);
    }
    else if (uart == &huart4)
    {
        BspWeatherRs485_RxCompleteIsr(BSP_WEATHER_RS485_GAS);
    }
    else if (uart == &huart6)
    {
        BspDtu_RxCompleteIsr();
    }
}

/**
 * @brief 将 USART6 的发送完成通知 DTU。
 * @param uart 触发回调的 UART 句柄。
 * @note 由 HAL 在中断上下文调用。
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart6)
    {
        BspDtu_TxCompleteIsr();
    }
}

/**
 * @brief 按 UART 路由转发 HAL 错误码，供对应模块恢复。
 * @param uart 触发回调的 UART 句柄。
 * @note 由 HAL 在中断上下文调用。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart3)
    {
        BspWeatherRs485_ErrorIsr(BSP_WEATHER_RS485_MET, HAL_UART_GetError(uart));
    }
    else if (uart == &huart4)
    {
        BspWeatherRs485_ErrorIsr(BSP_WEATHER_RS485_GAS, HAL_UART_GetError(uart));
    }
    else if (uart == &huart6)
    {
        BspDtu_ErrorIsr(HAL_UART_GetError(uart));
    }
}

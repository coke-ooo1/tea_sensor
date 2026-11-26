/**
 * @file bsp_weather_rs485.h
 * @brief 气象 RS485 串口适配，管理独立接收环、逐字节时间和链路恢复。
 */

#ifndef BSP_WEATHER_RS485_H
#define BSP_WEATHER_RS485_H
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    BSP_WEATHER_RS485_MET = 0,
    BSP_WEATHER_RS485_GAS,
    BSP_WEATHER_RS485_PORT_COUNT
} BspWeatherRs485Port;

typedef enum
{
    BSP_WEATHER_RS485_OK = 0,
    BSP_WEATHER_RS485_UART_ERROR,
    BSP_WEATHER_RS485_UART_BUSY,
    BSP_WEATHER_RS485_UART_TIMEOUT,
    BSP_WEATHER_RS485_RX_NOT_READY,
    BSP_WEATHER_RS485_IRQ_DISABLED,
    BSP_WEATHER_RS485_BAD_ARGUMENT
} BspWeatherRs485Status;

typedef struct
{
    uint8_t byte;
    uint32_t t_ms;
} BspWeatherRs485Byte;

typedef struct
{
    size_t length;
    uint8_t data_lost;
    uint32_t uart_error;
    BspWeatherRs485Status lost_status;
} BspWeatherRs485ReadResult;

typedef struct
{
    uint32_t rx_bytes;
    uint32_t rx_overflows;
    uint32_t rx_errors;
    uint32_t rx_start_errors;

    uint32_t tx_attempts;
    uint32_t tx_errors;

    uint32_t last_uart_error;
    uint32_t last_rx_ms;

    uint8_t rx_seen;
    uint8_t rx_active;
    BspWeatherRs485Status last_rx_status;
    BspWeatherRs485Status last_tx_status;
} BspWeatherRs485Diagnostics;

/**
 * @brief 初始化气象端口并挂接中断接收。
 * @param port 气象 RS485 端口。
 * @return 初始化结果。
 */
BspWeatherRs485Status BspWeatherRs485_Init(BspWeatherRs485Port port);

/**
 * @brief 推进端口的异步接收恢复并处理失步。
 * @param port 需要处理的端口。
 */
void BspWeatherRs485_Process(BspWeatherRs485Port port);

/**
 * @brief 清理旧接收数据、挂接字节中断后执行限时发送。
 * @param port 目标端口。
 * @param data 待发送数据。
 * @param length 发送字节数。
 * @return 发送或接收准备阶段的状态。
 */
BspWeatherRs485Status BspWeatherRs485_Send(
    BspWeatherRs485Port port, const uint8_t *data, size_t length);

/**
 * @brief 从气象端口读取带接收时间的字节。
 * @param port 读取端口。
 * @param data 输出数组。
 * @param capacity 输出数组的元素容量。
 * @return 长度、丢失标记和 UART 错误。
 * @note 每次最多取 16 字节，丢失时清空积压数据。
 */
BspWeatherRs485ReadResult BspWeatherRs485_Read(
    BspWeatherRs485Port port, BspWeatherRs485Byte *data, size_t capacity);

/**
 * @brief 读取指定端口的 UART 诊断。
 * @param port 查询端口。
 * @return 短临界区内复制的快照。
 */
BspWeatherRs485Diagnostics BspWeatherRs485_GetDiagnostics(BspWeatherRs485Port port);
/* 以下两个 ISR 接口只供 BSP UART 分发模块调用。 */

/**
 * @brief 在 UART 接收回调中更新接收环和时间戳。
 * @param port 回调分发选定的端口。
 */
void BspWeatherRs485_RxCompleteIsr(BspWeatherRs485Port port);

/**
 * @brief 在 UART 错误回调中记录错误并安排后续恢复。
 * @param port 回调分发选定的端口。
 * @param error HAL UART 错误位掩码。
 */
void BspWeatherRs485_ErrorIsr(BspWeatherRs485Port port, uint32_t error);

/**
 * @brief 读取气象串口状态的固定名称。
 * @param status 状态枚举。
 * @return 只读名称字符串。
 */
const char *BspWeatherRs485_StatusName(BspWeatherRs485Status status);
#endif

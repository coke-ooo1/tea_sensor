/**
 * @file bsp_console.h
 * @brief USART2 诊断控制台，保留 RS485 方向处理和 Arm C 库重定向。
 */

#ifndef BSP_CONSOLE_H
#define BSP_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    BSP_CONSOLE_OK = 0,
    BSP_CONSOLE_BAD_LENGTH,
    BSP_CONSOLE_UART_ERROR,
    BSP_CONSOLE_UART_BUSY,
    BSP_CONSOLE_UART_TIMEOUT,
    BSP_CONSOLE_TC_TIMEOUT,
    BSP_CONSOLE_DRAIN_PENDING
} BspConsoleStatus;

typedef struct
{
    uint32_t tx_attempts;
    uint32_t tx_errors;
    BspConsoleStatus last_tx_status;
    uint8_t stdout_unbuffered;
} BspConsoleDiagnostics;

/**
 * @brief 初始化 USART2 控制台、RS485 方向和标准输出缓冲模式。
 * @note 控制台使用 PD5/PD6，115200 8N1。
 */
void BspConsole_Init(void);

/**
 * @brief 轮询发送完成并在需要时恢复 RS485 接收方向。
 */
void BspConsole_Process(void);

/**
 * @brief 以有界等待通过控制台发送字节。
 * @param data 待发送数据。
 * @param length 发送字节数。
 * @return 发送结果或等待超时状态。
 */
BspConsoleStatus BspConsole_Send(const char *data, size_t length);

/**
 * @brief 读取控制台传输诊断，计数包括 C 库标准输出。
 * @return 模块持有的只读诊断指针。
 */
const BspConsoleDiagnostics *BspConsole_GetDiagnostics(void);

/**
 * @brief 读取控制台发送状态的固定名称。
 * @param status 控制台状态。
 * @return 只读名称字符串。
 */
const char *BspConsole_StatusName(BspConsoleStatus status);

#endif

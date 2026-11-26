/**
 * @file bsp_dtu.h
 * @brief USART6 DTU 传输，管理中断收发、缓冲区所有权和错误恢复。
 */

#ifndef BSP_DTU_H
#define BSP_DTU_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    BSP_DTU_OK = 0,
    BSP_DTU_BAD_LENGTH,
    BSP_DTU_UART_ERROR,
    BSP_DTU_UART_BUSY,
    BSP_DTU_UART_TIMEOUT,
    BSP_DTU_RX_NOT_READY
} BspDtuStatus;

typedef struct
{
    size_t length;
    uint8_t data_lost; /* 丢弃未完成的应用层行，遇到 LF 后重新同步。 */
} BspDtuReadResult;

typedef enum
{
    BSP_DTU_TX_IDLE = 0,
    BSP_DTU_TX_STARTING,
    BSP_DTU_TX_SENDING,
    BSP_DTU_TX_COMPLETE,
    BSP_DTU_TX_FAILED,
    BSP_DTU_TX_BLOCKED
} BspDtuTxState;

typedef struct
{
    uint32_t tx_attempts;
    uint32_t tx_errors;

    uint32_t rx_bytes;
    uint32_t rx_overflows;
    uint32_t rx_errors;
    uint32_t rx_start_errors;

    uint32_t last_uart_error;
    BspDtuStatus last_tx_status;
    BspDtuStatus last_rx_status;
    BspDtuStatus last_abort_status;
    uint8_t rx_active;

    uint32_t tx_completed;
    uint32_t tx_abort_errors;
    uint8_t tx_active;
    BspDtuTxState tx_state;

    uint32_t recovery_attempts;
    uint32_t recovery_successes;
    BspDtuStatus last_tx_abort_status;
} BspDtuDiagnostics;

typedef struct
{
    uint8_t busy;
    uint8_t finished;

    BspDtuStatus status;
    uint32_t completed_ms;
} BspDtuTxResult;

/**
 * @brief 初始化 USART6 的 DTU 传输状态和单字节接收中断。
 * @return 接收启动结果。
 * @note CubeMX USART6 已初始化，现场使用自动方向。
 */
BspDtuStatus BspDtu_Init(void);

/**
 * @brief 获取单一 DTU 发送缓冲区。
 * @param capacity 用于返回容量的非空指针，单位字节。
 * @return 空闲时可写的缓冲区，否则为空。
 * @note 发送期间由 USART6 持有缓冲区。中止失败时保持占用，成功恢复后方可重写。
 */
char *BspDtu_TxBuffer(size_t *capacity);

/**
 * @brief 启动 USART6 中断发送。
 * @param length 要发送的字节数。
 * @return 提交结果。提交成功后由 Process 跟踪传输完成。
 */
BspDtuStatus BspDtu_StartSend(size_t length);

/**
 * @brief 检查传输终态，处理超时、中止和有限频率恢复。
 * @return 繁忙状态、完成事件及完成 tick。
 */
BspDtuTxResult BspDtu_Process(void);

/**
 * @brief 从接收环形缓冲区取出数据并报告失步。
 * @param data 输出字节数组。
 * @param capacity 数组容量，单位字节。
 * @return 读出长度及丢失标记。
 * @note 每次最多读取 32 字节。发生丢失时丢弃已排队的字节，调用方随后重新同步。
 */
BspDtuReadResult BspDtu_Read(uint8_t *data, size_t capacity);

/**
 * @brief 在短临界区内复制 DTU 诊断。
 * @return 接收、发送和恢复状态的快照。
 */
BspDtuDiagnostics BspDtu_GetDiagnostics(void);
/* 以下三个 ISR 接口只供 BSP UART 分发模块调用。 */

/**
 * @brief 在 USART6 接收中断中入队单字节并重挂接收。
 */
void BspDtu_RxCompleteIsr(void);

/**
 * @brief 记录 USART6 发送完成及其 tick，供主循环消费。
 */
void BspDtu_TxCompleteIsr(void);

/**
 * @brief 记录 USART6 错误并标记接收恢复需求。
 * @param error HAL UART 错误位掩码。
 */
void BspDtu_ErrorIsr(uint32_t error);

/**
 * @brief 读取 DTU 传输状态的固定名称。
 * @param status DTU 状态。
 * @return 只读名称字符串。
 */
const char *BspDtu_StatusName(BspDtuStatus status);

#endif

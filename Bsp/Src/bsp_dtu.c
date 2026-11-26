/**
 * @file bsp_dtu.c
 * @brief USART6 DTU 传输，管理中断收发、缓冲区所有权和错误恢复。
 */

#include "bsp_dtu.h"
#include "bsp_config.h"
#include "usart.h"

/* 模块数据 */

static volatile uint8_t s_rx_buffer[BSP_DTU_RX_BUFFER_SIZE];
static uint8_t s_rx_byte;
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile uint8_t s_initialized;
static volatile uint8_t s_rx_data_lost;
static volatile uint8_t s_abort_required;
static volatile BspDtuDiagnostics s_diagnostics;
static char s_tx_buffer[BSP_DTU_TX_BUFFER_CAPACITY];
/* 主循环在短临界区发布 STARTING，再开中断调用 HAL。
 * ISR 锁存终态，主循环负责消费事件和释放发送缓冲区。 */
static volatile BspDtuTxState s_tx_state;
static volatile BspDtuStatus s_tx_status;
static volatile uint32_t s_tx_completed_ms;
static uint32_t s_tx_started_ms;
static uint32_t s_recovery_last_ms;
static uint8_t s_tx_boundary;
static uint8_t s_tx_abort_attempts;

/* 私有函数声明 */

static void BspDtu_SetTxState(BspDtuTxState state);
static BspDtuStatus BspDtu_MapHalStatus(HAL_StatusTypeDef status);
static BspDtuStatus BspDtu_StartReceive(void);

/* 公共接口 */

BspDtuStatus BspDtu_Init(void)
{
    const BspDtuDiagnostics empty = {0};
    BspDtuStatus status;
    uint32_t irq_state;

    /* 重新初始化前先归还发送缓冲区所有权。正常恢复由 Process 执行并保留累计诊断。 */
    if (s_tx_state != BSP_DTU_TX_IDLE)
    {
        status = BspDtu_MapHalStatus(HAL_UART_AbortTransmit(&huart6));
        if (status != BSP_DTU_OK)
        {
            return status;
        }
        __HAL_UART_CLEAR_FLAG(&huart6, UART_CLEAR_TCF);
    }
    irq_state = __get_PRIMASK();
    __disable_irq();
    s_rx_head = s_rx_tail = 0U;
    s_rx_data_lost = 0U;
    s_abort_required = s_initialized;
    s_initialized = 1U;
    s_diagnostics = empty;
    BspDtu_SetTxState(BSP_DTU_TX_IDLE);
    s_tx_status = BSP_DTU_OK;
    s_tx_abort_attempts = s_tx_boundary = 0U;
    status = BspDtu_StartReceive();
    __set_PRIMASK(irq_state);
    return status;
}

char *BspDtu_TxBuffer(size_t *capacity)
{
    if (!capacity)
    {
        return NULL;
    }
    *capacity = 0U;
    if (s_tx_state != BSP_DTU_TX_IDLE)
    {
        return NULL;
    }
    *capacity = sizeof(s_tx_buffer) - 2U;
    return s_tx_buffer + 2;
}

BspDtuStatus BspDtu_StartSend(size_t length)
{
    BspDtuStatus status;
    const uint8_t *bytes = (const uint8_t *)(s_tx_buffer + 2);
    size_t send_length = length;
    uint32_t irq_state;
    uint32_t started = HAL_GetTick();

    ++s_diagnostics.tx_attempts;
    if (s_tx_state != BSP_DTU_TX_IDLE)
    {
        status = BSP_DTU_UART_BUSY;
    }
    else if (!length || length > sizeof(s_tx_buffer) - 2U)
    {
        status = BSP_DTU_BAD_LENGTH;
    }
    else if (!s_initialized || !s_diagnostics.rx_active)
    {
        status = BSP_DTU_RX_NOT_READY;
    }
    else
    {
        if (s_tx_boundary)
        {
            s_tx_buffer[0] = '\r';
            s_tx_buffer[1] = '\n';
            bytes = (const uint8_t *)s_tx_buffer;
            send_length += 2U;
        }
        irq_state = __get_PRIMASK();
        __disable_irq();
        s_tx_started_ms = started;
        s_tx_abort_attempts = 0U;
        s_tx_status = BSP_DTU_OK;
        BspDtu_SetTxState(BSP_DTU_TX_STARTING);
        __set_PRIMASK(irq_state);
        status = BspDtu_MapHalStatus(HAL_UART_Transmit_IT(&huart6, bytes, (uint16_t)send_length));
        irq_state = __get_PRIMASK();
        __disable_irq();
        if (s_tx_state == BSP_DTU_TX_STARTING)
        {
            if (status == BSP_DTU_OK)
            {
                BspDtu_SetTxState(BSP_DTU_TX_SENDING);
            }
            else
            {
                BspDtu_SetTxState(BSP_DTU_TX_IDLE);
            }
            s_tx_status = status;
            s_diagnostics.last_tx_status = status;
        }
        /* HAL 启动期间若 ISR 已记录 TC 或错误，以 ISR 的结果为准。错误也需要完成安全清理。 */
        if (s_tx_state == BSP_DTU_TX_FAILED || s_tx_state == BSP_DTU_TX_COMPLETE)
        {
            status = BSP_DTU_OK;
        }
        if (status == BSP_DTU_OK)
        {
            s_tx_boundary = 0U;
        }
        __set_PRIMASK(irq_state);
        if (status != BSP_DTU_OK)
        {
            ++s_diagnostics.tx_errors;
        }
        return status;
    }
    s_diagnostics.last_tx_status = status;
    ++s_diagnostics.tx_errors;
    return status;
}

BspDtuTxResult BspDtu_Process(void)
{
    BspDtuTxResult result = {0U, 0U, BSP_DTU_OK, 0U};
    uint32_t now_ms = HAL_GetTick();
    uint32_t irq_state;
    BspDtuStatus cleanup;

    irq_state = __get_PRIMASK();
    __disable_irq();
    if (s_tx_state == BSP_DTU_TX_IDLE)
    {
        __set_PRIMASK(irq_state);
        return result;
    }
    result.busy = 1U;
    if ((s_tx_state == BSP_DTU_TX_STARTING || s_tx_state == BSP_DTU_TX_SENDING) &&
        (uint32_t)(now_ms - s_tx_started_ms) >= BSP_DTU_TX_TIMEOUT_MS)
    {
        s_tx_status = BSP_DTU_UART_TIMEOUT;
        s_diagnostics.last_tx_status = s_tx_status;
        BspDtu_SetTxState(BSP_DTU_TX_FAILED);
    }
    if (s_tx_state == BSP_DTU_TX_COMPLETE)
    {
        result.finished = 1U;
        result.busy = 0U;
        result.completed_ms = s_tx_completed_ms;
        ++s_diagnostics.tx_completed;
        BspDtu_SetTxState(BSP_DTU_TX_IDLE);
        __set_PRIMASK(irq_state);
        return result;
    }
    if (s_tx_state == BSP_DTU_TX_BLOCKED)
    {
        __set_PRIMASK(irq_state);
        if ((uint32_t)(now_ms - s_recovery_last_ms) < BSP_DTU_RECOVERY_PERIOD_MS)
        {
            return result;
        }
        s_recovery_last_ms = now_ms;
        ++s_diagnostics.recovery_attempts;
        cleanup = BspDtu_MapHalStatus(HAL_UART_AbortTransmit(&huart6));
        s_diagnostics.last_tx_abort_status = cleanup;
        if (cleanup == BSP_DTU_OK)
        {
            __HAL_UART_CLEAR_FLAG(&huart6, UART_CLEAR_TCF);
            irq_state = __get_PRIMASK();
            __disable_irq();
            ++s_diagnostics.recovery_successes;
            BspDtu_SetTxState(BSP_DTU_TX_IDLE);
            s_tx_boundary = 1U;
            result.busy = 0U;
            __set_PRIMASK(irq_state);
        }
        else
        {
            ++s_diagnostics.tx_abort_errors;
        }
        return result; /* 恢复探测不重复报告原传输的完成事件。 */
    }
    if (s_tx_state != BSP_DTU_TX_FAILED)
    {
        __set_PRIMASK(irq_state);
        return result;
    }
    __set_PRIMASK(irq_state);
    cleanup = BspDtu_MapHalStatus(HAL_UART_AbortTransmit(&huart6));
    s_diagnostics.last_tx_abort_status = cleanup;
    s_tx_boundary = 1U;
    if (cleanup != BSP_DTU_OK)
    {
        ++s_diagnostics.tx_abort_errors;
        if (++s_tx_abort_attempts < 3U)
        {
            return result;
        }
    }
    else
    {
        __HAL_UART_CLEAR_FLAG(&huart6, UART_CLEAR_TCF);
    }
    irq_state = __get_PRIMASK();
    __disable_irq();
    result.status = s_tx_status;
    result.finished = 1U;
    result.completed_ms = now_ms;
    ++s_diagnostics.tx_errors;
    if (cleanup == BSP_DTU_OK)
    {
        BspDtu_SetTxState(BSP_DTU_TX_IDLE);
        result.busy = 0U;
    }
    else
    {
        BspDtu_SetTxState(BSP_DTU_TX_BLOCKED);
        s_recovery_last_ms = now_ms;
    }
    __set_PRIMASK(irq_state);
    return result;
}

BspDtuReadResult BspDtu_Read(uint8_t *data, size_t capacity)
{
    BspDtuReadResult result = {0U, 0U};
    uint32_t irq_state;

    if ((data == NULL) || (capacity == 0U))
    {
        return result;
    }
    /* 限制单次复制量，控制关中断时间。 */
    if (capacity > 32U)
    {
        capacity = 32U;
    }
    irq_state = __get_PRIMASK();
    __disable_irq();
    if ((s_initialized != 0U) && (s_diagnostics.rx_active == 0U))
    {
        (void)BspDtu_StartReceive();
    }
    if (s_rx_data_lost != 0U)
    {
        s_rx_tail = s_rx_head;
        s_rx_data_lost = 0U;
        result.data_lost = 1U;
    }
    else
    {
        while ((result.length < capacity) && (s_rx_tail != s_rx_head))
        {
            data[result.length++] = s_rx_buffer[s_rx_tail];
            s_rx_tail = (uint16_t)((s_rx_tail + 1U) % BSP_DTU_RX_BUFFER_SIZE);
        }
    }
    __set_PRIMASK(irq_state);
    return result;
}

BspDtuDiagnostics BspDtu_GetDiagnostics(void)
{
    BspDtuDiagnostics result;
    uint32_t irq_state = __get_PRIMASK();

    __disable_irq();
    result = s_diagnostics;
    __set_PRIMASK(irq_state);
    return result;
}

const char *BspDtu_StatusName(BspDtuStatus status)
{
    switch (status)
    {
        case BSP_DTU_OK:
            return "OK";
        case BSP_DTU_BAD_LENGTH:
            return "BAD_LENGTH";
        case BSP_DTU_UART_BUSY:
            return "UART_BUSY";
        case BSP_DTU_UART_TIMEOUT:
            return "UART_TIMEOUT";
        case BSP_DTU_RX_NOT_READY:
            return "RX_NOT_READY";
        default:
            return "UART_ERROR";
    }
}

/* 私有实现 */

/**
 * @brief 同步发送主状态和用于诊断的镜像状态。
 * @param state 新的发送状态。
 */
static void BspDtu_SetTxState(BspDtuTxState state)
{
    s_tx_state = state;
    s_diagnostics.tx_state = state;
    s_diagnostics.tx_active = state != BSP_DTU_TX_IDLE;
}

static BspDtuStatus BspDtu_MapHalStatus(HAL_StatusTypeDef status)
{
    switch (status)
    {
        case HAL_OK:
            return BSP_DTU_OK;
        case HAL_BUSY:
            return BSP_DTU_UART_BUSY;
        case HAL_TIMEOUT:
            return BSP_DTU_UART_TIMEOUT;
        default:
            return BSP_DTU_UART_ERROR;
    }
}

/**
 * @brief 为下一字节重挂 USART6 的中断接收。
 * @return 接收启动状态。
 * @note 主循环须在保护区内调用，也可由 UART 接收中断调用。
 */
static BspDtuStatus BspDtu_StartReceive(void)
{
    BspDtuStatus status;

    if (s_abort_required != 0U)
    {
        status = BspDtu_MapHalStatus(HAL_UART_AbortReceive(&huart6));
        s_diagnostics.last_abort_status = status;
        if (status != BSP_DTU_OK)
        {
            return status;
        }
        s_abort_required = 0U;
    }
    status = BspDtu_MapHalStatus(HAL_UART_Receive_IT(&huart6, &s_rx_byte, 1U));
    s_diagnostics.last_rx_status = status;
    s_diagnostics.rx_active = (status == BSP_DTU_OK) ? 1U : 0U;
    if (status != BSP_DTU_OK)
    {
        ++s_diagnostics.rx_start_errors;
        s_rx_data_lost = 1U;
        s_abort_required = 1U;
    }
    return status;
}

/* 中断与回调 */

/**
 * @brief 记录 USART6 发送完成及其 tick，供主循环消费。
 */
void BspDtu_TxCompleteIsr(void)
{
    /* 只有 TC 才会令 HAL 进入 READY 且剩余计数归零。中止和下一次启动清除旧 TC，保留 ACK 接收。 */
    if ((s_tx_state == BSP_DTU_TX_STARTING || s_tx_state == BSP_DTU_TX_SENDING) &&
        huart6.gState == HAL_UART_STATE_READY && huart6.TxXferCount == 0U)
    {
        s_tx_completed_ms = HAL_GetTick();
        s_tx_status = BSP_DTU_OK;
        s_diagnostics.last_tx_status = BSP_DTU_OK;
        BspDtu_SetTxState(BSP_DTU_TX_COMPLETE);
    }
}

/**
 * @brief 在 USART6 接收中断中入队单字节并重挂接收。
 */
void BspDtu_RxCompleteIsr(void)
{
    uint16_t next;

    if ((s_initialized == 0U) || (s_diagnostics.rx_active == 0U))
    {
        return;
    }
    s_diagnostics.rx_active = 0U;
    ++s_diagnostics.rx_bytes;
    next = (uint16_t)((s_rx_head + 1U) % BSP_DTU_RX_BUFFER_SIZE);
    if (next == s_rx_tail)
    {
        ++s_diagnostics.rx_overflows;
        s_rx_data_lost = 1U;
    }
    else
    {
        s_rx_buffer[s_rx_head] = s_rx_byte;
        s_rx_head = next;
    }
    (void)BspDtu_StartReceive();
}

/**
 * @brief 记录 USART6 错误并标记接收恢复需求。
 * @param error HAL UART 错误位掩码。
 */
void BspDtu_ErrorIsr(uint32_t error)
{
    if (!s_initialized)
    {
        return;
    }
    /* 主循环消费 TX 完成事件前，RX 错误仍可使本次发送失败。消费后两条路径独立。 */
    if (s_tx_state == BSP_DTU_TX_STARTING || s_tx_state == BSP_DTU_TX_SENDING ||
        s_tx_state == BSP_DTU_TX_COMPLETE)
    {
        s_tx_status = BSP_DTU_UART_ERROR;
        s_diagnostics.last_tx_status = s_tx_status;
        BspDtu_SetTxState(BSP_DTU_TX_FAILED);
    }
    ++s_diagnostics.rx_errors;
    s_diagnostics.last_uart_error = error;
    s_diagnostics.rx_active = 0U;
    s_rx_data_lost = 1U;
    s_abort_required = 1U;
    (void)BspDtu_StartReceive();
}

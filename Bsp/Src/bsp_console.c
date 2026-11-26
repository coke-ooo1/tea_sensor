/**
 * @file bsp_console.c
 * @brief USART2 诊断控制台，保留 RS485 方向处理和 Arm C 库重定向。
 */

#include "bsp_console.h"
#include "bsp_rs485_dir.h"
#include "bsp_config.h"
#include "usart.h"
#include <stdint.h>
#include <stdio.h>
#include <rt_sys.h>
#include <string.h>

/* 宏定义与编译期检查 */

/* MDK 目标使用 ARMCC 5 标准 C 库，useUlib=0。 */
#if defined(__MICROLIB)
#error This retarget requires the configured standard Arm C library
#endif
#if defined(__CC_ARM)
#pragma import(__use_no_semihosting_swi)
#endif
#define BSP_CONSOLE_STDIO_HANDLE 1

/* 模块数据 */

/* 由本模块定义标准流名称，避免链接 Arm sys_io.o。 */
const char __stdin_name[] = ":STDIN";
const char __stdout_name[] = ":STDOUT";
const char __stderr_name[] = ":STDERR";
static UART_HandleTypeDef *s_console_uart;
static uint8_t s_drain_pending;
static BspConsoleDiagnostics s_diagnostics;

/* 私有函数声明 */

static BspConsoleStatus BspConsole_Transmit(const char *data, size_t length);

/* 公共接口 */

void BspConsole_Init(void)
{
    s_console_uart = &huart2;
    s_drain_pending = 0U;
    s_diagnostics.tx_attempts = 0U;
    s_diagnostics.tx_errors = 0U;
    s_diagnostics.last_tx_status = BSP_CONSOLE_OK;
    BspRs485Dir_Init();
    /* 沿用 C 库 FILE 对象，标准输出采用无缓冲模式。 */
    s_diagnostics.stdout_unbuffered = (setvbuf(stdout, NULL, _IONBF, 0U) == 0) ? 1U : 0U;
    clearerr(stdout);
}

void BspConsole_Process(void)
{
    if ((s_drain_pending != 0U) && (__HAL_UART_GET_FLAG(s_console_uart, UART_FLAG_TC) != RESET))
    {
        BspRs485Dir_Receive();
        s_drain_pending = 0U;
    }
}

BspConsoleStatus BspConsole_Send(const char *data, size_t length)
{
    BspConsoleStatus status = BspConsole_Transmit(data, length);

    ++s_diagnostics.tx_attempts;
    s_diagnostics.last_tx_status = status;
    if (status != BSP_CONSOLE_OK)
    {
        ++s_diagnostics.tx_errors;
    }
    return status;
}

const BspConsoleDiagnostics *BspConsole_GetDiagnostics(void)
{
    return &s_diagnostics;
}

const char *BspConsole_StatusName(BspConsoleStatus status)
{
    switch (status)
    {
        case BSP_CONSOLE_OK:
            return "OK";
        case BSP_CONSOLE_BAD_LENGTH:
            return "TX_BAD_LENGTH";
        case BSP_CONSOLE_UART_BUSY:
            return "TX_UART_BUSY";
        case BSP_CONSOLE_UART_TIMEOUT:
            return "TX_UART_TIMEOUT";
        case BSP_CONSOLE_TC_TIMEOUT:
            return "TX_TC_TIMEOUT";
        case BSP_CONSOLE_DRAIN_PENDING:
            return "TX_DRAIN_PENDING";
        default:
            return "TX_UART_ERROR";
    }
}

/* 私有实现 */

/**
 * @brief 执行同步 UART 发送并等待 TC，确保最后一位结束后再切方向。
 * @param data 输出数据。
 * @param length 字节数。
 * @return 发送及方向恢复相关的状态。
 */
static BspConsoleStatus BspConsole_Transmit(const char *data, size_t length)
{
    HAL_StatusTypeDef hal_status;
    uint32_t started_ms;

    if ((s_console_uart == NULL) || (data == NULL) || (length == 0U) || (length > UINT16_MAX))
    {
        return BSP_CONSOLE_BAD_LENGTH;
    }
    BspConsole_Process();
    if (s_drain_pending != 0U)
    {
        return BSP_CONSOLE_DRAIN_PENDING;
    }
    /* UART 已被其他路径占用时，保留当前方向并报告忙。 */
    if (HAL_UART_GetState(s_console_uart) != HAL_UART_STATE_READY)
    {
        return BSP_CONSOLE_UART_BUSY;
    }

    BspRs485Dir_Transmit();
    hal_status = HAL_UART_Transmit(
        s_console_uart, (const uint8_t *)data, (uint16_t)length, BSP_CONSOLE_TX_TIMEOUT_MS);
    /* HAL 发送错误时也等待 TC。TXE 置位时最后一位仍可能在发送。 */
    started_ms = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(s_console_uart, UART_FLAG_TC) == RESET)
    {
        if ((uint32_t)(HAL_GetTick() - started_ms) >= BSP_CONSOLE_TC_TIMEOUT_MS)
        {
            /* 超时后保留 TX 方向，交由 Process 在 TC 后切回接收，避免截断最后一个字节。 */
            s_drain_pending = 1U;
            return BSP_CONSOLE_TC_TIMEOUT;
        }
    }
    BspRs485Dir_Receive();

    switch (hal_status)
    {
        case HAL_OK:
            return BSP_CONSOLE_OK;
        case HAL_BUSY:
            return BSP_CONSOLE_UART_BUSY;
        case HAL_TIMEOUT:
            return BSP_CONSOLE_UART_TIMEOUT;
        default:
            return BSP_CONSOLE_UART_ERROR;
    }
}

/* Arm C 库重定向 */

/* 按 rt_sys.h 为 Arm C 库重定向标准流。stdout 和 stderr 使用 USART2，文件与输入操作返回失败。 */
FILEHANDLE _sys_open(const char *name, int openmode)
{
    (void)openmode;
    if ((strcmp(name, __stdout_name) == 0) || (strcmp(name, __stderr_name) == 0) ||
        (strcmp(name, __stdin_name) == 0))
    {
        return BSP_CONSOLE_STDIO_HANDLE;
    }
    return -1;
}

int _sys_write(FILEHANDLE handle, const unsigned char *buffer, unsigned length, int mode)
{
    (void)mode;
    if (handle != BSP_CONSOLE_STDIO_HANDLE)
    {
        return -1;
    }
    if (length == 0U)
    {
        return 0;
    }
    return (BspConsole_Send((const char *)buffer, length) == BSP_CONSOLE_OK) ? 0 : -1;
}

int _sys_read(FILEHANDLE handle, unsigned char *buffer, unsigned length, int mode)
{
    (void)handle;
    (void)buffer;
    (void)length;
    (void)mode;
    return -1;
}

int _sys_close(FILEHANDLE handle)
{
    return (handle == BSP_CONSOLE_STDIO_HANDLE) ? 0 : -1;
}

int _sys_istty(FILEHANDLE handle)
{
    return (handle == BSP_CONSOLE_STDIO_HANDLE) ? 1 : 0;
}

int _sys_seek(FILEHANDLE handle, long position)
{
    (void)handle;
    (void)position;
    return -1;
}

long _sys_flen(FILEHANDLE handle)
{
    (void)handle;
    return -1;
}

int _sys_ensure(FILEHANDLE handle)
{
    return (handle == BSP_CONSOLE_STDIO_HANDLE) ? 0 : -1;
}

int _sys_tmpnam(char *name, int signature, unsigned capacity)
{
    (void)name;
    (void)signature;
    (void)capacity;
    return 0;
}

char *_sys_command_string(char *command, int capacity)
{
    if (capacity <= 0)
    {
        return NULL;
    }
    command[0] = '\0';
    return command;
}

void _ttywrch(int character)
{
    /* 运行库的备用诊断输出保持静默，DTU 传输走独立串口。 */
    (void)character;
}

void _sys_exit(int return_code)
{
    (void)return_code;
    for (;;)
    {
    }
}

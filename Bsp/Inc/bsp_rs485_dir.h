/**
 * @file bsp_rs485_dir.h
 * @brief 控制台 RS485 方向适配，支持硬件自动方向和经确认的 GPIO 手动方向。
 */

#ifndef BSP_RS485_DIR_H
#define BSP_RS485_DIR_H

/**
 * @brief 初始化控制台使用的自动或手动 RS485 方向控制。
 */
void BspRs485Dir_Init(void);

/**
 * @brief 在手动方向模式下将控制台切到发送。
 */
void BspRs485Dir_Transmit(void);

/**
 * @brief 在手动方向模式下将控制台切回接收。
 * @note 调用方应先确认 UART TC。
 */
void BspRs485Dir_Receive(void);

/**
 * @brief 读取当前方向模式的固定名称。
 * @return 编译时选定的模式名称。
 */
const char *BspRs485Dir_Name(void);

#endif

/**
 * @file bsp_time.h
 * @brief 板级计时与启动熵，提供 HAL tick、设备 UID 及 STM32H743 RNG 接口。
 */

#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stdint.h>

/**
 * @brief 读取 HAL 系统计时。
 * @return 允许自然回绕的上电计时，单位 ms。
 */
uint32_t BspTime_GetTickMs(void);

/**
 * @brief 按调用推进启动随机源并读取一个字。
 * @param word 成功时写入 32 位随机字。
 * @return 随机字就绪返回 1，需稍后再试时返回 0。
 */
uint8_t BspTime_ReadBootRandom(uint32_t *word);

/**
 * @brief 停止启动会话使用的 RNG 并释放本模块开启的 HSI48。
 */
void BspTime_StopBootRandom(void);

/**
 * @brief 读取设备 UID 的三个 32 位字。
 * @param words 输出数组，至少容纳三个元素。
 */
void BspTime_GetUniqueId(uint32_t words[3]);

#endif

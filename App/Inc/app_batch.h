/**
 * @file app_batch.h
 * @brief 待上传批次 FIFO，负责封存、按序读取及 ACK 确认后的出队。
 */

#ifndef APP_BATCH_H
#define APP_BATCH_H
#include "app_data.h"

/**
 * @brief 清空待上传队列和溢出计数。
 */
void AppBatch_Init(void);

/**
 * @brief 将完成的批次复制到 FIFO 队尾。
 * @param batch 待保存的批次。
 * @return 入队返回 1。队满时保留旧批次并返回 0。
 */
uint8_t AppBatch_Enqueue(const AppTelemetryBatch *batch);

/**
 * @brief 读取当前最早的待确认批次。
 * @return 队首的只读指针，队空时为空。
 */
const AppTelemetryBatch *AppBatch_Peek(void);

/**
 * @brief 移除已确认的队首批次。
 * @note 仅在队列非空且收到与该批次匹配的成功 ACK 后调用。
 */
void AppBatch_Confirm(void);

/**
 * @brief 读取当前队列长度。
 * @return 队列中尚未确认的批次数。
 */
unsigned AppBatch_Count(void);

/**
 * @brief 读取批次队列溢出统计。
 * @return 自初始化以来因队满而丢弃的新批次数。
 */
uint32_t AppBatch_Overflows(void);

#endif

/**
 * @file app_batch.c
 * @brief 待上传批次 FIFO，负责封存、按序读取及 ACK 确认后的出队。
 */

#include "app_batch.h"

/* 模块数据 */

static AppTelemetryBatch s_queue[APP_BATCH_QUEUE_CAPACITY];
static unsigned s_head;
static unsigned s_count;
static uint32_t s_overflows;

/* 公共接口 */

void AppBatch_Init(void)
{
    s_head = s_count = 0U;
    s_overflows = 0U;
}

uint8_t AppBatch_Enqueue(const AppTelemetryBatch *batch)
{
    if (s_count == APP_BATCH_QUEUE_CAPACITY)
    {
        ++s_overflows;
        return 0U;
    }
    s_queue[(s_head + s_count) % APP_BATCH_QUEUE_CAPACITY] = *batch;
    ++s_count;
    return 1U;
}

const AppTelemetryBatch *AppBatch_Peek(void)
{
    return s_count ? &s_queue[s_head] : 0;
}

void AppBatch_Confirm(void)
{
    s_head = (s_head + 1U) % APP_BATCH_QUEUE_CAPACITY;
    --s_count;
}

unsigned AppBatch_Count(void)
{
    return s_count;
}

uint32_t AppBatch_Overflows(void)
{
    return s_overflows;
}

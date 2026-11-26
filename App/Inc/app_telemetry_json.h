/**
 * @file app_telemetry_json.h
 * @brief V1.0 遥测编码，校验配置和批次标识并分步写入固定缓冲区。
 */

#ifndef APP_TELEMETRY_JSON_H
#define APP_TELEMETRY_JSON_H
#include "app_data.h"
#include <stddef.h>

#define APP_TELEMETRY_PROTOCOL "tea_sense.telemetry"
#define APP_TELEMETRY_ACK_PROTOCOL "tea_sense.ack"
#define APP_BOOT_SESSION_WORDS 4U
#define APP_BOOT_SESSION_HEX_LENGTH (APP_BOOT_SESSION_WORDS * 8U)
#define APP_BATCH_ID_CAPACITY (64U + 1U + APP_BOOT_SESSION_HEX_LENGTH + 1U + 10U + 1U)
#define APP_UPLOAD_FRAME_CAPACITY 12286U

typedef struct
{
    const AppTelemetryBatch *batch;
    char *buffer;

    size_t capacity;
    size_t offset;

    unsigned group;
    unsigned index;
    uint8_t complete;
} AppTelemetryJson;

/**
 * @brief 检查上传凭据和设备标识是否满足 V1.0 编码前置条件。
 * @return 配置有效返回 1，否则返回 0。
 */
uint8_t AppTelemetryJson_IsConfigured(void);

/**
 * @brief 按设备标识、启动会话和序号生成批次 ID。
 * @param id 至少有 APP_BATCH_ID_CAPACITY 字节的输出数组。
 * @param boot_session 以 NUL 结束的固定长度小写十六进制会话标识。
 * @param sequence 批次序号。
 * @return 成功返回 0，参数格式或容量检查失败返回 -1。
 */
int AppTelemetryJson_BatchId(
    char id[APP_BATCH_ID_CAPACITY], const char *boot_session, uint32_t sequence);

/**
 * @brief 初始化增量编码状态并写入批次头。
 * @param writer 编码上下文。
 * @param buffer 输出缓冲区。
 * @param capacity 缓冲区容量，单位字节。
 * @param batch 编码期间保持有效的只读批次。
 * @param batch_id 当前批次的标识。
 * @param now 启动编码时的上电计时，单位 ms。
 * @return 成功返回 0，参数无效或写入失败返回 -1。
 */
int AppTelemetryJson_Begin(
    AppTelemetryJson *writer,
    char *buffer,
    size_t capacity,
    const AppTelemetryBatch *batch,
    const char *batch_id,
    uint32_t now);

/**
 * @brief 写入至多一条记录或一个分隔部分。
 * @param writer 已初始化的编码上下文。
 * @return 未完成返回 0，失败返回 -1，完成时返回含 CRLF 的帧字节数。
 */
int AppTelemetryJson_Step(AppTelemetryJson *writer);

#endif

/**
 * @file app_upload.h
 * @brief DTU 上传状态机，负责启动会话、帧发送、ACK 校验与超时重试。
 */

#ifndef APP_UPLOAD_H
#define APP_UPLOAD_H
#include "app_telemetry_json.h"

#define APP_UPLOAD_ACK_CAPACITY 512U

typedef enum
{
    APP_UPLOAD_ACK_INVALID = 0,
    APP_UPLOAD_ACK_IGNORED,
    APP_UPLOAD_ACK_ACCEPTED
} AppUploadAckStatus;

typedef struct
{
    uint32_t ack_count;
    uint32_t ack_sequence;
    uint32_t last_sequence;
    uint32_t timeouts;
    uint32_t line_drops;
    uint32_t stream_gaps;
    uint32_t format_errors;
    uint32_t tx_errors;
    uint8_t tx_pending;
    uint8_t awaiting_ack;
    unsigned init_status;
} AppUploadDiagnostics;

/**
 * @brief 清空上传状态并初始化 DTU 传输。
 */
void AppUpload_Init(void);

/**
 * @brief 推进启动会话、JSON 编码、传输、ACK 匹配与重试。
 * @note 由主循环调用，单个待确认批次占用发送缓冲区。
 */
void AppUpload_Process(void);

/**
 * @brief 访问上传、接收和重试诊断。
 * @return 模块持有的只读诊断指针。
 */
const AppUploadDiagnostics *AppUpload_GetDiagnostics(void);

/**
 * @brief 校验 ACK JSON 及设备、序号和批次标识。
 * @param line 完整 ACK 的 JSON 字节序列。
 * @param length JSON 字节数。
 * @param expected_sequence 当前待确认批次的序号。
 * @param batch_id 当前待确认批次的标识。
 * @return 匹配、身份不符或格式无效的解析状态。
 */
AppUploadAckStatus AppUpload_ParseAck(
    const char *line, size_t length, uint32_t expected_sequence, const char *batch_id);
#endif

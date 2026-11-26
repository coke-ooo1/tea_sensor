/**
 * @file app_upload.c
 * @brief DTU 上传状态机，负责启动会话、帧发送、ACK 校验与超时重试。
 */

#include "app_upload.h"
#include "app_batch.h"
#include "app_version.h"
#include "bsp_dtu.h"
#include "bsp_time.h"
#include "bsp_config.h"
#include "core_json.h"
#include <string.h>

/* 私有类型 */

typedef char AppUploadHardwareCapacity
    [(APP_UPLOAD_FRAME_CAPACITY + 2U <= BSP_DTU_TX_BUFFER_CAPACITY) ? 1 : -1];

/* 模块数据 */

static AppUploadDiagnostics s_upload;
static AppTelemetryJson s_json;
static unsigned s_boot_words;
static uint32_t s_boot_entropy[APP_BOOT_SESSION_WORDS];
static uint32_t s_sent_ms;
static uint32_t s_next_attempt_ms;
static char s_boot_session[APP_BOOT_SESSION_HEX_LENGTH + 1U];
static char s_batch_id[APP_BATCH_ID_CAPACITY];
static uint8_t s_configured;
static uint8_t s_early_ack;
static uint8_t s_discard_line;
static uint8_t s_attempt_timer;
static uint8_t s_encoding;
static uint8_t s_rx_line[APP_UPLOAD_ACK_CAPACITY];
static uint8_t s_rx_chunk[32];
static size_t s_rx_length;
static size_t s_frame_length;

/* 私有函数声明 */

static int AppUpload_HexDigit(char character);
static uint8_t AppUpload_StringEquals(const char *text, size_t length, const char *expected);
static uint8_t AppUpload_ParseUnsigned(
    const char *text, size_t length, uint64_t maximum, uint64_t *value);
static void AppUpload_ProcessBootSession(void);
static void AppUpload_CommitAck(void);
static void AppUpload_ConsumeByte(uint8_t byte);
static void AppUpload_DeferRetry(uint32_t now);
static void AppUpload_Advance(uint32_t now);

/* 公共接口 */

AppUploadAckStatus AppUpload_ParseAck(
    const char *line, size_t length, uint32_t expected_sequence, const char *batch_id)
{
    size_t start = 0U;
    size_t next = 0U;
    size_t leading = 0U;
    unsigned seen = 0U;
    uint8_t matches = 1U;
    uint64_t sequence = 0U;
    JSONPair_t pair;
    JSONStatus_t result;

    if (!line || !batch_id || JSON_Validate(line, length) != JSONSuccess)
    {
        return APP_UPLOAD_ACK_INVALID;
    }
    while (leading < length && (line[leading] == ' ' || line[leading] == '\t' ||
                                   line[leading] == '\r' || line[leading] == '\n'))
    {
        ++leading;
    }
    if (leading == length || line[leading] != '{')
    {
        return APP_UPLOAD_ACK_INVALID;
    }
    while ((result = JSON_Iterate(line, length, &start, &next, &pair)) == JSONSuccess)
    {
        unsigned bit;
        const char *expected;

        if (AppUpload_StringEquals(pair.key, pair.keyLength, "protocol"))
        {
            bit = 1U;
            expected = APP_TELEMETRY_ACK_PROTOCOL;
        }
        else if (AppUpload_StringEquals(pair.key, pair.keyLength, "schema_version"))
        {
            bit = 2U;
            expected = APP_PROTOCOL_SCHEMA_VERSION;
        }
        else if (AppUpload_StringEquals(pair.key, pair.keyLength, "device_id"))
        {
            bit = 4U;
            expected = APP_DEVICE_ID;
        }
        else if (AppUpload_StringEquals(pair.key, pair.keyLength, "seq"))
        {
            bit = 8U;
            expected = NULL;
            if (pair.jsonType != JSONNumber ||
                !AppUpload_ParseUnsigned(pair.value, pair.valueLength, UINT32_MAX, &sequence))
            {
                return APP_UPLOAD_ACK_INVALID;
            }
            matches &= sequence == expected_sequence;
        }
        else if (AppUpload_StringEquals(pair.key, pair.keyLength, "batch_id"))
        {
            bit = 16U;
            expected = batch_id;
        }
        else if (AppUpload_StringEquals(pair.key, pair.keyLength, "status"))
        {
            bit = 32U;
            expected = "accepted";
            if (AppUpload_StringEquals(pair.value, pair.valueLength, "duplicate"))
            {
                expected = "duplicate";
            }
        }
        else
        {
            continue;
        }
        if (seen & bit)
        {
            return APP_UPLOAD_ACK_INVALID;
        }
        seen |= bit;
        if (expected)
        {
            if (pair.jsonType != JSONString)
            {
                return APP_UPLOAD_ACK_INVALID;
            }
            matches &= AppUpload_StringEquals(pair.value, pair.valueLength, expected);
        }
    }
    if (result != JSONNotFound)
    {
        return APP_UPLOAD_ACK_INVALID;
    }
    return (seen == 63U && matches) ? APP_UPLOAD_ACK_ACCEPTED : APP_UPLOAD_ACK_IGNORED;
}

void AppUpload_Init(void)
{
    memset(&s_upload, 0, sizeof(s_upload));
    s_boot_words = 0U;
    s_early_ack = s_discard_line = s_attempt_timer = s_encoding = 0U;
    s_rx_length = s_frame_length = 0U;
    s_boot_session[0] = '\0';
    s_configured = AppTelemetryJson_IsConfigured();
    s_upload.init_status = (unsigned)BspDtu_Init();
}

void AppUpload_Process(void)
{
    BspDtuTxResult transmitted = BspDtu_Process();
    uint32_t now;
    unsigned batch;

    if (s_upload.tx_pending && transmitted.finished)
    {
        s_upload.tx_pending = 0U;
        if (transmitted.status == BSP_DTU_OK)
        {
            s_sent_ms = transmitted.completed_ms;
            s_upload.awaiting_ack = 1U;
            if (s_early_ack)
            {
                AppUpload_CommitAck();
            }
        }
        else
        {
            ++s_upload.tx_errors;
        }
        s_early_ack = 0U;
    }
    for (batch = 0U; batch < 8U; ++batch)
    {
        size_t index;
        BspDtuReadResult received = BspDtu_Read(s_rx_chunk, sizeof(s_rx_chunk));

        if (received.data_lost)
        {
            if (!s_discard_line)
            {
                ++s_upload.stream_gaps;
            }
            s_discard_line = 1U;
            s_rx_length = 0U;
        }
        for (index = 0U; index < received.length; ++index)
        {
            AppUpload_ConsumeByte(s_rx_chunk[index]);
        }
        if (!received.length)
        {
            break;
        }
    }

    now = BspTime_GetTickMs();
    if (s_upload.awaiting_ack && (uint32_t)(now - s_sent_ms) >= APP_DTU_TIMEOUT_MS)
    {
        ++s_upload.timeouts;
        s_upload.awaiting_ack = 0U;
    }
    if (!s_configured)
    {
        return;
    }
    AppUpload_ProcessBootSession();
    if (!transmitted.busy)
    {
        AppUpload_Advance(now);
    }
}

const AppUploadDiagnostics *AppUpload_GetDiagnostics(void)
{
    return &s_upload;
}

/* 私有实现 */

static int AppUpload_HexDigit(char character)
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f')
    {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F')
    {
        return character - 'A' + 10;
    }
    return -1;
}

/* ASCII 标识按解码后的字符匹配，等价的 JSON 转义写法允许通过。 */
static uint8_t AppUpload_StringEquals(const char *text, size_t length, const char *expected)
{
    size_t index = 0U;

    while (index < length)
    {
        unsigned character = (unsigned char)text[index++];

        if (character == '\\')
        {
            if (index == length)
            {
                return 0U;
            }
            character = (unsigned char)text[index++];
            if (character == 'u')
            {
                unsigned digit;

                character = 0U;
                if (length - index < 4U)
                {
                    return 0U;
                }
                for (digit = 0U; digit < 4U; ++digit)
                {
                    int value = AppUpload_HexDigit(text[index++]);

                    if (value < 0)
                    {
                        return 0U;
                    }
                    character = character * 16U + (unsigned)value;
                }
            }
            else if (character != '\\' && character != '"' && character != '/')
            {
                return 0U;
            }
        }
        if (*expected == '\0' || character != (unsigned char)*expected++)
        {
            return 0U;
        }
    }
    return *expected == '\0' ? 1U : 0U;
}

static uint8_t AppUpload_ParseUnsigned(
    const char *text, size_t length, uint64_t maximum, uint64_t *value)
{
    size_t index;
    uint64_t number = 0U;

    if (!length)
    {
        return 0U;
    }
    for (index = 0U; index < length; ++index)
    {
        unsigned digit = (unsigned char)text[index] - (unsigned)'0';

        if (digit > 9U || number > (maximum - digit) / 10U)
        {
            return 0U;
        }
        number = number * 10U + digit;
    }
    *value = number;
    return 1U;
}

/**
 * @brief 分次读取启动熵，生成本次启动会话标识。
 */
static void AppUpload_ProcessBootSession(void)
{
    static const char hex[] = "0123456789abcdef";
    unsigned digit;
    uint32_t uid[3];

    if (s_boot_words == APP_BOOT_SESSION_WORDS ||
        !BspTime_ReadBootRandom(&s_boot_entropy[s_boot_words]))
    {
        return;
    }
    ++s_boot_words;
    if (s_boot_words != APP_BOOT_SESSION_WORDS)
    {
        return;
    }
    BspTime_StopBootRandom();
    BspTime_GetUniqueId(uid);
    for (digit = 0U; digit < 3U; ++digit)
    {
        s_boot_entropy[digit] ^= uid[digit];
    }
    for (digit = 0U; digit < APP_BOOT_SESSION_HEX_LENGTH; ++digit)
    {
        unsigned shift = (7U - digit % 8U) * 4U;

        s_boot_session[digit] = hex[(s_boot_entropy[digit / 8U] >> shift) & 0xFU];
    }
    s_boot_session[APP_BOOT_SESSION_HEX_LENGTH] = '\0';
}

static void AppUpload_CommitAck(void)
{
    s_upload.awaiting_ack = 0U;
    ++s_upload.ack_count;
    s_upload.ack_sequence = s_upload.last_sequence;
    AppBatch_Confirm();
    s_frame_length = 0U;
    s_next_attempt_ms = BspTime_GetTickMs() + APP_UPLOAD_NEXT_BATCH_MS;
}

/**
 * @brief 按 CRLF 组装 ACK，超长或丢失后在换行边界恢复。
 * @param byte 新收到的字节。
 */
static void AppUpload_ConsumeByte(uint8_t byte)
{
    if (s_discard_line)
    {
        if (byte == '\n')
        {
            s_discard_line = 0U;
        }
        return;
    }
    if (s_rx_length == sizeof(s_rx_line))
    {
        s_rx_length = 0U;
        s_discard_line = byte != '\n';
        ++s_upload.line_drops;
        return;
    }
    s_rx_line[s_rx_length++] = byte;
    if (byte != '\n')
    {
        return;
    }
    if ((s_upload.tx_pending ||
            (s_upload.awaiting_ack &&
                (uint32_t)(BspTime_GetTickMs() - s_sent_ms) < APP_DTU_TIMEOUT_MS)) &&
        AppUpload_ParseAck(
            (const char *)s_rx_line, s_rx_length, s_upload.last_sequence, s_batch_id) ==
            APP_UPLOAD_ACK_ACCEPTED)
    {
        if (s_upload.tx_pending)
        {
            s_early_ack = 1U;
        }
        else
        {
            AppUpload_CommitAck();
        }
    }
    s_rx_length = 0U;
}

static void AppUpload_DeferRetry(uint32_t now)
{
    s_attempt_timer = 1U;
    s_next_attempt_ms = now + APP_UPLOAD_RETRY_MS;
}

/**
 * @brief 推进队首批次状态，处理编码、发送、等待 ACK 和限时重试。
 * @param now 当前 tick，单位 ms。
 */
static void AppUpload_Advance(uint32_t now)
{
    const AppTelemetryBatch *batch = AppBatch_Peek();
    char *buffer;
    size_t capacity;
    int length;

    if (!batch || s_upload.tx_pending || s_upload.awaiting_ack || !s_boot_session[0] ||
        (s_attempt_timer && (int32_t)(now - s_next_attempt_ms) < 0))
    {
        return;
    }

    if (s_encoding)
    {
        length = AppTelemetryJson_Step(&s_json);
        if (!length)
        {
            return;
        }
        s_encoding = 0U;
        if (length < 0)
        {
            ++s_upload.format_errors;
            AppUpload_DeferRetry(now);
            return;
        }
        s_frame_length = (size_t)length;
    }
    else if (!s_frame_length)
    {
        buffer = BspDtu_TxBuffer(&capacity);
        if (!buffer)
        {
            return;
        }
        s_upload.last_sequence = batch->sequence;
        if (capacity > APP_UPLOAD_FRAME_CAPACITY)
        {
            capacity = APP_UPLOAD_FRAME_CAPACITY;
        }
        if (AppTelemetryJson_BatchId(s_batch_id, s_boot_session, batch->sequence) < 0 ||
            AppTelemetryJson_Begin(&s_json, buffer, capacity, batch, s_batch_id, now) < 0)
        {
            ++s_upload.format_errors;
            AppUpload_DeferRetry(now);
            return;
        }
        s_encoding = 1U;
        return;
    }

    /* TC、中止和重发期间沿用同一份帧。收到匹配 ACK 后才出队并复用缓冲区。 */
    AppUpload_DeferRetry(now);
    if (BspDtu_StartSend(s_frame_length) != BSP_DTU_OK)
    {
        ++s_upload.tx_errors;
        return;
    }
    s_upload.tx_pending = 1U;
    s_early_ack = 0U;
}

/**
 * @file app_telemetry_json.c
 * @brief V1.0 遥测编码，校验配置和批次标识并分步写入固定缓冲区。
 */

#include "app_telemetry_json.h"
#include "app_version.h"
#include <app_secrets.h>
#include "core_json.h"
#include <stdio.h>
#include <string.h>

/* 宏定义与编译期检查 */

#define APP_JSON_TOKEN_LITERAL_MAX 3074U
#define APP_JSON_FRAME_BOUND 12000U
#define APP_JSON_APPEND(...)                                                                       \
    do                                                                                             \
    {                                                                                              \
        int written = snprintf(                                                                    \
            writer->buffer + writer->offset, writer->capacity - writer->offset, __VA_ARGS__);      \
        if (written < 0 || (size_t)written >= writer->capacity - writer->offset)                   \
        {                                                                                          \
            return -1;                                                                             \
        }                                                                                          \
        writer->offset += (size_t)written;                                                         \
    } while (0)

/* 私有类型 */

typedef char
    AppJsonTokenBudget[(sizeof(APP_INGEST_TOKEN_JSON) - 1U <= APP_JSON_TOKEN_LITERAL_MAX) ? 1 : -1];

typedef char AppJsonFrameBudget[(APP_JSON_FRAME_BOUND < APP_UPLOAD_FRAME_CAPACITY) ? 1 : -1];

typedef char
    AppJsonDeviceBudget[(sizeof(APP_DEVICE_ID) >= 2U && sizeof(APP_DEVICE_ID) <= 65U) ? 1 : -1];

enum
{
    GROUP_SOIL,
    GROUP_PH,
    GROUP_WIND,
    GROUP_RAIN,
    GROUP_WEATHER,
    GROUP_COUNT
};

/* 私有函数声明 */

static const char *AppJson_SampleQuality(unsigned valid, unsigned expected);
static uint8_t AppJson_WeatherValid(const AppWeatherRecord *record, unsigned field);
static unsigned AppJson_WeatherCount(const AppWeatherRecord *record);
static uint8_t AppJson_IsComplete(const AppTelemetryBatch *batch);
static const char *AppJson_Number(
    char output[16], uint8_t valid, uint32_t magnitude, unsigned decimals, uint8_t negative);
static int AppJson_Soil(
    AppTelemetryJson *writer, const char *comma, uint32_t slot, const AppSoilRecord *r);
static int AppJson_Ph(AppTelemetryJson *writer, const AppPhRecord *r, uint32_t slot);
static int AppJson_Wind(AppTelemetryJson *writer, const char *comma, const AppMinuteRecord *r);
static int AppJson_Rain(AppTelemetryJson *writer, const char *comma, const AppMinuteRecord *r);
static int AppJson_Weather(
    AppTelemetryJson *writer, const char *comma, uint32_t slot, const AppWeatherRecord *r);

/* 公共接口 */

uint8_t AppTelemetryJson_IsConfigured(void)
{
    const size_t length = sizeof(APP_INGEST_TOKEN_JSON) - 1U;
    const char *id = APP_DEVICE_ID;

    if (APP_INGEST_TOKEN_CONFIGURED != 1 || length <= 2U || APP_INGEST_TOKEN_JSON[0] != '"' ||
        APP_INGEST_TOKEN_JSON[length - 1U] != '"' ||
        JSON_Validate(APP_INGEST_TOKEN_JSON, length) != JSONSuccess)
    {
        return 0U;
    }
    for (; *id; ++id)
    {
        if (!((*id >= 'a' && *id <= 'z') || (*id >= 'A' && *id <= 'Z') ||
                (*id >= '0' && *id <= '9') || *id == '_' || *id == '-'))
        {
            return 0U;
        }
    }
    return 1U;
}

int AppTelemetryJson_BatchId(
    char id[APP_BATCH_ID_CAPACITY], const char *boot_session, uint32_t sequence)
{
    unsigned i;
    int length;

    if (strlen(boot_session) != APP_BOOT_SESSION_HEX_LENGTH)
    {
        return -1;
    }
    for (i = 0U; i < APP_BOOT_SESSION_HEX_LENGTH; ++i)
    {
        if (!((boot_session[i] >= '0' && boot_session[i] <= '9') ||
                (boot_session[i] >= 'a' && boot_session[i] <= 'f')))
        {
            return -1;
        }
    }
    length = snprintf(
        id,
        APP_BATCH_ID_CAPACITY,
        "%s:%s:%lu",
        APP_DEVICE_ID,
        boot_session,
        (unsigned long)sequence);
    return length > 0 && (unsigned)length < APP_BATCH_ID_CAPACITY ? 0 : -1;
}

int AppTelemetryJson_Begin(
    AppTelemetryJson *writer,
    char *buffer,
    size_t capacity,
    const AppTelemetryBatch *batch,
    const char *batch_id,
    uint32_t now)
{
    if (!buffer || !capacity || batch->minute_count > APP_MINUTE_RECORDS ||
        batch->slow_count > APP_SLOW_RECORDS || batch->ph_present > 1U)
    {
        return -1;
    }
    writer->batch = batch;
    writer->buffer = buffer;
    writer->capacity = capacity;
    writer->offset = 0U;
    writer->group = GROUP_SOIL;
    writer->index = 0U;
    writer->complete = 0U;
    APP_JSON_APPEND(
        "{\"protocol\":\"" APP_TELEMETRY_PROTOCOL
        "\",\"schema_version\":\"" APP_PROTOCOL_SCHEMA_VERSION "\","
        "\"device_id\":\"%s\",\"seq\":%lu,\"batch_id\":\"%s\",\"upload_interval_s\":%u,"
        "\"window_start_ts\":null,\"window_end_ts\":null,\"upload_ts\":null,\"time_valid\":false,"
        "\"batch_status\":\"%s\",\"window_start_uptime_ms\":%lu,\"window_end_uptime_ms\":%lu,"
        "\"uptime_ms\":%lu,\"data\":{\"soil_moisture\":[",
        APP_DEVICE_ID,
        (unsigned long)batch->sequence,
        batch_id,
        TELEMETRY_UPLOAD_INTERVAL_MS / 1000U,
        AppJson_IsComplete(batch) ? "COMPLETE" : "PARTIAL",
        (unsigned long)batch->start_ms,
        (unsigned long)batch->end_ms,
        (unsigned long)now);
    return 0;
}

int AppTelemetryJson_Step(AppTelemetryJson *writer)
{
    const AppTelemetryBatch *batch = writer->batch;
    const unsigned counts[GROUP_COUNT] = {batch->slow_count,
        batch->ph_present,
        batch->minute_count,
        batch->minute_count,
        batch->slow_count};
    static const char *const groups[GROUP_COUNT] = {
        "soil_moisture", "soil_ph", "wind", "rain", "weather_box"};
    const unsigned index = writer->index;
    const uint32_t slow_slot = batch->start_ms + (index + 1U) * SOIL_MOISTURE_INTERVAL_MS;
    const uint32_t weather_slot = batch->start_ms + (index + 1U) * WEATHER_BOX_INTERVAL_MS;
    const char *comma = index ? "," : "";
    int result;

    if (writer->complete)
    {
        return (int)writer->offset;
    }
    if (index == counts[writer->group])
    {
        ++writer->group;
        writer->index = 0U;
        if (writer->group == GROUP_COUNT)
        {
            APP_JSON_APPEND("]},\"token\":%s}\r\n", APP_INGEST_TOKEN_JSON);
            if (writer->offset > APP_JSON_FRAME_BOUND)
            {
                return -1;
            }
            writer->complete = 1U;
            return (int)writer->offset;
        }
        APP_JSON_APPEND("],\"%s\":[", groups[writer->group]);
        return 0;
    }
    switch (writer->group)
    {
        case GROUP_SOIL:
            result = AppJson_Soil(writer, comma, slow_slot, &batch->soil[index]);
            break;
        case GROUP_PH:
            result = AppJson_Ph(writer, &batch->ph, batch->end_ms);
            break;
        case GROUP_WIND:
            result = AppJson_Wind(writer, comma, &batch->minute[index]);
            break;
        case GROUP_RAIN:
            result = AppJson_Rain(writer, comma, &batch->minute[index]);
            break;
        default:
            result = AppJson_Weather(writer, comma, weather_slot, &batch->weather[index]);
            break;
    }
    ++writer->index;
    return result;
}

/* 私有实现 */

static const char *AppJson_SampleQuality(unsigned valid, unsigned expected)
{
    if (valid == expected)
    {
        return "OK";
    }
    return valid ? "PARTIAL" : "INVALID";
}

static uint8_t AppJson_WeatherValid(const AppWeatherRecord *record, unsigned field)
{
    return record->status[field] && !strcmp(record->status[field], "OK");
}

static unsigned AppJson_WeatherCount(const AppWeatherRecord *record)
{
    unsigned valid = 0U;
    unsigned field;

    for (field = 0U; field < APP_WEATHER_FIELDS; ++field)
    {
        valid += AppJson_WeatherValid(record, field);
    }
    return valid;
}

/**
 * @brief 按五组记录数和有效标记判断批次是否完整。
 * @param batch 待编码批次。
 * @return 完整返回 1，否则返回 0。
 */
static uint8_t AppJson_IsComplete(const AppTelemetryBatch *batch)
{
    unsigned i;

    if (batch->minute_count != APP_MINUTE_RECORDS || batch->slow_count != APP_SLOW_RECORDS ||
        !batch->ph_present || !batch->ph.valid)
    {
        return 0U;
    }
    for (i = 0U; i < batch->minute_count; ++i)
    {
        const AppMinuteRecord *r = &batch->minute[i];

        if (r->speed_count != APP_MINUTE_SAMPLES || r->direction_quality != APP_DIRECTION_VALID ||
            r->direction_count != APP_MINUTE_SAMPLES || r->rain_count != APP_MINUTE_SAMPLES)
        {
            return 0U;
        }
    }
    for (i = 0U; i < batch->slow_count; ++i)
    {
        if (batch->soil[i].count != SOIL_ADC_SAMPLE_COUNT ||
            AppJson_WeatherCount(&batch->weather[i]) != APP_WEATHER_FIELDS)
        {
            return 0U;
        }
    }
    return 1U;
}

/* 定点输出固定使用小数点，避免浮点格式化和区域设置影响 JSON。 */
/**
 * @brief 在给定缓冲区生成定点 JSON 数字，或返回 null 字面量。
 * @param output 至少 16 字节的数字缓冲区。
 * @param valid 值的有效标记。
 * @param magnitude 缩放后的非负整数幅度。
 * @param decimals 小数位数。
 * @param negative 负号标记。
 * @return 用于写入 JSON 的数字或 null 字符串。
 */
static const char *AppJson_Number(
    char output[16], uint8_t valid, uint32_t magnitude, unsigned decimals, uint8_t negative)
{
    char *cursor = output + 15;
    unsigned digits = 0U;

    if (!valid)
    {
        return "null";
    }
    *cursor = '\0';
    do
    {
        if (decimals && digits == decimals)
        {
            *--cursor = '.';
        }
        *--cursor = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
        ++digits;
    } while (magnitude || digits <= decimals);
    if (negative)
    {
        *--cursor = '-';
    }
    return cursor;
}

static int AppJson_Soil(
    AppTelemetryJson *writer, const char *comma, uint32_t slot, const AppSoilRecord *r)
{
    char value[16];

    APP_JSON_APPEND(
        "%s{\"ts\":null,\"uptime_ms\":%lu,\"slot_uptime_ms\":%lu,\"moisture_pct\":%s,"
        "\"sample_count\":%u,\"valid_count\":%u,\"aggregation\":\"trimmed_mean\",\"quality\":\"%s\"}",
        comma,
        (unsigned long)r->t_ms,
        (unsigned long)slot,
        AppJson_Number(value, r->count == SOIL_ADC_SAMPLE_COUNT, r->value, 2U, 0U),
        SOIL_ADC_SAMPLE_COUNT,
        (unsigned)r->count,
        AppJson_SampleQuality(r->count, SOIL_ADC_SAMPLE_COUNT));
    return 0;
}

static int AppJson_Ph(AppTelemetryJson *writer, const AppPhRecord *r, uint32_t slot)
{
    char value[16];

    APP_JSON_APPEND(
        "{\"ts\":null,\"uptime_ms\":%lu,\"slot_uptime_ms\":%lu,\"ph\":%s,\"quality\":\"%s\"}",
        (unsigned long)r->t_ms,
        (unsigned long)slot,
        AppJson_Number(value, r->valid, r->value, 2U, 0U),
        r->valid ? "OK" : "INVALID");
    return 0;
}

static int AppJson_Wind(AppTelemetryJson *writer, const char *comma, const AppMinuteRecord *r)
{
    char speed[16];
    char direction[16];
    const uint8_t direction_valid = r->direction_quality == APP_DIRECTION_VALID;
    const char *direction_quality = "INVALID";

    if (direction_valid)
    {
        direction_quality = AppJson_SampleQuality(r->direction_count, APP_MINUTE_SAMPLES);
    }
    if (r->direction_quality == APP_DIRECTION_CALM)
    {
        direction_quality = "CALM";
    }
    APP_JSON_APPEND(
        "%s{\"ts\":null,\"uptime_ms\":%lu,\"speed_avg_m_s\":%s,\"direction_avg_deg\":%s,"
        "\"sample_count\":%u,\"speed_valid_count\":%u,\"direction_valid_count\":%u,"
        "\"speed_aggregation\":\"arithmetic_mean\",\"direction_aggregation\":\"circular_mean\","
        "\"quality\":\"%s\",\"direction_quality\":\"%s\"}",
        comma,
        (unsigned long)r->t_ms,
        AppJson_Number(speed, r->speed_count != 0U, r->speed, 2U, 0U),
        AppJson_Number(direction, direction_valid, r->direction, 2U, 0U),
        APP_MINUTE_SAMPLES,
        (unsigned)r->speed_count,
        (unsigned)r->direction_count,
        AppJson_SampleQuality(r->speed_count, APP_MINUTE_SAMPLES),
        direction_quality);
    return 0;
}

static int AppJson_Rain(AppTelemetryJson *writer, const char *comma, const AppMinuteRecord *r)
{
    char average[16];
    char peak[16];
    const uint8_t valid = r->rain_count != 0U;

    APP_JSON_APPEND(
        "%s{\"ts\":null,\"uptime_ms\":%lu,\"intensity_avg_mm_min\":%s,\"intensity_max_mm_min\":%s,"
        "\"sample_count\":%u,\"valid_count\":%u,\"aggregation\":\"arithmetic_mean\",\"quality\":\"%s\"}",
        comma,
        (unsigned long)r->t_ms,
        AppJson_Number(average, valid, r->rain, 2U, 0U),
        AppJson_Number(peak, valid, r->rain_peak, 2U, 0U),
        APP_MINUTE_SAMPLES,
        (unsigned)r->rain_count,
        AppJson_SampleQuality(r->rain_count, APP_MINUTE_SAMPLES));
    return 0;
}

static int AppJson_Weather(
    AppTelemetryJson *writer, const char *comma, uint32_t slot, const AppWeatherRecord *r)
{
    char temp[16];
    char humidity[16];
    char light[16];
    char pressure[16];
    char pm25[16];
    char pm10[16];
    const uint8_t negative = r->temperature < 0;
    const uint32_t temperature =
        negative ? (uint32_t)(-(int32_t)r->temperature) : (uint32_t)r->temperature;

    APP_JSON_APPEND(
        "%s{\"ts\":null,\"uptime_ms\":%lu,\"slot_uptime_ms\":%lu,\"temperature_c\":%s,"
        "\"humidity_rh_pct\":%s,\"light_lx\":%s,\"pressure_kpa\":%s,\"pm25_ug_m3\":%s,"
        "\"pm10_ug_m3\":%s,\"quality\":\"%s\"}",
        comma,
        (unsigned long)r->t_ms,
        (unsigned long)slot,
        AppJson_Number(temp, AppJson_WeatherValid(r, 0U), temperature, 1U, negative),
        AppJson_Number(humidity, AppJson_WeatherValid(r, 1U), r->humidity, 1U, 0U),
        AppJson_Number(light, AppJson_WeatherValid(r, 2U), r->light, 0U, 0U),
        AppJson_Number(pressure, AppJson_WeatherValid(r, 3U), r->pressure, 2U, 0U),
        AppJson_Number(pm25, AppJson_WeatherValid(r, 4U), r->pm25, 0U, 0U),
        AppJson_Number(pm10, AppJson_WeatherValid(r, 5U), r->pm10, 0U, 0U),
        AppJson_SampleQuality(AppJson_WeatherCount(r), APP_WEATHER_FIELDS));
    return 0;
}

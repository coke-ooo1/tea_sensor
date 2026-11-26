/**
 * @file app_data.h
 * @brief 应用层批次模型，定义窗口、各传感器记录与数据质量字段。
 */

#ifndef APP_DATA_H
#define APP_DATA_H
#include "app_config.h"
#include <stdint.h>

#define APP_MINUTE_RECORDS (TELEMETRY_UPLOAD_INTERVAL_MS / WIND_RECORD_INTERVAL_MS)
#define APP_SLOW_RECORDS (TELEMETRY_UPLOAD_INTERVAL_MS / SOIL_MOISTURE_INTERVAL_MS)
#define APP_MINUTE_SAMPLES (WIND_RECORD_INTERVAL_MS / WIND_SAMPLE_INTERVAL_MS)
#define APP_WEATHER_FIELDS 6U

typedef enum
{
    APP_DIRECTION_INSUFFICIENT,
    APP_DIRECTION_VALID,
    APP_DIRECTION_CALM,
    APP_DIRECTION_VARIABLE
} AppDirectionQuality;

typedef struct
{
    uint32_t t_ms;
    /* 风速、风向和雨强分别以 m/s、°、mm/min 的百分之一为单位。 */
    uint16_t speed;
    uint16_t speed_peak;
    uint16_t direction;
    uint16_t rain;
    uint16_t rain_peak;

    uint8_t speed_count;
    uint8_t pair_count;
    uint8_t direction_count;
    uint8_t rain_count;
    uint8_t direction_quality;
} AppMinuteRecord;

typedef struct
{
    uint32_t t_ms;
    uint16_t value; /* 相对湿润度，百分之一百分点。 */
    uint8_t count;
    uint8_t range;
} AppSoilRecord;

typedef struct
{
    uint32_t t_ms;
    uint16_t value; /* pH x100 */
    uint8_t valid;
    uint8_t range;
} AppPhRecord;

typedef struct
{
    uint32_t t_ms;

    uint32_t light;
    int16_t temperature; /* 0.1 ℃ */
    uint16_t humidity;   /* 0.1 %RH */
    uint16_t pressure;   /* 0.01 kPa */
    uint16_t pm25;       /* μg/m³ */
    uint16_t pm10;       /* μg/m³ */

    const char *status[APP_WEATHER_FIELDS]; /* 各气象字段的质量名称。 */
} AppWeatherRecord;

typedef struct
{
    uint32_t sequence;
    uint32_t start_ms;
    uint32_t end_ms;

    AppMinuteRecord minute[APP_MINUTE_RECORDS];
    AppSoilRecord soil[APP_SLOW_RECORDS];
    AppWeatherRecord weather[APP_SLOW_RECORDS];
    AppPhRecord ph;

    uint8_t minute_count;
    uint8_t slow_count;
    uint8_t ph_present;
} AppTelemetryBatch;

#endif

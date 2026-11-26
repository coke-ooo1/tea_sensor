/**
 * @file upload_wire_fixture.c
 * @brief 遥测测试帧生成器，用于校验 V1.0 序列化及容量边界。
 */

#include "app_telemetry_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static int Test_Format(
    char *buffer, size_t capacity, const AppTelemetryBatch *batch, const char *id)
{
    AppTelemetryJson writer;
    int result = AppTelemetryJson_Begin(&writer, buffer, capacity, batch, id, UINT32_MAX);
    unsigned steps = 0U;

    while (result == 0)
    {
        result = AppTelemetryJson_Step(&writer);
        assert(++steps <= 42U);
    }
    return result;
}

int main(int argc, char **argv)
{
    static char buffer[APP_UPLOAD_FRAME_CAPACITY];
    static AppTelemetryBatch batch;
    char id[APP_BATCH_ID_CAPACITY];
    unsigned i;
    unsigned j;
    int length;

    if (argc == 2 && !strcmp(argv[1], "missing"))
    {
        assert(!AppTelemetryJson_IsConfigured());
        return 0;
    }
    assert(AppTelemetryJson_IsConfigured());
    batch.start_ms = UINT32_MAX - TELEMETRY_UPLOAD_INTERVAL_MS;
    batch.end_ms = batch.sequence = UINT32_MAX;
    batch.minute_count = APP_MINUTE_RECORDS;
    batch.slow_count = APP_SLOW_RECORDS;
    batch.ph_present = 1U;
    for (i = 0U; i < APP_MINUTE_RECORDS; ++i)
    {
        AppMinuteRecord *r = &batch.minute[i];

        r->t_ms = batch.start_ms + (i + 1U) * WIND_RECORD_INTERVAL_MS;
        r->speed = r->direction = r->rain = r->rain_peak = UINT16_MAX;
        r->direction_quality = APP_DIRECTION_VALID;
        r->speed_count = r->direction_count = r->rain_count = APP_MINUTE_SAMPLES - 1U;
    }
    for (i = 0U; i < APP_SLOW_RECORDS; ++i)
    {
        AppWeatherRecord *r = &batch.weather[i];

        batch.soil[i].t_ms = r->t_ms = batch.start_ms + (i + 1U) * SOIL_MOISTURE_INTERVAL_MS;
        batch.soil[i].value = UINT16_MAX;
        batch.soil[i].count = SOIL_ADC_SAMPLE_COUNT;
        r->temperature = INT16_MIN;
        r->light = UINT32_MAX;
        r->humidity = r->pressure = r->pm25 = r->pm10 = UINT16_MAX;
        for (j = 0U; j < APP_WEATHER_FIELDS; ++j)
        {
            r->status[j] = "OK";
        }
    }
    batch.ph.t_ms = UINT32_MAX;
    batch.ph.value = UINT16_MAX;
    batch.ph.valid = 1U;
    if (argc == 2 && !strcmp(argv[1], "partial"))
    {
        batch.minute_count = 2U;
        batch.slow_count = 1U;
        batch.ph_present = 0U;
        batch.soil[0].count = 9U;
        batch.minute[0].speed = 0U;
        batch.minute[0].speed_count = 1U;
        batch.minute[0].direction_quality = APP_DIRECTION_CALM;
        batch.minute[0].direction_count = 0U;
        batch.minute[0].rain = batch.minute[0].rain_peak = 0U;
        batch.minute[0].rain_count = APP_MINUTE_SAMPLES;
        batch.minute[1].speed_count = batch.minute[1].direction_count = batch.minute[1].rain_count =
            0U;
        batch.minute[1].direction_quality = APP_DIRECTION_INSUFFICIENT;
        batch.weather[0].status[2] = "TIMEOUT";
    }
    assert(AppTelemetryJson_BatchId(id, "112233445566778899aabbccddeeff00", batch.sequence) == 0);
    length = Test_Format(buffer, sizeof(buffer), &batch, id);
    assert(length > 0 && (size_t)length < sizeof(buffer));
    assert(Test_Format(buffer, (size_t)length, &batch, id) < 0);
    assert(Test_Format(buffer, (size_t)length + 1U, &batch, id) == length);
#ifdef _WIN32
    assert(_setmode(_fileno(stdout), _O_BINARY) >= 0);
#endif
    return fwrite(buffer, 1U, (size_t)length, stdout) == (size_t)length ? 0 : 1;
}

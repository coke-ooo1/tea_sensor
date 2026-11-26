/**
 * @file app_main.c
 * @brief 应用层采集调度，协调分钟统计、慢速采集、批次封存和后台上传。
 */

#include "app_main.h"
#include "app_diagnostics.h"
#include "app_batch.h"
#include "app_statistics.h"
#include "app_upload.h"
#include "app_weather.h"
#include "bsp_time.h"
#include <string.h>

/* 模块数据 */

static AppTelemetryBatch s_batch;
static AppDiagnostics s_diagnostics;
static uint32_t s_next_sample;
static uint32_t s_batch_start;
static uint32_t s_next_soil;
static unsigned s_minutes;
static unsigned s_slow_slot;
static unsigned s_soil_slots;
static uint8_t s_slow_active;
static uint8_t s_ph_done;

/* 私有函数声明 */

static uint8_t App_Due(uint32_t now, uint32_t deadline);
static void App_StartBatch(uint32_t start, uint32_t sequence);
static void App_RecordAdcError(AppAdcDiagnostics *d, const BspAdcResult *a);
static void App_AcquireMinuteSample(uint32_t now);
static void App_ProcessSoil(uint32_t now);
static void App_ProcessSlow(uint32_t now);
static void App_ReadPh(void);
static void App_CloseMinutes(uint32_t now);

/* 公共接口 */

void App_Init(void)
{
    memset(&s_diagnostics, 0, sizeof(s_diagnostics));
    AppBatch_Init();
    AppUpload_Init();
    BspAdc_Init();
    AppWeather_Init();
    AppStatistics_Init();
    s_next_sample = BspTime_GetTickMs();
    App_StartBatch(s_next_sample, 1U);
}

void App_Process(void)
{
    uint32_t now;

    AppUpload_Process();
    BspAdc_Process();
    AppWeather_Process();
    now = BspTime_GetTickMs();
    App_CloseMinutes(now);
    if (App_Due(now, s_batch.end_ms) && !s_ph_done)
    {
        s_ph_done = 1U;
        if (!App_Due(now, s_batch.end_ms + WEATHER_BOX_TIMEOUT_MS))
        {
            App_ReadPh();
        }
    }
    now = BspTime_GetTickMs();
    App_ProcessSlow(now);
    if (s_ph_done && s_slow_slot == APP_SLOW_RECORDS)
    {
        uint32_t start = s_batch.end_ms;
        uint32_t sequence = s_batch.sequence + 1U;
        uint32_t expiration = start + TELEMETRY_UPLOAD_INTERVAL_MS + WEATHER_BOX_TIMEOUT_MS;

        s_batch.minute_count = (uint8_t)s_minutes;
        s_batch.slow_count = (uint8_t)s_slow_slot;
        s_batch.ph_present = s_ph_done;
        AppBatch_Enqueue(&s_batch);
        ++s_diagnostics.completed_windows;
        s_diagnostics.last_window_end_ms = start;
        if (App_Due(now, expiration))
        {
            uint32_t skipped = 1U + (now - expiration) / TELEMETRY_UPLOAD_INTERVAL_MS;

            s_diagnostics.missed_batches += skipped;
            start += skipped * TELEMETRY_UPLOAD_INTERVAL_MS;
            sequence += skipped;
            AppStatistics_Init();
        }
        App_StartBatch(start, sequence);
    }
    now = BspTime_GetTickMs();
    App_CloseMinutes(now);
    if (App_Due(now, s_next_sample))
    {
        s_next_sample +=
            (1U + (now - s_next_sample) / WIND_SAMPLE_INTERVAL_MS) * WIND_SAMPLE_INTERVAL_MS;
        App_AcquireMinuteSample(now);
    }
}

const AppDiagnostics *App_GetDiagnostics(void)
{
    return &s_diagnostics;
}

/* 私有实现 */

/**
 * @brief 用有符号 tick 差比较截止时间，兼容计数回绕。
 * @param now 当前 tick，单位 ms。
 * @param deadline 截止 tick，单位 ms。
 * @return 到期返回 1，否则返回 0。
 * @note 比较的两个 tick 相差应小于半个计数周期。
 */
static uint8_t App_Due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

/**
 * @brief 建立新批次窗口，并预填每个慢速槽的时间和初始气象状态。
 * @param start 窗口起始 tick，单位 ms。
 * @param sequence 新批次序号。
 */
static void App_StartBatch(uint32_t start, uint32_t sequence)
{
    unsigned index;
    unsigned field;

    memset(&s_batch, 0, sizeof(s_batch));
    s_batch_start = start;
    s_batch.start_ms = start;
    s_batch.end_ms = start + TELEMETRY_UPLOAD_INTERVAL_MS;
    s_batch.sequence = sequence;
    s_batch.ph.t_ms = s_batch.end_ms;
    for (index = 0U; index < APP_SLOW_RECORDS; ++index)
    {
        uint32_t due = start + (index + 1U) * SOIL_MOISTURE_INTERVAL_MS;

        s_batch.soil[index].t_ms = due;
        s_batch.weather[index].t_ms = due;
        for (field = 0U; field < APP_WEATHER_FIELDS; ++field)
        {
            s_batch.weather[index].status[field] = "TIMEOUT";
        }
    }
    s_minutes = s_slow_slot = s_soil_slots = 0U;
    s_slow_active = s_ph_done = 0U;
}

static void App_RecordAdcError(AppAdcDiagnostics *d, const BspAdcResult *a)
{
    if (a->valid)
    {
        return;
    }
    if ((unsigned)a->status <= BSP_ADC_OUT_OF_RANGE)
    {
        ++d->error_counts[a->status];
    }
    if (a->stop_attempted && a->stop_status != BSP_ADC_HAL_OK)
    {
        ++d->cleanup_errors;
    }
    if (a->prepare_attempted && a->prepare_status != BSP_ADC_HAL_OK)
    {
        ++d->prepare_errors;
    }
    if (a->config_attempted && a->config_status != BSP_ADC_HAL_OK)
    {
        ++d->config_errors;
    }
    d->last_error = *a;
}

static void App_AcquireMinuteSample(uint32_t now)
{
    AppSampleRound *sample = &s_diagnostics.last_round;

    sample->round_id = ++s_diagnostics.sequence;
    sample->round_t_ms = now;
    sample->wind_read_start_ms = BspTime_GetTickMs();
    sample->wind = SensorWindSpeed_Read();
    sample->wind_read_end_ms = BspTime_GetTickMs();
    sample->ad3_read_start_ms = sample->wind_read_end_ms;
    sample->ad3 = BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_DIRECTION_AD3);
    sample->ad3_read_end_ms = BspTime_GetTickMs();
    sample->wind_direction.valid = 0U;
    if (sample->ad3.valid && sample->ad3.status == BSP_ADC_OK)
    {
        sample->wind_direction = SensorWindDirection_Convert((uint32_t)sample->ad3.raw * 100U);
    }
    sample->ad5 = BspAdc_ReadRaw(BSP_ADC_INPUT_RAIN_AD5);
    sample->ad5_read_end_ms = BspTime_GetTickMs();
    sample->round_span_ms = sample->ad5_read_end_ms - now;
    s_diagnostics.last_ad5 = sample->ad5;
    s_diagnostics.ad5_sample_t_ms = sample->ad5_read_end_ms;
    ++s_diagnostics.ad5_sequence;
    if (!sample->wind.adc.valid)
    {
        const BspAdcResult *error = &sample->wind.adc;
        SensorWindSpeedDiagnostics *wind = &s_diagnostics.wind_diagnostics;

        ++s_diagnostics.wind_sample_errors;
        if ((unsigned)error->status <= BSP_ADC_OUT_OF_RANGE)
        {
            ++wind->error_counts[error->status];
        }
        if (error->stop_attempted && error->stop_status != BSP_ADC_HAL_OK)
        {
            ++wind->cleanup_error_count;
        }
        wind->last_error = *error;
    }
    if (!sample->ad3.valid)
    {
        ++s_diagnostics.ad3_sample_errors;
    }
    if (!sample->ad5.valid)
    {
        ++s_diagnostics.ad5_sample_errors;
    }
    App_RecordAdcError(&s_diagnostics.ad3_diagnostics, &sample->ad3);
    App_RecordAdcError(&s_diagnostics.ad5_diagnostics, &sample->ad5);
    AppStatistics_AddMinuteSample(sample);
}

/**
 * @brief 按计划槽收集土壤原码，跳过错过的槽并遵守该组截止时间。
 * @param now 当前 tick，单位 ms。
 */
static void App_ProcessSoil(uint32_t now)
{
    AppSampleRound *sample = &s_diagnostics.last_round;
    AppSoilRecord *record = &s_batch.soil[s_slow_slot];
    uint32_t elapsed_slots;

    if (!App_Due(now, s_next_soil) || s_soil_slots >= SOIL_ADC_SAMPLE_COUNT)
    {
        return;
    }
    elapsed_slots = (now - s_next_soil) / SOIL_ADC_SAMPLE_INTERVAL_MS;
    if (elapsed_slots < SOIL_ADC_SAMPLE_COUNT - s_soil_slots)
    {
        s_next_soil += (elapsed_slots + 1U) * SOIL_ADC_SAMPLE_INTERVAL_MS;
        s_soil_slots += elapsed_slots + 1U;
        sample->soil_read_start_ms = now;
        sample->soil = SensorSoil_Read();
        sample->soil_read_end_ms = BspTime_GetTickMs();
        uint32_t deadline =
            s_batch_start + (s_slow_slot + 1U) * SOIL_MOISTURE_INTERVAL_MS + WEATHER_BOX_TIMEOUT_MS;

        if (!App_Due(sample->soil_read_end_ms, deadline))
        {
            record->t_ms = sample->soil_read_end_ms;
            AppStatistics_AddSoilSample(&sample->soil.adc);
        }
        if (!sample->soil.adc.valid)
        {
            const BspAdcResult *error = &sample->soil.adc;

            ++s_diagnostics.sample_errors;
            if ((unsigned)error->status <= BSP_ADC_OUT_OF_RANGE)
            {
                ++s_diagnostics.sample_error_counts[error->status];
            }
            if (error->stop_attempted && error->stop_status != BSP_ADC_HAL_OK)
            {
                ++s_diagnostics.cleanup_errors;
            }
            if (error->prepare_attempted && error->prepare_status != BSP_ADC_HAL_OK)
            {
                ++s_diagnostics.prepare_errors;
            }
            if (error->config_attempted && error->config_status != BSP_ADC_HAL_OK)
            {
                ++s_diagnostics.config_errors;
            }
            s_diagnostics.last_adc_error = *error;
        }
    }
    else
    {
        s_soil_slots = SOIL_ADC_SAMPLE_COUNT;
    }
    if (s_soil_slots == SOIL_ADC_SAMPLE_COUNT)
    {
        AppStatistics_FinishSoil(record);
    }
}

/**
 * @brief 推进慢速槽内的土壤和气象采集，在组完成或截止时结算。
 * @param now 当前 tick，单位 ms。
 */
static void App_ProcessSlow(uint32_t now)
{
    while (s_slow_slot < APP_SLOW_RECORDS)
    {
        uint32_t due = s_batch_start + (s_slow_slot + 1U) * SOIL_MOISTURE_INTERVAL_MS;
        uint32_t deadline = due + WEATHER_BOX_TIMEOUT_MS;

        if (!App_Due(now, due))
        {
            return;
        }
        if (!s_slow_active && !App_Due(now, deadline))
        {
            s_slow_active = 1U;
            s_soil_slots = 0U;
            s_next_soil = now;
            AppWeather_BeginGroup(&s_batch.weather[s_slow_slot], now, deadline);
        }
        if (s_slow_active)
        {
            if (App_Due(now, deadline) && s_soil_slots < SOIL_ADC_SAMPLE_COUNT)
            {
                AppStatistics_FinishSoil(&s_batch.soil[s_slow_slot]);
                s_soil_slots = SOIL_ADC_SAMPLE_COUNT;
            }
            else
            {
                App_ProcessSoil(now);
            }
            if (s_soil_slots < SOIL_ADC_SAMPLE_COUNT || AppWeather_GroupPending())
            {
                return;
            }
        }
        s_slow_active = 0U;
        ++s_slow_slot;
    }
}

static void App_ReadPh(void)
{
    AppSampleRound *sample = &s_diagnostics.last_round;

    sample->ad4_read_start_ms = BspTime_GetTickMs();
    sample->ad4 = BspAdc_ReadRaw(BSP_ADC_INPUT_PH_AD4);
    sample->ad4_read_end_ms = BspTime_GetTickMs();
    sample->soil_ph = SensorPh_Convert(sample->ad4.raw, sample->ad4.valid ? 1U : 0U);
    if (!App_Due(sample->ad4_read_end_ms, s_batch.end_ms + WEATHER_BOX_TIMEOUT_MS))
    {
        s_batch.ph.t_ms = sample->ad4_read_end_ms;
        s_batch.ph.value = (uint16_t)sample->soil_ph.soil_ph_x100;
        s_batch.ph.valid = sample->soil_ph.valid;
        s_batch.ph.range = (uint8_t)sample->soil_ph.range;
    }
    if (!sample->ad4.valid)
    {
        ++s_diagnostics.ad4_sample_errors;
    }
    App_RecordAdcError(&s_diagnostics.ad4_diagnostics, &sample->ad4);
}

static void App_CloseMinutes(uint32_t now)
{
    while (s_minutes < APP_MINUTE_RECORDS)
    {
        uint32_t end = s_batch_start + (s_minutes + 1U) * WIND_RECORD_INTERVAL_MS;

        if (!App_Due(now, end))
        {
            break;
        }
        AppStatistics_FinishMinute(end, &s_batch.minute[s_minutes]);
        ++s_minutes;
    }
}

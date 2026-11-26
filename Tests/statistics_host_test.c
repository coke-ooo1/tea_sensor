/**
 * @file statistics_host_test.c
 * @brief 窗口统计主机测试，验证风向、雨强和土壤截尾平均。
 */

#include "app_statistics.h"
#include <assert.h>
#include <stdio.h>

BspAdcResult BspAdc_ReadRaw(BspAdcInput input)
{
    BspAdcResult result = {0};

    (void)input;
    assert(!"Statistics do not own ADC sampling");
    return result;
}

int main(void)
{
    AppSampleRound sample = {0};
    AppMinuteRecord minute;
    AppSoilRecord soil = {0};
    BspAdcResult adc = {0};
    const uint16_t soil_samples[SOIL_ADC_SAMPLE_COUNT] = {
        65535U, 15791U, 15791U, 15791U, 15791U, 0U, 15791U, 15791U, 15791U, 15791U};
    unsigned i;

    AppStatistics_Init();
    sample.wind.adc.valid = sample.wind.value.valid = sample.wind_direction.valid = 1U;
    sample.ad5.valid = 1U;
    for (i = 0U; i < APP_MINUTE_SAMPLES; ++i)
    {
        sample.round_t_ms = i * WIND_SAMPLE_INTERVAL_MS;
        sample.wind.value.wind_speed_mps = i % 2U ? 6.0 : 2.0;
        {
            static const uint16_t north_samples[] = {35000U, 35500U, 500U, 1000U};

            sample.wind_direction.angle_deg_x100 = north_samples[i % 4U];
        }
        sample.ad5.raw = (i == 11U) ? 65535U : 0U;
        sample.ad5.valid = (i != 3U);
        AppStatistics_AddMinuteSample(&sample);
    }
    AppStatistics_FinishMinute(WIND_RECORD_INTERVAL_MS, &minute);
    assert(minute.t_ms == 60000U && minute.speed == 400U && minute.speed_peak == 600U);
    assert(minute.speed_count == 12U && minute.pair_count == 12U && minute.direction_count == 12U);
    assert(minute.direction_quality == APP_DIRECTION_VALID && minute.direction == 0U);
    assert(minute.rain_count == 11U && minute.rain == 73U && minute.rain_peak == 800U);

    sample.wind.value.wind_speed_mps = 0.0;
    sample.ad5.raw = 0U;
    AppStatistics_AddMinuteSample(&sample);
    AppStatistics_FinishMinute(120000U, &minute);
    assert(
        minute.speed == 0U && minute.speed_count == 1U &&
        minute.direction_quality == APP_DIRECTION_CALM);
    assert(minute.rain_count == 1U && minute.rain == 0U);
    AppStatistics_FinishMinute(180000U, &minute);
    assert(!minute.rain_count && !minute.speed_count && minute.t_ms == 180000U);

    sample.wind.value.wind_speed_mps = 1.0;
    for (i = 0U; i < 4U; ++i)
    {
        sample.wind_direction.angle_deg_x100 = (uint16_t)(9000U * i);
        AppStatistics_AddMinuteSample(&sample);
    }
    AppStatistics_FinishMinute(240000U, &minute);
    assert(minute.direction_quality == APP_DIRECTION_VARIABLE);

    adc.valid = 1U;
    adc.status = BSP_ADC_OK;
    for (i = 0U; i < SOIL_ADC_SAMPLE_COUNT; ++i)
    {
        adc.raw = soil_samples[i];
        AppStatistics_AddSoilSample(&adc);
    }
    AppStatistics_FinishSoil(&soil);
    assert(soil.count == 10U && soil.value == 5000U && soil.range == SENSOR_SOIL_RANGE_OK);
    for (i = 0U; i < SOIL_ADC_SAMPLE_COUNT; ++i)
    {
        adc.valid = i < SOIL_ADC_SAMPLE_COUNT - 1U;
        AppStatistics_AddSoilSample(&adc);
    }
    AppStatistics_FinishSoil(&soil);
    assert(soil.count == 9U);

    fprintf(
        stderr, "PASS: minute means, circular direction, rain peak/zero/failure and soil trim\n");
    return 0;
}

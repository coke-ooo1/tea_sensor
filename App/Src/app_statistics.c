/**
 * @file app_statistics.c
 * @brief 应用层窗口统计，计算风雨分钟聚合、圆周风向及土壤截尾平均。
 */

#include "app_statistics.h"
#include <math.h>
#include <string.h>

/* 宏定义与编译期检查 */

#define PI 3.14159265358979323846

/* 模块数据 */

static struct
{
    double speed_sum;
    double speed_peak;
    double east;
    double north;
    double rain_sum;
    double rain_peak;
    uint8_t speed_count;
    uint8_t pair_count;
    uint8_t direction_count;
    uint8_t rain_count;
} s_minute;

static struct
{
    uint32_t sum;
    uint16_t min;
    uint16_t max;
    uint8_t count;
} s_soil;

/* 私有函数声明 */

static uint16_t AppStatistics_Scale(double value);

/* 公共接口 */

void AppStatistics_Init(void)
{
    memset(&s_minute, 0, sizeof(s_minute));
    memset(&s_soil, 0, sizeof(s_soil));
    s_soil.min = UINT16_MAX;
}

void AppStatistics_AddMinuteSample(const AppSampleRound *sample)
{
    if (sample->wind.adc.valid && sample->wind.value.valid)
    {
        double speed = sample->wind.value.wind_speed_mps;

        ++s_minute.speed_count;
        s_minute.speed_sum += speed;
        if (speed > s_minute.speed_peak)
        {
            s_minute.speed_peak = speed;
        }
        if (sample->wind_direction.valid)
        {
            ++s_minute.pair_count;
            if (speed * 100.0 >= APP_WIND_CALM_MPS_X100)
            {
                double radians = sample->wind_direction.angle_deg_x100 * (PI / 18000.0);

                s_minute.east += sin(radians);
                s_minute.north += cos(radians);
                ++s_minute.direction_count;
            }
        }
    }

    if (sample->ad5.valid)
    {
        SensorRainData rain = SensorRain_Convert(sample->ad5.raw, 1U);

        if (rain.valid)
        {
            ++s_minute.rain_count;
            s_minute.rain_sum += rain.intensity_mm_min;
            if (rain.intensity_mm_min > s_minute.rain_peak)
            {
                s_minute.rain_peak = rain.intensity_mm_min;
            }
        }
    }
}

void AppStatistics_FinishMinute(uint32_t end_ms, AppMinuteRecord *record)
{
    double speed = 0.0;

    memset(record, 0, sizeof(*record));
    record->t_ms = end_ms;
    record->speed_count = s_minute.speed_count;
    record->pair_count = s_minute.pair_count;
    record->direction_count = s_minute.direction_count;
    record->rain_count = s_minute.rain_count;
    if (s_minute.speed_count)
    {
        speed = s_minute.speed_sum / s_minute.speed_count;
        record->speed = AppStatistics_Scale(speed);
        record->speed_peak = AppStatistics_Scale(s_minute.speed_peak);
    }
    if (s_minute.speed_count && speed * 100.0 < APP_WIND_CALM_MPS_X100)
    {
        record->direction_quality = APP_DIRECTION_CALM;
    }
    else if (s_minute.direction_count >= 3U)
    {
        double resultant = sqrt(s_minute.east * s_minute.east + s_minute.north * s_minute.north);

        if (resultant * 1000.0 < APP_DIRECTION_MIN_R_X1000 * s_minute.direction_count)
        {
            record->direction_quality = APP_DIRECTION_VARIABLE;
        }
        else
        {
            double angle = atan2(s_minute.east, s_minute.north) * (18000.0 / PI);

            if (angle < 0.0)
            {
                angle += 36000.0;
            }
            record->direction = (uint16_t)((uint32_t)(angle + 0.5) % 36000U);
            record->direction_quality = APP_DIRECTION_VALID;
        }
    }
    if (s_minute.rain_count)
    {
        record->rain = AppStatistics_Scale(s_minute.rain_sum / s_minute.rain_count);
        record->rain_peak = AppStatistics_Scale(s_minute.rain_peak);
    }
    memset(&s_minute, 0, sizeof(s_minute));
}

void AppStatistics_AddSoilSample(const BspAdcResult *sample)
{
    if (!sample->valid || sample->status != BSP_ADC_OK)
    {
        return;
    }
    ++s_soil.count;
    s_soil.sum += sample->raw;
    if (sample->raw < s_soil.min)
    {
        s_soil.min = sample->raw;
    }
    if (sample->raw > s_soil.max)
    {
        s_soil.max = sample->raw;
    }
}

void AppStatistics_FinishSoil(AppSoilRecord *record)
{
    SensorSoilRange range;

    record->count = s_soil.count;
    if (record->count == SOIL_ADC_SAMPLE_COUNT)
    {
        record->value = SensorSoil_ConvertMean(
            s_soil.sum - s_soil.max - s_soil.min, SOIL_ADC_SAMPLE_COUNT - 2U, &range);
        record->range = (uint8_t)range;
    }
    memset(&s_soil, 0, sizeof(s_soil));
    s_soil.min = UINT16_MAX;
}

/* 私有实现 */

static uint16_t AppStatistics_Scale(double value)
{
    return (uint16_t)(value * 100.0 + 0.5);
}

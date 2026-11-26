/**
 * @file sensor_rain.h
 * @brief 雨强标定，按 AD5 原码均值换算雨强和范围状态。
 */

#ifndef SENSOR_RAIN_H
#define SENSOR_RAIN_H
#include <stdint.h>
#include "bsp_adc.h"
/* AD5 / PA3 / ADC1 CH15. User-labelled 0V/5V printed-record means,
 * nominal 0..8 mm/min electrical mapping; not rainfall accuracy certification. */
#define SENSOR_RAIN_RAW_ZERO_X100 14379U
#define SENSOR_RAIN_RAW_FULL_X100 3130255U
#define SENSOR_RAIN_RAW_SPAN_X100 (SENSOR_RAIN_RAW_FULL_X100 - SENSOR_RAIN_RAW_ZERO_X100)
#define SENSOR_RAIN_INTENSITY_MAX_X100 800U

typedef enum
{
    SENSOR_RAIN_RANGE_UNAVAILABLE = 0,
    SENSOR_RAIN_RANGE_OK,
    SENSOR_RAIN_RANGE_LOW,
    SENSOR_RAIN_RANGE_HIGH
} SensorRainRange;

typedef struct
{
    uint8_t valid;
    uint32_t rain_intensity_mm_min_x100; /* mm/min x100; display 0..800. */
    double intensity_mm_min;             /* Unrounded physical value for time integration. */
    SensorRainRange range;               /* Unrounded, unclamped raw fraction. */
} SensorRainData;

/**
 * @brief 将 AD5 原码均值转换为雨强。
 * @param raw_sum 有效 ADC 原码之和。
 * @param count 原码样本数。
 * @return 标定范围状态、显示雨强及用于统计的雨强，单位 mm/min。
 */
SensorRainData SensorRain_Convert(uint64_t raw_sum, uint32_t count);

/**
 * @brief 读取雨强范围状态的固定名称。
 * @param range 雨强范围。
 * @return 只读名称字符串。
 */
const char *SensorRain_RangeName(SensorRainRange range);
#endif

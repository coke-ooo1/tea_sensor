/**
 * @file sensor_wind_direction.h
 * @brief 风向标定，按 AD3 原码计算角度与范围并保留端子电压。
 */

#ifndef SENSOR_WIND_DIRECTION_H
#define SENSOR_WIND_DIRECTION_H

#include <stdint.h>

typedef enum
{
    SENSOR_WIND_DIRECTION_RANGE_UNAVAILABLE = 0,
    SENSOR_WIND_DIRECTION_RANGE_LOW,
    SENSOR_WIND_DIRECTION_RANGE_OK,
    SENSOR_WIND_DIRECTION_RANGE_HIGH
} SensorWindDirectionRange;

typedef struct
{
    uint8_t valid;
    int32_t input_mv;        /* AD3 terminal voltage, extrapolated outside 1..5V. Not PA5. */
    uint32_t angle_deg_x100; /* Display limited to 0..36000; range is retained. */
    SensorWindDirectionRange range;
} SensorWindDirectionValue;

/**
 * @brief 按 AD3 标定换算端子电压与角度。
 * @param raw_x100 ADC 原码乘以 100，允许表达校准观测的小数。
 * @return 端子电压、百分之一度的方向角和范围状态。
 * @note 调用方负责完成采样。超过 16 位 ADC 原码范围时返回无效。
 */
SensorWindDirectionValue SensorWindDirection_Convert(uint32_t raw_x100);

/**
 * @brief 读取风向范围状态的固定名称。
 * @param range 风向范围。
 * @return 只读名称字符串。
 */
const char *SensorWindDirection_RangeName(SensorWindDirectionRange range);

#endif

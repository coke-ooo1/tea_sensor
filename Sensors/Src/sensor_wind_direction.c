/**
 * @file sensor_wind_direction.c
 * @brief 风向标定，按 AD3 原码计算角度与范围并保留端子电压。
 */

#include "sensor_wind_direction.h"
#include <math.h>
#include <string.h>

/* 宏定义与编译期检查 */

/* Current board electrical calibration, in chronological record order:
 * 5V:31000.43, 1V:6400.67, 3.5V:21920.14, 2.5V:15614.18.
 * Only 1V and 5V define the line; the other two are independent checks.
 * No soil/AD2 divider, VREF, orientation offset or automatic calibration. */
#define SENSOR_WIND_DIRECTION_RAW_1V_X100 640067L
#define SENSOR_WIND_DIRECTION_RAW_5V_X100 3100043L
#define SENSOR_WIND_DIRECTION_RAW_SPAN_X100                                                        \
    (SENSOR_WIND_DIRECTION_RAW_5V_X100 - SENSOR_WIND_DIRECTION_RAW_1V_X100)
#define SENSOR_WIND_DIRECTION_INPUT_LOW_MV 1000L
#define SENSOR_WIND_DIRECTION_INPUT_SPAN_MV 4000L
#define SENSOR_WIND_DIRECTION_FULL_ANGLE_X100 36000L
#if SENSOR_WIND_DIRECTION_RAW_SPAN_X100 <= 0
#error Wind direction calibration raw span must be positive
#endif

/* 私有函数声明 */

static int32_t SensorWindDirection_RoundDivide(int64_t numerator, int64_t denominator);

/* 公共接口 */

SensorWindDirectionValue SensorWindDirection_Convert(uint32_t raw_x100)
{
    SensorWindDirectionValue value = {0};
    int64_t delta;
    int64_t span;
    int64_t voltage_numerator;

    if (raw_x100 > (uint32_t)UINT16_MAX * 100U)
    {
        return value;
    }
    span = SENSOR_WIND_DIRECTION_RAW_SPAN_X100;
    delta = (int64_t)raw_x100 - SENSOR_WIND_DIRECTION_RAW_1V_X100;
    voltage_numerator =
        SENSOR_WIND_DIRECTION_INPUT_LOW_MV * span + delta * SENSOR_WIND_DIRECTION_INPUT_SPAN_MV;
    value.input_mv = SensorWindDirection_RoundDivide(voltage_numerator, span);
    /* 范围判断使用未舍入的原码关系，与终端电压显示精度无关。 */
    value.range = delta < 0      ? SENSOR_WIND_DIRECTION_RANGE_LOW
                  : delta > span ? SENSOR_WIND_DIRECTION_RANGE_HIGH
                                 : SENSOR_WIND_DIRECTION_RANGE_OK;
    if (delta <= 0)
    {
        value.angle_deg_x100 = 0U;
    }
    else if (delta >= span)
    {
        value.angle_deg_x100 = SENSOR_WIND_DIRECTION_FULL_ANGLE_X100;
    }
    else
    {
        /* 角度直接按原码计算，避免电压取整引入二次误差。 */
        value.angle_deg_x100 = (uint32_t)SensorWindDirection_RoundDivide(
            delta * SENSOR_WIND_DIRECTION_FULL_ANGLE_X100, span);
    }
    value.valid = 1U;
    return value;
}

const char *SensorWindDirection_RangeName(SensorWindDirectionRange range)
{
    switch (range)
    {
        case SENSOR_WIND_DIRECTION_RANGE_LOW:
            return "LOW";
        case SENSOR_WIND_DIRECTION_RANGE_OK:
            return "OK";
        case SENSOR_WIND_DIRECTION_RANGE_HIGH:
            return "HIGH";
        default:
            return "NA";
    }
}

/* 私有实现 */

/**
 * @brief 进行最接近整数的有符号除法，中点向零的反方向取整。
 * @param numerator 分子。
 * @param denominator 正分母。
 * @return 舍入后的商。
 */
static int32_t SensorWindDirection_RoundDivide(int64_t numerator, int64_t denominator)
{
    /* 16 位原码放大 100 倍后再乘角度量程，使用 64 位中间值。 */
    return numerator >= 0 ? (int32_t)((numerator + denominator / 2) / denominator)
                          : -(int32_t)((-numerator + denominator / 2) / denominator);
}

/**
 * @file sensor_wind_speed.c
 * @brief 风速标定，封装 AD2 采样并计算定点风速与范围。
 */

#include "sensor_wind_speed.h"
#include <string.h>

/* 私有函数声明 */

static int32_t SensorWindSpeed_ScaleMean(
    int64_t delta, uint32_t count, int32_t scale, int32_t offset);

/* 公共接口 */

SensorWindSpeedValue SensorWindSpeed_ConvertMean(uint64_t raw_sum, uint32_t count)
{
    SensorWindSpeedValue value;
    int64_t denominator;
    int64_t delta;

    memset(&value, 0, sizeof(value));
    if (!count || raw_sum > (uint64_t)UINT16_MAX * count)
    {
        return value;
    }
    denominator = (int64_t)SENSOR_WIND_SPEED_RAW_SPAN_X100 * count;
    delta = (int64_t)raw_sum * 100 - (int64_t)SENSOR_WIND_SPEED_RAW_ZERO_X100 * count;
    value.wind_speed_mps =
        delta <= 0 ? 0.0
        : delta >= denominator
            ? SENSOR_WIND_SPEED_FULL_SCALE_MPS_X100 / 100.0
            : (SENSOR_WIND_SPEED_FULL_SCALE_MPS_X100 / 100.0) * (double)delta / (double)denominator;
    value.input_mv = SensorWindSpeed_ScaleMean(
        delta, count, SENSOR_WIND_SPEED_INPUT_SPAN_MV, SENSOR_WIND_SPEED_INPUT_ZERO_MV);
    value.range = delta < 0             ? SENSOR_WIND_SPEED_RANGE_LOW
                  : delta > denominator ? SENSOR_WIND_SPEED_RANGE_HIGH
                                        : SENSOR_WIND_SPEED_RANGE_OK;
    if (delta <= 0)
    {
        value.wind_speed_mps_x100 = 0U;
    }
    else if (delta >= denominator)
    {
        value.wind_speed_mps_x100 = SENSOR_WIND_SPEED_FULL_SCALE_MPS_X100;
    }
    else
    {
        /* 风速按原码直接计算，使用电气标定端点与传感器名义量程。 */
        value.wind_speed_mps_x100 = (uint32_t)SensorWindSpeed_ScaleMean(
            delta, count, SENSOR_WIND_SPEED_FULL_SCALE_MPS_X100, 0);
    }
    value.valid = 1U;
    return value;
}

SensorWindSpeedData SensorWindSpeed_Read(void)
{
    SensorWindSpeedData data;

    memset(&data, 0, sizeof(data));
    data.adc = BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_SPEED_AD2);
    data.value = SensorWindSpeed_ConvertMean(data.adc.raw, data.adc.valid ? 1U : 0U);
    return data;
}

const char *SensorWindSpeed_RangeName(SensorWindSpeedRange range)
{
    switch (range)
    {
        case SENSOR_WIND_SPEED_RANGE_OK:
            return "OK";
        case SENSOR_WIND_SPEED_RANGE_LOW:
            return "LOW";
        case SENSOR_WIND_SPEED_RANGE_HIGH:
            return "HIGH";
        default:
            return "NA";
    }
}

/* 私有实现 */

/* 分段求商与余数，在最大样本数下避免乘法溢出。 */
/**
 * @brief 按标定端点缩放未取整的风速原码均值。
 * @param delta 累积原码相对低端点的偏差。
 * @param count 有效样本数。
 * @param scale 物理量跨度。
 * @param offset 物理量低端值。
 * @return 最终舍入后的定点值。
 */
static int32_t SensorWindSpeed_ScaleMean(
    int64_t delta, uint32_t count, int32_t scale, int32_t offset)
{
    int64_t span = SENSOR_WIND_SPEED_RAW_SPAN_X100;
    int64_t denominator = span * count;
    int64_t scaled = (delta / count) * scale + (int64_t)offset * span;
    int64_t whole = scaled / span;
    int64_t remainder = (scaled % span) * count + (delta % count) * scale;

    whole += remainder / denominator;
    remainder %= denominator;
    /* 在最终对称舍入前，将商与余数调整为同一方向。 */
    if (whole > 0 && remainder < 0)
    {
        --whole;
        remainder += denominator;
    }
    else if (whole < 0 && remainder > 0)
    {
        ++whole;
        remainder -= denominator;
    }
    return (int32_t)(whole + (remainder >= 0 ? (remainder + denominator / 2) / denominator
                                             : -((-remainder + denominator / 2) / denominator)));
}

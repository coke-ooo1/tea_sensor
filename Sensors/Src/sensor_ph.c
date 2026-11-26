/**
 * @file sensor_ph.c
 * @brief 土壤 pH 标定，按 AD4 原码均值计算端子电压和 pH 范围。
 */

#include "sensor_ph.h"
#include <string.h>

/* 私有函数声明 */

static int32_t SensorPh_ScaleMean(int64_t delta, uint32_t count, int32_t scale, int32_t offset);

/* 公共接口 */

SensorPhValue SensorPh_Convert(uint64_t raw_sum, uint32_t count)
{
    SensorPhValue soil_ph;
    int64_t delta;
    int64_t denominator;

    memset(&soil_ph, 0, sizeof(soil_ph));
    if (!count || raw_sum > (uint64_t)UINT16_MAX * count)
    {
        return soil_ph;
    }
    denominator = (int64_t)SENSOR_PH_RAW_SPAN_X100 * count;
    delta = (int64_t)raw_sum * 100 - (int64_t)SENSOR_PH_RAW_LOW_X100 * count;
    soil_ph.input_mv = SensorPh_ScaleMean(delta, count, SENSOR_PH_INPUT_SPAN_MV, 0);
    soil_ph.range = delta < 0             ? SENSOR_PH_RANGE_LOW
                    : delta > denominator ? SENSOR_PH_RANGE_HIGH
                                          : SENSOR_PH_RANGE_OK;
    if (delta <= 0)
    {
        soil_ph.soil_ph_x100 = SENSOR_PH_SOIL_PH_LOW_X100;
    }
    else if (delta >= denominator)
    {
        soil_ph.soil_ph_x100 = SENSOR_PH_SOIL_PH_HIGH_X100;
    }
    else
    {
        /* 直接用原码均值计算 pH，避免电压取整带来的二次误差。 */
        soil_ph.soil_ph_x100 = (uint32_t)SensorPh_ScaleMean(
            delta, count, SENSOR_PH_SOIL_PH_SPAN_X100, SENSOR_PH_SOIL_PH_LOW_X100);
    }
    soil_ph.valid = 1U;
    return soil_ph;
}

const char *SensorPh_RangeName(SensorPhRange range)
{
    switch (range)
    {
        case SENSOR_PH_RANGE_OK:
            return "OK";
        case SENSOR_PH_RANGE_LOW:
            return "LOW";
        case SENSOR_PH_RANGE_HIGH:
            return "HIGH";
        default:
            return "NA";
    }
}

/* 私有实现 */

/* 分段保留商和余数，使最大样本数下仍可在有符号 64 位内完成运算。 */
/**
 * @brief 用 64 位有符号算术实施一次最终舍入，保留分段除法余数。
 * @param delta 累积原码相对低端点的偏差。
 * @param count 有效样本数。
 * @param scale 物理量跨度。
 * @param offset 物理量低端值。
 * @return 按目标单位缩放后的数值。
 */
static int32_t SensorPh_ScaleMean(int64_t delta, uint32_t count, int32_t scale, int32_t offset)
{
    int64_t span = SENSOR_PH_RAW_SPAN_X100;
    int64_t denominator = span * count;
    int64_t scaled = (delta / count) * scale + (int64_t)offset * span;
    int64_t whole = scaled / span;
    int64_t remainder = (scaled % span) * count + (delta % count) * scale;

    whole += remainder / denominator;
    remainder %= denominator;
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
    /* 只在最终结果舍入，中点向远离零的方向取整。 */
    return (int32_t)(whole + (remainder >= 0 ? (remainder + denominator / 2) / denominator
                                             : -((-remainder + denominator / 2) / denominator)));
}

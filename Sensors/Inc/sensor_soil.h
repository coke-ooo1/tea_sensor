/**
 * @file sensor_soil.h
 * @brief 土壤湿润度标定，封装 AD0 读取及两点定标换算。
 */

#ifndef SENSOR_SOIL_H
#define SENSOR_SOIL_H

#include "bsp_adc.h"

#define SENSOR_SOIL_CHANNEL_NAME "AD0"
#define SENSOR_SOIL_MODEL "RE-SD-V05-TR"
/* Rounded means supplied by the user: 90 air-dry / 37 fully immersed samples.
 * These endpoints define relative wetness, not volumetric soil water content. */
#define SENSOR_SOIL_RAW_DRY 436U
#define SENSOR_SOIL_RAW_WET 31146U
#if SENSOR_SOIL_RAW_WET <= SENSOR_SOIL_RAW_DRY
#error SENSOR_SOIL_RAW_WET must be greater than SENSOR_SOIL_RAW_DRY
#endif
#if SENSOR_SOIL_RAW_WET > UINT16_MAX
#error Soil calibration endpoints must fit the unsigned 16-bit ADC
#endif

typedef enum
{
    SENSOR_SOIL_RANGE_UNAVAILABLE = 0,
    SENSOR_SOIL_RANGE_OK,
    SENSOR_SOIL_RANGE_LOW,
    SENSOR_SOIL_RANGE_HIGH
} SensorSoilRange;

typedef struct
{
    BspAdcResult adc;
    /* 0.01% relative wetness, valid only when adc.valid is set. */
    uint32_t moisture_pct_x100;
    SensorSoilRange range;
} SensorSoilData;

/**
 * @brief 采样一次 AD0 并计算相对土壤湿润度。
 * @return ADC 结果、范围状态和湿润度，单位百分之一百分点。
 */
SensorSoilData SensorSoil_Read(void);
/* Unique raw-to-percent conversion: clamped, rounded, no VREF/gain factor. */

/**
 * @brief 使用同一两点标定换算单个 AD0 原码。
 * @param raw ADC 原码。
 * @return 相对湿润度，单位百分之一百分点。
 */
uint32_t SensorSoil_MoisturePercentX100(uint16_t raw);

/**
 * @brief 直接对未提前取整的 AD0 均值执行两点换算。
 * @param raw_sum 有效 ADC 原码之和。
 * @param count 原码样本数。
 * @param range 范围状态的输出指针。
 * @return 限定端点后的相对湿润度，单位百分之一百分点。
 */
uint32_t SensorSoil_ConvertMean(uint64_t raw_sum, uint32_t count, SensorSoilRange *range);

#endif

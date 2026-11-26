/**
 * @file sensor_wind_speed.h
 * @brief 风速标定，封装 AD2 采样并计算定点风速与范围。
 */

#ifndef SENSOR_WIND_SPEED_H
#define SENSOR_WIND_SPEED_H

#include "bsp_adc.h"

#define SENSOR_WIND_SPEED_CHANNEL_NAME "AD2"
/* Measured AD2 terminal-voltage endpoints; speed uses the nominal 0..30m/s relation. */
#define SENSOR_WIND_SPEED_RAW_ZERO_X100 6779U
#define SENSOR_WIND_SPEED_RAW_FULL_X100 3151863U
#define SENSOR_WIND_SPEED_RAW_SPAN_X100                                                            \
    (SENSOR_WIND_SPEED_RAW_FULL_X100 - SENSOR_WIND_SPEED_RAW_ZERO_X100)
#define SENSOR_WIND_SPEED_INPUT_ZERO_MV 40
#define SENSOR_WIND_SPEED_INPUT_SPAN_MV 5000
#define SENSOR_WIND_SPEED_FULL_SCALE_MPS_X100 3000U

typedef enum
{
    SENSOR_WIND_SPEED_RANGE_UNAVAILABLE = 0,
    SENSOR_WIND_SPEED_RANGE_OK,
    SENSOR_WIND_SPEED_RANGE_LOW,
    SENSOR_WIND_SPEED_RANGE_HIGH
} SensorWindSpeedRange;

typedef struct
{
    uint8_t valid;    /* Availability of converted values, independent of ADC status. */
    int32_t input_mv; /* AD2 terminal voltage, not the MCU PA6 node. */
    uint32_t wind_speed_mps_x100;
    double wind_speed_mps;      /* Unrounded physical value for App accumulation. */
    SensorWindSpeedRange range; /* Unrounded raw mean vs measured zero/full endpoints. */
} SensorWindSpeedValue;

typedef struct
{
    BspAdcResult adc;
    SensorWindSpeedValue value;
} SensorWindSpeedData;

typedef struct
{
    uint32_t error_counts[BSP_ADC_OUT_OF_RANGE + 1U];
    uint32_t cleanup_error_count;
    uint32_t prepare_error_count;
    uint32_t config_error_count;
    BspAdcResult last_error;
} SensorWindSpeedDiagnostics;

/**
 * @brief 采样一次 AD2 并进行风速标定换算。
 * @return ADC 结果、风速和范围状态。
 */
SensorWindSpeedData SensorWindSpeed_Read(void);
/* One fixed zero-compensated mapping for a sample or the exact raw sum/count.
 * No acquisition, pre-rounded raw/mV, VREF, divider guess or runtime mode. */

/**
 * @brief 将调用方提供的 AD2 原码均值直接转换为风速。
 * @param raw_sum 有效 ADC 原码之和。
 * @param count 原码样本数。
 * @return 电压、风速及范围状态，风速量化单位为 0.01 m/s。
 */
SensorWindSpeedValue SensorWindSpeed_ConvertMean(uint64_t raw_sum, uint32_t count);

/**
 * @brief 读取风速范围状态的固定名称。
 * @param range 风速范围。
 * @return 只读名称字符串。
 */
const char *SensorWindSpeed_RangeName(SensorWindSpeedRange range);

#endif

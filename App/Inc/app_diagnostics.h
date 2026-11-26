/**
 * @file app_diagnostics.h
 * @brief 应用采样诊断，记录 ADC 各阶段错误、采样时间和最近一轮结果。
 */

#ifndef APP_DIAGNOSTICS_H
#define APP_DIAGNOSTICS_H
#include "sensor_soil.h"
#include "sensor_wind_speed.h"
#include "sensor_wind_direction.h"
#include "sensor_ph.h"
#include "sensor_rain.h"

typedef struct
{
    uint32_t error_counts[BSP_ADC_OUT_OF_RANGE + 1U];
    uint32_t cleanup_errors;
    uint32_t prepare_errors;
    uint32_t config_errors;

    BspAdcResult last_error;
} AppAdcDiagnostics;

typedef struct
{
    uint32_t round_id;
    uint32_t round_t_ms;

    uint32_t soil_read_start_ms;
    uint32_t soil_read_end_ms;
    uint32_t wind_read_start_ms;
    uint32_t wind_read_end_ms;
    uint32_t pair_span_ms;
    uint32_t ad3_read_start_ms;
    uint32_t ad3_read_end_ms;
    uint32_t ad4_read_start_ms;
    uint32_t ad4_read_end_ms;
    uint32_t round_span_ms;

    SensorSoilData soil;
    SensorWindSpeedData wind;
    BspAdcResult ad3;
    BspAdcResult ad4;
    BspAdcResult ad5;
    uint32_t ad5_read_end_ms;

    SensorPhValue soil_ph;
    SensorWindDirectionValue wind_direction;
} AppSampleRound;

typedef struct AppDiagnostics
{
    uint32_t sequence;
    uint32_t printf_errors;
    /* 保留最近一次 AD5 尝试。雨强窗口复用该样本，序号包含失败尝试，时间为采样完成 tick。 */
    uint32_t ad5_sequence;
    uint32_t ad5_sample_t_ms;
    uint32_t ad5_sample_errors;
    BspAdcResult last_ad5;

    uint32_t completed_windows;
    uint32_t missed_batches;
    uint32_t sample_errors;

    uint32_t sample_error_counts[BSP_ADC_OUT_OF_RANGE + 1U];
    uint32_t cleanup_errors;
    uint32_t prepare_errors;
    uint32_t config_errors;

    uint32_t last_window_end_ms;
    BspAdcResult last_adc_error;

    SensorWindSpeedDiagnostics wind_diagnostics;
    uint32_t wind_sample_errors;
    uint32_t ad3_sample_errors;
    uint32_t ad4_sample_errors;
    AppAdcDiagnostics ad3_diagnostics;
    AppAdcDiagnostics ad4_diagnostics;
    AppAdcDiagnostics ad5_diagnostics;

    AppSampleRound last_round;
} AppDiagnostics;

#endif

/**
 * @file bsp_adc.h
 * @brief ADC1 板级采集，负责输入切换、限时转换、校准及错误诊断。
 */

#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>

typedef enum
{
    BSP_ADC_INPUT_SOIL_AD0 = 0,       /* PB1 -> ADC1 channel 5 */
    BSP_ADC_INPUT_WIND_SPEED_AD2,     /* PA6 -> ADC1 channel 3, NOT ADC2 */
    BSP_ADC_INPUT_WIND_DIRECTION_AD3, /* PA5 -> ADC1 channel 19 */
    BSP_ADC_INPUT_PH_AD4,             /* PA4 -> ADC1 channel 18; conversion belongs to Sensors */
    BSP_ADC_INPUT_RAIN_AD5            /* PA3 → ADC1 通道 15，雨强采样。 */
} BspAdcInput;

/* 详细阶段用于本地诊断，报文错误分类保持一致。 */
typedef enum
{
    BSP_ADC_STAGE_CALIBRATION = 0,
    BSP_ADC_STAGE_INPUT,
    BSP_ADC_STAGE_PREPARE,
    BSP_ADC_STAGE_CONFIG,
    BSP_ADC_STAGE_START,
    BSP_ADC_STAGE_POLL,
    BSP_ADC_STAGE_VALUE,
    BSP_ADC_STAGE_STOP,
    BSP_ADC_STAGE_COMPLETE
} BspAdcStage;

typedef enum
{
    BSP_ADC_OK = 0,
    BSP_ADC_CAL_FAILED,
    BSP_ADC_START_FAILED,
    BSP_ADC_POLL_FAILED,
    BSP_ADC_STOP_FAILED,
    BSP_ADC_OUT_OF_RANGE
} BspAdcStatus;

/* 将 HAL 调用结果映射为 BSP 状态，供上层读取。 */
typedef enum
{
    BSP_ADC_HAL_OK = 0,
    BSP_ADC_HAL_ERROR,
    BSP_ADC_HAL_BUSY,
    BSP_ADC_HAL_TIMEOUT
} BspAdcHalStatus;

typedef struct
{
    BspAdcStatus status;
    BspAdcHalStatus hal_status;
    BspAdcHalStatus stop_status;

    uint16_t raw; /* ADC1 的无符号原码。 */
    uint8_t valid;

    uint8_t stop_attempted;
    BspAdcStage stage;
    BspAdcHalStatus prepare_status;
    BspAdcHalStatus config_status;
    uint8_t prepare_attempted;
    uint8_t config_attempted;
} BspAdcResult;

typedef struct
{
    uint32_t calibration_attempts;
    uint32_t calibration_failures;
    uint32_t recovery_attempts;
    uint32_t recovery_successes;
    uint32_t recovery_stop_errors;

    BspAdcHalStatus initial_calibration_status;
    BspAdcHalStatus last_calibration_status;
    BspAdcHalStatus last_recovery_stop_status;
    uint8_t calibrated;
} BspAdcDiagnostics;

/* 正常启动只校准一次。首次失败才启用停止 ADC 后的低频恢复。 */

/**
 * @brief 重置 ADC1 诊断并执行初次校准。
 * @note 须先完成 CubeMX 的 ADC1 初始化。
 */
void BspAdc_Init(void);

/**
 * @brief 处理初次校准失败后的限频恢复。
 * @note 在主循环调用，转换期间不启动恢复。
 */
void BspAdc_Process(void);

/**
 * @brief 读取 ADC1 校准与恢复诊断。
 * @return 模块持有的只读诊断指针。
 */
const BspAdcDiagnostics *BspAdc_GetDiagnostics(void);

/**
 * @brief 显式切换 ADC1 输入并进行一次限时转换和停止清理。
 * @param input 板级 ADC 输入标识。
 * @return 原码、有效性、失败阶段及 HAL 清理状态。
 */
BspAdcResult BspAdc_ReadRaw(BspAdcInput input);

/**
 * @brief 将 ADC 原码换算成 MCU ADC 引脚电压。
 * @param raw 无符号 ADC 原码。
 * @return 引脚电压，单位 mV。
 */
uint32_t BspAdc_PinMillivolts(uint32_t raw);

/**
 * @brief 读取 ADC 采集状态的固定名称。
 * @param status ADC 采集状态。
 * @return 只读名称字符串。
 */
const char *BspAdc_StatusName(BspAdcStatus status);

/**
 * @brief 读取 HAL 结果的固定名称。
 * @param status BSP 中的 HAL 结果。
 * @return 只读名称字符串。
 */
const char *BspAdc_HalStatusName(BspAdcHalStatus status);

/**
 * @brief 读取 ADC 失败阶段的固定名称。
 * @param stage ADC 转换阶段。
 * @return 只读名称字符串。
 */
const char *BspAdc_StageName(BspAdcStage stage);

#endif

/**
 * @file stabilization_cases.h
 * @brief 主机恢复用例，验证校准错误后的限频恢复及诊断。
 */

static void Test_AdcCalibrationRecovery(void)
{
    unsigned i;
    unsigned cal;
    uint32_t bases[2] = {0U, UINT32_MAX - 1000U};
    for (i = 0U; i < 2U; ++i)
    {
        uint32_t base = bases[i];
        BspAdcResult raw;
        Test_Reset(base);
        s_cal_result = HAL_TIMEOUT;
        BspAdc_Init();
        cal = s_cal_calls;
        assert(BspAdc_GetDiagnostics()->initial_calibration_status == BSP_ADC_HAL_TIMEOUT);
        raw = BspAdc_ReadRaw(BSP_ADC_INPUT_PH_AD4);
        assert(!raw.valid && raw.status == BSP_ADC_CAL_FAILED);
        s_cal_result = HAL_OK;
        s_tick_ms = base + BSP_ADC_CAL_RECOVERY_PERIOD_MS - 1U;
        BspAdc_Process();
        assert(s_cal_calls == cal && !BspAdc_GetDiagnostics()->recovery_attempts);
        s_prepare_result = HAL_ERROR;
        s_tick_ms = base + BSP_ADC_CAL_RECOVERY_PERIOD_MS;
        BspAdc_Process();
        assert(s_cal_calls == cal && BspAdc_GetDiagnostics()->recovery_stop_errors == 1U);
        s_prepare_result = HAL_OK;
        s_tick_ms = base + 2U * BSP_ADC_CAL_RECOVERY_PERIOD_MS - 1U;
        BspAdc_Process();
        assert(s_cal_calls == cal);
        s_tick_ms = base + 2U * BSP_ADC_CAL_RECOVERY_PERIOD_MS;
        BspAdc_Process();
        assert(s_cal_calls == cal + 1U && BspAdc_GetDiagnostics()->recovery_successes == 1U);
        assert(BspAdc_GetDiagnostics()->calibration_failures == 1U);
        assert(BspAdc_GetDiagnostics()->initial_calibration_status == BSP_ADC_HAL_TIMEOUT);
        s_raw = 0U;
        raw = BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);
        assert(raw.valid && raw.raw == 0U);
        s_tick_ms += 10U * BSP_ADC_CAL_RECOVERY_PERIOD_MS;
        BspAdc_Process();
        assert(s_cal_calls == cal + 1U);
        Test_Reset(base);
        s_cal_result = HAL_ERROR;
        BspAdc_Init();
        cal = s_cal_calls;
        s_tick_ms = base + BSP_ADC_CAL_RECOVERY_PERIOD_MS;
        BspAdc_Process();
        assert(s_cal_calls == cal + 1U && !BspAdc_GetDiagnostics()->calibrated);
        s_tick_ms = base + 2U * BSP_ADC_CAL_RECOVERY_PERIOD_MS - 1U;
        BspAdc_Process();
        assert(s_cal_calls == cal + 1U);
        s_tick_ms++;
        BspAdc_Process();
        assert(s_cal_calls == cal + 2U && BspAdc_GetDiagnostics()->calibration_failures == 3U);
        raw = BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_SPEED_AD2);
        assert(!raw.valid && raw.status == BSP_ADC_CAL_FAILED);
    }
}

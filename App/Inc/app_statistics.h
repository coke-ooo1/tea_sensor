/**
 * @file app_statistics.h
 * @brief 应用层窗口统计，计算风雨分钟聚合、圆周风向及土壤截尾平均。
 */

#ifndef APP_STATISTICS_H
#define APP_STATISTICS_H
#include "app_data.h"
#include "app_diagnostics.h"

/**
 * @brief 清空分钟和土壤统计累积器。
 */
void AppStatistics_Init(void);

/**
 * @brief 将有效的风速、风向和雨量采样计入分钟统计。
 * @param sample 当前轮采样及转换结果。
 */
void AppStatistics_AddMinuteSample(const AppSampleRound *sample);

/**
 * @brief 结束分钟窗口，写出物理值和有效计数，再清空该窗口的累积器。
 * @param end_ms 记录的窗口结束 tick，单位 ms。
 * @param record 输出记录。
 */
void AppStatistics_FinishMinute(uint32_t end_ms, AppMinuteRecord *record);

/**
 * @brief 累计有效的土壤 ADC 原码并更新最小值和最大值。
 * @param sample ADC 采样结果及有效标记。
 */
void AppStatistics_AddSoilSample(const BspAdcResult *sample);

/**
 * @brief 生成当前土壤窗口记录，并重置用于截尾平均的累积器。
 * @param record 输出记录，保留调用方设定的时间。
 */
void AppStatistics_FinishSoil(AppSoilRecord *record);

#endif

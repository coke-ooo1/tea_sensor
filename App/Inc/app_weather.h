/**
 * @file app_weather.h
 * @brief 气象采集调度，协调分组查询、结果入窗及可选气体端口探测。
 */

#ifndef APP_WEATHER_H
#define APP_WEATHER_H
#include "sensor_weather.h"
#include "app_data.h"

typedef struct
{
    uint32_t query_sequence;
    uint32_t queries;
    uint32_t completions;
    uint32_t last_start_ms;
    uint32_t last_complete_ms;

    uint32_t no_response_streak;
    uint32_t backoff_count;
    uint32_t probe_count;
    uint32_t ignored_idle_bytes;
    uint32_t echo_count;
    uint32_t uart_error;

    uint32_t printf_errors;

    uint8_t waiting;
    uint8_t backoff;

    SensorWeatherItem item;
    SensorWeatherStatus last_status;
    BspWeatherRs485Status init_status;
} AppWeatherPortDiagnostics;

/**
 * @brief 初始化气象采集状态和启用的 RS485 端口。
 */
void AppWeather_Init(void);

/**
 * @brief 推进气象组查询、串口解析、超时及可选气体通道探测。
 */
void AppWeather_Process(void);

/**
 * @brief 开始新的气象组采集。
 * @param record 组采集结束前保持有效的输出记录。
 * @param now_ms 组开始 tick，单位 ms。
 * @param deadline 组截止 tick，单位 ms。
 */
void AppWeather_BeginGroup(AppWeatherRecord *record, uint32_t now_ms, uint32_t deadline);

/**
 * @brief 查询气象组是否还在运行。
 * @return 仍有采集组返回 1，结束后返回 0。
 */
uint8_t AppWeather_GroupPending(void);

/**
 * @brief 读取指定气象端口的调度诊断。
 * @param port 气象 RS485 端口。
 * @return 启用端口的只读诊断指针，其他端口返回空。
 */
const AppWeatherPortDiagnostics *AppWeather_GetDiagnostics(BspWeatherRs485Port port);

/**
 * @brief 读取指定测项的轮询周期。
 * @param item 气象测项。
 * @return 周期，单位 ms。
 */
uint32_t AppWeather_PeriodMs(SensorWeatherItem item);
#endif

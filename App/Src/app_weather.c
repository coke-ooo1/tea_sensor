/**
 * @file app_weather.c
 * @brief 气象采集调度，协调分组查询、结果入窗及可选气体端口探测。
 */

#include "app_weather.h"
#include "app_config.h"
#include "bsp_config.h"
#include "bsp_time.h"
#include <string.h>

/* 私有类型 */

typedef struct
{
    AppWeatherPortDiagnostics diagnostics;
    SensorWeatherParser parser;
    uint32_t tx_end_ms;
    uint32_t rx_start_ms;
    uint32_t rx_end_ms;
    uint32_t rx_count;
    uint32_t probe_last_ms;
    uint8_t rx_trace[32];
    uint8_t trace_length;
    uint8_t trace_truncated;
    uint8_t ended_once;
    SensorWeatherItem cursor;
} AppWeatherPort;

/* 模块数据 */

static AppWeatherPort s_ports[BSP_WEATHER_RS485_PORT_COUNT];
static uint8_t s_attempted[SENSOR_WEATHER_ITEM_COUNT];
static uint32_t s_sequence;
static uint32_t s_group_deadline;
static AppWeatherRecord *s_group;
static SensorWeatherItem s_next_met;

/* 私有函数声明 */

static SensorWeatherStatus AppWeather_UartStatus(BspWeatherRs485Status status);
static const char *AppWeather_FieldStatus(const SensorWeatherReply *reply, uint16_t mask);
static void AppWeather_StoreReply(SensorWeatherItem item, const SensorWeatherReply *reply);
static void AppWeather_Finish(
    BspWeatherRs485Port port, SensorWeatherStatus status, uint32_t now_ms);
static uint8_t AppWeather_LineReady(BspWeatherRs485Port port, uint32_t now_ms);
static uint8_t AppWeather_GapReady(BspWeatherRs485Port port, uint32_t now_ms);
static void AppWeather_Start(SensorWeatherItem item, uint32_t now_ms);
static void AppWeather_Receive(BspWeatherRs485Port port);

/* 公共接口 */

uint32_t AppWeather_PeriodMs(SensorWeatherItem item)
{
    if (item <= SENSOR_WEATHER_PRESSURE)
    {
        return WEATHER_BOX_INTERVAL_MS;
    }
    return item == SENSOR_WEATHER_CO2 ? APP_WEATHER_CO2_PERIOD_MS : APP_WEATHER_OXYGEN_PERIOD_MS;
}

void AppWeather_Init(void)
{
    unsigned port;

    memset(s_ports, 0, sizeof(s_ports));
    memset(s_attempted, 0, sizeof(s_attempted));
    s_sequence = 0U;
    SensorWeather_Init();
    s_group = NULL;
    s_ports[BSP_WEATHER_RS485_GAS].cursor = SENSOR_WEATHER_CO2;
    for (port = 0U; port < (APP_WEATHER_GAS_ENABLED ? BSP_WEATHER_RS485_PORT_COUNT : 1U); ++port)
    {
        s_ports[port].diagnostics.init_status = BspWeatherRs485_Init((BspWeatherRs485Port)port);
    }
}

void AppWeather_BeginGroup(AppWeatherRecord *record, uint32_t now_ms, uint32_t deadline)
{
    s_group = record;
    s_group->t_ms = now_ms;
    s_group_deadline = deadline;
    s_next_met = SENSOR_WEATHER_TEMP_HUM;
}

uint8_t AppWeather_GroupPending(void)
{
    return s_group != NULL;
}

void AppWeather_Process(void)
{
    AppWeatherPort *met = &s_ports[BSP_WEATHER_RS485_MET];
    uint32_t now_ms;

    AppWeather_Receive(BSP_WEATHER_RS485_MET);
    now_ms = BspTime_GetTickMs();
    if (s_group && !met->diagnostics.waiting && s_next_met > SENSOR_WEATHER_PRESSURE)
    {
        s_group = NULL;
    }
    if (s_group && (int32_t)(now_ms - s_group_deadline) >= 0)
    {
        if (met->diagnostics.waiting)
        {
            AppWeather_Finish(BSP_WEATHER_RS485_MET, SENSOR_WEATHER_TIMEOUT, now_ms);
        }
        s_group->t_ms = s_group_deadline;
        s_group = NULL;
    }
    if (s_group && !met->diagnostics.waiting &&
        AppWeather_GapReady(BSP_WEATHER_RS485_MET, now_ms) &&
        AppWeather_LineReady(BSP_WEATHER_RS485_MET, now_ms))
    {
        AppWeather_Start(s_next_met, now_ms);
        s_next_met = (SensorWeatherItem)(s_next_met + 1);
    }

    if (APP_WEATHER_GAS_ENABLED)
    {
        AppWeatherPort *gas = &s_ports[BSP_WEATHER_RS485_GAS];
        unsigned index;

        AppWeather_Receive(BSP_WEATHER_RS485_GAS);
        now_ms = BspTime_GetTickMs();
        if (gas->diagnostics.waiting || !AppWeather_GapReady(BSP_WEATHER_RS485_GAS, now_ms) ||
            !AppWeather_LineReady(BSP_WEATHER_RS485_GAS, now_ms))
        {
            return;
        }
        if (gas->diagnostics.backoff)
        {
            if ((uint32_t)(now_ms - gas->probe_last_ms) >= APP_WEATHER_PROBE_PERIOD_MS)
            {
                AppWeather_Start(SENSOR_WEATHER_CO2, now_ms);
            }
            return;
        }
        for (index = 0U; index < 2U; ++index)
        {
            SensorWeatherItem item =
                (SensorWeatherItem)(4U + ((unsigned)gas->cursor - 4U + index) % 2U);
            const SensorWeatherMeasurement *measurement = SensorWeather_GetMeasurement(item);

            if (!s_attempted[item] ||
                (uint32_t)(now_ms - measurement->last_query_t_ms) >= AppWeather_PeriodMs(item))
            {
                AppWeather_Start(item, now_ms);
                break;
            }
        }
    }
}

const AppWeatherPortDiagnostics *AppWeather_GetDiagnostics(BspWeatherRs485Port port)
{
    return (unsigned)port < (APP_WEATHER_GAS_ENABLED ? BSP_WEATHER_RS485_PORT_COUNT : 1U)
               ? &s_ports[port].diagnostics
               : NULL;
}

/* 私有实现 */

static SensorWeatherStatus AppWeather_UartStatus(BspWeatherRs485Status status)
{
    switch (status)
    {
        case BSP_WEATHER_RS485_UART_BUSY:
            return SENSOR_WEATHER_UART_BUSY;
        case BSP_WEATHER_RS485_UART_TIMEOUT:
            return SENSOR_WEATHER_UART_TIMEOUT;
        case BSP_WEATHER_RS485_RX_NOT_READY:
            return SENSOR_WEATHER_RX_NOT_READY;
        case BSP_WEATHER_RS485_IRQ_DISABLED:
            return SENSOR_WEATHER_IRQ_DISABLED;
        default:
            return SENSOR_WEATHER_UART_ERROR;
    }
}

static const char *AppWeather_FieldStatus(const SensorWeatherReply *reply, uint16_t mask)
{
    if (reply->status != SENSOR_WEATHER_OK)
    {
        return SensorWeather_StatusName(reply->status);
    }
    return (reply->valid_fields & mask) ? "OK" : "INVALID_VALUE";
}

static void AppWeather_StoreReply(SensorWeatherItem item, const SensorWeatherReply *reply)
{
    switch (item)
    {
        case SENSOR_WEATHER_TEMP_HUM:
            s_group->temperature = reply->value.temperature_c_x10;
            s_group->humidity = reply->value.humidity_rh_pct_x10;
            s_group->status[0] = AppWeather_FieldStatus(reply, SENSOR_WEATHER_FIELD_TEMPERATURE);
            s_group->status[1] = AppWeather_FieldStatus(reply, SENSOR_WEATHER_FIELD_HUMIDITY);
            break;
        case SENSOR_WEATHER_LIGHT:
            s_group->light = reply->value.light_lux;
            s_group->status[2] = AppWeather_FieldStatus(reply, SENSOR_WEATHER_FIELD_LIGHT);
            break;
        case SENSOR_WEATHER_PRESSURE:
            s_group->pressure = reply->value.pressure_kpa_x100;
            s_group->status[3] = AppWeather_FieldStatus(reply, SENSOR_WEATHER_FIELD_PRESSURE);
            break;
        case SENSOR_WEATHER_PM:
            s_group->pm25 = reply->value.pm25_ug_m3;
            s_group->pm10 = reply->value.pm10_ug_m3;
            s_group->status[4] = AppWeather_FieldStatus(reply, SENSOR_WEATHER_FIELD_PM25);
            s_group->status[5] = AppWeather_FieldStatus(reply, SENSOR_WEATHER_FIELD_PM10);
            break;
        default:
            break;
    }
}

/* 每次查询只结算一次。新窗口中的失败字段保持无效，历史有效值另行保存。 */
/**
 * @brief 结算单次查询并更新结果及连续失败状态。
 * @param port 查询端口。
 * @param status 查询结果。
 * @param now_ms 当前 tick，单位 ms。
 */
static void AppWeather_Finish(BspWeatherRs485Port port, SensorWeatherStatus status, uint32_t now_ms)
{
    AppWeatherPort *state = &s_ports[port];
    AppWeatherPortDiagnostics *diagnostics = &state->diagnostics;
    SensorWeatherReply *reply = &state->parser.reply;
    uint8_t legal = (status == SENSOR_WEATHER_OK || status == SENSOR_WEATHER_MODBUS_EXCEPTION);

    reply->status = status;
    SensorWeather_Complete(diagnostics->item, reply, state->rx_end_ms, now_ms);
    if (port == BSP_WEATHER_RS485_MET && s_group)
    {
        AppWeather_StoreReply(diagnostics->item, reply);
        s_group->t_ms = legal ? state->rx_end_ms : now_ms;
        if ((int32_t)(s_group->t_ms - s_group_deadline) >= 0)
        {
            s_group->t_ms = s_group_deadline;
        }
    }
    diagnostics->waiting = 0U;
    diagnostics->last_status = status;
    diagnostics->last_complete_ms = now_ms;
    ++diagnostics->completions;
    diagnostics->echo_count += state->parser.echo_count;
    state->ended_once = 1U;
    if (port != BSP_WEATHER_RS485_GAS)
    {
        return;
    }
    if (legal)
    {
        diagnostics->no_response_streak = 0U;
        diagnostics->backoff = 0U;
    }
    else
    {
        if (diagnostics->no_response_streak < APP_WEATHER_NO_RESPONSE_LIMIT)
        {
            ++diagnostics->no_response_streak;
        }
        if (diagnostics->no_response_streak >= APP_WEATHER_NO_RESPONSE_LIMIT &&
            !diagnostics->backoff)
        {
            diagnostics->backoff = 1U;
            ++diagnostics->backoff_count;
            state->probe_last_ms = now_ms;
        }
    }
}

static uint8_t AppWeather_LineReady(BspWeatherRs485Port port, uint32_t now_ms)
{
    BspWeatherRs485Diagnostics diagnostics = BspWeatherRs485_GetDiagnostics(port);

    return diagnostics.rx_active &&
           (!diagnostics.rx_seen ||
               (uint32_t)(now_ms - diagnostics.last_rx_ms) >= BSP_WEATHER_RX_QUIET_MS);
}

static uint8_t AppWeather_GapReady(BspWeatherRs485Port port, uint32_t now_ms)
{
    return !s_ports[port].ended_once ||
           (uint32_t)(now_ms - s_ports[port].diagnostics.last_complete_ms) >=
               BSP_WEATHER_TRANSACTION_GAP_MS;
}

static void AppWeather_Start(SensorWeatherItem item, uint32_t now_ms)
{
    BspWeatherRs485Port port = SensorWeather_Port(item);
    AppWeatherPort *state = &s_ports[port];
    BspWeatherRs485Status status;

    SensorWeather_ParserInit(&state->parser, item);
    state->trace_length = state->trace_truncated = 0U;
    state->rx_count = 0U;
    state->rx_start_ms = state->rx_end_ms = 0U;
    state->diagnostics.item = item;
    state->diagnostics.query_sequence = ++s_sequence;
    state->diagnostics.last_start_ms = now_ms;
    state->diagnostics.uart_error = 0U;
    state->diagnostics.waiting = 1U;
    ++state->diagnostics.queries;
    s_attempted[item] = 1U;
    if (state->diagnostics.backoff)
    {
        ++state->diagnostics.probe_count;
        state->probe_last_ms = now_ms;
    }
    SensorWeather_RecordQuery(item, s_sequence, now_ms);
    state->cursor = (SensorWeatherItem)(item + 1);
    if (port == BSP_WEATHER_RS485_GAS && state->cursor > SENSOR_WEATHER_OXYGEN)
    {
        state->cursor = SENSOR_WEATHER_CO2;
    }
    status = BspWeatherRs485_Send(port, state->parser.request, 8U);
    state->tx_end_ms = BspTime_GetTickMs();
    if (status != BSP_WEATHER_RS485_OK)
    {
        AppWeather_Finish(port, AppWeather_UartStatus(status), state->tx_end_ms);
    }
}

/**
 * @brief 读取带时间的气象串口数据，推进帧解析及超时处理。
 * @param port 正在处理的端口。
 */
static void AppWeather_Receive(BspWeatherRs485Port port)
{
    AppWeatherPort *state = &s_ports[port];
    unsigned batch;

    BspWeatherRs485_Process(port);
    for (batch = 0U; batch < 4U; ++batch)
    {
        BspWeatherRs485Byte bytes[16];
        size_t index;
        BspWeatherRs485ReadResult received = BspWeatherRs485_Read(port, bytes, 16U);

        if (received.data_lost && state->diagnostics.waiting)
        {
            state->diagnostics.uart_error = received.uart_error;
            AppWeather_Finish(
                port,
                received.lost_status != BSP_WEATHER_RS485_OK
                    ? AppWeather_UartStatus(received.lost_status)
                    : SENSOR_WEATHER_RX_OVERFLOW,
                BspTime_GetTickMs());
        }
        for (index = 0U; index < received.length; ++index)
        {
            if (!state->diagnostics.waiting ||
                (port == BSP_WEATHER_RS485_MET && s_group &&
                    (int32_t)(bytes[index].t_ms - s_group_deadline) >= 0) ||
                (uint32_t)(bytes[index].t_ms - state->diagnostics.last_start_ms) >=
                    (uint32_t)(state->tx_end_ms - state->diagnostics.last_start_ms) +
                        BSP_WEATHER_RESPONSE_TIMEOUT_MS)
            {
                ++state->diagnostics.ignored_idle_bytes;
                continue;
            }
            if (!state->rx_count)
            {
                state->rx_start_ms = bytes[index].t_ms;
            }
            state->rx_end_ms = bytes[index].t_ms;
            ++state->rx_count;
            if (state->trace_length < sizeof(state->rx_trace))
            {
                state->rx_trace[state->trace_length++] = bytes[index].byte;
            }
            else
            {
                state->trace_truncated = 1U;
            }
            if (SensorWeather_ParseByte(
                    &state->parser, bytes[index].byte, bytes[index].t_ms, BSP_WEATHER_RX_QUIET_MS))
            {
                AppWeather_Finish(port, state->parser.reply.status, BspTime_GetTickMs());
            }
        }
        if (received.length < 16U)
        {
            break;
        }
    }
    if (state->diagnostics.waiting &&
        (uint32_t)(BspTime_GetTickMs() - state->tx_end_ms) >= BSP_WEATHER_RESPONSE_TIMEOUT_MS)
    {
        AppWeather_Finish(port, SensorWeather_ParserError(&state->parser), BspTime_GetTickMs());
    }
}

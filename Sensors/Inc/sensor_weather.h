/**
 * @file sensor_weather.h
 * @brief 气象 Modbus RTU 协议，组帧、解析响应并维护最后有效测量。
 */

#ifndef SENSOR_WEATHER_H
#define SENSOR_WEATHER_H
#include "bsp_weather_rs485.h"

#define SENSOR_WEATHER_ADDRESS 1U

typedef enum
{
    SENSOR_WEATHER_TEMP_HUM = 0,
    SENSOR_WEATHER_LIGHT,
    SENSOR_WEATHER_PM,
    SENSOR_WEATHER_PRESSURE,
    SENSOR_WEATHER_CO2,
    SENSOR_WEATHER_OXYGEN,
    SENSOR_WEATHER_ITEM_COUNT
} SensorWeatherItem;

typedef enum
{
    SENSOR_WEATHER_NO_DATA = 0,
    SENSOR_WEATHER_PENDING,
    SENSOR_WEATHER_OK,
    SENSOR_WEATHER_TIMEOUT,
    SENSOR_WEATHER_CRC_ERROR,
    SENSOR_WEATHER_FRAME_ERROR,
    SENSOR_WEATHER_MODBUS_EXCEPTION,
    SENSOR_WEATHER_UART_ERROR,
    SENSOR_WEATHER_UART_BUSY,
    SENSOR_WEATHER_UART_TIMEOUT,
    SENSOR_WEATHER_RX_NOT_READY,
    SENSOR_WEATHER_IRQ_DISABLED,
    SENSOR_WEATHER_RX_OVERFLOW
} SensorWeatherStatus;

#define SENSOR_WEATHER_FIELD_TEMPERATURE 1U
#define SENSOR_WEATHER_FIELD_HUMIDITY 2U
#define SENSOR_WEATHER_FIELD_LIGHT 4U
#define SENSOR_WEATHER_FIELD_PM25 8U
#define SENSOR_WEATHER_FIELD_PM10 16U
#define SENSOR_WEATHER_FIELD_PRESSURE 32U
#define SENSOR_WEATHER_FIELD_CO2 64U
#define SENSOR_WEATHER_FIELD_OXYGEN 128U

typedef enum
{
    SENSOR_WEATHER_FRESHNESS_NO_DATA = 0,
    SENSOR_WEATHER_FRESH,
    SENSOR_WEATHER_STALE
} SensorWeatherFreshness;

typedef struct
{
    int16_t temperature_c_x10;
    uint16_t humidity_rh_pct_x10;
    uint32_t light_lux;

    uint16_t pm25_ug_m3;
    uint16_t pm10_ug_m3;
    uint16_t pressure_kpa_x100;
    uint16_t co2_ppm;
    uint16_t oxygen_pct_x10;
} SensorWeatherValue;

typedef struct
{
    SensorWeatherStatus status;
    uint8_t exception_code;
    SensorWeatherValue value;
    uint16_t valid_fields;
} SensorWeatherReply;

typedef struct
{
    SensorWeatherValue value;
    uint32_t valid_t_ms;
    uint32_t valid_sequence;
    uint32_t last_query_t_ms;
    uint32_t query_sequence;
    SensorWeatherStatus last_status;
    uint8_t has_value;
    uint8_t exception_code;

    SensorWeatherValue completed_value;
    uint32_t completed_sequence;
    uint32_t completed_t_ms;
    uint32_t sample_t_ms;
    SensorWeatherStatus completed_status;
    uint16_t valid_fields;
    uint16_t cached_fields;

    uint32_t field_sequence[8];
    uint32_t field_t_ms[8];

    uint8_t has_completed;
    uint8_t in_progress;
} SensorWeatherMeasurement;

typedef struct
{
    SensorWeatherItem item;

    uint8_t request[8];
    uint8_t bytes[16];
    uint8_t length;

    uint32_t last_byte_ms;
    uint32_t echo_count;
    uint32_t crc_errors;
    uint32_t frame_errors;
    uint8_t byte_seen;

    SensorWeatherReply reply;
} SensorWeatherParser;

/**
 * @brief 计算 Modbus RTU CRC-16。
 * @param bytes 参与校验的字节。
 * @param length 字节数。
 * @return CRC 数值，在线路上低字节先发送。
 */
uint16_t SensorWeather_Crc16(const uint8_t *bytes, size_t length);

/**
 * @brief 按测项构造读取保持寄存器的请求帧。
 * @param item 气象测项。
 * @param request 8 字节输出数组。
 * @return 构造成功返回 1，无效参数返回 0。
 */
uint8_t SensorWeather_BuildRequest(SensorWeatherItem item, uint8_t request[8]);

/**
 * @brief 获取测项使用的物理串口组。
 * @param item 气象测项。
 * @return 气象或气体组对应的 RS485 端口。
 */
BspWeatherRs485Port SensorWeather_Port(SensorWeatherItem item);

/**
 * @brief 读取气象测项的固定名称。
 * @param item 气象测项。
 * @return 只读名称字符串。
 */
const char *SensorWeather_ItemName(SensorWeatherItem item);

/**
 * @brief 读取气象响应状态的固定名称。
 * @param status 气象状态。
 * @return 只读名称字符串。
 */
const char *SensorWeather_StatusName(SensorWeatherStatus status);

/**
 * @brief 重置增量解析器并保存本次请求特征。
 * @param parser 解析上下文。
 * @param item 本次请求测项。
 */
void SensorWeather_ParserInit(SensorWeatherParser *parser, SensorWeatherItem item);

/**
 * @brief 向有界缓冲区追加一个响应字节并尝试组成 RTU 帧。
 * @param parser 解析上下文。
 * @param byte 收到的字节。
 * @param t_ms 收字节时的 tick，单位 ms。
 * @param quiet_ms 用于识别帧边界的静默时长，单位 ms。
 * @return 完整且校验通过的正常或异常帧返回 1，其余返回 0。
 */
uint8_t SensorWeather_ParseByte(
    SensorWeatherParser *parser, uint8_t byte, uint32_t t_ms, uint32_t quiet_ms);

/**
 * @brief 在等不到完整响应时汇总解析失败原因。
 * @param parser 解析上下文。
 * @return 对应的超时、CRC 或帧错误状态。
 */
SensorWeatherStatus SensorWeather_ParserError(const SensorWeatherParser *parser);

/**
 * @brief 清空各测项的查询和最后有效值缓存。
 */
void SensorWeather_Init(void);

/**
 * @brief 更新测项的查询开始状态。
 * @param item 气象测项。
 * @param sequence 本次查询序号。
 * @param t_ms 查询开始 tick，单位 ms。
 */
void SensorWeather_RecordQuery(SensorWeatherItem item, uint32_t sequence, uint32_t t_ms);

/**
 * @brief 记录完成结果，成功时更新该测项及其有效字段的缓存。
 * @param item 气象测项。
 * @param reply 响应或失败结果。
 * @param response_t_ms 响应字节时间，单位 ms。
 * @param completed_t_ms 查询结束 tick，单位 ms。
 */
void SensorWeather_Complete(
    SensorWeatherItem item,
    const SensorWeatherReply *reply,
    uint32_t response_t_ms,
    uint32_t completed_t_ms);

/**
 * @brief 访问测项的最新查询状态和最后有效值。
 * @param item 气象测项。
 * @return 模块持有的只读记录指针。
 */
const SensorWeatherMeasurement *SensorWeather_GetMeasurement(SensorWeatherItem item);

/**
 * @brief 根据最后有效值的年龄判断新鲜度。
 * @param item 气象测项。
 * @param now_ms 当前 tick，单位 ms。
 * @param period_ms 测项周期，单位 ms。
 * @return 无数据、新鲜或过期状态。
 */
SensorWeatherFreshness SensorWeather_GetFreshness(
    SensorWeatherItem item, uint32_t now_ms, uint32_t period_ms);
#endif

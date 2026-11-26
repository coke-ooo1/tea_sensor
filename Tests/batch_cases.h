/**
 * @file batch_cases.h
 * @brief 主机批次用例，覆盖采样窗口、队列、ACK、时序回绕及重试。
 */

static void Test_Through(uint32_t end)
{
    while (s_tick_ms != end)
    {
        const uint32_t distance = end - s_tick_ms;
        unsigned pass;
        Test_At(s_tick_ms + (distance < 100U ? distance : 100U));
        for (pass = 1U; pass < 8U; ++pass)
        {
            App_Process();
        }
    }
}

static uint8_t Test_JsonField(const char *path, const char *expected)
{
    const char *value;
    size_t size;
    return JSON_SearchConst(
               s_transmitted, s_transmitted_length, path, strlen(path), &value, &size, NULL) ==
               JSONSuccess &&
           size == strlen(expected) && !memcmp(value, expected, size);
}

#define TEST_FIRST_SEND_MS (TELEMETRY_UPLOAD_INTERVAL_MS + WEATHER_BOX_TIMEOUT_MS + 700U)

static void Test_FirstBatch(uint32_t base)
{
    Test_Reset(base);
    Test_At(base);
    Test_Through(base + TEST_FIRST_SEND_MS);
    assert(s_uart6_sends == 1U && AppBatch_Count() == 1U);
}

static void Test_BatchSchedule(void)
{
    uint32_t bases[] = {0U, UINT32_MAX - 450U};
    unsigned i;
    for (i = 0U; i < 2U; ++i)
    {
        uint32_t base = bases[i];
        Test_Reset(base);
        Test_At(base);
        Test_Through(base + SOIL_MOISTURE_INTERVAL_MS - 1U);
        assert(s_wind_reads == 60U && s_ad3_reads == 60U && s_ad5_reads == 60U);
        assert(s_soil_reads == 0U && s_ad4_reads == 0U && s_uart6_sends == 0U);
        Test_At(base + SOIL_MOISTURE_INTERVAL_MS);
        assert(s_soil_reads == 1U);
        Test_At(base + SOIL_MOISTURE_INTERVAL_MS + SOIL_ADC_SAMPLE_INTERVAL_MS - 1U);
        assert(s_soil_reads == 1U);
        Test_Through(base + TELEMETRY_UPLOAD_INTERVAL_MS - 1U);
        assert(s_soil_reads == 20U && s_ad4_reads == 0U && s_uart6_sends == 0U);
        Test_At(base + TELEMETRY_UPLOAD_INTERVAL_MS);
        assert(s_soil_reads == 21U && s_ad4_reads == 1U && s_uart6_sends == 0U);
        Test_Through(base + TEST_FIRST_SEND_MS);
        assert(s_soil_reads == 30U && s_ad4_reads == 1U && s_uart6_sends == 1U);
        assert(App_GetDiagnostics()->last_window_end_ms == base + TELEMETRY_UPLOAD_INTERVAL_MS);
        assert(
            Test_JsonField("schema_version", "1.0") && Test_JsonField("upload_interval_s", "900"));
        assert(
            Test_JsonField("data.wind[0].speed_valid_count", "12") &&
            Test_JsonField("data.rain[14].valid_count", "12"));
        assert(
            Test_JsonField("data.soil_moisture[0].valid_count", "10") &&
            Test_JsonField("data.soil_moisture[2].moisture_pct", "50.00"));
        assert(
            Test_JsonField("data.soil_ph[0].ph", "6.30") &&
            Test_JsonField("batch_status", "PARTIAL"));
    }
}

static void Test_BatchRetryAndAck(void)
{
    const AppUploadDiagnostics *upload;
    char stale[APP_UPLOAD_ACK_CAPACITY];
    static char first_frame[APP_UPLOAD_FRAME_CAPACITY];
    uint32_t first_tx;
    uint32_t retry;
    size_t ack_length;
    size_t first_length;
    char *id;
    Test_FirstBatch(0U);
    upload = AppUpload_GetDiagnostics();
    memcpy(stale, s_ack, s_ack_length + 1U);
    id = strstr(stale, "\"batch_id\":\"") + strlen("\"batch_id\":\"");
    *id = *id == '0' ? '1' : '0';
    first_tx = s_test_tx_tick;
    first_length = s_transmitted_length;
    memcpy(first_frame, s_transmitted, first_length);
    Test_Inject(stale, s_ack_length);
    Test_At(s_tick_ms + 1U);
    assert(AppBatch_Count() == 1U && upload->ack_count == 0U);

    Test_Through(first_tx + APP_DTU_TIMEOUT_MS + 100U);
    assert(upload->timeouts == 1U && s_uart6_sends == 1U);
    Test_Through(first_tx + APP_UPLOAD_RETRY_MS);
    assert(s_uart6_sends == 2U && upload->last_sequence == 1U);
    assert(
        s_transmitted_length == first_length && !memcmp(s_transmitted, first_frame, first_length));
    retry = s_tick_ms;
    ack_length = s_ack_length;
    Test_Inject(s_ack, ack_length / 2U);
    Test_At(retry + 1U);
    assert(AppBatch_Count() == 1U && !Test_Acks());
    Test_Inject(s_ack + ack_length / 2U, ack_length - ack_length / 2U);
    Test_At(retry + 2U);
    assert(AppBatch_Count() == 0U && Test_Acks() == 1U);

    Test_FirstBatch(0U);
    Test_Through(2U * TELEMETRY_UPLOAD_INTERVAL_MS + WEATHER_BOX_TIMEOUT_MS + 700U);
    assert(AppBatch_Count() == 2U && upload->last_sequence == 1U && s_uart6_sends > 2U);
    assert(s_soil_reads == 60U && s_ad4_reads == 2U);
    Test_Through(s_test_tx_tick + APP_UPLOAD_RETRY_MS);
    Test_Inject(s_ack, s_ack_length);
    Test_At(s_tick_ms + 1U);
    assert(AppBatch_Count() == 1U);
    Test_Through(s_tick_ms + APP_UPLOAD_NEXT_BATCH_MS + 700U);
    assert(upload->last_sequence == 2U);
    Test_Inject(s_ack, s_ack_length);
    Test_At(s_tick_ms + 1U);
    assert(AppBatch_Count() == 0U && upload->ack_count == 2U);

    Test_Reset(0U);
    s_tx_auto_complete = 0U;
    Test_At(0U);
    Test_Through(TEST_FIRST_SEND_MS);
    Test_Inject(s_ack, s_ack_length);
    Test_At(s_tick_ms + 1U);
    assert(upload->tx_pending && AppBatch_Count() == 1U && !upload->ack_count);
    Test_TxComplete();
    Test_At(s_tick_ms + 1U);
    assert(!AppBatch_Count() && upload->ack_count == 1U);
}

static void Test_BatchRecovery(void)
{
    unsigned index;
    char noise = 'x';
    Test_Reset(0U);
    s_tx_auto_complete = 0U;
    Test_At(0U);
    Test_Through(TEST_FIRST_SEND_MS);
    s_abort_tx_result = HAL_ERROR;
    Test_Through(s_tick_ms + BSP_DTU_TX_TIMEOUT_MS + 300U);
    assert(AppUpload_GetDiagnostics()->tx_errors == 1U);
    assert(AppBatch_Count() == 1U);
    s_abort_tx_result = HAL_OK;
    s_tx_auto_complete = 1U;
    Test_Through(s_tick_ms + BSP_DTU_RECOVERY_PERIOD_MS + APP_UPLOAD_RETRY_MS);
    assert(s_uart6_sends >= 2U && AppUpload_GetDiagnostics()->last_sequence == 1U);

    Test_FirstBatch(0U);
    for (index = 0U; index < APP_UPLOAD_ACK_CAPACITY + 1U; ++index)
    {
        Test_Inject(&noise, 1U);
        if (index % 32U == 31U)
        {
            Test_At(s_tick_ms + 1U);
        }
    }
    Test_Inject("\n", 1U);
    Test_At(s_tick_ms + 1U);
    assert(!Test_Acks() && AppUpload_GetDiagnostics()->line_drops == 1U);
    Test_Error(HAL_UART_ERROR_ORE);
    Test_At(s_tick_ms + 1U);
    Test_Inject("\n", 1U);
    Test_At(s_tick_ms + 1U);
    Test_Inject(s_ack, s_ack_length);
    Test_At(s_tick_ms + 1U);
    assert(Test_Acks() == 1U && AppUpload_GetDiagnostics()->stream_gaps == 1U);

    Test_Reset(0U);
    s_format_fault = 1;
    Test_At(0U);
    Test_Through(TEST_FIRST_SEND_MS);
    assert(s_uart6_sends == 0U && AppUpload_GetDiagnostics()->format_errors == 1U);
    s_format_fault = 0;
    Test_Through(s_tick_ms + APP_UPLOAD_RETRY_MS);
    assert(s_uart6_sends == 1U && AppUpload_GetDiagnostics()->last_sequence == 1U);
}

static void Test_QueueCapacity(void)
{
    unsigned i;
    AppTelemetryBatch batch = {0};
    Test_Reset(0U);
    for (i = 0; i < APP_BATCH_QUEUE_CAPACITY + 1U; ++i)
    {
        batch.sequence = i + 1U;
        assert(AppBatch_Enqueue(&batch) == (i < APP_BATCH_QUEUE_CAPACITY));
    }
    assert(AppBatch_Count() == APP_BATCH_QUEUE_CAPACITY);
    assert(AppBatch_Overflows() == 1U);
    assert(AppBatch_Peek()->sequence == 1U);
    AppBatch_Confirm();
    assert(AppBatch_Peek()->sequence == 2U);
    assert(AppBatch_Enqueue(&batch) && AppBatch_Count() == APP_BATCH_QUEUE_CAPACITY);
}

static void Test_SamplingWhileUploading(void)
{
    AppTelemetryBatch batch = {0};
    unsigned i;
    Test_Reset(0U);
    s_tx_auto_complete = 0U;
    Test_At(0U);
    Test_At(1U);
    Test_At(2U);
    Test_At(3U);
    batch.sequence = 1U;
    assert(AppBatch_Enqueue(&batch));
    Test_At(WIND_SAMPLE_INTERVAL_MS - 1U);
    Test_At(WIND_SAMPLE_INTERVAL_MS);
    assert(!s_uart6_sends && s_wind_reads == 2U && s_ad5_reads == 2U);
    for (i = 0U; i < 20U; ++i)
    {
        Test_At(2U * WIND_SAMPLE_INTERVAL_MS - 1U);
    }
    assert(AppUpload_GetDiagnostics()->tx_pending);
    Test_At(2U * WIND_SAMPLE_INTERVAL_MS);
    assert(AppUpload_GetDiagnostics()->tx_pending && s_wind_reads == 3U && s_ad5_reads == 3U);
}

#define TEST_ACK(version, device, sequence, id, status)                                            \
    "{\"protocol\":\"tea_sense.ack\",\"schema_version\":" version ",\"device_id\":" device         \
    ",\"seq\":" sequence ",\"batch_id\":" id ",\"status\":" status "}\r\n"

static void Test_AckValidation(void)
{
    const char identity[] = "tea_h743_001:0123456789abcdef0123456789abcdef:1";
#define VALID_ID "\"tea_h743_001:0123456789abcdef0123456789abcdef:1\""
#define VALID_DEVICE "\"tea_h743_001\""
#define VALID_VERSION "\"1.0\""
    const char *invalid[] = {TEST_ACK("1", VALID_DEVICE, "1", VALID_ID, "\"accepted\""),
        TEST_ACK(VALID_VERSION, VALID_DEVICE, "\"1\"", VALID_ID, "\"accepted\""),
        TEST_ACK(VALID_VERSION, VALID_DEVICE, "1.0", VALID_ID, "\"accepted\""),
        TEST_ACK(VALID_VERSION, VALID_DEVICE, "4294967296", VALID_ID, "\"accepted\""),
        TEST_ACK(VALID_VERSION, VALID_DEVICE, "1", "false", "\"accepted\""),
        TEST_ACK(VALID_VERSION, VALID_DEVICE, "1", VALID_ID, "true"),
        "{\"seq\":1,\"seq\":1}",
        "{\"status\":\"accepted\" \"seq\":1}"};
    const char *ignored[] = {TEST_ACK(VALID_VERSION, VALID_DEVICE, "1", VALID_ID, "\"rejected\""),
        TEST_ACK(VALID_VERSION, "\"other\"", "1", VALID_ID, "\"accepted\""),
        TEST_ACK(VALID_VERSION, VALID_DEVICE, "2", VALID_ID, "\"accepted\""),
        TEST_ACK(VALID_VERSION, VALID_DEVICE, "1", "\"other-batch\"", "\"accepted\""),
        TEST_ACK("\"2.0\"", VALID_DEVICE, "1", VALID_ID, "\"accepted\""),
        "{\"ok\":true,\"device_id\":\"tea_h743_001\",\"seq\":1,\"id\":1}"};
    const char *success[] = {TEST_ACK(VALID_VERSION, VALID_DEVICE, "1", VALID_ID, "\"accepted\""),
        TEST_ACK(VALID_VERSION, VALID_DEVICE, "1", VALID_ID, "\"duplicate\"")};
    unsigned i;
    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        assert(
            AppUpload_ParseAck(invalid[i], strlen(invalid[i]), 1U, identity) ==
            APP_UPLOAD_ACK_INVALID);
    }
    for (i = 0U; i < sizeof(ignored) / sizeof(ignored[0]); ++i)
    {
        assert(
            AppUpload_ParseAck(ignored[i], strlen(ignored[i]), 1U, identity) ==
            APP_UPLOAD_ACK_IGNORED);
    }
    for (i = 0U; i < sizeof(success) / sizeof(success[0]); ++i)
    {
        assert(
            AppUpload_ParseAck(success[i], strlen(success[i]), 1U, identity) ==
            APP_UPLOAD_ACK_ACCEPTED);
    }
}

#undef VALID_ID
#undef VALID_DEVICE
#undef VALID_VERSION
#undef TEST_ACK

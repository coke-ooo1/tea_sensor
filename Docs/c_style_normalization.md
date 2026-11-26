# STM32 自编 C 代码规范化验收

本轮统一了 App、Bsp、Sensors 和 Tests 的排版及注释。生产模块按头文件、宏与私有类型、模块数据、私有声明、公共接口、私有实现及中断回调分区。对同一行的独立声明和语句作了展开，结构体字段按职责留白，保留原有字段顺序和类型。

新增中文文件头和 Doxygen 接口注释，重点说明 15 分钟批次、ACK 配对与 FIFO 出队、USART 中断共享状态、RS485 接收环、ADC 清理与故障恢复、统计单位及定点舍入。已有源文件的换行方式得到保留，中文注释采用 UTF-8 无 BOM。 [Keil 编辑器编码说明](https://www.keil.com/support/man/docs/uv4/uv4_dg_editsettings.asp)

## 修改文件清单

| 范围 | 文件 |
| --- | --- |
| App/Src | app_batch.c、app_main.c、app_statistics.c、app_telemetry_json.c、app_upload.c、app_weather.c |
| App/Inc | app_batch.h、app_config.h、app_data.h、app_diagnostics.h、app_main.h、app_statistics.h、app_telemetry_json.h、app_upload.h、app_version.h、app_weather.h |
| Bsp/Src | bsp_adc.c、bsp_console.c、bsp_dtu.c、bsp_rs485_dir.c、bsp_time.c、bsp_uart_irq.c、bsp_weather_rs485.c |
| Bsp/Inc | bsp_adc.h、bsp_config.h、bsp_console.h、bsp_dtu.h、bsp_rs485_dir.h、bsp_time.h、bsp_weather_rs485.h |
| Sensors/Src | sensor_ph.c、sensor_rain.c、sensor_soil.c、sensor_weather.c、sensor_wind_direction.c、sensor_wind_speed.c |
| Sensors/Inc | sensor_ph.h、sensor_rain.h、sensor_soil.h、sensor_weather.h、sensor_wind_direction.h、sensor_wind_speed.h |
| Core/Src | main.c 的应用初始化、主循环和故障处理 USER CODE 区域 |
| Tests | arm_integration_main.c、dtu_host_test.c、host_stdio.c、soil_host_test.c、statistics_host_test.c、upload_wire_fixture.c |
| Tests 用例头文件 | ad3_cases.h、ad5_cases.h、adc_raw_cases.h、batch_cases.h、ph_cases.h、rain_cases.h、stabilization_cases.h、weather_cases.h、wind_direction_cases.h、wind_speed_cases.h |
| Tests/stubs | adc.h、rt_sys.h、stdio_fault.h、stm32h7xx_hal.h、usart.h |
| Tests 验收脚本 | check_architecture.py、run_host_tests.py。适配格式化后的空白与 UTF-8 读取，保留原有检查 |
| 工程根目录 | .clang-format、AGENTS.md |
| 验收材料 | 本文及 Docs/validation/c_style_mdk_rebuild.log |

清单包含 26 个 C 源文件、38 个头文件、2 个现有验收脚本和 2 个根目录规范文件。MDK 的常规构建输出由最终 Rebuild 刷新。

## 差异与格式验收

- 当前工程目录没有 Git 仓库。使用修改前源码快照运行 git diff --no-index 和空白检查，68 个源码与规范文件通过。
- 对 64 个 C 源和头文件核对函数体语法树、变量声明、条件分支、编译指令、常量与协议字符串；新增的私有声明与原函数签名一致。另校验 1,186 个受保护文件的 SHA-256，以及 Core/Src/main.c 的 USER CODE 外字节。
- clang-format 22.1.0 对 63 个完整自编文件通过 --dry-run --Werror。Core/Src/main.c 的 USER CODE 单独按局部范围核对。既有测试夹具的定义顺序按依赖保留。

## 构建与行为验收

| 验证 | 实际结果 |
| --- | --- |
| MDK-ARM 全量 Rebuild | tea_sense 目标，ARM Compiler 5.06 update 7 build 960，**0 错误、0 警告** |
| 架构检查 | 12 组通过，包括 ADC 通道、USART 引脚和 IRQ、层间依赖及唯一 UART 回调分发 |
| 主机回归 | 5 种配置各运行 soil_host_test 和 dtu_host_test，均通过。额外统计、最大帧、无凭据和错误配置回归也通过 |
| V1.0 通信 | 修改前后 8,100 字节测试帧的 SHA-256 相同，拆包、重传及 accepted/duplicate ACK 通过 |
| ARM 指令模拟 | 两个真实 C 构建的 15 分钟批次和 ACK 通过，覆盖采样、分组读取和标定 |
| 关键路径反汇编 | 对照重定位后的指令和操作数，21 个函数一致，涵盖主调度、ADC、USART IRQ、HAL 回调与 DTU 收发 |

正式构建镜像的 Code 从 43,140 增至 43,148 字节，RO-data 2,436、RW-data 148、ZI-data 65,148 字节。函数分区后编译器的常量池和代码布局有变化，因此新旧 HEX 的 SHA-256 不同。功能对照以源码、关键路径反汇编及回归运行结果为依据。 [最终 MDK 构建日志](validation/c_style_mdk_rebuild.log)

# 历史 V3 整合入口

当前固件协议见 [Tea Sense Telemetry Protocol V1.0](tea_sense_telemetry_protocol_v1.md)。

固件 0.11.0-dev 采用分组采集与 15 分钟 V3 批次上报，标定版本继续使用 baseline-3。配套 FastAPI 服务把采集明细存入现有本地 SQLite，兼容 V1/V2。

- [采样计划、V3 批次及本地接入](../../tea_sense_server/STM32_INTEGRATION.md)
- [本地服务启动与验证](../../tea_sense_server/README.md)
- [旧 V2 协议](../../Docs/protocol_v2.md)
- [接线与供电](../../Docs/wiring_power_interfaces.md)
- [标定登记](../../Docs/device_calibration_register.md)
- [GPS 解析与同连接鉴权](../../Docs/gps_integration.md)

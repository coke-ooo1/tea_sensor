/**
 * @file app_main.h
 * @brief 应用层采集调度，协调分钟统计、慢速采集、批次封存和后台上传。
 */

#ifndef APP_MAIN_H
#define APP_MAIN_H
typedef struct AppDiagnostics AppDiagnostics;

/**
 * @brief 初始化采集统计、上传队列和外设服务，并以当前 tick 建立首个窗口。
 * @note 由主程序在 CubeMX 外设初始化结束后调用。
 */
void App_Init(void);

/**
 * @brief 在主循环推进采样、批次封存与上传状态。
 * @note 连续调用以支持各窗口和外设恢复时限。
 */
void App_Process(void);

/**
 * @brief 访问应用层采样和批次诊断。
 * @return 模块持有的只读诊断指针。
 */
const AppDiagnostics *App_GetDiagnostics(void);
#endif

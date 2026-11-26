# Tea Sense STM32 工程维护规范

## 范围与边界

- 规范适用于 App、Bsp、Sensors 和 Tests 内自主维护的 C 源文件和头文件，以及 Core 中由项目维护的 USER CODE 区域。
- 保留 CubeMX 生成代码及 USER CODE 标记的位置和拼写。只在相关 USER CODE 标记之间编辑应用代码。
- HAL、CMSIS、第三方库、启动文件和私有生成配置按各自来源维护。私有配置 App/Inc/app_secrets.h 通过现有工具生成并只在本机保存。
- 保持当前分层、调用方向、命名、可见性和链接属性。头文件提供调用方所需的公共类型、宏和声明，私有内容留在实现文件。
- 格式维护应保留语句执行顺序、通信协议、数据类型、缓冲区容量、采样周期和硬件配置。中断、DMA、时序及临界区的改动需要独立的功能任务和验证。

## 排版

- 使用 Allman 大括号、四个空格、UTF-8 无 BOM，并保留文件原换行方式。Keil 编辑器选择 Encode in UTF-8 without signature。 [Keil 编辑器设置](https://www.keil.com/support/man/docs/uv4/uv4_dg_editsettings.asp)
- 函数体与控制语句展开。条件和执行语句分行，每行一条独立语句。函数之间保留一个空行，逻辑段落适当留白。
- 运算符两侧和逗号后保留空格，列宽以 100 为目标。长参数表和条件按逻辑边界换行，保留协议字符串和硬件宏的内容。
- 每行声明一个变量。声明区和执行逻辑之间留空行，结构体成员按职责分组。保持 static、volatile、const 等语义，避免复杂的嵌套条件和三目表达式。

## 文件与注释

- 源文件顺序为头文件引用、宏、私有类型、私有变量、私有函数声明、公共接口、私有实现、中断与回调。在相关编译条件内维持声明的有效范围。
- 用简洁的中文文件头说明文件名、模块功能与主要职责。公共接口与重要辅助函数采用 Doxygen 的 @brief、@param、@return 和按需使用的 @note。
- 注释重点说明硬件约束、状态转换、采集与通信流程、ISR 与主循环共享的数据、缓冲区上限、异常恢复和数值单位。标明适用条件和调用上下文，避免复述代码。

## 格式与构建验收

- 使用工程根目录 .clang-format。维护时使用 clang-format 22，对改动的自编文件运行 clang-format -i，再运行 clang-format --dry-run --Werror。Core 源文件按 USER CODE 范围手工核对。
- 复核 diff 中的运算、声明、宏、字符串和中断顺序。运行 py -3.12 Tests/check_architecture.py、py -3.12 Tests/run_host_tests.py 和 py -3.12 Tests/check_arm_build.py。记录 MDK 实际错误与警告数。

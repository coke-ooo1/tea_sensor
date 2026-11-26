/**
 * @file stdio_fault.h
 * @brief 主机标准 I/O 故障注入接口，用于模拟格式化边界。
 */

#ifndef TEST_STDIO_FAULT_H
#define TEST_STDIO_FAULT_H
#include <stdio.h>
/* Test-only interception after the native stdio declarations/inline bodies. */
int TestSnprintf(char *buffer, size_t capacity, const char *format, ...);
#define snprintf TestSnprintf
#endif

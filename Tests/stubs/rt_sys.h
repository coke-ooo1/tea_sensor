/**
 * @file rt_sys.h
 * @brief 主机 Arm C 库接口桩，提供控制台重定向用的声明。
 */

#ifndef TEST_RT_SYS_H
#define TEST_RT_SYS_H
/* Native declarations matching the configured Arm C library contract. */
typedef int FILEHANDLE;
#define OPEN_W 4
extern const char __stdin_name[];
extern const char __stdout_name[];
extern const char __stderr_name[];
FILEHANDLE _sys_open(const char *, int);
int _sys_write(FILEHANDLE, const unsigned char *, unsigned, int);
int _sys_read(FILEHANDLE, unsigned char *, unsigned, int);
int _sys_close(FILEHANDLE);
int _sys_istty(FILEHANDLE);
int _sys_seek(FILEHANDLE, long);
long _sys_flen(FILEHANDLE);
int _sys_ensure(FILEHANDLE);
int _sys_tmpnam(char *, int, unsigned);
char *_sys_command_string(char *, int);
void _ttywrch(int);
void _sys_exit(int);
#endif

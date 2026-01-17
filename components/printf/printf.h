
#ifndef _PRINTF_H_
#define _PRINTF_H_

#include <stdarg.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * 将字符输出到自定义设备，如 UART，由 printf（） 函数使用
 * 此功能仅在此声明。你必须在某个地方写下你的自定义实现
 * \param 字符输出
 */
void _putchar(char character);


/**
 * 微型 printf 实现
 * 如果你使用 printf（），必须实现_putchar
 * 为避免与常规 printf（） API 冲突，其被宏定义覆盖
 * 以及内部带下划线附加函数如 printf_（）
 * \param 格式 一个表示输出格式的字符串
 * \return 写入数组的字符数，不包括终止的空字符
 */
#define printf printf_
int printf_(const char* format, ...);


/**
 * 微小的 sprintf 实现
 * 出于安全原因（缓冲区溢出），你应该考虑使用 （V）SNPRINTF！
 * \param 缓冲区 指向缓冲区的指针，用于存储格式化字符串。必须足够大来存储输出！
 * \param 格式 一个表示输出格式的字符串
 * \return 已写入缓冲区的字符数，不包括终止的空字符
 */
#define sprintf sprintf_
int sprintf_(char* buffer, const char* format, ...);


/**
*  微小的 snprintf/vsnprintf 实现
 * \param 缓冲区 指向存储格式化字符串的缓冲区的指针
 * \param count 缓冲区中存储的最大字符数，包括终止的空字符
 * \param 格式 一个表示输出格式的字符串
 * \param va 一个表示变量参数列表的值
 * \return 可能写入缓冲区的字符数，不包括终止字符
 * 无字元。值大于或大于计数表示截断。只有当返回的值时
 * 是非负且小于 计数，字符串已被完全写入。
 */
#define snprintf  snprintf_
#define vsnprintf vsnprintf_
int  snprintf_(char* buffer, size_t count, const char* format, ...);
int vsnprintf_(char* buffer, size_t count, const char* format, va_list va);


/**
 * 微小的vprintf实现
 * \param 格式 一个表示输出格式的字符串
 * \param va 一个表示变量参数列表的值
 * \return 已写入缓冲区的字符数，不包括终止的空字符
 */
#define vprintf vprintf_
int vprintf_(const char* format, va_list va);


/**
 * 带有输出功能的 printf
 * 你可以将此作为 printf（） 的动态替代方案，其输出为固定的 _putchar（）
 * \param 输出函数，取一个字符和一个参数指针
 * \param arg 传递给输出函数的用户数据参数指针
 * \param 格式 一个表示输出格式的字符串
 * \return 发送到输出函数的字符数，不包括终止的空字符
 */
int fctprintf(void (*out)(char character, void* arg), void* arg, const char* format, ...);


#ifdef __cplusplus
}
#endif


#endif  // _PRINTF_H_

/*
 *  linux/include/string.h
 *
 *  (C) 1991  Linus Torvalds
 *
 *  本文件是内核专用的字符串/内存函数库的声明，实现集中在 lib/string.c。
 *  函数全部用 x86 串操作指令（cld / rep / movs / scas / stos）手工优化过。
 *
 *  说明：赵炯《Linux 内核完全注释》V3.0 把这些函数以内嵌汇编形式直接写在
 *  本头文件里（见 annotated-v3.0/include/string.h，约 1000 行，带完整中文
 *  注释）。那种写法用的是 VC 风格内嵌汇编，无法在现代 GCC 上编译，因此本
 *  版本改为「头文件声明 + lib/string.c 实现」，语义完全一致。
 *  想对照阅读内嵌汇编实现时，请打开 annotated-v3.0/include/string.h。
 */
#ifndef _STRING_H_
#define _STRING_H_

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

extern char * strerror(int errno);

/*
 * This string-include defines all string functions as inline
 * functions. Use gcc. It also assumes ds=es=data space, this should be
 * normal. Most of the string-functions are rather heavily hand-optimized,
 * see especially strtok,strstr,str[c]spn. They should work, but are not
 * very easy to understand. Everything is done entirely within the register
 * set, making the functions fast and clean. String instructions have been
 * used through-out, making for "slightly" unclear code :-)
 *
 *		(C) 1991 Linus Torvalds
 */

extern char * ___strtok;

/*
 * Changes by falcon<zhangjinw@gmail.com>, the original return value is static
 * inline ... it can not be called by other functions in another files.
 */

/* 以下函数均在 lib/string.c 中实现。 */
extern char * strcpy(char * dest, const char * src);
extern char * strncpy(char * dest, const char * src, int count);
extern char * strcat(char * dest, const char * src);
extern char * strncat(char * dest, const char * src, int count);
extern int strcmp(const char * cs, const char * ct);
extern int strncmp(const char * cs, const char * ct, int count);
extern char * strchr(const char * s, char c);
extern char * strrchr(const char * s, char c);
extern int strspn(const char * cs, const char * ct);
extern int strcspn(const char * cs, const char * ct);
extern char * strpbrk(const char * cs, const char * ct);
extern char * strstr(const char * cs, const char * ct);
extern int strlen(const char * s);
extern char * strtok(char * s, const char * ct);
extern void * memcpy(void * dest, const void * src, int n);
extern void * memmove(void * dest, const void * src, int n);
extern int memcmp(const void * cs, const void * ct, int count);
extern void * memchr(const void * cs, char c, int count);
extern void * memset(void * s, char c, int count);
#endif

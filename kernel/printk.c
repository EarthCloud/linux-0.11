/* passed
* linux/kernel/printk.c
*
* (C) 1991 Linus Torvalds
*/
#include <set_seg.h>

/*
* When in kernel-mode, we cannot use printf, as fs is liable to
* point to 'interesting' things. Make a printf with fs-saving, and
* all is well.
*/
/*
* 当处于内核模式时，我们不能使用printf，因为寄存器fs 指向其它不感兴趣的地方。
* 自己编制一个printf 并在使用前保存fs，一切就解决了。
*/
#include <stdarg.h>		// 标准参数头文件。以宏的形式定义变量参数列表。主要说明了-个
// 类型(va_list)和三个宏(va_start, va_arg 和va_end)，用于
// vsprintf、vprintf、vfprintf 函数。
#include <stddef.h>		// 标准定义头文件。定义了NULL, offsetof(TYPE, MEMBER)。

#include <linux/kernel.h>	// 内核头文件。含有一些内核常用函数的原形定义。

static char buf[1024];

// 下面该函数vsprintf()在linux/kernel/vsprintf.c 中92 行开始。
extern int vsprintf (char *buf, const char *fmt, va_list args);

int printk(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	i=vsprintf(buf,fmt,args);
	va_end(args);
	__asm__("push %%fs\n\t"
		"push %%ds\n\t"
		"pop %%fs\n\t"
		"pushl %0\n\t"
		"pushl $buf\n\t"
		"pushl $0\n\t"
		"call tty_write\n\t"
		"addl $8,%%esp\n\t"
		"popl %0\n\t"
		"pop %%fs"
		::"r" (i):"ax","cx","dx");
	return i;
}

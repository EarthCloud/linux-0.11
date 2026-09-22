//// 硬件端口字节输出函数。
// 参数：value - 欲输出字节；port - 端口。
#define outb(value, port)                                       \
	__asm__("outb %%al,%%dx" ::"a"((unsigned char)(value)), \
		"d"((unsigned short)(port)))

//// 硬件端口字节输入函数。
// 参数：port - 端口。返回读取的字节。
#define inb(port)                                                \
	({                                                       \
		unsigned char _v;                                \
		__asm__ volatile("inb %%dx,%%al"                 \
				 : "=a"(_v)                      \
				 : "d"((unsigned short)(port))); \
		_v;                                              \
	})

//// 带延迟的硬件端口字节输出函数。
// 参数：value - 欲输出字节；port - 端口。
// 原理：连续两个 jmp 到下一行，起到 I/O 延时作用（老式 ISA 设备需要）。
#define outb_p(value, port)                         \
	__asm__("outb %%al,%%dx\n"                  \
		"\tjmp 1f\n"                        \
		"1:\tjmp 1f\n"                      \
		"1:" ::"a"((unsigned char)(value)), \
		"d"((unsigned short)(port)))

//// 带延迟的硬件端口字节输入函数。
// 参数：port - 端口。返回读取的字节。
#define inb_p(port)                                              \
	({                                                       \
		unsigned char _v;                                \
		__asm__ volatile("inb %%dx,%%al\n"               \
				 "\tjmp 1f\n"                    \
				 "1:\tjmp 1f\n"                  \
				 "1:"                            \
				 : "=a"(_v)                      \
				 : "d"((unsigned short)(port))); \
		_v;                                              \
	})

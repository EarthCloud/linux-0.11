/* passed
* linux/kernel/console.c
*
* (C) 1991 Linus Torvalds
*/

/*
* console.c
*
* 该模块实现控制台输入输出功能
* 'void con_init(void)'
* 'void con_write(struct tty_queue * queue)'
* 希望这是一个非常完整的VT102 实现。
*
* 感谢John T Kohl 实现了蜂鸣指示。
*/

/*
* 注意!!! 我们有时短暂地禁止和允许中断(在将一个字(word)放到视频IO)，但即使
* 对于键盘中断这也是可以工作的。因为我们使用陷阱门，所以我们知道在获得一个
* 键盘中断时中断是不允许的。希望一切均正常。
*/

/*
* 检测不同显示卡的代码大多数是Galen Hunt 编写的，
* <g-hunt@ee.utah.edu>
*/

#include <linux/sched.h> // 调度程序头文件，定义了任务结构task_struct、初始任务0 的数据，
// 还有一些有关描述符参数设置和获取的嵌入式汇编函数宏语句。
#include <linux/tty.h> // tty 头文件，定义了有关tty_io，串行通信方面的参数、常数。
#include <asm/io.h>    // io 头文件。定义硬件端口输入/输出宏汇编语句。
#include <asm/system.h> // 系统头文件。定义了设置或修改描述符/中断门等的嵌入式汇编宏。

/*
* 这些是设置子程序setup 在引导启动系统时设置的参数：
*/

// 参见对boot/setup.s 的注释，和setup 程序读取并保留的参数表。
#define ORIG_X (*(unsigned char *)0x90000)		      // 光标列号。
#define ORIG_Y (*(unsigned char *)0x90001)		      // 光标行号。
#define ORIG_VIDEO_PAGE (*(unsigned short *)0x90004)	      // 显示页面。
#define ORIG_VIDEO_MODE ((*(unsigned short *)0x90006) & 0xff) // 显示模式。
#define ORIG_VIDEO_COLS \
	(((*(unsigned short *)0x90006) & 0xff00) >> 8) // 字符列数。
#define ORIG_VIDEO_LINES (25)			       // 显示行数。
#define ORIG_VIDEO_EGA_AX (*(unsigned short *)0x90008) // [??]
#define ORIG_VIDEO_EGA_BX \
	(*(unsigned short *)0x9000a) // 显示内存大小和色彩模式。
#define ORIG_VIDEO_EGA_CX (*(unsigned short *)0x9000c) // 显示卡特性参数。

// 定义显示器单色/彩色显示模式类型符号常数。
#define VIDEO_TYPE_MDA 0x10  /* 单色文本 */
#define VIDEO_TYPE_CGA 0x11  /* CGA 显示器 */
#define VIDEO_TYPE_EGAM 0x20 /* EGA/VGA 单色 */
#define VIDEO_TYPE_EGAC 0x21 /* EGA/VGA 彩色 */

#define NPAR 16

extern void keyboard_interrupt(void); // 键盘中断处理程序(keyboard.S)。

static unsigned char video_type;	/* 使用的显示类型 */
static unsigned long video_num_columns; /* 屏幕文本列数 */
static unsigned long video_size_row;	/* 每行使用的字节数 */
static unsigned long video_num_lines;	/* 屏幕文本行数 */
static unsigned char video_page;	/* 初始显示页面 */
static unsigned long video_mem_start;	/* 显示内存起始地址 */
static unsigned long video_mem_end;	/* 显示内存结束(末端)地址 */
static unsigned short video_port_reg;	/* 显示控制索引寄存器端口 */
static unsigned short video_port_val;	/* 显示控制数据寄存器端口 */
static unsigned short video_erase_char; /* 擦除字符属性与字符(0x0720) */

// 以下这些变量用于屏幕卷屏操作。
static unsigned long origin; /* 用于EGA/VGA 快速滚屏 */	 // 滚屏起始内存地址。
static unsigned long scr_end; /* 用于EGA/VGA 快速滚屏 */ // 滚屏末端内存地址。
static unsigned long pos;	  // 当前光标对应的显示内存位置。
static unsigned long x, y;	  // 当前光标位置。
static unsigned long top, bottom; // 滚动时顶行行号；底行行号。
// state 用于标明处理ESC 转义序列时的当前步骤。npar,par[]用于存放ESC 序列的中间处理参数。
static unsigned long state = 0;	      // ANSI 转义字符序列处理状态。
static unsigned long npar, par[NPAR]; // ANSI 转义字符序列参数个数和参数数组。
static unsigned long ques = 0;
static unsigned char attr = 0x07; // 字符属性(黑底白字)。

static void sysbeep(void); // 系统蜂鸣函数。

/*
* 下面是终端回应ESC-Z 或csi0c 请求的应答(=vt100 响应)。
*/
// csi - 控制序列引导码(Control Sequence Introducer)。
#define RESPONSE "\033[?1;2c"

/* 注意！gotoxy 函数认为x==video_num_columns，这是正确的 */
//// 跟踪光标当前位置。
// 参数：new_x - 光标所在列号；new_y - 光标所在行号。
// 更新当前光标位置变量x,y，并修正pos 指向光标在显示内存中的对应位置。
static inline void gotoxy(unsigned int new_x, unsigned int new_y)
{
	// 如果输入的光标行号超出显示器列数，或者光标行号超出显示的最大行数，则退出。
	if (new_x > video_num_columns || new_y >= video_num_lines)
		return;
	// 更新当前光标变量；更新光标位置对应的在显示内存中位置变量pos。
	x = new_x;
	y = new_y;
	pos = origin + y * video_size_row + (x << 1);
}

//// 设置滚屏起始显示内存地址。
static inline void set_origin(void)
{
	cli();
	// 首先选择显示控制数据寄存器r12，然后写入卷屏起始地址高字节。向右移动9 位，表示向右移动
	// 8 位，再除以2(2 字节代表屏幕上1 字符)。是相对于默认显示内存操作的。
	outb_p(12, video_port_reg);
	outb_p((unsigned char)(0xff & ((origin - video_mem_start) >> 9)),
	       video_port_val);
	// 再选择显示控制数据寄存器r13，然后写入卷屏起始地址底字节。向右移动1 位表示除以2。
	outb_p(13, video_port_reg);
	outb_p((unsigned char)(0xff & ((origin - video_mem_start) >> 1)),
	       video_port_val);
	sti();
}

//// 向上卷动一行（屏幕窗口向下移动）。
// 将屏幕窗口向下移动一行。参见程序列表后说明。
static void scrup(void)
{
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM) {
		if (!top && bottom == video_num_lines) {
			origin += video_size_row;
			pos += video_size_row;
			scr_end += video_size_row;
			if (scr_end > video_mem_end) {
				__asm__("cld\n\t"
					"rep\n\t"
					"movsl\n\t"
					"movl video_num_columns,%1\n\t"
					"rep\n\t"
					"stosw" ::"a"(video_erase_char),
					"c"((video_num_lines - 1) *
						video_num_columns >>
					    1),
					"D"(video_mem_start),
					"S"(origin));
				scr_end -= origin - video_mem_start;
				pos -= origin - video_mem_start;
				origin = video_mem_start;
			} else {
				__asm__("cld\n\t"
					"rep\n\t"
					"stosw" ::"a"(video_erase_char),
					"c"(video_num_columns),
					"D"(scr_end - video_size_row));
			}
			set_origin();
		} else {
			__asm__(
			    "cld\n\t"
			    "rep\n\t"
			    "movsl\n\t"
			    "movl video_num_columns,%%ecx\n\t"
			    "rep\n\t"
			    "stosw" ::"a"(video_erase_char),
			    "c"((bottom - top - 1) * video_num_columns >> 1),
			    "D"(origin + video_size_row * top),
			    "S"(origin + video_size_row * (top + 1)));
		}
	} else /* Not EGA/VGA */
	{
		__asm__("cld\n\t"
			"rep\n\t"
			"movsl\n\t"
			"movl video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw" ::"a"(video_erase_char),
			"c"((bottom - top - 1) * video_num_columns >> 1),
			"D"(origin + video_size_row * top),
			"S"(origin + video_size_row * (top + 1)));
	}
}

//// 向下卷动一行（屏幕窗口向上移动）。
// 将屏幕窗口向上移动一行，屏幕显示的内容向下移动1 行，在被移动开始行的上方出现一新行。参见
// 程序列表后说明。处理方法与scrup()相似，只是为了在移动显示内存数据时不出现数据覆盖错误情
// 况，复制是以反方向进行的，也即从屏幕倒数第2 行的最后一个字符开始复制
static void scrdown(void)
{
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM) {
		__asm__("std\n\t"
			"rep\n\t"
			"movsl\n\t"
			"addl $2,%%edi\n\t" /* %edi has been decremented by 4 */
			"movl video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw" ::"a"(video_erase_char),
			"c"((bottom - top - 1) * video_num_columns >> 1),
			"D"(origin + video_size_row * bottom - 4),
			"S"(origin + video_size_row * (bottom - 1) - 4));
	} else /* Not EGA/VGA */
	{
		__asm__("std\n\t"
			"rep\n\t"
			"movsl\n\t"
			"addl $2,%%edi\n\t" /* %edi has been decremented by 4 */
			"movl video_num_columns,%%ecx\n\t"
			"rep\n\t"
			"stosw" ::"a"(video_erase_char),
			"c"((bottom - top - 1) * video_num_columns >> 1),
			"D"(origin + video_size_row * bottom - 4),
			"S"(origin + video_size_row * (bottom - 1) - 4));
	}
}

//// 光标位置下移一行(lf - line feed 换行)。
static void lf(void)
{
	// 如果光标没有处在倒数第2 行之后，则直接修改光标当前行变量y++，并调整光标对应显示内存位置
	// pos(加上屏幕一行字符所对应的内存长度)。
	if (y + 1 < bottom) {
		y++;
		pos += video_size_row;
		return;
	}
	// 否则需要将屏幕内容上移一行。
	scrup();
}

//// 光标上移一行(ri - reverse line feed 反向换行)。
static void ri(void)
{
	// 如果光标不在第1 行上，则直接修改光标当前行标量y--，并调整光标对应显示内存位置pos，减去
	// 屏幕上一行字符所对应的内存长度字节数。
	if (y > top) {
		y--;
		pos -= video_size_row;
		return;
	}
	// 否则需要将屏幕内容下移一行。
	scrdown();
}

// 光标回到第1 列(0 列)左端(cr - carriage return 回车)。
static void cr(void)
{
	// 光标所在的列号*2 即0 列到光标所在列对应的内存字节长度。
	pos -= x << 1;
	x = 0;
}

// 擦除光标前一字符(用空格替代)(del - delete 删除)。
static void del(void)
{
	// 如果光标没有处在0 列，则将光标对应内存位置指针pos 后退2 字节(对应屏幕上一个字符)，然后
	// 将当前光标变量列值减1，并将光标所在位置字符擦除。
	if (x) {
		pos -= 2;
		x--;
		*(unsigned short *)pos = video_erase_char;
	}
}

//// 删除屏幕上与光标位置相关的部分，以屏幕为单位。csi - 控制序列引导码(Control Sequence
// Introducer)。
// ANSI 转义序列：'ESC [sJ'(s = 0 删除光标到屏幕底端；1 删除屏幕开始到光标处；2 整屏删除)。
// 参数：par - 对应上面s。
static void csi_J(int par)
{
	long count;
	long start;

	switch (par) {
	case 0: /* erase from cursor to end of display */
		count = (scr_end - pos) >> 1;
		start = pos;
		break;
	case 1: /* erase from start to cursor */
		count = (pos - origin) >> 1;
		start = origin;
		break;
	case 2: /* erase whole display */
		count = video_num_columns * video_num_lines;
		start = origin;
		break;
	default:
		return;
	}
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t" ::"c"(count),
		"D"(start),
		"a"(video_erase_char));
}

//// 删除行内与光标位置相关的部分，以一行为单位。
// ANSI 转义字符序列：'ESC [sK'(s = 0 删除到行尾；1 从开始删除；2 整行都删除)。
static void csi_K(int par)
{
	long count;
	long start;

	switch (par) {
	case 0: /* erase from cursor to end of line */
		if (x >= video_num_columns)
			return;
		count = video_num_columns - x;
		start = pos;
		break;
	case 1: /* erase from start of line to cursor */
		start = pos - (x << 1);
		count = (x < video_num_columns) ? x : video_num_columns;
		break;
	case 2: /* erase whole line */
		start = pos - (x << 1);
		count = video_num_columns;
		break;
	default:
		return;
	}
	__asm__("cld\n\t"
		"rep\n\t"
		"stosw\n\t" ::"c"(count),
		"D"(start),
		"a"(video_erase_char));
}

//// 允许翻译(重显)（允许重新设置字符显示方式，比如加粗、加下划线、闪烁、反显等）。
// ANSI 转义字符序列：'ESC [nm'。n = 0 正常显示；1 加粗；4 加下划线；7 反显；27 正常显示。
void csi_m(void)
{
	unsigned int i;

	for (i = 0; i <= npar; i++)
		switch (par[i]) {
		case 0:
			attr = 0x07;
			break;
		case 1:
			attr = 0x0f;
			break;
		case 4:
			attr = 0x0f;
			break;
		case 7:
			attr = 0x70;
			break;
		case 27:
			attr = 0x07;
			break;
		}
}

//// 根据设置显示光标。
// 根据显示内存光标对应位置pos，设置显示控制器光标的显示位置。
static inline void set_cursor(void)
{
	cli();
	// 首先使用索引寄存器端口选择显示控制数据寄存器r14(光标当前显示位置高字节)，然后写入光标
	// 当前位置高字节(向右移动9 位表示高字节移到低字节再除以2)。是相对于默认显示内存操作的。
	outb_p(14, video_port_reg);
	outb_p((unsigned char)(0xff & ((pos - video_mem_start) >> 9)),
	       video_port_val);
	// 再使用索引寄存器选择r15，并将光标当前位置低字节写入其中。
	outb_p(15, video_port_reg);
	outb_p((unsigned char)(0xff & ((pos - video_mem_start) >> 1)),
	       video_port_val);
	sti();
}

//// 发送对终端VT100 的响应序列。
// 将响应序列放入读缓冲队列中。
static void respond(struct tty_struct * tty)
{
	char * p = RESPONSE;

	cli();	     // 关中断。
	while (*p) { // 将字符序列放入写队列。
		PUTCH(*p, tty->read_q);
		p++;
	}
	sti();		     // 开中断。
	copy_to_cooked(tty); // 转换成规范模式(放入辅助队列中)。
}

//// 在光标处插入一空格字符。
static void insert_char(void)
{
	unsigned long i = x;
	unsigned short tmp, old = video_erase_char;
	unsigned short * p = (unsigned short *)pos;

	// 光标开始的所有字符右移一格，并将擦除字符插入在光标所在处。
	// 若一行上都有字符的话，则行最后一个字符将不会更动??
	while (i++ < video_num_columns) {
		tmp = *p;
		*p = old;
		old = tmp;
		p++;
	}
}

//// 在光标处插入一行（则光标将处在新的空行上）。
// 将屏幕从光标所在行到屏幕底向下卷动一行。
static void insert_line(void)
{
	int oldtop, oldbottom;

	oldtop = top; // 保存原top，bottom 值。
	oldbottom = bottom;
	top = y;		  // 设置屏幕卷动开始行。
	bottom = video_num_lines; // 设置屏幕卷动最后行。
	scrdown();		  // 从光标开始处，屏幕内容向下滚动一行。
	top = oldtop;		  // 恢复原top，bottom 值。
	bottom = oldbottom;
}

//// 删除光标处的一个字符。
static void delete_char(void)
{
	unsigned long i;
	unsigned short * p = (unsigned short *)pos;

	// 如果光标超出屏幕最右列，则返回。
	if (x >= video_num_columns)
		return;
	// 从光标右一个字符开始到行末所有字符左移一格。
	i = x;
	while (++i < video_num_columns) {
		*p = *(p + 1);
		p++;
	}
	// 最后一个字符处填入擦除字符(空格字符)。
	*p = video_erase_char;
}

//// 删除光标所在行。
// 从光标所在行开始屏幕内容上卷一行。
static void delete_line(void)
{
	int oldtop, oldbottom;

	oldtop = top; // 保存原top，bottom 值。
	oldbottom = bottom;
	top = y;		  // 设置屏幕卷动开始行。
	bottom = video_num_lines; // 设置屏幕卷动最后行。
	scrup();		  // 从光标开始处，屏幕内容向上滚动一行。
	top = oldtop;		  // 恢复原top，bottom 值。
	bottom = oldbottom;
}

//// 在光标处插入nr 个字符。
// ANSI 转义字符序列：'ESC [n@ '。
// 参数 nr = 上面n。
static void csi_at(unsigned int nr)
{
	// 如果插入的字符数大于一行字符数，则截为一行字符数；若插入字符数nr 为0，则插入1 个字符。
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	// 循环插入指定的字符数。
	while (nr--)
		insert_char();
}

//// 在光标位置处插入nr 行。
// ANSI 转义字符序列'ESC [nL'。
static void csi_L(unsigned int nr)
{
	// 如果插入的行数大于屏幕最多行数，则截为屏幕显示行数；若插入行数nr 为0，则插入1 行。
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	// 循环插入指定行数nr。
	while (nr--)
		insert_line();
}

//// 删除光标处的nr 个字符。
// ANSI 转义序列：'ESC [nP'。
static void csi_P(unsigned int nr)
{
	// 如果删除的字符数大于一行字符数，则截为一行字符数；若删除字符数nr 为0，则删除1 个字符。
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;
	// 循环删除指定字符数nr。
	while (nr--)
		delete_char();
}

//// 删除光标处的nr 行。
// ANSI 转义序列：'ESC [nM'。
static void csi_M(unsigned int nr)
{
	// 如果删除的行数大于屏幕最多行数，则截为屏幕显示行数；若删除的行数nr 为0，则删除1 行。
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	// 循环删除指定行数nr。
	while (nr--)
		delete_line();
}

static int saved_x = 0; // 保存的光标列号。
static int saved_y = 0; // 保存的光标行号。

//// 保存当前光标位置。
static void save_cur(void)
{
	saved_x = x;
	saved_y = y;
}

//// 恢复保存的光标位置。
static void restore_cur(void)
{
	gotoxy(saved_x, saved_y);
}

//// 控制台写函数。
// 从终端对应的tty 写缓冲队列中取字符，并显示在屏幕上。
void con_write(struct tty_struct * tty)
{
	int nr;
	char c;

	nr = CHARS(tty->write_q);
	while (nr--) {
		GETCH(tty->write_q, c);
		switch (state) {
		case 0:
			if (c > 31 && c < 127) {
				if (x >= video_num_columns) {
					x -= video_num_columns;
					pos -= video_size_row;
					lf();
				}
				__asm__("movb attr,%%ah\n\t"
					"movw %%ax,%1\n\t" ::"a"(c),
					"m"(*(short *)pos));
				pos += 2;
				x++;
			} else if (c == 27)
				state = 1;
			else if (c == 10 || c == 11 || c == 12)
				lf();
			else if (c == 13)
				cr();
			else if (c == ERASE_CHAR(tty))
				del();
			else if (c == 8) {
				if (x) {
					x--;
					pos -= 2;
				}
			} else if (c == 9) {
				c = 8 - (x & 7);
				x += c;
				pos += c << 1;
				if (x > video_num_columns) {
					x -= video_num_columns;
					pos -= video_size_row;
					lf();
				}
				c = 9;
			} else if (c == 7)
				sysbeep();
			break;
		case 1:
			state = 0;
			if (c == '[')
				state = 2;
			else if (c == 'E')
				gotoxy(0, y + 1);
			else if (c == 'M')
				ri();
			else if (c == 'D')
				lf();
			else if (c == 'Z')
				respond(tty);
			else if (x == '7')
				save_cur();
			else if (x == '8')
				restore_cur();
			break;
		case 2:
			for (npar = 0; npar < NPAR; npar++)
				par[npar] = 0;
			npar = 0;
			state = 3;
			if ((ques = (c == '?')))
				break;
		case 3:
			if (c == ';' && npar < NPAR - 1) {
				npar++;
				break;
			} else if (c >= '0' && c <= '9') {
				par[npar] = 10 * par[npar] + c - '0';
				break;
			} else
				state = 4;
		case 4:
			state = 0;
			switch (c) {
			case 'G':
			case '`':
				if (par[0])
					par[0]--;
				gotoxy(par[0], y);
				break;
			case 'A':
				if (!par[0])
					par[0]++;
				gotoxy(x, y - par[0]);
				break;
			case 'B':
			case 'e':
				if (!par[0])
					par[0]++;
				gotoxy(x, y + par[0]);
				break;
			case 'C':
			case 'a':
				if (!par[0])
					par[0]++;
				gotoxy(x + par[0], y);
				break;
			case 'D':
				if (!par[0])
					par[0]++;
				gotoxy(x - par[0], y);
				break;
			case 'E':
				if (!par[0])
					par[0]++;
				gotoxy(0, y + par[0]);
				break;
			case 'F':
				if (!par[0])
					par[0]++;
				gotoxy(0, y - par[0]);
				break;
			case 'd':
				if (par[0])
					par[0]--;
				gotoxy(x, par[0]);
				break;
			case 'H':
			case 'f':
				if (par[0])
					par[0]--;
				if (par[1])
					par[1]--;
				gotoxy(par[1], par[0]);
				break;
			case 'J':
				csi_J(par[0]);
				break;
			case 'K':
				csi_K(par[0]);
				break;
			case 'L':
				csi_L(par[0]);
				break;
			case 'M':
				csi_M(par[0]);
				break;
			case 'P':
				csi_P(par[0]);
				break;
			case '@':
				csi_at(par[0]);
				break;
			case 'm':
				csi_m();
				break;
			case 'r':
				if (par[0])
					par[0]--;
				if (!par[1])
					par[1] = video_num_lines;
				if (par[0] < par[1] &&
				    par[1] <= video_num_lines) {
					top = par[0];
					bottom = par[1];
				}
				break;
			case 's':
				save_cur();
				break;
			case 'u':
				restore_cur();
				break;
			}
		}
	}
	set_cursor();
}

/*
* void con_init(void);
* 这个子程序初始化控制台中断，其它什么都不做。如果你想让屏幕干净的话，就使用
* 适当的转义字符序列调用tty_write()函数。
*
* 读取setup.s 程序保存的信息，用以确定当前显示器类型，并且设置所有相关参数。
*/
void con_init(void)
{
	register unsigned char a;
	char * display_desc = "????";
	char * display_ptr;

	video_num_columns = ORIG_VIDEO_COLS;	     // 显示器显示字符列数。
	video_size_row = video_num_columns * 2;	     // 每行需使用字节数。
	video_num_lines = ORIG_VIDEO_LINES;	     // 显示器显示字符行数。
	video_page = (unsigned char)ORIG_VIDEO_PAGE; // 当前显示页面。
	video_erase_char = 0x0720; // 擦除字符(0x20 显示字符， 0x07 是属性)。

	// 如果原始显示模式等于7，则表示是单色显示器。
	if (ORIG_VIDEO_MODE == 7) /* Is this a monochrome display? */
	{
		video_mem_start = 0xb0000; // 设置单显映象内存起始地址。
		video_port_reg = 0x3b4;	   // 设置单显索引寄存器端口。
		video_port_val = 0x3b5;	   // 设置单显数据寄存器端口。
		// 根据BIOS 中断int 0x10 功能0x12 获得的显示模式信息，判断显示卡单色显示卡还是彩色显示卡。
		// 如果使用上述中断功能所得到的BX 寄存器返回值不等于0x10，则说明是EGA 卡。因此初始
		// 显示类型为EGA 单色；所使用映象内存末端地址为0xb8000；并置显示器描述字符串为'EGAm'。
		// 在系统初始化期间显示器描述字符串将显示在屏幕的右上角。
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10) {
			video_type =
			    VIDEO_TYPE_EGAM;	 // 设置显示类型(EGA 单色)。
			video_mem_end = 0xb8000; // 设置显示内存末端地址。
			display_desc = "EGAm";	 // 设置显示描述字符串。
		}
		// 如果BX 寄存器的值等于0x10，则说明是单色显示卡MDA。则设置相应参数。
		else {
			video_type = VIDEO_TYPE_MDA; // 设置显示类型(MDA 单色)。
			video_mem_end = 0xb2000;     // 设置显示内存末端地址。
			display_desc = "*MDA";	     // 设置显示描述字符串。
		}
	}
	// 如果显示模式不为7，则为彩色模式。此时所用的显示内存起始地址为0xb800；显示控制索引寄存
	// 器端口地址为0x3d4；数据寄存器端口地址为0x3d5。
	else /* If not, it is color. */
	{
		video_mem_start = 0xb8000; // 显示内存起始地址。
		video_port_reg = 0x3d4;	   // 设置彩色显示索引寄存器端口。
		video_port_val = 0x3d5;	   // 设置彩色显示数据寄存器端口。
		// 再判断显示卡类别。如果BX 不等于0x10，则说明是EGA 显示卡。
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10) {
			video_type =
			    VIDEO_TYPE_EGAC;	 // 设置显示类型(EGA 彩色)。
			video_mem_end = 0xbc000; // 设置显示内存末端地址。
			display_desc = "EGAc";	 // 设置显示描述字符串。
		}
		// 如果BX 寄存器的值等于0x10，则说明是CGA 显示卡。则设置相应参数。
		else {
			video_type = VIDEO_TYPE_CGA; // 设置显示类型(CGA)。
			video_mem_end = 0xba000;     // 设置显示内存末端地址。
			display_desc = "*CGA";	     // 设置显示描述字符串。
		}
	}

	/* Let the user known what kind of display driver we are using */
	/* 让用户知道我们正在使用哪一类显示驱动程序 */

	// 在屏幕的右上角显示显示描述字符串。采用的方法是直接将字符串写到显示内存的相应位置处。
	// 首先将显示指针display_ptr 指到屏幕第一行右端差4 个字符处(每个字符需2 个字节，因此减8)。
	display_ptr = ((char *)video_mem_start) + video_size_row - 8;
	// 然后循环复制字符串中的字符，并且每复制一个字符都空开一个属性字节。
	while (*display_desc) {
		*display_ptr++ = *display_desc++; // 复制字符。
		display_ptr++;			  // 空开属性字节位置。
	}

	/* 初始化用于滚屏的变量(主要用于EGA/VGA) */

	origin = video_mem_start; // 滚屏起始显示内存地址。
	scr_end = video_mem_start +
		  video_num_lines * video_size_row; // 滚屏结束内存地址。
	top = 0;				    // 最顶行号。
	bottom = video_num_lines;		    // 最底行号。

	gotoxy(ORIG_X, ORIG_Y); // 初始化光标位置x,y 和对应的内存位置pos。
	set_trap_gate(0x21, &keyboard_interrupt); // 设置键盘中断陷阱门。
	outb_p((unsigned char)(inb_p(0x21) & 0xfd),
	       0x21);	 // 取消8259A 中对键盘中断的屏蔽，允许IRQ1。
	a = inb_p(0x61); // 延迟读取键盘端口0x61(8255A 端口PB)。
	outb_p((unsigned char)(a | 0x80), 0x61); // 设置禁止键盘工作(位7 置位)，
	outb(a, 0x61); // 再允许键盘工作，用以复位键盘操作。
}

/* from bsd-net-2: */

//// 停止蜂鸣。
// 复位8255A PB 端口的位1 和位0。
void sysbeepstop(void)
{
	/* disable counter 2 */ /* 禁止定时器2 */
	outb((unsigned char)(inb_p(0x61) & 0xFC), 0x61);
}

int beepcount = 0;

// 开通蜂鸣。
// 8255A 芯片PB 端口的位1 用作扬声器的开门信号；位0 用作8253 定时器2 的门信号，该定时器的
// 输出脉冲送往扬声器，作为扬声器发声的频率。因此要使扬声器蜂鸣，需要两步：首先开启PB 端口
// 位1 和位0（置位），然后设置定时器发送一定的定时频率即可。
static void sysbeep(void)
{
	/* enable counter 2 */ /* 开启定时器2 */
	outb_p((unsigned char)(inb_p(0x61) | 3), 0x61);
	/* set command for counter 2, 2 byte write */ /* 送设置定时器2 命令 */
	outb_p(0xB6, 0x43);
	/* send 0x637 for 750 HZ */ /* 设置频率为750HZ，因此送定时值0x637 */
	outb_p(0x37, 0x42);
	outb(0x06, 0x42);
	/* 1/8 second */ /* 蜂鸣时间为1/8 秒 */
	beepcount = HZ / 8;
}

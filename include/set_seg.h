#ifndef SET_SEG_H
#define SET_SEG_H

/*
 * set_seg.h —— 段操作兼容头文件。
 *
 * 说明：在赵炯《Linux 内核完全注释》V3.0 的 Windows(Visual C++) 版本中，
 * 本文件只有一行内容：
 *
 *      #define _inline _forceinline
 *
 * 作用是把源码里的 _inline 映射到 MSVC 的 _forceinline 关键字。
 *
 * 本版本已移植为 GCC 方言：源码中所有 _inline / _forceinline 都已统一替换为
 * 标准的 inline，因此这里不再需要任何宏定义。保留本文件是为了不修改各处源码
 * 中的 #include <set_seg.h> 语句，便于与原版对照阅读。
 */

#endif

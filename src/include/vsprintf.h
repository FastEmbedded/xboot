#ifndef __VSPRINTF_H__
#define __VSPRINTF_H__

#include <configs.h>
#include <default.h>
#include <stdarg.h>


x_u32 simple_strtou32(const x_s8 *cp, x_s8 **endp, x_u32 base);
x_s32 simple_strtos32(const x_s8 *cp, x_s8 **endp, x_u32 base);
x_u64 simple_strtou64(const x_s8 *cp, x_s8 **endp, x_u32 base);
x_s64 simple_strtos64(const x_s8 *cp, x_s8 **endp, x_u32 base);

x_s32 vsnprintf(x_s8 *buf, x_s32 size, const x_s8 *fmt, va_list args);
x_s32 vsscanf(const x_s8 * buf, const x_s8 * fmt, va_list args);

x_s32 sprintf(x_s8 * buf, const x_s8 * fmt, ...);
x_s32 snprintf(x_s8 * buf, x_s32 size, const x_s8 * fmt, ...);
x_s32 sscanf(const x_s8 * buf, const x_s8 * fmt, ...);

x_s32 ssize(x_s8 * buf, x_u64 size);

x_s32 utf8_to_ucs4(x_u32 * dst, x_s32 dst_size, const x_u8 * src, x_s32 src_size, const x_u8 ** src_end);
x_s32 ucs4_to_utf8(x_u32 c, x_u8 * buf);

#endif /* __VSPRINTF_H__ */

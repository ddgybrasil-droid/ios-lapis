/* klog: printf-style logging for the drivers.
 * The kernel is built for the Darwin arm64 ABI, where *variadic* arguments are passed
 * on the stack (8-byte slots), while this blob is compiled by aarch64-linux gcc (AAPCS64:
 * variadic args in x1..x7).  So we never call a kernel variadic function with our
 * arguments: we format here (gcc va_list, internal only) and hand the finished string to
 * IOLog("%s", s) through drv_iolog_str (common/drv_entry.S), which puts `s` on the stack. */
#include "../include/kern.h"
#include <stdarg.h>
extern void drv_iolog_str(const char *s);

static char *put(char *p, char *e, char c) { if (p < e) *p = c; return p + 1; }

int kvsnprintf(char *buf, size_t n, const char *f, va_list ap)
{
	char *p = buf, *e = buf + (n ? n - 1 : 0);
	for (; *f; f++) {
		if (*f != '%') { p = put(p, e, *f); continue; }
		int alt = 0, zero = 0, width = 0, lng = 0;
		f++;
		for (;; f++) { if (*f == '#') alt = 1; else if (*f == '0') zero = 1; else break; }
		while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
		while (*f == 'l') { lng++; f++; }
		char c = *f, tmp[24]; int neg = 0, len = 0; uint64_t v = 0; unsigned base = 10; int upper = 0;
		switch (c) {
		case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
			int l = (int)strlen(s); for (; l < width; width--) p = put(p, e, ' ');
			while (*s) { p = put(p, e, *s++); }
			continue; }
		case 'c': p = put(p, e, (char)va_arg(ap, int)); continue;
		case '%': p = put(p, e, '%'); continue;
		case 'p': v = (uint64_t)va_arg(ap, void *); base = 16; alt = 1; break;
		case 'd': case 'i': { int64_t s = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
			if (s < 0) { neg = 1; v = (uint64_t)-s; } else v = (uint64_t)s; break; }
		case 'u': v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned); break;
		case 'X': upper = 1; /* fallthrough */
		case 'x': v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned); base = 16; break;
		default: p = put(p, e, '%'); p = put(p, e, c); continue;
		}
		do { unsigned d = (unsigned)(v % base); tmp[len++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10); v /= base; } while (v);
		int pre = neg + (alt && base == 16 ? 2 : 0);
		if (!zero) for (int w = len + pre; w < width; w++) p = put(p, e, ' ');
		if (neg) p = put(p, e, '-');
		if (alt && base == 16) { p = put(p, e, '0'); p = put(p, e, 'x'); }
		if (zero) for (int w = len + pre; w < width; w++) p = put(p, e, '0');
		while (len) p = put(p, e, tmp[--len]);
	}
	if (n) *(p < e ? p : e) = 0;
	return (int)(p - buf);
}

int ksnprintf(char *buf, size_t n, const char *f, ...)
{ va_list ap; va_start(ap, f); int r = kvsnprintf(buf, n, f, ap); va_end(ap); return r; }

void klog(const char *f, ...)
{
	char b[320]; va_list ap; va_start(ap, f); kvsnprintf(b, sizeof(b), f, ap); va_end(ap);
	drv_iolog_str(b);
}

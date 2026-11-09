
#pragma once
#include <stdint.h>

/* lapis splash FB (M0-verified): XRGB8888, landscape, no rotation */
#define FB_BASE		0xD5100000UL
#define FB_W		2944
#define FB_H		1840
#define FB_STRIDE	11776

#define RED	0xffff0000U
#define BLUE	0xff0000a0U
#define WHITE	0xffffffffU
#define YELLOW	0xffffff00U
#define GREEN	0xff00ff00U

extern volatile uint32_t *con_fb;

void con_clear(uint32_t bg);
void con_skip(void);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);

/* s8: free-positioned text (any scale), and hex formatting helper */
void con_text_at(int x, int y, int scale, const char *s, uint32_t fg, uint32_t bg);
char *con_fmt_hex(char *dst, uint64_t v, int digits);

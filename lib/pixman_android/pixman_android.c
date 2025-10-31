/*
 * Copyright © 2008 Red Hat, Inc.
 * Copyright © 2010 Intel Corporation
 * Copyright © 2024 Android NDK Optimized Version
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include "pixman.h"

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

#ifndef PIXMAN_EXPORT
#define PIXMAN_EXPORT PIXMAN_API
#endif

// ========== 添加 CPU 特性检测（利用 NDK API） ==========
#ifdef __ANDROID__
#include <cpu-features.h>
static uint64_t cpu_features = 0;
static android_cpu_family_t cpu_family = ANDROID_CPU_FAMILY_UNKNOWN;
static void init_cpu_features() {
    if (!cpu_features) {
        cpu_family = android_getCpuFamily();
        cpu_features = android_getCpuFeatures();
    }
}
// 与上游 pixman 语义对齐：SIMD 代表 ARMv6 SIMD，NEON 代表 ARMv7/ARM64 的 NEON/ASIMD
#define HAS_ARMV6() (cpu_family == ANDROID_CPU_FAMILY_ARM && (cpu_features & ANDROID_CPU_ARM_FEATURE_ARMv6))
#define HAS_ARMV7() (cpu_family == ANDROID_CPU_FAMILY_ARM && (cpu_features & ANDROID_CPU_ARM_FEATURE_ARMv7))
#define HAS_NEON() ((cpu_family == ANDROID_CPU_FAMILY_ARM  && (cpu_features & ANDROID_CPU_ARM_FEATURE_NEON)) || \
                    (cpu_family == ANDROID_CPU_FAMILY_ARM64 && (cpu_features & ANDROID_CPU_ARM64_FEATURE_ASIMD)))
#define HAS_SIMD() (HAS_ARMV6())
#else
#define init_cpu_features() do {} while (0)
#if defined(__aarch64__)
#define HAS_NEON() 1
#define HAS_SIMD() 0
#else
#define HAS_NEON() (__ARM_NEON)
#define HAS_SIMD() (__ARM_ARCH__ >= 6)
#endif
#endif

// ========== 辅助：像素/字节步长换算 ==========
static inline void neon_row_copy(const uint32_t* restrict src, uint32_t* restrict dst, int w) {
    init_cpu_features(); // 确保运行时特性已就绪
#if defined(__ARM_NEON) || defined(__aarch64__)
    if (HAS_NEON()) {
#if defined(__GNUC__)
        __builtin_prefetch(src, 0, 1);
        __builtin_prefetch(dst, 1, 1);
#endif
        int n = w;
        for (; n >= 4; n -= 4, src += 4, dst += 4) {
            uint32x4_t v = vld1q_u32(src);
            vst1q_u32(dst, v);
        }
        while (n--) *dst++ = *src++;
    } else {
        memcpy(dst, src, (size_t)w * 4);
    }
#else
    memcpy(dst, src, (size_t)w * 4);
#endif
}

static inline void neon_row_fill(uint32_t* restrict dst, int w, uint32_t color) {
    init_cpu_features(); // 确保运行时特性已就绪
#if defined(__ARM_NEON) || defined(__aarch64__)
    if (HAS_NEON()) {
#if defined(__GNUC__)
        __builtin_prefetch(dst, 1, 1);
#endif
        int n = w;
        uint32x4_t v = vdupq_n_u32(color);
        for (; n >= 4; n -= 4, dst += 4) vst1q_u32(dst, v);
        while (n--) *dst++ = color;
    } else {
        for (int i = 0; i < w; ++i) dst[i] = color;
    }
#else
    for (int i = 0; i < w; ++i) dst[i] = color;
#endif
}

// 使用固定点安全矩阵乘法（替代 NEON 版，避免 32x32 溢出）
static inline pixman_fixed_t fixed_mul(pixman_fixed_t a, pixman_fixed_t b)
{
    int64_t t = (int64_t)a * (int64_t)b;
    t += (t >= 0 ? 0x8000 : -0x8000); // 最近舍入
    return (pixman_fixed_t)(t >> 16);
}

static inline pixman_fixed_t fixed_shift_r16_round(int64_t v)
{
    v += (v >= 0 ? 0x8000 : -0x8000);
    return (pixman_fixed_t)(v >> 16);
}

static inline void fixed_matrix_multiply(pixman_fixed_t dst[3][3],
                                         const pixman_fixed_t a[3][3],
                                         const pixman_fixed_t b[3][3])
{
    // 与上游一致：使用 64 位中间结果并带最近舍入
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            int64_t s0 = (int64_t)a[i][0] * b[0][j];
            int64_t s1 = (int64_t)a[i][1] * b[1][j];
            int64_t s2 = (int64_t)a[i][2] * b[2][j];
            dst[i][j] = fixed_shift_r16_round(s0 + s1 + s2);
        }
    }
}

// ========== 安全裁剪（使用 NEON 加速向量比较，如果适用） ==========
static int clip_triple_rect(
    int* sx, int* sy, int sw, int sh,
    int* dx, int* dy, int dw, int dh,
    int* mx, int* my, int mw, int mh,
    int* w,  int* h)
{
    if (*w <= 0 || *h <= 0) return 0;

    // 裁剪到 D 边界
    if (*dx < 0) { int shf = -(*dx); *dx = 0; *sx += shf; if (mx) *mx += shf; *w -= shf; }
    if (*dy < 0) { int shf = -(*dy); *dy = 0; *sy += shf; if (my) *my += shf; *h -= shf; }
    if (*dx + *w > dw) *w = dw - *dx;
    if (*dy + *h > dh) *h = dh - *dy;
    if (*w <= 0 || *h <= 0) return 0;

    // 裁剪到 S 边界
    if (*sx < 0) { int shf = -(*sx); *sx = 0; *dx += shf; *w -= shf; }
    if (*sy < 0) { int shf = -(*sy); *sy = 0; *dy += shf; *h -= shf; }
    if (*sx + *w > sw) *w = sw - *sx;
    if (*sy + *h > sh) *h = sh - *sy;
    if (*w <= 0 || *h <= 0) return 0;

    // 裁剪到 M（如有）
    if (mx && my && mw > 0 && mh > 0) {
        if (*mx < 0) { int shf = -(*mx); *mx = 0; *dx += shf; *w -= shf; *sx += shf; }
        if (*my < 0) { int shf = -(*my); *my = 0; *dy += shf; *h -= shf; *sy += shf; }
        if (*mx + *w > mw) *w = mw - *mx;
        if (*my + *h > mh) *h = mh - *my;
        if (*w <= 0 || *h <= 0) return 0;
    }
    return 1;
}

/* ========== Region32 基础 API：与头文件对齐 ========== */
PIXMAN_EXPORT void
pixman_region32_init (pixman_region32_t *region)
{
    if (!region) return;
    region->extents.x1 = region->extents.y1 = 0;
    region->extents.x2 = region->extents.y2 = 0;
    region->data = NULL;
}

PIXMAN_EXPORT void
pixman_region32_init_rect (pixman_region32_t *region,
                           int x, int y, unsigned int width, unsigned int height)
{
    if (!region) return;
    region->extents.x1 = x;
    region->extents.y1 = y;
    region->extents.x2 = x + (int)width;
    region->extents.y2 = y + (int)height;
    region->data = NULL;
}

PIXMAN_EXPORT void
pixman_region32_fini (pixman_region32_t *region)
{
    if (!region) return;
    if (region->data) { free(region->data); region->data = NULL; }
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_contains_point (pixman_region32_t *region, int x, int y,
                                pixman_box32_t *box)
{
    if (!region) return FALSE;
    if (x >= region->extents.x1 && x < region->extents.x2 &&
        y >= region->extents.y1 && y < region->extents.y2)
    {
        if (box) *box = region->extents;
        return TRUE;
    }
    return FALSE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_not_empty (pixman_region32_t *region)
{
    if (!region) return FALSE;
    return (region->extents.x1 < region->extents.x2 &&
            region->extents.y1 < region->extents.y2) ? TRUE : FALSE;
}

PIXMAN_EXPORT void
pixman_region32_clear (pixman_region32_t *region)
{
    if (!region) return;
    region->extents.x1 = region->extents.y1 = 0;
    region->extents.x2 = region->extents.y2 = 0;
    if (region->data) { free(region->data); region->data = NULL; }
}

/* ========== Image 构造/销毁/属性：与头文件对齐并修正所有权 ========== */
PIXMAN_EXPORT pixman_image_t *
pixman_image_create_solid_fill (const pixman_color_t *color)
{
    if (!color) return NULL;
    pixman_image_t *img = malloc(sizeof(pixman_image_t));
    if (!img) return NULL;

    img->type = SOLID;
    img->solid.color = *color;
    img->solid.color_32 =
        (((uint32_t)color->alpha >> 8) << 24) |
        (((uint32_t)color->red   >> 8) << 16) |
        (((uint32_t)color->green     & 0xff00)) |
        (((uint32_t)color->blue  >> 8));
    img->solid.color_float[0] = (float)color->blue  / 65535.0f;
    img->solid.color_float[1] = (float)color->green / 65535.0f;
    img->solid.color_float[2] = (float)color->red   / 65535.0f;
    img->solid.color_float[3] = (float)color->alpha / 65535.0f;

    img->ref_count = 1;
    // 初始化 clip 空矩形
    img->common.clip_region.extents.x1 = img->common.clip_region.extents.y1 = 0;
    img->common.clip_region.extents.x2 = img->common.clip_region.extents.y2 = 0;
    img->common.clip_region.data = NULL;
    return img;
}

PIXMAN_EXPORT pixman_image_t *
pixman_image_create_bits (pixman_format_code_t format,
                          int width, int height,
                          uint32_t *bits, int stride)
{
    pixman_image_t *img;
    
    img = malloc (sizeof (pixman_image_t));
    if (!img)
        return NULL;
        
    img->type = BITS;
    img->bits.format = format;
    img->bits.width = width;
    img->bits.height = height;
    img->bits.bits = bits;
    img->bits.stride = stride;
    
    // Calculate bits per pixel
    int bpp = PIXMAN_FORMAT_BPP(format);

    if (!bits)
    {
        int byte_stride = stride ? stride : ((width * bpp + 31) >> 5) * 4;
        int size = height * byte_stride;

        void* aligned = NULL;
#if defined(_POSIX_VERSION)
        if (posix_memalign(&aligned, 16, (size_t)size) == 0)
            img->bits.bits = aligned;
        else
            img->bits.bits = malloc((size_t)size);
#else
        img->bits.bits = malloc((size_t)size);
#endif
        if (!img->bits.bits) { free(img); return NULL; }
        memset(img->bits.bits, 0, size);
        img->bits.own_data = 1;
        img->bits.stride   = byte_stride; // 字节步长
    }
    else
    {
        img->bits.own_data = 0;
        if (!img->bits.stride)
            img->bits.stride = ((width * bpp + 31) >> 5) * 4;
    }
    img->ref_count = 1;
    img->common.clip_region.extents.x1 = img->common.clip_region.extents.y1 = 0;
    img->common.clip_region.extents.x2 = img->common.clip_region.extents.y2 = 0;
    img->common.clip_region.data = NULL;
    return img;
}

PIXMAN_EXPORT pixman_image_t *
pixman_image_create_bits_no_clear (pixman_format_code_t format,
                                   int width, int height,
                                   uint32_t *bits, int stride)
{
    pixman_image_t *img;
    int bpp;
    
    img = malloc (sizeof (pixman_image_t));
    if (!img)
        return NULL;
        
    img->type = BITS;
    img->bits.format = format;
    img->bits.width = width;
    img->bits.height = height;
    img->bits.bits = bits;
    img->bits.stride = stride;
    
    // Calculate bits per pixel
    bpp = PIXMAN_FORMAT_BPP(format);

    if (!bits)
    {
        int byte_stride = stride ? stride : ((width * bpp + 31) >> 5) * 4;
        int size = height * byte_stride;

        void* aligned = NULL;
#if defined(_POSIX_VERSION)
        if (posix_memalign(&aligned, 16, (size_t)size) == 0)
            img->bits.bits = aligned;
        else
            img->bits.bits = malloc((size_t)size);
#else
        img->bits.bits = malloc((size_t)size);
#endif
        if (!img->bits.bits) { free(img); return NULL; }
        img->bits.own_data = 1;
        img->bits.stride   = byte_stride; // 字节步长
    }
    else
    {
        img->bits.own_data = 0;
        if (!img->bits.stride)
            img->bits.stride = ((width * bpp + 31) >> 5) * 4;
    }
    img->ref_count = 1;
    img->common.clip_region.extents.x1 = img->common.clip_region.extents.y1 = 0;
    img->common.clip_region.extents.x2 = img->common.clip_region.extents.y2 = 0;
    img->common.clip_region.data = NULL;
    return img;
}

PIXMAN_EXPORT pixman_image_t *
pixman_image_ref (pixman_image_t *image)
{
    if (image)
        image->ref_count++;
    return image;
}

PIXMAN_EXPORT pixman_bool_t
pixman_image_unref (pixman_image_t *image)
{
    if (!image) return FALSE;
    image->ref_count--;
    if (image->ref_count <= 0)
    {
        if (image->type == BITS && image->bits.own_data && image->bits.bits)
            free(image->bits.bits);
        free(image);
        return TRUE;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_image_set_clip_region32 (pixman_image_t *image, pixman_region32_t *region)
{
    if (!image || !region) return FALSE;
    image->common.clip_region = *region; // 存储裁剪外包矩形
    return TRUE;
}

// ========== Transform API：以头文件声明为准 ========== //
PIXMAN_EXPORT void
pixman_transform_init_identity (pixman_transform_t *matrix)
{
    if (!matrix) return;
    memset(matrix, 0, sizeof(*matrix));
    matrix->matrix[0][0] = pixman_int_to_fixed(1);
    matrix->matrix[1][1] = pixman_int_to_fixed(1);
    matrix->matrix[2][2] = pixman_int_to_fixed(1);
}

// 新增：按头文件声明实现点变换
PIXMAN_EXPORT pixman_bool_t
pixman_transform_point (const pixman_transform_t *t,
                        const pixman_vector_t *v,
                        pixman_vector_t *out)
{
    if (!t || !v || !out) return FALSE;
    for (int i = 0; i < 3; ++i) {
        int64_t s0 = (int64_t)t->matrix[i][0] * v->vector[0];
        int64_t s1 = (int64_t)t->matrix[i][1] * v->vector[1];
        int64_t s2 = (int64_t)t->matrix[i][2] * v->vector[2];
        out->vector[i] = fixed_shift_r16_round(s0 + s1 + s2);
    }
    return TRUE;
}

// 改签名：按头文件(dst, src, sx, sy)
PIXMAN_EXPORT pixman_bool_t
pixman_transform_scale (pixman_transform_t *dst,
                        const pixman_transform_t *src,
                        pixman_fixed_t sx, pixman_fixed_t sy)
{
    if (!dst || !sx || !sy) return FALSE;
    pixman_transform_t s = {{0}};
    s.matrix[0][0] = sx;
    s.matrix[1][1] = sy;
    s.matrix[2][2] = pixman_int_to_fixed(1);
    if (src)
        fixed_matrix_multiply(dst->matrix, src->matrix, s.matrix);
    else
        *dst = s;
    return TRUE;
}

// 改签名：按头文件(dst, src, cos, sin)
PIXMAN_EXPORT pixman_bool_t
pixman_transform_rotate (pixman_transform_t *dst,
                         const pixman_transform_t *src,
                         pixman_fixed_t c, pixman_fixed_t s)
{
    if (!dst) return FALSE;
    pixman_transform_t r = {{0}};
    r.matrix[0][0] = c;  r.matrix[0][1] = -s;
    r.matrix[1][0] = s;  r.matrix[1][1] = c;
    r.matrix[2][2] = pixman_int_to_fixed(1);
    if (src)
        fixed_matrix_multiply(dst->matrix, src->matrix, r.matrix);
    else
        *dst = r;
    return TRUE;
}

// 改签名：按头文件(dst, src, tx, ty)
PIXMAN_EXPORT pixman_bool_t
pixman_transform_translate (pixman_transform_t *dst,
                            const pixman_transform_t *src,
                            pixman_fixed_t tx, pixman_fixed_t ty)
{
    if (!dst) return FALSE;
    pixman_transform_t tr = {{0}};
    tr.matrix[0][0] = tr.matrix[1][1] = tr.matrix[2][2] = pixman_int_to_fixed(1);
    tr.matrix[0][2] = tx;
    tr.matrix[1][2] = ty;
    if (src)
        fixed_matrix_multiply(dst->matrix, src->matrix, tr.matrix);
    else
        *dst = tr;
    return TRUE;
}

// 其余 transform_* 已在文件中，保持不变（invert / is_*）

/* ========== Region16 API：最简 extents 实现 ========== */
PIXMAN_EXPORT void
pixman_region_init (pixman_region16_t *region)
{
    if (!region) return;
    region->extents.x1 = region->extents.y1 = 0;
    region->extents.x2 = region->extents.y2 = 0;
    region->data = NULL;
}

PIXMAN_EXPORT void
pixman_region_init_rect (pixman_region16_t *region, int x, int y,
                         unsigned int width, unsigned int height)
{
    if (!region) return;
    region->extents.x1 = (int16_t)x;
    region->extents.y1 = (int16_t)y;
    region->extents.x2 = (int16_t)(x + (int)width);
    region->extents.y2 = (int16_t)(y + (int)height);
    region->data = NULL;
}

PIXMAN_EXPORT void
pixman_region_fini (pixman_region16_t *region)
{
    if (!region) return;
    if (region->data) { free(region->data); region->data = NULL; }
}

PIXMAN_EXPORT void
pixman_region_translate (pixman_region16_t *region, int x, int y)
{
    if (!region) return;
    region->extents.x1 += (int16_t)x; region->extents.x2 += (int16_t)x;
    region->extents.y1 += (int16_t)y; region->extents.y2 += (int16_t)y;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_copy (pixman_region16_t *dest, const pixman_region16_t *src)
{
    if (!dest || !src) return FALSE;
    *dest = *src;
    if (dest->data) dest->data = NULL;
    return TRUE;
}

static inline pixman_box16_t region16_intersect_box(pixman_box16_t a, pixman_box16_t b)
{
    pixman_box16_t r;
    r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
    r.x2 = a.x2 < b.x2 ? a.x2 : b.x2;
    r.y2 = a.y2 < b.y2 ? a.y2 : b.y2;
    if (r.x2 < r.x1 || r.y2 < r.y1) { r.x1=r.y1=r.x2=r.y2=0; }
    return r;
}

static inline pixman_box16_t region16_union_box(pixman_box16_t a, pixman_box16_t b)
{
    pixman_box16_t r;
    r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    r.x2 = a.x2 > b.x2 ? a.x2 : b.x2;
    r.y2 = a.y2 > b.y2 ? a.y2 : b.y2;
    return r;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_intersect (pixman_region16_t *dst,
                         const pixman_region16_t *r1,
                         const pixman_region16_t *r2)
{
    if (!dst || !r1 || !r2) return FALSE;
    dst->extents = region16_intersect_box(r1->extents, r2->extents);
    dst->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_union (pixman_region16_t *dst,
                     const pixman_region16_t *r1,
                     const pixman_region16_t *r2)
{
    if (!dst || !r1 || !r2) return FALSE;
    dst->extents = region16_union_box(r1->extents, r2->extents);
    dst->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_subtract (pixman_region16_t *reg_d,
                        const pixman_region16_t *reg_m,
                        const pixman_region16_t *reg_s)
{
    if (!reg_d || !reg_m || !reg_s) return FALSE;
    // 简化策略：若相交则置空；否则为 reg_m
    pixman_region16_t tmp;
    pixman_region_intersect(&tmp, reg_m, reg_s);
    if (tmp.extents.x1 < tmp.extents.x2 && tmp.extents.y1 < tmp.extents.y2)
        pixman_region_clear(reg_d);
    else
        pixman_region_copy(reg_d, reg_m);
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_contains_point (const pixman_region16_t *region, int x, int y, pixman_box16_t *box)
{
    if (!region) return FALSE;
    if (x >= region->extents.x1 && x < region->extents.x2 &&
        y >= region->extents.y1 && y < region->extents.y2)
    {
        if (box) *box = region->extents;
        return TRUE;
    }
    return FALSE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_not_empty (const pixman_region16_t *region)
{
    if (!region) return FALSE;
    return (region->extents.x1 < region->extents.x2 &&
            region->extents.y1 < region->extents.y2) ? TRUE : FALSE;
}

PIXMAN_EXPORT pixman_box16_t *
pixman_region_extents (const pixman_region16_t *region)
{
    // 非 const 返回按头文件，直接丢弃 const 限定
    return (pixman_box16_t *)&region->extents;
}

PIXMAN_EXPORT int
pixman_region_n_rects (const pixman_region16_t *region)
{
    return pixman_region_not_empty(region) ? 1 : 0;
}

PIXMAN_EXPORT pixman_box16_t *
pixman_region_rectangles (const pixman_region16_t *region, int *n_rects)
{
    if (n_rects) *n_rects = pixman_region_n_rects(region);
    return (pixman_box16_t *)&region->extents;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_equal (const pixman_region16_t *r1, const pixman_region16_t *r2)
{
    if (!r1 || !r2) return FALSE;
    return memcmp(&r1->extents, &r2->extents, sizeof(r1->extents)) == 0;
}

PIXMAN_EXPORT void
pixman_region_reset (pixman_region16_t *region, const pixman_box16_t *box)
{
    if (!region || !box) return;
    region->extents = *box;
    region->data = NULL;
}

PIXMAN_EXPORT void
pixman_region_clear (pixman_region16_t *region)
{
    if (!region) return;
    region->extents.x1 = region->extents.y1 = 0;
    region->extents.x2 = region->extents.y2 = 0;
    if (region->data) { free(region->data); region->data = NULL; }
}

/* ========== Region32 API：补齐头文件声明 ========== */
PIXMAN_EXPORT void
pixman_region32_translate (pixman_region32_t *region, int x, int y)
{
    if (!region) return;
    region->extents.x1 += x; region->extents.x2 += x;
    region->extents.y1 += y; region->extents.y2 += y;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_copy (pixman_region32_t *dest, const pixman_region32_t *source)
{
    if (!dest || !source) return FALSE;
    *dest = *source;
    if (dest->data) dest->data = NULL;
    return TRUE;
}

static inline pixman_box32_t region32_intersect_box(pixman_box32_t a, pixman_box32_t b)
{
    pixman_box32_t r;
    r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
    r.x2 = a.x2 < b.x2 ? a.x2 : b.x2;
    r.y2 = a.y2 < b.y2 ? a.y2 : b.y2;
    if (r.x2 < r.x1 || r.y2 < r.y1) { r.x1=r.y1=r.x2=r.y2=0; }
    return r;
}

static inline pixman_box32_t region32_union_box(pixman_box32_t a, pixman_box32_t b)
{
    pixman_box32_t r;
    r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    r.x2 = a.x2 > b.x2 ? a.x2 : b.x2;
    r.y2 = a.y2 > b.y2 ? a.y2 : b.y2;
    return r;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_intersect (pixman_region32_t *dst,
                           const pixman_region32_t *r1,
                           const pixman_region32_t *r2)
{
    if (!dst || !r1 || !r2) return FALSE;
    dst->extents = region32_intersect_box(r1->extents, r2->extents);
    dst->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_union (pixman_region32_t *dst,
                       const pixman_region32_t *r1,
                       const pixman_region32_t *r2)
{
    if (!dst || !r1 || !r2) return FALSE;
    dst->extents = region32_union_box(r1->extents, r2->extents);
    dst->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_intersect_rect (pixman_region32_t *dest,
                                const pixman_region32_t *source,
                                int x, int y, unsigned int w, unsigned int h)
{
    if (!dest || !source) return FALSE;
    pixman_box32_t box = { x, y, x + (int)w, y + (int)h };
    dest->extents = region32_intersect_box(source->extents, box);
    dest->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_union_rect (pixman_region32_t *dest,
                            const pixman_region32_t *source,
                            int x, int y, unsigned int w, unsigned int h)
{
    if (!dest || !source) return FALSE;
    pixman_box32_t box = { x, y, x + (int)w, y + (int)h };
    dest->extents = region32_union_box(source->extents, box);
    dest->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_subtract (pixman_region32_t *reg_d,
                          const pixman_region32_t *reg_m,
                          const pixman_region32_t *reg_s)
{
    if (!reg_d || !reg_m || !reg_s) return FALSE;
    pixman_region32_t tmp;
    pixman_region32_intersect(&tmp, reg_m, reg_s);
    if (tmp.extents.x1 < tmp.extents.x2 && tmp.extents.y1 < tmp.extents.y2)
        pixman_region32_clear(reg_d);
    else
        pixman_region32_copy(reg_d, reg_m);
    return TRUE;
}

PIXMAN_EXPORT pixman_box32_t *
pixman_region32_extents (const pixman_region32_t *region)
{
    return (pixman_box32_t *)&region->extents;
}

PIXMAN_EXPORT int
pixman_region32_n_rects (const pixman_region32_t *region)
{
    return pixman_region32_not_empty((pixman_region32_t *)region) ? 1 : 0;
}

PIXMAN_EXPORT pixman_box32_t *
pixman_region32_rectangles (const pixman_region32_t *region, int *n_rects)
{
    if (n_rects) *n_rects = pixman_region32_n_rects(region);
    return (pixman_box32_t *)&region->extents;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_equal (const pixman_region32_t *r1, const pixman_region32_t *r2)
{
    if (!r1 || !r2) return FALSE;
    return memcmp(&r1->extents, &r2->extents, sizeof(r1->extents)) == 0;
}

PIXMAN_EXPORT void
pixman_region32_reset (pixman_region32_t *region, const pixman_box32_t *box)
{
    if (!region || !box) return;
    region->extents = *box;
    region->data = NULL;
}

/* ========== Image API：补齐缺失 ========== */

// 头文件中声明了 16-bit 版本
PIXMAN_EXPORT pixman_bool_t
pixman_image_set_clip_region (pixman_image_t *image, pixman_region16_t *region16)
{
    if (!image || !region16) return FALSE;
    // 将 16-bit extents 简单提升为 32-bit 存入 common.clip_region
    pixman_region32_t r32;
    r32.extents.x1 = region16->extents.x1;
    r32.extents.y1 = region16->extents.y1;
    r32.extents.x2 = region16->extents.x2;
    r32.extents.y2 = region16->extents.y2;
    r32.data = NULL;
    image->common.clip_region = r32;
    return TRUE;
}

// 头文件要求存在，但此实现为 no-op，占位以保持 ABI
PIXMAN_EXPORT pixman_bool_t
pixman_image_set_transform (pixman_image_t *image, const pixman_transform_t *transform)
{
    if (!image) return FALSE;
    // 若内部有 common.transform，可在此复制；未知结构时作为 no-op 返回 TRUE
    (void)transform;
    return TRUE;
}

PIXMAN_EXPORT uint32_t *
pixman_image_get_data (pixman_image_t *image)
{
    if (!image) return NULL;
    if (image->type == BITS) return image->bits.bits;
    return NULL;
}

PIXMAN_EXPORT int
pixman_image_get_width (pixman_image_t *image)
{
    if (!image) return 0;
    if (image->type == BITS) return image->bits.width;
    return 1; // SOLID 视为 1x1
}

PIXMAN_EXPORT int
pixman_image_get_height (pixman_image_t *image)
{
    if (!image) return 0;
    if (image->type == BITS) return image->bits.height;
    return 1; // SOLID 视为 1x1
}

PIXMAN_EXPORT pixman_format_code_t
pixman_image_get_format (pixman_image_t *image)
{
    if (!image) return 0;
    if (image->type == BITS) return image->bits.format;
    return 0;
}

/* ========== Composite / Fill / Version ========== */

PIXMAN_EXPORT void
pixman_image_composite32 (pixman_op_t op,
                          pixman_image_t *src,
                          pixman_image_t *mask,
                          pixman_image_t *dst,
                          int32_t src_x, int32_t src_y,
                          int32_t mask_x, int32_t mask_y,
                          int32_t dst_x, int32_t dst_y,
                          int32_t width, int32_t height)
{
    (void)mask; (void)mask_x; (void)mask_y;
    if (!src || !dst || width <= 0 || height <= 0) return;
    if (op != PIXMAN_OP_SRC) {
        // 仅实现常用 SRC，其他可按需扩展
        return;
    }
    if (src->type != BITS || dst->type != BITS) return;

    uint32_t *sbits = src->bits.bits;
    uint32_t *dbits = dst->bits.bits;
    int sstride = src->bits.stride; // bytes
    int dstride = dst->bits.stride; // bytes

    const uint32_t *srow = (const uint32_t *)((const uint8_t *)sbits + (size_t)(src_y) * sstride) + src_x;
    uint32_t *drow = (uint32_t *)((uint8_t *)dbits + (size_t)(dst_y) * dstride) + dst_x;

    for (int y = 0; y < height; ++y) {
        neon_row_copy(srow, drow, width);
        srow = (const uint32_t *)((const uint8_t *)srow + sstride);
        drow = (uint32_t *)((uint8_t *)drow + dstride);
    }
}

PIXMAN_EXPORT pixman_bool_t
pixman_fill (uint32_t *bits, int stride, int bpp,
             int x, int y, int width, int height, uint32_t _xor)
{
    if (!bits || width <= 0 || height <= 0) return FALSE;
    uint8_t *row = (uint8_t *)bits + (size_t)y * stride + (size_t)x * (bpp / 8);
    if (bpp == 32) {
        for (int j = 0; j < height; ++j) {
            neon_row_fill((uint32_t *)row, width, _xor);
            row += stride;
        }
        return TRUE;
    } else if (bpp == 16) {
        uint16_t val = (uint16_t)_xor;
        for (int j = 0; j < height; ++j) {
            uint16_t *p = (uint16_t *)row;
            for (int i = 0; i < width; ++i) p[i] = val;
            row += stride;
        }
        return TRUE;
    }
    // 其它 bpp 简化为逐字节填充
    for (int j = 0; j < height; ++j) {
        memset(row, (int)(_xor & 0xFF), (size_t)((width * bpp) / 8));
        row += stride;
    }
    return TRUE;
}

PIXMAN_EXPORT int
pixman_version (void)
{
    return PIXMAN_VERSION;
}

PIXMAN_EXPORT const char *
pixman_version_string (void)
{
    return PIXMAN_VERSION_STRING;
}

/* ========== 清理：移除头文件未声明的多余导出函数 ========== */
/* 请删除此前的 PIXMAN_EXPORT pixman_bool_t pixman_image_set_filter(...) 实现。 */

// ================== 添加 pixman_arm_get_implementations 函数（与上游一致，动态选择 ARM 实现） ==================
PIXMAN_EXPORT pixman_implementation_t *
pixman_arm_get_implementations (pixman_implementation_t *imp)
{
    init_cpu_features();
#if defined(USE_ARM_SIMD)
    if (HAS_SIMD())
    {
        imp = pixman_implementation_create_armv6 (imp);
        imp = pixman_implementation_create_arm_simd (imp);
    }
#endif
#if defined(USE_ARM_NEON)
    if (HAS_NEON())
        imp = pixman_implementation_create_arm_neon (imp);
#endif
    return imp;
}
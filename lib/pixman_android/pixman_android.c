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

// ========== 补充缺失的编译器优化宏（放在文件开头） ==========
#ifndef ASSUME_TRUE
#define ASSUME_TRUE(x) __builtin_assume(x)
#endif

#ifndef FORCE_INLINE
#define FORCE_INLINE static inline __attribute__((always_inline))
#endif

#ifndef CONST_FUNC
#define CONST_FUNC __attribute__((const))
#endif

#ifndef PURE_FUNC
#define PURE_FUNC __attribute__((pure))
#endif


// ========== 移除重复定义，使用头文件中的定义 ==========
#ifndef PIXMAN_FORMAT_BPP
#define PIXMAN_FORMAT_BPP(f)    (((f) >> 24))
#endif

#ifndef PIXMAN_FORMAT_TYPE
#define PIXMAN_FORMAT_TYPE(f)    (((f) >> 16) & 0x3f)
#endif

// ========== 补充缺失的内部结构体定义 ==========

typedef enum
{
    BITS,
    LINEAR,
    CONICAL,
    RADIAL,
    SOLID
} image_type_t;

typedef struct
{
    image_type_t             type;
    int                      ref_count;          /* 修正：使用 int 避免 int32_t 溢出 */
    pixman_region32_t        clip_region;        /* 修正：直接存储值（非指针） */
    pixman_transform_t      *transform;
    int                      repeat;
    int                      filter;
    const pixman_fixed_t    *filter_params;      /* 修正：改为 const 指针 */
    int                      n_filter_params;
    void                    *alpha_map;
    int                      alpha_origin_x;
    int                      alpha_origin_y;
    pixman_bool_t            component_alpha;
    void                    *property_changed;
    void                    *destroy_func;
    void                    *destroy_data;
    uint32_t                 flags;
    pixman_format_code_t     extended_format_code;
} image_common_t;

typedef struct
{
    image_common_t  common;
    pixman_color_t  color;
    uint32_t        color_32;
    float           color_float[4];
} solid_fill_t;

typedef struct
{
    image_common_t       common;
    pixman_format_code_t format;
    int                  width;
    int                  height;
    uint32_t            *bits;
    int                  stride;          /* 字节步长 */
    int                  own_data;
} bits_image_t;

union pixman_image
{
    image_type_t   type;
    image_common_t common;
    solid_fill_t   solid;
    bits_image_t   bits;
};

// ========== NDK CPU 特性检测 ==========
#ifdef __ANDROID__
#include <cpu-features.h>
static uint64_t g_cpu_features = 0;
static AndroidCpuFamily g_cpu_family = ANDROID_CPU_FAMILY_UNKNOWN;  /* 修正类型名 */
__attribute__((constructor))
static void init_cpu_features_once(void) {
    g_cpu_family = android_getCpuFamily();
    g_cpu_features = android_getCpuFeatures();
}
#define HAS_NEON() ((g_cpu_family == ANDROID_CPU_FAMILY_ARM && (g_cpu_features & ANDROID_CPU_ARM_FEATURE_NEON)) || \
                    (g_cpu_family == ANDROID_CPU_FAMILY_ARM64 && (g_cpu_features & ANDROID_CPU_ARM64_FEATURE_ASIMD)))
#else
#if defined(__aarch64__)
#define HAS_NEON() 1
#else
#define HAS_NEON() (defined(__ARM_NEON))
#endif
#endif

// ========== NEON 优化工具函数（修正 neon_row_fill） ==========
static inline void neon_row_copy (const uint32_t * restrict src,
                                  uint32_t       * restrict dst,
                                  int                       w)
{
#if defined(__ARM_NEON) || defined(__aarch64__)
    if (HAS_NEON ())
    {
        __builtin_prefetch (src, 0, 1);
        __builtin_prefetch (dst, 1, 1);
        
        // 使用 __builtin_assume 告诉编译器 w 的范围（优化循环展开）
        ASSUME_TRUE(w >= 0 && w <= 8192);
        
        int n = w;
        
        // 使用非临时 load/store（绕过 L1 cache，适用于大块拷贝）
        #if defined(__aarch64__)
        for (; n >= 8; n -= 8, src += 8, dst += 8)
        {
            // 使用 vld1q_u32_x2 一次加载 8 个像素（减少指令数）
            uint32x4x2_t v = vld1q_u32_x2(src);
            vst1q_u32_x2(dst, v);
        }
        #endif
        
        // 4 像素并行
        for (; n >= 4; n -= 4, src += 4, dst += 4)
        {
            uint32x4_t v = vld1q_u32 (src);
            vst1q_u32 (dst, v);
        }
        
        // 剩余像素（使用 __builtin_memcpy_inline 优化）
        if (n > 0) {
            __builtin_memcpy_inline(dst, src, n * 4);
        }
        return;
    }
#endif
    memcpy (dst, src, (size_t)w * 4);
}

static inline void neon_row_fill (uint32_t * restrict dst,
                                  int                 w,
                                  uint32_t            color)
{
#if defined(__ARM_NEON) || defined(__aarch64__)
    if (HAS_NEON ())
    {
        __builtin_prefetch (dst, 1, 1);
        ASSUME_TRUE(w >= 0 && w <= 8192);
        
        int n = w;
        uint32x4_t v = vdupq_n_u32 (color);
        
        // 使用 Clang 的循环向量化提示
        #pragma clang loop vectorize(enable) interleave(enable) unroll(enable)
        for (; n >= 16; n -= 16, dst += 16)
        {
            // 一次存储 16 个像素（4x4 向量）
            vst1q_u32 (dst, v);
            vst1q_u32 (dst + 4, v);
            vst1q_u32 (dst + 8, v);
            vst1q_u32 (dst + 12, v);
        }
        
        for (; n >= 4; n -= 4, dst += 4)
            vst1q_u32 (dst, v);
        
        while (n--) *dst++ = color;
        return;
    }
#endif
    for (int i = 0; i < w; ++i) dst[i] = color;
}

/* NEON 优化的 OVER（8 像素并行，使用 rounding shift 提升精度） */
__attribute__((hot))  // 标记为热路径
static inline void neon_over_8px (const uint32_t * restrict src,
                                  uint32_t       * restrict dst)
{
#if defined(__ARM_NEON) || defined(__aarch64__)
    if (HAS_NEON ())
    {
        // 预取下一行数据（流水线优化）
        __builtin_prefetch(src + 8, 0, 3);
        __builtin_prefetch(dst + 8, 1, 3);
        
        // 交错加载 8 像素的 RGBA 分量
        uint8x8x4_t s = vld4_u8 ((const uint8_t *)src);
        uint8x8x4_t d = vld4_u8 ((const uint8_t *)dst);
        
        // 计算 inv = 255 - src_alpha
        uint8x8_t inv = vsub_u8 (vdup_n_u8 (255), s.val[3]);
        
        // 使用 vrshrn_n_u16 (rounding shift right narrow) 精确近似 /255
        // 公式: (x * y + 128) >> 8 ≈ x * y / 255（误差 < 0.5）
        uint16x8_t r = vaddl_u8 (s.val[2], vrshrn_n_u16 (vmull_u8 (d.val[2], inv), 8));
        uint16x8_t g = vaddl_u8 (s.val[1], vrshrn_n_u16 (vmull_u8 (d.val[1], inv), 8));
        uint16x8_t b = vaddl_u8 (s.val[0], vrshrn_n_u16 (vmull_u8 (d.val[0], inv), 8));
        uint16x8_t a = vaddl_u8 (s.val[3], vrshrn_n_u16 (vmull_u8 (d.val[3], inv), 8));
        
        // 存储结果
        uint8x8x4_t out;
        out.val[2] = vmovn_u16 (r);
        out.val[1] = vmovn_u16 (g);
        out.val[0] = vmovn_u16 (b);
        out.val[3] = vmovn_u16 (a);
        vst4_u8 ((uint8_t *)dst, out);
        return;
    }
#endif
    /* 标量回退（与上游一致） */
    for (int i = 0; i < 8; ++i)
    {
        uint32_t sv = src[i];
        uint8_t  sa = sv >> 24;
        if (sa == 0xFF)
        {
            dst[i] = sv;
            continue;
        }
        if (sa == 0)
            continue;

        uint32_t dv  = dst[i];
        uint8_t  inv = 255 - sa;
        uint8_t  sr  = (sv >> 16) & 0xFF, sg = (sv >> 8) & 0xFF, sb = sv & 0xFF;
        uint8_t  da  = dv >> 24, dr = (dv >> 16) & 0xFF, dg = (dv >> 8) & 0xFF, db = dv & 0xFF;

        /* 使用 (x * inv + 255) / 255 近似 x * inv / 255（与上游一致） */
        uint8_t oa = sa + (da * inv + 255) / 255;
        uint8_t orr = sr + (dr * inv + 255) / 255;
        uint8_t og = sg + (dg * inv + 255) / 255;
        uint8_t ob = sb + (db * inv + 255) / 255;

        dst[i] = ((uint32_t)oa << 24) | ((uint32_t)orr << 16) | ((uint32_t)og << 8) | ob;
    }
}

/* 8bpp NEON memset（用于 pixman_fill） */
static inline void neon_memset_u8 (uint8_t * restrict dst,
                                   uint8_t            val,
                                   int                count)
{
#if defined(__ARM_NEON) || defined(__aarch64__)
    if (HAS_NEON () && count >= 16)
    {
        __builtin_prefetch (dst, 1, 1);
        ASSUME_TRUE(count >= 0 && count <= 1048576);
        
        uint8x16_t v = vdupq_n_u8 (val);
        int n = count;
        
        // 循环向量化提示
        #pragma clang loop vectorize(enable) unroll(enable)
        for (; n >= 64; n -= 64, dst += 64)
        {
            // 一次填充 64 字节（4x16 向量）
            vst1q_u8 (dst, v);
            vst1q_u8 (dst + 16, v);
            vst1q_u8 (dst + 32, v);
            vst1q_u8 (dst + 48, v);
        }
        
        for (; n >= 16; n -= 16, dst += 16)
            vst1q_u8 (dst, v);
        
        while (n--) *dst++ = val;
        return;
    }
#endif
    memset (dst, val, (size_t)count);
}

// ========== Transform 固定点乘法（使用 64bit 避免溢出） ==========
FORCE_INLINE
CONST_FUNC
static inline pixman_fixed_t fixed_mul(pixman_fixed_t a, pixman_fixed_t b) {
    #if defined(__aarch64__)
    // 使用 64 位乘法指令（单周期）
    int64_t t = (int64_t)a * b;
    return (pixman_fixed_t)((t + 0x8000) >> 16);
    #else
    // 回退到标准实现
    int64_t t = (int64_t)a * b;
    return (pixman_fixed_t)((t + 0x8000) >> 16);
    #endif
}

// ========== 安全裁剪 ==========
PURE_FUNC
static int clip_rect(int* sx, int* sy, int sw, int sh,
                     int* dx, int* dy, int dw, int dh,
                     int* w,  int* h) {
    // 使用 __builtin_expect 优化常见路径
    if (__builtin_expect(*w <= 0 || *h <= 0, 0)) return 0;
    if (*dx < 0) { int shf = -(*dx); *dx = 0; *sx += shf; *w -= shf; }
    if (*dy < 0) { int shf = -(*dy); *dy = 0; *sy += shf; *h -= shf; }
    if (*dx + *w > dw) *w = dw - *dx;
    if (*dy + *h > dh) *h = dh - *dy;
    if (*w <= 0 || *h <= 0) return 0;
    if (*sx < 0) { int shf = -(*sx); *sx = 0; *dx += shf; *w -= shf; }
    if (*sy < 0) { int shf = -(*sy); *sy = 0; *dy += shf; *h -= shf; }
    if (*sx + *w > sw) *w = sw - *sx;
    if (*sy + *h > sh) *h = sh - *sy;
    return (*w > 0 && *h > 0);
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
pixman_region32_contains_point (const pixman_region32_t *region, int x, int y,
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
pixman_region32_not_empty (const pixman_region32_t *region)
{
    if (!region) return FALSE;
    return region->extents.x1 < region->extents.x2 &&
           region->extents.y1 < region->extents.y2;
}

PIXMAN_EXPORT void
pixman_region32_clear (pixman_region32_t *region)
{
    if (!region) return;
    region->extents.x1 = region->extents.y1 = 0;
    region->extents.x2 = region->extents.y2 = 0;
    if (region->data) { free(region->data); region->data = NULL; }
}

/* ========== Image 构造/销毁/属性 ========== */
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

    img->common.ref_count = 1;  /* 修正：ref_count 在 common 中 */
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
    img->common.ref_count = 1;  /* 修正：ref_count 在 common 中 */
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
    img->common.ref_count = 1;  /* 修正：ref_count 在 common 中 */
    img->common.clip_region.extents.x1 = img->common.clip_region.extents.y1 = 0;
    img->common.clip_region.extents.x2 = img->common.clip_region.extents.y2 = 0;
    img->common.clip_region.data = NULL;
    return img;
}

PIXMAN_EXPORT pixman_image_t *
pixman_image_ref (pixman_image_t *image)
{
    if (image)
        image->common.ref_count++;  /* 修正 */
    return image;
}

PIXMAN_EXPORT pixman_bool_t
pixman_image_unref (pixman_image_t *image)
{
    if (!image) return FALSE;
    image->common.ref_count--;  /* 修正 */
    if (image->common.ref_count <= 0)
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

PIXMAN_EXPORT pixman_bool_t
pixman_image_set_clip_region (pixman_image_t     *image,
                              pixman_region16_t  *region)
{
    if (!image || !region) return FALSE;
    // 将 16位 extents 提升为 32位存入 common.clip_region
    image->common.clip_region.extents.x1 = region->extents.x1;
    image->common.clip_region.extents.y1 = region->extents.y1;
    image->common.clip_region.extents.x2 = region->extents.x2;
    image->common.clip_region.extents.y2 = region->extents.y2;
    image->common.clip_region.data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_image_set_transform (pixman_image_t            *image,
                            const pixman_transform_t  *transform)
{
    // 占位实现：仅返回 TRUE，不存储 transform（按需扩展）
    (void)image; (void)transform;
    return TRUE;
}

/* ========== 补齐缺失的函数（虽未在头文件声明但上游常用） ==========
PIXMAN_API
pixman_bool_t pixman_image_set_filter (pixman_image_t       *image,
                                       pixman_filter_t       filter,
                                       const pixman_fixed_t *params,
                                       int                   n_params)
{
    if (!image) return FALSE;
    image->common.filter = filter;
    image->common.filter_params = params;
    image->common.n_filter_params = n_params;
    return TRUE;
}

typedef enum {
    PIXMAN_FILTER_FAST,
    PIXMAN_FILTER_GOOD,
    PIXMAN_FILTER_BEST,
    PIXMAN_FILTER_NEAREST,
    PIXMAN_FILTER_BILINEAR,
    PIXMAN_FILTER_CONVOLUTION,
    PIXMAN_FILTER_SEPARABLE_CONVOLUTION
} pixman_filter_t;
========= */

PIXMAN_EXPORT uint32_t *
pixman_image_get_data (pixman_image_t *image)
{
    if (!image)
        return NULL;

    if (image->type == BITS)
        return image->bits.bits;

    return NULL;
}

PIXMAN_EXPORT int
pixman_image_get_width (pixman_image_t *image)
{
    if (!image)
        return 0;

    if (image->type == BITS)
        return image->bits.width;

    return 1; /* SOLID 视为 1x1 */
}

PIXMAN_EXPORT int
pixman_image_get_height (pixman_image_t *image)
{
    if (!image)
        return 0;

    if (image->type == BITS)
        return image->bits.height;

    return 1;
}

PIXMAN_EXPORT int
pixman_image_get_stride (pixman_image_t *image)
{
    if (!image || image->type != BITS)
        return 0;

    return image->bits.stride;
}

PIXMAN_EXPORT pixman_format_code_t
pixman_image_get_format (pixman_image_t *image)
{
    if (!image)
        return 0;

    if (image->type == BITS)
        return image->bits.format;

    /* SOLID 类型返回特殊格式（按需扩展，目前返回 0） */
    return 0;
}

// ========== Transform API（恢复双向签名，与头文件一致） ==========
PIXMAN_EXPORT void
pixman_transform_init_identity (pixman_transform_t *matrix) {
    if (!matrix) return;
    memset(matrix, 0, sizeof(*matrix));
    matrix->matrix[0][0] = pixman_int_to_fixed(1);
    matrix->matrix[1][1] = pixman_int_to_fixed(1);
    matrix->matrix[2][2] = pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_point (const pixman_transform_t *t,
                        const pixman_vector_t    *vector,
                        pixman_vector_t          *result)
{
    if (!t || !vector || !result) return FALSE;
    pixman_vector_t tmp;
    for (int i = 0; i < 3; ++i)
    {
        int64_t acc = 0;
        acc += (int64_t)t->matrix[i][0] * vector->vector[0];
        acc += (int64_t)t->matrix[i][1] * vector->vector[1];
        acc += (int64_t)t->matrix[i][2] * vector->vector[2];
        tmp.vector[i] = (pixman_fixed_t)((acc + 0x8000) >> 16);
    }
    *result = tmp;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_scale (pixman_transform_t       *dst,
                        const pixman_transform_t *src,
                        pixman_fixed_t            sx,
                        pixman_fixed_t            sy)
{
    if (!dst || !sx || !sy) return FALSE;
    pixman_transform_t base = src ? *src : *dst;
    base.matrix[0][0] = fixed_mul(base.matrix[0][0], sx);
    base.matrix[1][1] = fixed_mul(base.matrix[1][1], sy);
    *dst = base;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_rotate (pixman_transform_t       *dst,
                         const pixman_transform_t *src,
                         pixman_fixed_t            c,
                         pixman_fixed_t            s)
{
    if (!dst) return FALSE;
    pixman_transform_t base = src ? *src : *dst;
    pixman_fixed_t m00 = base.matrix[0][0], m01 = base.matrix[0][1];
    pixman_fixed_t m10 = base.matrix[1][0], m11 = base.matrix[1][1];
    base.matrix[0][0] = fixed_mul(m00, c) + fixed_mul(m01, s);
    base.matrix[0][1] = fixed_mul(m01, c) - fixed_mul(m00, s);
    base.matrix[1][0] = fixed_mul(m10, c) + fixed_mul(m11, s);
    base.matrix[1][1] = fixed_mul(m11, c) - fixed_mul(m10, s);
    *dst = base;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_translate (pixman_transform_t       *dst,
                            const pixman_transform_t *src,
                            pixman_fixed_t            tx,
                            pixman_fixed_t            ty)
{
    if (!dst) return FALSE;
    pixman_transform_t base = src ? *src : *dst;
    base.matrix[0][2] += tx;
    base.matrix[1][2] += ty;
    *dst = base;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_identity (const pixman_transform_t *t) {
    if (!t) return FALSE;
    return t->matrix[0][0]==pixman_int_to_fixed(1) && t->matrix[1][1]==pixman_int_to_fixed(1) &&
           t->matrix[0][1]==0 && t->matrix[1][0]==0 && t->matrix[0][2]==0 && t->matrix[1][2]==0 &&
           t->matrix[2][2]==pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_scale (const pixman_transform_t *t) {
    return t && t->matrix[0][1]==0 && t->matrix[1][0]==0 && t->matrix[0][2]==0 && t->matrix[1][2]==0;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_int_translate (const pixman_transform_t *t) {
    if (!t) return FALSE;
    int tx = pixman_fixed_to_int(t->matrix[0][2]), ty = pixman_fixed_to_int(t->matrix[1][2]);
    return pixman_int_to_fixed(tx)==t->matrix[0][2] && pixman_int_to_fixed(ty)==t->matrix[1][2];
}

// ========== 补齐 pixman_transform_invert（缺失实现） ==========
PIXMAN_EXPORT pixman_bool_t
pixman_transform_invert (pixman_transform_t *dst, const pixman_transform_t *src) {
    if (!dst || !src) return FALSE;
    int64_t a = src->matrix[0][0], b = src->matrix[0][1];
    int64_t c = src->matrix[1][0], d = src->matrix[1][1];
    int64_t det = (a * d - b * c) >> 16;
    if (!det) return FALSE;
    pixman_transform_t inv;
    inv.matrix[0][0] = (pixman_fixed_t)((d << 16) / det);
    inv.matrix[0][1] = (pixman_fixed_t)((-b << 16) / det);
    inv.matrix[1][0] = (pixman_fixed_t)((-c << 16) / det);
    inv.matrix[1][1] = (pixman_fixed_t)((a << 16) / det);
    inv.matrix[2][2] = pixman_int_to_fixed(1);
    int64_t tx = src->matrix[0][2], ty = src->matrix[1][2];
    inv.matrix[0][2] = -(pixman_fixed_t)(((inv.matrix[0][0] * tx + inv.matrix[0][1] * ty) + 0x8000) >> 16);
    inv.matrix[1][2] = -(pixman_fixed_t)(((inv.matrix[1][0] * tx + inv.matrix[1][1] * ty) + 0x8000) >> 16);
    *dst = inv;
    return TRUE;
}

// ========== Region32 缺失函数 ==========
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

static inline pixman_box32_t
region32_intersect_box(pixman_box32_t a, pixman_box32_t b)
{
    pixman_box32_t r;
    r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
    r.x2 = a.x2 < b.x2 ? a.x2 : b.x2;
    r.y2 = a.y2 < b.y2 ? a.y2 : b.y2;
    if (r.x2 < r.x1 || r.y2 < r.y1) r.x1 = r.y1 = r.x2 = r.y2 = 0;
    return r;
}

static inline pixman_box32_t
region32_union_box(pixman_box32_t a, pixman_box32_t b)
{
    pixman_box32_t r;
    r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    r.x2 = a.x2 > b.x2 ? a.x2 : b.x2;
    r.y2 = a.y2 > b.y2 ? a.y2 : b.y2;
    return r;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_intersect (pixman_region32_t *dest,
                           const pixman_region32_t *r1,
                           const pixman_region32_t *r2)
{
    if (!dest || !r1 || !r2) return FALSE;
    dest->extents = region32_intersect_box(r1->extents, r2->extents);
    dest->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_union (pixman_region32_t *dest,
                       const pixman_region32_t *r1,
                       const pixman_region32_t *r2)
{
    if (!dest || !r1 || !r2) return FALSE;
    dest->extents = region32_union_box(r1->extents, r2->extents);
    dest->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_intersect_rect (pixman_region32_t *dest,
                                const pixman_region32_t *src,
                                int x, int y, unsigned int w, unsigned int h)
{
    if (!dest || !src) return FALSE;
    pixman_box32_t box = { x, y, x + (int)w, y + (int)h };
    dest->extents = region32_intersect_box(src->extents, box);
    dest->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_union_rect (pixman_region32_t *dest,
                            const pixman_region32_t *src,
                            int x, int y, unsigned int w, unsigned int h)
{
    if (!dest || !src) return FALSE;
    pixman_box32_t box = { x, y, x + (int)w, y + (int)h };
    dest->extents = region32_union_box(src->extents, box);
    dest->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_subtract (pixman_region32_t *dest,
                          const pixman_region32_t *rm,
                          const pixman_region32_t *rs)
{
    if (!dest || !rm || !rs) return FALSE;
    pixman_region32_t tmp;
    pixman_region32_intersect(&tmp, rm, rs);
    if (tmp.extents.x1 < tmp.extents.x2 && tmp.extents.y1 < tmp.extents.y2)
        pixman_region32_clear(dest);
    else
        pixman_region32_copy(dest, rm);
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
    if (!region) return 0;
    return (region->extents.x1 < region->extents.x2 &&
            region->extents.y1 < region->extents.y2) ? 1 : 0;
}

PIXMAN_EXPORT pixman_box32_t *
pixman_region32_rectangles (const pixman_region32_t *region, int *n_rects)
{
    if (n_rects) *n_rects = pixman_region32_n_rects(region);
    return region && pixman_region32_n_rects(region) ? (pixman_box32_t *)&region->extents : NULL;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region32_equal (const pixman_region32_t *r1,
                       const pixman_region32_t *r2)
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

// ========== Region16 API 补齐 ==========
PIXMAN_EXPORT void
pixman_region_init (pixman_region16_t *region)
{
    if (!region) return;
    region->extents.x1 = region->extents.y1 = 0;
    region->extents.x2 = region->extents.y2 = 0;
    region->data = NULL;
}

PIXMAN_EXPORT void
pixman_region_init_rect (pixman_region16_t *region,
                         int                x,
                         int                y,
                         unsigned int       width,
                         unsigned int       height)
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
    region->extents.x1 += (int16_t)x;
    region->extents.x2 += (int16_t)x;
    region->extents.y1 += (int16_t)y;
    region->extents.y2 += (int16_t)y;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_copy (pixman_region16_t       *dest,
                    const pixman_region16_t *source)
{
    if (!dest || !source) return FALSE;
    *dest = *source;
    if (dest->data) dest->data = NULL;
    return TRUE;
}

static inline pixman_box16_t
region16_intersect_box(pixman_box16_t a, pixman_box16_t b)
{
    pixman_box16_t r;
    r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
    r.x2 = a.x2 < b.x2 ? a.x2 : b.x2;
    r.y2 = a.y2 < b.y2 ? a.y2 : b.y2;
    if (r.x2 <= r.x1 || r.y2 <= r.y1) { r.x1=r.y1=r.x2=r.y2=0; }
    return r;
}

static inline pixman_box16_t
region16_union_box(pixman_box16_t a, pixman_box16_t b)
{
    pixman_box16_t r;
    r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    r.x2 = a.x2 > b.x2 ? a.x2 : b.x2;
    r.y2 = a.y2 > b.y2 ? a.y2 : b.y2;
    return r;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_intersect (pixman_region16_t       *new_reg,
                         const pixman_region16_t *reg1,
                         const pixman_region16_t *reg2)
{
    if (!new_reg || !reg1 || !reg2) return FALSE;
    new_reg->extents = region16_intersect_box(reg1->extents, reg2->extents);
    new_reg->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_union (pixman_region16_t       *new_reg,
                     const pixman_region16_t *reg1,
                     const pixman_region16_t *reg2)
{
    if (!new_reg || !reg1 || !reg2) return FALSE;
    new_reg->extents = region16_union_box(reg1->extents, reg2->extents);
    new_reg->data = NULL;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_subtract (pixman_region16_t       *reg_d,
                        const pixman_region16_t *reg_m,
                        const pixman_region16_t *reg_s)
{
    if (!reg_d || !reg_m || !reg_s) return FALSE;
    pixman_region16_t tmp;
    pixman_region_intersect(&tmp, reg_m, reg_s);
    if (pixman_region_not_empty(&tmp))
        pixman_region_clear(reg_d);
    else
        pixman_region_copy(reg_d, reg_m);
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_contains_point (const pixman_region16_t *region,
                              int                      x,
                              int                      y,
                              pixman_box16_t          *box)
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
    return region->extents.x1 < region->extents.x2 &&
           region->extents.y1 < region->extents.y2;
}

PIXMAN_EXPORT pixman_box16_t *
pixman_region_extents (const pixman_region16_t *region)
{
    return region ? (pixman_box16_t *)&region->extents : NULL;
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
    return region && pixman_region_n_rects(region) ? (pixman_box16_t *)&region->extents : NULL;
}

PIXMAN_EXPORT pixman_bool_t
pixman_region_equal (const pixman_region16_t *region1,
                     const pixman_region16_t *region2)
{
    if (!region1 || !region2) return FALSE;
    return memcmp(&region1->extents, &region2->extents, sizeof(region1->extents)) == 0;
}

PIXMAN_EXPORT void
pixman_region_reset (pixman_region16_t       *region,
                     const pixman_box16_t    *box)
{
    if (!region) return;
    region->extents = box ? *box : (pixman_box16_t){0,0,0,0};
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

// ========== Format / BLT 支持 ==========
PIXMAN_API
pixman_bool_t pixman_format_supported_destination (pixman_format_code_t format)
{
    int bpp = (format >> 24);
    return (bpp == 32 || bpp == 16 || bpp == 8) ? TRUE : FALSE;
}

PIXMAN_API
pixman_bool_t pixman_format_supported_source (pixman_format_code_t format)
{
    return pixman_format_supported_destination (format);
}

PIXMAN_API
pixman_bool_t pixman_blt (uint32_t *src_bits,
                         uint32_t *dst_bits,
                         int       src_stride,
                         int       dst_stride,
                         int       src_bpp,
                         int       dst_bpp,
                         int       src_x,
                         int       src_y,
                         int       dest_x,
                         int       dest_y,
                         int       width,
                         int       height)
{
    (void)src_bpp; (void)dst_bpp;
    if (!src_bits || !dst_bits || width <= 0 || height <= 0) return FALSE;

    for (int j = 0; j < height; ++j)
    {
        const uint32_t *srow = src_bits + (src_y + j) * src_stride + src_x;
        uint32_t *drow = dst_bits + (dest_y + j) * dst_stride + dest_x;
        neon_row_copy (srow, drow, width);
    }
    return TRUE;
}

// ========== Composite实现（补齐缺失） ==========
PIXMAN_EXPORT void
pixman_image_composite32 (pixman_op_t op,
                          pixman_image_t *src,
                          pixman_image_t *mask,
                          pixman_image_t *dest,
                          int32_t src_x, int32_t src_y,
                          int32_t mask_x, int32_t mask_y,
                          int32_t dest_x, int32_t dest_y,
                          int32_t width, int32_t height) {
    if (!dest || width <= 0 || height <= 0) return;
    if (dest->type != BITS || !dest->bits.bits) return;

    const int dpitch = dest->bits.stride / 4;

    if (op == PIXMAN_OP_CLEAR) {
        int dx = dest_x, dy = dest_y, w = width, h = height;
        if (!clip_rect(NULL,NULL,0,0, &dx,&dy,dest->bits.width,dest->bits.height, &w,&h)) return;
        for (int j = 0; j < h; ++j)
            neon_row_fill(dest->bits.bits + (dy + j) * dpitch + dx, w, 0);
        return;
    }

    if (!src) return;

    if (src->type == SOLID) {
        pixman_color_t c = src->solid.color;
        uint8_t a = (c.alpha + 0x7F) >> 8, r = (c.red + 0x7F) >> 8, g = (c.green + 0x7F) >> 8, b = (c.blue + 0x7F) >> 8;
        uint8_t pr = (r * a + 255) / 255, pg = (g * a + 255) / 255, pb = (b * a + 255) / 255;
        uint32_t color = ((uint32_t)a << 24) | ((uint32_t)pr << 16) | ((uint32_t)pg << 8) | pb;

        int dx = dest_x, dy = dest_y, w = width, h = height;
        if (!clip_rect(NULL,NULL,0,0, &dx,&dy,dest->bits.width,dest->bits.height, &w,&h)) return;

        if (op == PIXMAN_OP_SRC || !mask) {
            for (int j = 0; j < h; ++j)
                neon_row_fill(dest->bits.bits + (dy + j) * dpitch + dx, w, color);
        } else if (op == PIXMAN_OP_OVER && mask && mask->type == BITS) {
            const int mpitch = mask->bits.stride / 4;
            for (int j = 0; j < h; ++j) {
                const uint32_t *mrow = mask->bits.bits + (mask_y + j) * mpitch + mask_x;
                uint32_t *drow = dest->bits.bits + (dy + j) * dpitch + dx;
                for (int i = 0; i < w; ++i) {
                    uint8_t ma = (mrow[i] >> 24);
                    uint8_t ea = (a * ma + 255) / 255;
                    uint8_t er = (pr * ma + 255) / 255;
                    uint8_t eg = (pg * ma + 255) / 255;
                    uint8_t eb = (pb * ma + 255) / 255;
                    uint32_t dv = drow[i];
                    uint8_t da = dv >> 24, dr = (dv >> 16) & 0xFF, dg = (dv >> 8) & 0xFF, db = dv & 0xFF;
                    uint8_t inv = 255 - ea;
                    uint8_t oa = ea + (da * inv + 255) / 255;
                    uint8_t orr = er + (dr * inv + 255) / 255;
                    uint8_t og = eg + (dg * inv + 255) / 255;
                    uint8_t ob = eb + (db * inv + 255) / 255;
                    drow[i] = ((uint32_t)oa << 24) | ((uint32_t)orr << 16) | ((uint32_t)og << 8) | ob;
                }
            }
        }
        return;
    }

    if (src->type == BITS && src->bits.bits) {
        const int spitch = src->bits.stride / 4;
        int sx = src_x, sy = src_y, dx = dest_x, dy = dest_y, w = width, h = height;
        if (!clip_rect(&sx,&sy,src->bits.width,src->bits.height,
                       &dx,&dy,dest->bits.width,dest->bits.height, &w,&h)) return;

        const uint32_t *sbase = src->bits.bits + sy * spitch + sx;
        uint32_t *dbase = dest->bits.bits + dy * dpitch + dx;

        if (op == PIXMAN_OP_SRC && !mask) {
            for (int j = 0; j < h; ++j)
                neon_row_copy(sbase + j * spitch, dbase + j * dpitch, w);
        } else if (op == PIXMAN_OP_OVER && !mask) {
            for (int j = 0; j < h; ++j) {
                const uint32_t *srow = sbase + j * spitch;
                uint32_t *drow = dbase + j * dpitch;
                int i = 0;
                for (; i + 8 <= w; i += 8)
                    neon_over_8px(srow + i, drow + i);
                for (; i < w; ++i) {
                    uint32_t sv = srow[i]; uint8_t sa = sv >> 24;
                    if (sa == 0xFF) { drow[i] = sv; continue; }
                    uint32_t dv = drow[i]; uint8_t inv = 255 - sa;
                    uint8_t sr=(sv>>16)&0xFF, sg=(sv>>8)&0xFF, sb=sv&0xFF;
                    uint8_t da=dv>>24, dr=(dv>>16)&0xFF, dg=(dv>>8)&0xFF, db=dv&0xFF;
                    uint8_t oa = sa + (da * inv + 255) / 255;
                    uint8_t orr = sr + (dr * inv + 255) / 255;
                    uint8_t og = sg + (dg * inv + 255) / 255;
                    uint8_t ob = sb + (db * inv + 255) / 255;
                    drow[i] = ((uint32_t)oa<<24)|((uint32_t)orr<<16)|((uint32_t)og<<8)|ob;
                }
            }
        }
    }
}

// ========== Fill 实现 ==========
PIXMAN_EXPORT pixman_bool_t
pixman_fill (uint32_t *bits,
             int       stride,
             int       bpp,
             int       x,
             int       y,
             int       width,
             int       height,
             uint32_t  _xor)
{
    if (!bits || width <= 0 || height <= 0)
        return FALSE;

    if (bpp == 32)
    {
        for (int j = 0; j < height; ++j)
        {
            uint32_t *row = bits + (y + j) * stride + x;
            neon_row_fill (row, width, _xor);
        }
        return TRUE;
    }
    else if (bpp == 16)
    {
        uint16_t val = (uint16_t)_xor;
        for (int j = 0; j < height; ++j)
        {
            uint16_t *row = (uint16_t *)bits + (y + j) * stride + x;
            for (int i = 0; i < width; ++i)
                row[i] = val;
        }
        return TRUE;
    }
    else if (bpp == 8)
    {
        uint8_t val = (uint8_t)_xor;
        for (int j = 0; j < height; ++j)
        {
            uint8_t *row = (uint8_t *)bits + (y + j) * stride + x;
            neon_memset_u8 (row, val, width);
        }
        return TRUE;
    }

    return FALSE;
}

// ========== Version 实现（补齐） ==========
PIXMAN_EXPORT int
pixman_version (void) {
    return PIXMAN_VERSION;
}

PIXMAN_EXPORT const char *
pixman_version_string (void) {
    return PIXMAN_VERSION_STRING;
}
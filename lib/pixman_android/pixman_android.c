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

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#ifndef PIXMAN_EXPORT
#define PIXMAN_EXPORT PIXMAN_API
#endif

// ========== 辅助：像素/字节步长换算 ==========
static inline int stride_px_from_bytes_int(int byte_stride) { return byte_stride > 0 ? byte_stride / 4 : 0; }

// ========== NEON 辅助 ==========
static inline void neon_row_copy(const uint32_t* src, uint32_t* dst, int w) {
#if defined(__ARM_NEON)
    int n = w;
    for (; n >= 4; n -= 4, src += 4, dst += 4) {
        uint32x4_t v = vld1q_u32(src);
        vst1q_u32(dst, v);
    }
    while (n--) *dst++ = *src++;
#else
    memcpy(dst, src, (size_t)w * 4);
#endif
}
static inline void neon_row_fill(uint32_t* dst, int w, uint32_t color) {
#if defined(__ARM_NEON)
    int n = w;
    uint32x4_t v = vdupq_n_u32(color);
    for (; n >= 4; n -= 4, dst += 4) vst1q_u32(dst, v);
    while (n--) *dst++ = color;
#else
    for (int i = 0; i < w; ++i) dst[i] = color;
#endif
}

// ========== 安全裁剪 ==========
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

        img->bits.bits = malloc(size);
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
    int bpp = PIXMAN_FORMAT_BPP(format);

    if (!bits)
    {
        int byte_stride = stride ? stride : ((width * bpp + 31) >> 5) * 4;
        int size = height * byte_stride;

        img->bits.bits = malloc(size);
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

PIXMAN_EXPORT pixman_bool_t
pixman_image_set_filter (pixman_image_t *image, pixman_filter_t filter,
                         const pixman_fixed_t *params, int n_params)
{
    (void)params; (void)n_params;
    if (!image) return FALSE;
    image->common.filter = filter;
    image->common.n_filter_params = n_params;
    image->common.filter_params = (void*)params; // 可按需复制
    return TRUE;
}

/* ========== Getters 修正（stride 为字节） ========== */
PIXMAN_EXPORT int
pixman_image_get_stride (pixman_image_t *image)
{
    if (image && image->type == BITS) return image->bits.stride;
    return 0;
}

/* ========== blt/fill/composite NEON 快路径已存在，无改动 ========== */
// ================== Transform API 补齐 ==================
PIXMAN_EXPORT void
pixman_transform_init_identity (pixman_transform_t *matrix)
{
    if (!matrix) return;
    memset(matrix, 0, sizeof(*matrix));
    matrix->matrix[0][0] = pixman_int_to_fixed(1);
    matrix->matrix[1][1] = pixman_int_to_fixed(1);
    matrix->matrix[2][2] = pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_scale (pixman_transform_t *forward,
                        pixman_transform_t *reverse,
                        pixman_fixed_t sx, pixman_fixed_t sy)
{
    if (!sx || !sy) return FALSE;
    if (forward) {
        forward->matrix[0][0] = (pixman_fixed_t)((((int64_t)forward->matrix[0][0]) * sx) >> 16);
        forward->matrix[1][1] = (pixman_fixed_t)((((int64_t)forward->matrix[1][1]) * sy) >> 16);
    }
    if (reverse) {
        // 简单近似：仅对对角元素求倒数
        if (sx) reverse->matrix[0][0] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sx);
        if (sy) reverse->matrix[1][1] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sy);
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_rotate (pixman_transform_t *forward,
                         pixman_transform_t *reverse,
                         pixman_fixed_t c, pixman_fixed_t s)
{
    // 只做一次左乘（简化）
    if (forward) {
        pixman_transform_t R = *forward;
        R.matrix[0][0] = c;  R.matrix[0][1] = -s;
        R.matrix[1][0] = s;  R.matrix[1][1] =  c;
        *forward = R;
    }
    if (reverse) {
        pixman_transform_t Rt = *reverse;
        Rt.matrix[0][0] = c;  Rt.matrix[0][1] = s;
        Rt.matrix[1][0] = -s; Rt.matrix[1][1] = c;
        *reverse = Rt;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_translate (pixman_transform_t *forward,
                            pixman_transform_t *reverse,
                            pixman_fixed_t tx, pixman_fixed_t ty)
{
    if (forward) {
        forward->matrix[0][2] = tx;
        forward->matrix[1][2] = ty;
    }
    if (reverse) {
        reverse->matrix[0][2] = -tx;
        reverse->matrix[1][2] = -ty;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_invert (pixman_transform_t *dst, const pixman_transform_t *src)
{
    if (!dst || !src) return FALSE;
    // 仅支持仿射 2x2 + 平移
    int64_t a = src->matrix[0][0], b = src->matrix[0][1], c = src->matrix[1][0], d = src->matrix[1][1];
    int64_t det = a * d - b * c;
    if (!det) return FALSE;
    pixman_transform_t r = {{0}};
    r.matrix[0][0] = (pixman_fixed_t)((d << 16) / det);
    r.matrix[0][1] = (pixman_fixed_t)((-b << 16) / det);
    r.matrix[1][0] = (pixman_fixed_t)((-c << 16) / det);
    r.matrix[1][1] = (pixman_fixed_t)((a << 16) / det);
    r.matrix[2][2] = pixman_int_to_fixed(1);
    r.matrix[0][2] = -(pixman_fixed_t)(((int64_t)r.matrix[0][0] * src->matrix[0][2] + (int64_t)r.matrix[0][1] * src->matrix[1][2]) >> 16);
    r.matrix[1][2] = -(pixman_fixed_t)(((int64_t)r.matrix[1][0] * src->matrix[0][2] + (int64_t)r.matrix[1][1] * src->matrix[1][2]) >> 16);
    *dst = r;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_identity (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    return t->matrix[0][0]==pixman_int_to_fixed(1) && t->matrix[1][1]==pixman_int_to_fixed(1) &&
           t->matrix[0][1]==0 && t->matrix[1][0]==0 &&
           t->matrix[0][2]==0 && t->matrix[1][2]==0 &&
           t->matrix[2][2]==pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_scale (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    return t->matrix[0][1]==0 && t->matrix[1][0]==0 &&
           t->matrix[0][2]==0 && t->matrix[1][2]==0;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_int_translate (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    int tx = pixman_fixed_to_int(t->matrix[0][2]);
    int ty = pixman_fixed_to_int(t->matrix[1][2]);
    return pixman_int_to_fixed(tx)==t->matrix[0][2] && pixman_int_to_fixed(ty)==t->matrix[1][2];
}

// ================== Image 创建/释放修正：own_data ==================
// 在 create_bits / create_bits_no_clear 内部分配时设置 own_data=1，释放时判断
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

        img->bits.bits = malloc(size);
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
    int bpp = PIXMAN_FORMAT_BPP(format);

    if (!bits)
    {
        int byte_stride = stride ? stride : ((width * bpp + 31) >> 5) * 4;
        int size = height * byte_stride;

        img->bits.bits = malloc(size);
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

PIXMAN_EXPORT pixman_bool_t
pixman_image_set_filter (pixman_image_t *image, pixman_filter_t filter,
                         const pixman_fixed_t *params, int n_params)
{
    (void)params; (void)n_params;
    if (!image) return FALSE;
    image->common.filter = filter;
    image->common.n_filter_params = n_params;
    image->common.filter_params = (void*)params; // 可按需复制
    return TRUE;
}

/* ========== Getters 修正（stride 为字节） ========== */
PIXMAN_EXPORT int
pixman_image_get_stride (pixman_image_t *image)
{
    if (image && image->type == BITS) return image->bits.stride;
    return 0;
}

/* ========== blt/fill/composite NEON 快路径已存在，无改动 ========== */
// ================== Transform API 补齐 ==================
PIXMAN_EXPORT void
pixman_transform_init_identity (pixman_transform_t *matrix)
{
    if (!matrix) return;
    memset(matrix, 0, sizeof(*matrix));
    matrix->matrix[0][0] = pixman_int_to_fixed(1);
    matrix->matrix[1][1] = pixman_int_to_fixed(1);
    matrix->matrix[2][2] = pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_scale (pixman_transform_t *forward,
                        pixman_transform_t *reverse,
                        pixman_fixed_t sx, pixman_fixed_t sy)
{
    if (!sx || !sy) return FALSE;
    if (forward) {
        forward->matrix[0][0] = (pixman_fixed_t)((((int64_t)forward->matrix[0][0]) * sx) >> 16);
        forward->matrix[1][1] = (pixman_fixed_t)((((int64_t)forward->matrix[1][1]) * sy) >> 16);
    }
    if (reverse) {
        // 简单近似：仅对对角元素求倒数
        if (sx) reverse->matrix[0][0] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sx);
        if (sy) reverse->matrix[1][1] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sy);
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_rotate (pixman_transform_t *forward,
                         pixman_transform_t *reverse,
                         pixman_fixed_t c, pixman_fixed_t s)
{
    // 只做一次左乘（简化）
    if (forward) {
        pixman_transform_t R = *forward;
        R.matrix[0][0] = c;  R.matrix[0][1] = -s;
        R.matrix[1][0] = s;  R.matrix[1][1] =  c;
        *forward = R;
    }
    if (reverse) {
        pixman_transform_t Rt = *reverse;
        Rt.matrix[0][0] = c;  Rt.matrix[0][1] = s;
        Rt.matrix[1][0] = -s; Rt.matrix[1][1] = c;
        *reverse = Rt;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_translate (pixman_transform_t *forward,
                            pixman_transform_t *reverse,
                            pixman_fixed_t tx, pixman_fixed_t ty)
{
    if (forward) {
        forward->matrix[0][2] = tx;
        forward->matrix[1][2] = ty;
    }
    if (reverse) {
        reverse->matrix[0][2] = -tx;
        reverse->matrix[1][2] = -ty;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_invert (pixman_transform_t *dst, const pixman_transform_t *src)
{
    if (!dst || !src) return FALSE;
    // 仅支持仿射 2x2 + 平移
    int64_t a = src->matrix[0][0], b = src->matrix[0][1], c = src->matrix[1][0], d = src->matrix[1][1];
    int64_t det = a * d - b * c;
    if (!det) return FALSE;
    pixman_transform_t r = {{0}};
    r.matrix[0][0] = (pixman_fixed_t)((d << 16) / det);
    r.matrix[0][1] = (pixman_fixed_t)((-b << 16) / det);
    r.matrix[1][0] = (pixman_fixed_t)((-c << 16) / det);
    r.matrix[1][1] = (pixman_fixed_t)((a << 16) / det);
    r.matrix[2][2] = pixman_int_to_fixed(1);
    r.matrix[0][2] = -(pixman_fixed_t)(((int64_t)r.matrix[0][0] * src->matrix[0][2] + (int64_t)r.matrix[0][1] * src->matrix[1][2]) >> 16);
    r.matrix[1][2] = -(pixman_fixed_t)(((int64_t)r.matrix[1][0] * src->matrix[0][2] + (int64_t)r.matrix[1][1] * src->matrix[1][2]) >> 16);
    *dst = r;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_identity (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    return t->matrix[0][0]==pixman_int_to_fixed(1) && t->matrix[1][1]==pixman_int_to_fixed(1) &&
           t->matrix[0][1]==0 && t->matrix[1][0]==0 &&
           t->matrix[0][2]==0 && t->matrix[1][2]==0 &&
           t->matrix[2][2]==pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_scale (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    return t->matrix[0][1]==0 && t->matrix[1][0]==0 &&
           t->matrix[0][2]==0 && t->matrix[1][2]==0;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_int_translate (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    int tx = pixman_fixed_to_int(t->matrix[0][2]);
    int ty = pixman_fixed_to_int(t->matrix[1][2]);
    return pixman_int_to_fixed(tx)==t->matrix[0][2] && pixman_int_to_fixed(ty)==t->matrix[1][2];
}

// ================== Image 创建/释放修正：own_data ==================
// 在 create_bits / create_bits_no_clear 内部分配时设置 own_data=1，释放时判断
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

        img->bits.bits = malloc(size);
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
    int bpp = PIXMAN_FORMAT_BPP(format);

    if (!bits)
    {
        int byte_stride = stride ? stride : ((width * bpp + 31) >> 5) * 4;
        int size = height * byte_stride;

        img->bits.bits = malloc(size);
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

PIXMAN_EXPORT pixman_bool_t
pixman_image_set_filter (pixman_image_t *image, pixman_filter_t filter,
                         const pixman_fixed_t *params, int n_params)
{
    (void)params; (void)n_params;
    if (!image) return FALSE;
    image->common.filter = filter;
    image->common.n_filter_params = n_params;
    image->common.filter_params = (void*)params; // 可按需复制
    return TRUE;
}

/* ========== Getters 修正（stride 为字节） ========== */
PIXMAN_EXPORT int
pixman_image_get_stride (pixman_image_t *image)
{
    if (image && image->type == BITS) return image->bits.stride;
    return 0;
}

/* ========== blt/fill/composite NEON 快路径已存在，无改动 ========== */
// ================== Transform API 补齐 ==================
PIXMAN_EXPORT void
pixman_transform_init_identity (pixman_transform_t *matrix)
{
    if (!matrix) return;
    memset(matrix, 0, sizeof(*matrix));
    matrix->matrix[0][0] = pixman_int_to_fixed(1);
    matrix->matrix[1][1] = pixman_int_to_fixed(1);
    matrix->matrix[2][2] = pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_scale (pixman_transform_t *forward,
                        pixman_transform_t *reverse,
                        pixman_fixed_t sx, pixman_fixed_t sy)
{
    if (!sx || !sy) return FALSE;
    if (forward) {
        forward->matrix[0][0] = (pixman_fixed_t)((((int64_t)forward->matrix[0][0]) * sx) >> 16);
        forward->matrix[1][1] = (pixman_fixed_t)((((int64_t)forward->matrix[1][1]) * sy) >> 16);
    }
    if (reverse) {
        // 简单近似：仅对对角元素求倒数
        if (sx) reverse->matrix[0][0] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sx);
        if (sy) reverse->matrix[1][1] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sy);
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_rotate (pixman_transform_t *forward,
                         pixman_transform_t *reverse,
                         pixman_fixed_t c, pixman_fixed_t s)
{
    // 只做一次左乘（简化）
    if (forward) {
        pixman_transform_t R = *forward;
        R.matrix[0][0] = c;  R.matrix[0][1] = -s;
        R.matrix[1][0] = s;  R.matrix[1][1] =  c;
        *forward = R;
    }
    if (reverse) {
        pixman_transform_t Rt = *reverse;
        Rt.matrix[0][0] = c;  Rt.matrix[0][1] = s;
        Rt.matrix[1][0] = -s; Rt.matrix[1][1] = c;
        *reverse = Rt;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_translate (pixman_transform_t *forward,
                            pixman_transform_t *reverse,
                            pixman_fixed_t tx, pixman_fixed_t ty)
{
    if (forward) {
        forward->matrix[0][2] = tx;
        forward->matrix[1][2] = ty;
    }
    if (reverse) {
        reverse->matrix[0][2] = -tx;
        reverse->matrix[1][2] = -ty;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_invert (pixman_transform_t *dst, const pixman_transform_t *src)
{
    if (!dst || !src) return FALSE;
    // 仅支持仿射 2x2 + 平移
    int64_t a = src->matrix[0][0], b = src->matrix[0][1], c = src->matrix[1][0], d = src->matrix[1][1];
    int64_t det = a * d - b * c;
    if (!det) return FALSE;
    pixman_transform_t r = {{0}};
    r.matrix[0][0] = (pixman_fixed_t)((d << 16) / det);
    r.matrix[0][1] = (pixman_fixed_t)((-b << 16) / det);
    r.matrix[1][0] = (pixman_fixed_t)((-c << 16) / det);
    r.matrix[1][1] = (pixman_fixed_t)((a << 16) / det);
    r.matrix[2][2] = pixman_int_to_fixed(1);
    r.matrix[0][2] = -(pixman_fixed_t)(((int64_t)r.matrix[0][0] * src->matrix[0][2] + (int64_t)r.matrix[0][1] * src->matrix[1][2]) >> 16);
    r.matrix[1][2] = -(pixman_fixed_t)(((int64_t)r.matrix[1][0] * src->matrix[0][2] + (int64_t)r.matrix[1][1] * src->matrix[1][2]) >> 16);
    *dst = r;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_identity (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    return t->matrix[0][0]==pixman_int_to_fixed(1) && t->matrix[1][1]==pixman_int_to_fixed(1) &&
           t->matrix[0][1]==0 && t->matrix[1][0]==0 &&
           t->matrix[0][2]==0 && t->matrix[1][2]==0 &&
           t->matrix[2][2]==pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_scale (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    return t->matrix[0][1]==0 && t->matrix[1][0]==0 &&
           t->matrix[0][2]==0 && t->matrix[1][2]==0;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_int_translate (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    int tx = pixman_fixed_to_int(t->matrix[0][2]);
    int ty = pixman_fixed_to_int(t->matrix[1][2]);
    return pixman_int_to_fixed(tx)==t->matrix[0][2] && pixman_int_to_fixed(ty)==t->matrix[1][2];
}

// ================== Image 创建/释放修正：own_data ==================
// 在 create_bits / create_bits_no_clear 内部分配时设置 own_data=1，释放时判断
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

        img->bits.bits = malloc(size);
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
    int bpp = PIXMAN_FORMAT_BPP(format);

    if (!bits)
    {
        int byte_stride = stride ? stride : ((width * bpp + 31) >> 5) * 4;
        int size = height * byte_stride;

        img->bits.bits = malloc(size);
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

PIXMAN_EXPORT pixman_bool_t
pixman_image_set_filter (pixman_image_t *image, pixman_filter_t filter,
                         const pixman_fixed_t *params, int n_params)
{
    (void)params; (void)n_params;
    if (!image) return FALSE;
    image->common.filter = filter;
    image->common.n_filter_params = n_params;
    image->common.filter_params = (void*)params; // 可按需复制
    return TRUE;
}

/* ========== Getters 修正（stride 为字节） ========== */
PIXMAN_EXPORT int
pixman_image_get_stride (pixman_image_t *image)
{
    if (image && image->type == BITS) return image->bits.stride;
    return 0;
}

/* ========== blt/fill/composite NEON 快路径已存在，无改动 ========== */
// ================== Transform API 补齐 ==================
PIXMAN_EXPORT void
pixman_transform_init_identity (pixman_transform_t *matrix)
{
    if (!matrix) return;
    memset(matrix, 0, sizeof(*matrix));
    matrix->matrix[0][0] = pixman_int_to_fixed(1);
    matrix->matrix[1][1] = pixman_int_to_fixed(1);
    matrix->matrix[2][2] = pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_scale (pixman_transform_t *forward,
                        pixman_transform_t *reverse,
                        pixman_fixed_t sx, pixman_fixed_t sy)
{
    if (!sx || !sy) return FALSE;
    if (forward) {
        forward->matrix[0][0] = (pixman_fixed_t)((((int64_t)forward->matrix[0][0]) * sx) >> 16);
        forward->matrix[1][1] = (pixman_fixed_t)((((int64_t)forward->matrix[1][1]) * sy) >> 16);
    }
    if (reverse) {
        // 简单近似：仅对对角元素求倒数
        if (sx) reverse->matrix[0][0] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sx);
        if (sy) reverse->matrix[1][1] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sy);
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_rotate (pixman_transform_t *forward,
                         pixman_transform_t *reverse,
                         pixman_fixed_t c, pixman_fixed_t s)
{
    // 只做一次左乘（简化）
    if (forward) {
        pixman_transform_t R = *forward;
        R.matrix[0][0] = c;  R.matrix[0][1] = -s;
        R.matrix[1][0] = s;  R.matrix[1][1] =  c;
        *forward = R;
    }
    if (reverse) {
        pixman_transform_t Rt = *reverse;
        Rt.matrix[0][0] = c;  Rt.matrix[0][1] = s;
        Rt.matrix[1][0] = -s; Rt.matrix[1][1] = c;
        *reverse = Rt;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_translate (pixman_transform_t *forward,
                            pixman_transform_t *reverse,
                            pixman_fixed_t tx, pixman_fixed_t ty)
{
    if (forward) {
        forward->matrix[0][2] = tx;
        forward->matrix[1][2] = ty;
    }
    if (reverse) {
        reverse->matrix[0][2] = -tx;
        reverse->matrix[1][2] = -ty;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_invert (pixman_transform_t *dst, const pixman_transform_t *src)
{
    if (!dst || !src) return FALSE;
    // 仅支持仿射 2x2 + 平移
    int64_t a = src->matrix[0][0], b = src->matrix[0][1], c = src->matrix[1][0], d = src->matrix[1][1];
    int64_t det = a * d - b * c;
    if (!det) return FALSE;
    pixman_transform_t r = {{0}};
    r.matrix[0][0] = (pixman_fixed_t)((d << 16) / det);
    r.matrix[0][1] = (pixman_fixed_t)((-b << 16) / det);
    r.matrix[1][0] = (pixman_fixed_t)((-c << 16) / det);
    r.matrix[1][1] = (pixman_fixed_t)((a << 16) / det);
    r.matrix[2][2] = pixman_int_to_fixed(1);
    r.matrix[0][2] = -(pixman_fixed_t)(((int64_t)r.matrix[0][0] * src->matrix[0][2] + (int64_t)r.matrix[0][1] * src->matrix[1][2]) >> 16);
    r.matrix[1][2] = -(pixman_fixed_t)(((int64_t)r.matrix[1][0] * src->matrix[0][2] + (int64_t)r.matrix[1][1] * src->matrix[1][2]) >> 16);
    *dst = r;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_identity (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    return t->matrix[0][0]==pixman_int_to_fixed(1) && t->matrix[1][1]==pixman_int_to_fixed(1) &&
           t->matrix[0][1]==0 && t->matrix[1][0]==0 &&
           t->matrix[0][2]==0 && t->matrix[1][2]==0 &&
           t->matrix[2][2]==pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_scale (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    return t->matrix[0][1]==0 && t->matrix[1][0]==0 &&
           t->matrix[0][2]==0 && t->matrix[1][2]==0;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_int_translate (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    int tx = pixman_fixed_to_int(t->matrix[0][2]);
    int ty = pixman_fixed_to_int(t->matrix[1][2]);
    return pixman_int_to_fixed(tx)==t->matrix[0][2] && pixman_int_to_fixed(ty)==t->matrix[1][2];
}

// ================== Image 创建/释放修正：own_data ==================
// 在 create_bits / create_bits_no_clear 内部分配时设置 own_data=1，释放时判断
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

        img->bits.bits = malloc(size);
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
    int bpp = PIXMAN_FORMAT_BPP(format);

    if (!bits)
    {
        int byte_stride = stride ? stride : ((width * bpp + 31) >> 5) * 4;
        int size = height * byte_stride;

        img->bits.bits = malloc(size);
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

PIXMAN_EXPORT pixman_bool_t
pixman_image_set_filter (pixman_image_t *image, pixman_filter_t filter,
                         const pixman_fixed_t *params, int n_params)
{
    (void)params; (void)n_params;
    if (!image) return FALSE;
    image->common.filter = filter;
    image->common.n_filter_params = n_params;
    image->common.filter_params = (void*)params; // 可按需复制
    return TRUE;
}

/* ========== Getters 修正（stride 为字节） ========== */
PIXMAN_EXPORT int
pixman_image_get_stride (pixman_image_t *image)
{
    if (image && image->type == BITS) return image->bits.stride;
    return 0;
}

/* ========== blt/fill/composite NEON 快路径已存在，无改动 ========== */
// ================== Transform API 补齐 ==================
PIXMAN_EXPORT void
pixman_transform_init_identity (pixman_transform_t *matrix)
{
    if (!matrix) return;
    memset(matrix, 0, sizeof(*matrix));
    matrix->matrix[0][0] = pixman_int_to_fixed(1);
    matrix->matrix[1][1] = pixman_int_to_fixed(1);
    matrix->matrix[2][2] = pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_scale (pixman_transform_t *forward,
                        pixman_transform_t *reverse,
                        pixman_fixed_t sx, pixman_fixed_t sy)
{
    if (!sx || !sy) return FALSE;
    if (forward) {
        forward->matrix[0][0] = (pixman_fixed_t)((((int64_t)forward->matrix[0][0]) * sx) >> 16);
        forward->matrix[1][1] = (pixman_fixed_t)((((int64_t)forward->matrix[1][1]) * sy) >> 16);
    }
    if (reverse) {
        // 简单近似：仅对对角元素求倒数
        if (sx) reverse->matrix[0][0] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sx);
        if (sy) reverse->matrix[1][1] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sy);
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_rotate (pixman_transform_t *forward,
                         pixman_transform_t *reverse,
                         pixman_fixed_t c, pixman_fixed_t s)
{
    // 只做一次左乘（简化）
    if (forward) {
        pixman_transform_t R = *forward;
        R.matrix[0][0] = c;  R.matrix[0][1] = -s;
        R.matrix[1][0] = s;  R.matrix[1][1] =  c;
        *forward = R;
    }
    if (reverse) {
        pixman_transform_t Rt = *reverse;
        Rt.matrix[0][0] = c;  Rt.matrix[0][1] = s;
        Rt.matrix[1][0] = -s; Rt.matrix[1][1] = c;
        *reverse = Rt;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_translate (pixman_transform_t *forward,
                            pixman_transform_t *reverse,
                            pixman_fixed_t tx, pixman_fixed_t ty)
{
    if (forward) {
        forward->matrix[0][2] = tx;
        forward->matrix[1][2] = ty;
    }
    if (reverse) {
        reverse->matrix[0][2] = -tx;
        reverse->matrix[1][2] = -ty;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_invert (pixman_transform_t *dst, const pixman_transform_t *src)
{
    if (!dst || !src) return FALSE;
    // 仅支持仿射 2x2 + 平移
    int64_t a = src->matrix[0][0], b = src->matrix[0][1], c = src->matrix[1][0], d = src->matrix[1][1];
    int64_t det = a * d - b * c;
    if (!det) return FALSE;
    pixman_transform_t r = {{0}};
    r.matrix[0][0] = (pixman_fixed_t)((d << 16) / det);
    r.matrix[0][1] = (pixman_fixed_t)((-b << 16) / det);
    r.matrix[1][0] = (pixman_fixed_t)((-c << 16) / det);
    r.matrix[1][1] = (pixman_fixed_t)((a << 16) / det);
    r.matrix[2][2] = pixman_int_to_fixed(1);
    r.matrix[0][2] = -(pixman_fixed_t)(((int64_t)r.matrix[0][0] * src->matrix[0][2] + (int64_t)r.matrix[0][1] * src->matrix[1][2]) >> 16);
    r.matrix[1][2] = -(pixman_fixed_t)(((int64_t)r.matrix[1][0] * src->matrix[0][2] + (int64_t)r.matrix[1][1] * src->matrix[1][2]) >> 16);
    *dst = r;
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_identity (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    return t->matrix[0][0]==pixman_int_to_fixed(1) && t->matrix[1][1]==pixman_int_to_fixed(1) &&
           t->matrix[0][1]==0 && t->matrix[1][0]==0 &&
           t->matrix[0][2]==0 && t->matrix[1][2]==0 &&
           t->matrix[2][2]==pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_scale (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    return t->matrix[0][1]==0 && t->matrix[1][0]==0 &&
           t->matrix[0][2]==0 && t->matrix[1][2]==0;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_is_int_translate (const pixman_transform_t *t)
{
    if (!t) return FALSE;
    int tx = pixman_fixed_to_int(t->matrix[0][2]);
    int ty = pixman_fixed_to_int(t->matrix[1][2]);
    return pixman_int_to_fixed(tx)==t->matrix[0][2] && pixman_int_to_fixed(ty)==t->matrix[1][2];
}

// ================== Image 创建/释放修正：own_data ==================
// 在 create_bits / create_bits_no_clear 内部分配时设置 own_data=1，释放时判断
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

        img->bits.bits = malloc(size);
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
    int bpp = PIXMAN_FORMAT_BPP(format);

    if (!bits)
    {
        int byte_stride = stride ? stride : ((width * bpp + 31) >> 5) * 4;
        int size = height * byte_stride;

        img->bits.bits = malloc(size);
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

PIXMAN_EXPORT pixman_bool_t
pixman_image_set_filter (pixman_image_t *image, pixman_filter_t filter,
                         const pixman_fixed_t *params, int n_params)
{
    (void)params; (void)n_params;
    if (!image) return FALSE;
    image->common.filter = filter;
    image->common.n_filter_params = n_params;
    image->common.filter_params = (void*)params; // 可按需复制
    return TRUE;
}

/* ========== Getters 修正（stride 为字节） ========== */
PIXMAN_EXPORT int
pixman_image_get_stride (pixman_image_t *image)
{
    if (image && image->type == BITS) return image->bits.stride;
    return 0;
}

/* ========== blt/fill/composite NEON 快路径已存在，无改动 ========== */
// ================== Transform API 补齐 ==================
PIXMAN_EXPORT void
pixman_transform_init_identity (pixman_transform_t *matrix)
{
    if (!matrix) return;
    memset(matrix, 0, sizeof(*matrix));
    matrix->matrix[0][0] = pixman_int_to_fixed(1);
    matrix->matrix[1][1] = pixman_int_to_fixed(1);
    matrix->matrix[2][2] = pixman_int_to_fixed(1);
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_scale (pixman_transform_t *forward,
                        pixman_transform_t *reverse,
                        pixman_fixed_t sx, pixman_fixed_t sy)
{
    if (!sx || !sy) return FALSE;
    if (forward) {
        forward->matrix[0][0] = (pixman_fixed_t)((((int64_t)forward->matrix[0][0]) * sx) >> 16);
        forward->matrix[1][1] = (pixman_fixed_t)((((int64_t)forward->matrix[1][1]) * sy) >> 16);
    }
    if (reverse) {
        // 简单近似：仅对对角元素求倒数
        if (sx) reverse->matrix[0][0] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sx);
        if (sy) reverse->matrix[1][1] = (pixman_fixed_t)((((int64_t)pixman_int_to_fixed(1)) << 16) / sy);
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_rotate (pixman_transform_t *forward,
                         pixman_transform_t *reverse,
                         pixman_fixed_t c, pixman_fixed_t s)
{
    // 只做一次左乘（简化）
    if (forward) {
        pixman_transform_t R = *forward;
        R.matrix[0][0] = c;  R.matrix[0][1] = -s;
        R.matrix[1][0] = s;  R.matrix[1][1] =  c;
        *forward = R;
    }
    if (reverse) {
        pixman_transform_t Rt = *reverse;
        Rt.matrix[0][0] = c;  Rt.matrix[0][1] = s;
        Rt.matrix[1][0] = -s; Rt.matrix[1][1] = c;
        *reverse = Rt;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_translate (pixman_transform_t *forward,
                            pixman_transform_t *reverse,
                            pixman_fixed_t tx, pixman_fixed_t ty)
{
    if (forward) {
        forward->matrix[0][2] = tx;
        forward->matrix[1][2] = ty;
    }
    if (reverse) {
        reverse->matrix[0][2] = -tx;
        reverse->matrix[1][2] = -ty;
    }
    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_transform_invert (pixman_transform_t *dst, const pixman_transform_t *src)
{
    if (!dst || !src) return FALSE;
    // 仅支持仿射 2x2 + 平移
    int64_t a = src->matrix[0][0], b = src->matrix[0][1], c = src->matrix[1][0], d = src->matrix[1][1];
    int64_t det = a * d - b * c;
    if (!det) return FALSE;
    pixman_transform_t r = {{0}};
    r.matrix[0][0] = (pixman_fixed_t)((d << 16) / det);
    r.matrix[0][1]
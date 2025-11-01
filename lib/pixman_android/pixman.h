/*
 * Copyright © 2008 Red Hat, Inc.
 * Copyright © 2024 Your Name or Organization
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

#ifndef PIXMAN_H__
#define PIXMAN_H__

#define PIXMAN_VERSION_MAJOR 0
#define PIXMAN_VERSION_MINOR 42
#define PIXMAN_VERSION_MICRO 2

#define PIXMAN_VERSION_STRING "0.42.2"

#define PIXMAN_VERSION_ENCODE(major, minor, micro) (	\
	  ((major) * 10000)				\
	+ ((minor) *   100)				\
	+ ((micro) *     1))

#define PIXMAN_VERSION PIXMAN_VERSION_ENCODE(	\
	PIXMAN_VERSION_MAJOR,			\
	PIXMAN_VERSION_MINOR,			\
	PIXMAN_VERSION_MICRO)

#ifndef PIXMAN_API
# define PIXMAN_API
#endif

#ifndef __cplusplus
# define PIXMAN_BEGIN_DECLS
# define PIXMAN_END_DECLS
#else
# define PIXMAN_BEGIN_DECLS extern "C" {
# define PIXMAN_END_DECLS }
#endif

PIXMAN_BEGIN_DECLS

/* Standard integers */
#include <stdint.h>

typedef int pixman_bool_t;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/* Format encoding (must be defined BEFORE using in enum) */
#define PIXMAN_FORMAT(bpp,type,a,r,g,b) (((bpp) << 24) |  \
                     ((type) << 16) | \
                     ((a) << 12) |    \
                     ((r) << 8) |     \
                     ((g) << 4) |     \
                     ((b)))

#define PIXMAN_TYPE_OTHER       0
#define PIXMAN_TYPE_A           1
#define PIXMAN_TYPE_ARGB        2
#define PIXMAN_TYPE_ABGR        3
#define PIXMAN_TYPE_COLOR       4
#define PIXMAN_TYPE_GRAY        5
#define PIXMAN_TYPE_YUY2        6
#define PIXMAN_TYPE_YV12        7
#define PIXMAN_TYPE_BGRA        8
#define PIXMAN_TYPE_RGBA        9

/* Fixed-point types */
typedef int32_t pixman_fixed_t;

#define pixman_fixed_e		((pixman_fixed_t) 1)
#define pixman_fixed_1		(pixman_int_to_fixed(1))
#define pixman_fixed_1_minus_e	(pixman_fixed_1 - pixman_fixed_e)
#define pixman_int_to_fixed(i)	((i) << 16)
#define pixman_double_to_fixed(d) ((pixman_fixed_t) ((d) * 65536.0))
#define pixman_fixed_to_int(f)	((int) ((f) >> 16))
#define pixman_fixed_to_double(f) ((double) (f) / 65536.0)
#define pixman_fixed_frac(f)	((f) & 0xffff)
#define pixman_fixed_floor(f)	((f) & ~0xffff)
#define pixman_fixed_ceil(f)	pixman_fixed_floor((f) + 0xffff)
#define pixman_fixed_fraction(f) ((f) - pixman_fixed_floor(f))

/* Structures */
typedef struct pixman_color pixman_color_t;
typedef struct pixman_point_fixed pixman_point_fixed_t;
typedef struct pixman_line_fixed pixman_line_fixed_t;
typedef struct pixman_vector pixman_vector_t;
typedef struct pixman_transform pixman_transform_t;

struct pixman_color
{
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    uint16_t alpha;
};

struct pixman_point_fixed
{
    pixman_fixed_t x;
    pixman_fixed_t y;
};

struct pixman_line_fixed
{
    pixman_point_fixed_t p1, p2;
};

struct pixman_vector
{
    pixman_fixed_t vector[3];
};

struct pixman_transform
{
    pixman_fixed_t matrix[3][3];
};

/* Transform functions */
PIXMAN_API
void pixman_transform_init_identity (pixman_transform_t *matrix);

PIXMAN_API
pixman_bool_t pixman_transform_point (const pixman_transform_t *transform,
                                      const pixman_vector_t    *vector,
                                      pixman_vector_t          *result);

PIXMAN_API
pixman_bool_t pixman_transform_invert (pixman_transform_t       *dst,
                                       const pixman_transform_t *src);

PIXMAN_API
pixman_bool_t pixman_transform_is_identity (const pixman_transform_t *t);

PIXMAN_API
pixman_bool_t pixman_transform_is_scale (const pixman_transform_t *t);

PIXMAN_API
pixman_bool_t pixman_transform_is_int_translate (const pixman_transform_t *t);

PIXMAN_API
pixman_bool_t pixman_transform_scale (pixman_transform_t *dst,
                                      const pixman_transform_t *src,
                                      pixman_fixed_t sx,
                                      pixman_fixed_t sy);

PIXMAN_API
pixman_bool_t pixman_transform_rotate (pixman_transform_t *dst,
                                       const pixman_transform_t *src,
                                       pixman_fixed_t cos,
                                       pixman_fixed_t sin);

PIXMAN_API
pixman_bool_t pixman_transform_translate (pixman_transform_t *dst,
                                          const pixman_transform_t *src,
                                          pixman_fixed_t tx,
                                          pixman_fixed_t ty);

/* Region structures and types */
typedef struct pixman_region16_data	pixman_region16_data_t;
typedef struct pixman_box16		pixman_box16_t;
typedef struct pixman_rectangle16	pixman_rectangle16_t;
typedef struct pixman_region16		pixman_region16_t;

typedef enum
{
    PIXMAN_REGION_OUT,
    PIXMAN_REGION_IN,
    PIXMAN_REGION_PART
} pixman_region_overlap_t;

struct pixman_region16_data {
    long		size;
    long		numRects;
};

struct pixman_rectangle16
{
    int16_t x, y;
    uint16_t width, height;
};

struct pixman_box16
{
    int16_t x1, y1, x2, y2;
};

struct pixman_region16
{
    pixman_box16_t          extents;
    pixman_region16_data_t  *data;
};

/* 16-bit region functions */
PIXMAN_API
void                    pixman_region_init               (pixman_region16_t *region);

PIXMAN_API
void                    pixman_region_init_rect          (pixman_region16_t *region,
							  int                x,
							  int                y,
							  unsigned int       width,
							  unsigned int       height);

PIXMAN_API
void                    pixman_region_fini               (pixman_region16_t *region);

PIXMAN_API
void                    pixman_region_translate          (pixman_region16_t *region,
							  int                x,
							  int                y);

PIXMAN_API
pixman_bool_t           pixman_region_copy               (pixman_region16_t       *dest,
							  const pixman_region16_t *source);

PIXMAN_API
pixman_bool_t           pixman_region_intersect          (pixman_region16_t       *new_reg,
							  const pixman_region16_t *reg1,
							  const pixman_region16_t *reg2);

PIXMAN_API
pixman_bool_t           pixman_region_union              (pixman_region16_t       *new_reg,
							  const pixman_region16_t *reg1,
							  const pixman_region16_t *reg2);

PIXMAN_API
pixman_bool_t           pixman_region_subtract           (pixman_region16_t       *reg_d,
							  const pixman_region16_t *reg_m,
							  const pixman_region16_t *reg_s);

PIXMAN_API
pixman_bool_t           pixman_region_contains_point     (const pixman_region16_t *region,
							  int                      x,
							  int                      y,
							  pixman_box16_t          *box);

PIXMAN_API
pixman_bool_t           pixman_region_not_empty          (const pixman_region16_t *region);

PIXMAN_API
pixman_box16_t *        pixman_region_extents            (const pixman_region16_t *region);

PIXMAN_API
int                     pixman_region_n_rects            (const pixman_region16_t *region);

PIXMAN_API
pixman_box16_t *        pixman_region_rectangles         (const pixman_region16_t *region,
							  int                     *n_rects);

PIXMAN_API
pixman_bool_t           pixman_region_equal              (const pixman_region16_t *region1,
							  const pixman_region16_t *region2);

PIXMAN_API
void                    pixman_region_reset              (pixman_region16_t       *region,
							  const pixman_box16_t    *box);

PIXMAN_API
void			pixman_region_clear		 (pixman_region16_t *region);

/* 32-bit region structures and types */
typedef struct pixman_region32_data	pixman_region32_data_t;
typedef struct pixman_box32		pixman_box32_t;
typedef struct pixman_rectangle32	pixman_rectangle32_t;
typedef struct pixman_region32		pixman_region32_t;

struct pixman_region32_data {
    long		size;
    long		numRects;
};

struct pixman_rectangle32
{
    int32_t x, y;
    uint32_t width, height;
};

struct pixman_box32
{
    int32_t x1, y1, x2, y2;
};

struct pixman_region32
{
    pixman_box32_t          extents;
    pixman_region32_data_t  *data;
};

/* 32-bit region functions */
PIXMAN_API
void                    pixman_region32_init               (pixman_region32_t *region);

PIXMAN_API
void                    pixman_region32_init_rect          (pixman_region32_t *region,
							    int                x,
							    int                y,
							    unsigned int       width,
							    unsigned int       height);

PIXMAN_API
void                    pixman_region32_fini               (pixman_region32_t *region);

PIXMAN_API
void                    pixman_region32_translate          (pixman_region32_t *region,
							    int                x,
							    int                y);

PIXMAN_API
pixman_bool_t           pixman_region32_copy               (pixman_region32_t       *dest,
							    const pixman_region32_t *source);

PIXMAN_API
pixman_bool_t           pixman_region32_intersect          (pixman_region32_t       *new_reg,
							    const pixman_region32_t *reg1,
							    const pixman_region32_t *reg2);

PIXMAN_API
pixman_bool_t           pixman_region32_union              (pixman_region32_t       *new_reg,
							    const pixman_region32_t *reg1,
							    const pixman_region32_t *reg2);

PIXMAN_API
pixman_bool_t		pixman_region32_intersect_rect     (pixman_region32_t       *dest,
							    const pixman_region32_t *source,
							    int                      x,
							    int                      y,
							    unsigned int             width,
							    unsigned int             height);

PIXMAN_API
pixman_bool_t           pixman_region32_union_rect         (pixman_region32_t       *dest,
							    const pixman_region32_t *source,
							    int                      x,
							    int                      y,
							    unsigned int             width,
							    unsigned int             height);

PIXMAN_API
pixman_bool_t           pixman_region32_subtract           (pixman_region32_t       *reg_d,
							    const pixman_region32_t *reg_m,
							    const pixman_region32_t *reg_s);

PIXMAN_API
pixman_bool_t           pixman_region32_contains_point     (const pixman_region32_t *region,
							    int                      x,
							    int                      y,
							    pixman_box32_t          *box);

PIXMAN_API
pixman_bool_t           pixman_region32_not_empty          (const pixman_region32_t *region);

PIXMAN_API
pixman_box32_t *        pixman_region32_extents            (const pixman_region32_t *region);

PIXMAN_API
int                     pixman_region32_n_rects            (const pixman_region32_t *region);

PIXMAN_API
pixman_box32_t *        pixman_region32_rectangles         (const pixman_region32_t *region,
							    int                     *n_rects);

PIXMAN_API
pixman_bool_t           pixman_region32_equal              (const pixman_region32_t *region1,
							    const pixman_region32_t *region2);

PIXMAN_API
void                    pixman_region32_reset              (pixman_region32_t    *region,
							    const pixman_box32_t *box);

PIXMAN_API
void			pixman_region32_clear		   (pixman_region32_t *region);

/* Image types and structures */
typedef union pixman_image pixman_image_t;

/* 修正：移除 typedef，直接使用 enum 作为类型 */
typedef enum pixman_format_code
{
    PIXMAN_a8r8g8b8 =    PIXMAN_FORMAT(32,PIXMAN_TYPE_ARGB,8,8,8,8),
    PIXMAN_x8r8g8b8 =    PIXMAN_FORMAT(32,PIXMAN_TYPE_ARGB,0,8,8,8),
    PIXMAN_a8b8g8r8 =    PIXMAN_FORMAT(32,PIXMAN_TYPE_ABGR,8,8,8,8),
    PIXMAN_x8b8g8r8 =    PIXMAN_FORMAT(32,PIXMAN_TYPE_ABGR,0,8,8,8),
    PIXMAN_b8g8r8a8 =    PIXMAN_FORMAT(32,PIXMAN_TYPE_BGRA,8,8,8,8),
    PIXMAN_b8g8r8x8 =    PIXMAN_FORMAT(32,PIXMAN_TYPE_BGRA,0,8,8,8),
    PIXMAN_r8g8b8a8 =    PIXMAN_FORMAT(32,PIXMAN_TYPE_RGBA,8,8,8,8),
    PIXMAN_r8g8b8x8 =    PIXMAN_FORMAT(32,PIXMAN_TYPE_RGBA,0,8,8,8),
    PIXMAN_r5g6b5 =      PIXMAN_FORMAT(16,PIXMAN_TYPE_ARGB,0,5,6,5),
    PIXMAN_b5g6r5 =      PIXMAN_FORMAT(16,PIXMAN_TYPE_ABGR,0,5,6,5),
    PIXMAN_a8 =          PIXMAN_FORMAT(8,PIXMAN_TYPE_A,8,0,0,0),
    PIXMAN_a1 =          PIXMAN_FORMAT(1,PIXMAN_TYPE_A,1,0,0,0)
} pixman_format_code_t;

/* Image functions */
PIXMAN_API
pixman_image_t *pixman_image_create_solid_fill       (const pixman_color_t         *color);

PIXMAN_API
pixman_image_t *pixman_image_create_bits             (pixman_format_code_t          format,
						      int                           width,
						      int                           height,
						      uint32_t                     *bits,
						      int                           rowstride_bytes);

PIXMAN_API
pixman_image_t *pixman_image_create_bits_no_clear    (pixman_format_code_t format,
						      int                  width,
						      int                  height,
						      uint32_t *           bits,
						      int                  rowstride_bytes);

PIXMAN_API
pixman_image_t *pixman_image_ref                     (pixman_image_t               *image);

PIXMAN_API
pixman_bool_t   pixman_image_unref                   (pixman_image_t               *image);

PIXMAN_API
pixman_bool_t   pixman_image_set_clip_region         (pixman_image_t               *image,
						      pixman_region16_t            *region);

PIXMAN_API
pixman_bool_t   pixman_image_set_clip_region32       (pixman_image_t               *image,
						      pixman_region32_t            *region);

PIXMAN_API
pixman_bool_t   pixman_image_set_transform           (pixman_image_t               *image,
						      const pixman_transform_t     *transform);

PIXMAN_API
uint32_t       *pixman_image_get_data                (pixman_image_t               *image);

PIXMAN_API
int		pixman_image_get_width               (pixman_image_t               *image);

PIXMAN_API
int             pixman_image_get_height              (pixman_image_t               *image);

PIXMAN_API
int		pixman_image_get_stride              (pixman_image_t               *image);

PIXMAN_API
pixman_format_code_t pixman_image_get_format	     (pixman_image_t		   *image);

/* Composite operations */
typedef enum {
    PIXMAN_OP_CLEAR			= 0x00,
    PIXMAN_OP_SRC			= 0x01,
    PIXMAN_OP_DST			= 0x02,
    PIXMAN_OP_OVER			= 0x03,
    PIXMAN_OP_OVER_REVERSE		= 0x04,
    PIXMAN_OP_IN			= 0x05,
    PIXMAN_OP_IN_REVERSE		= 0x06,
    PIXMAN_OP_OUT			= 0x07,
    PIXMAN_OP_OUT_REVERSE		= 0x08,
    PIXMAN_OP_ATOP			= 0x09,
    PIXMAN_OP_ATOP_REVERSE		= 0x0a,
    PIXMAN_OP_XOR			= 0x0b,
    PIXMAN_OP_ADD			= 0x0c,
    PIXMAN_OP_SATURATE			= 0x0d,
} pixman_op_t;

PIXMAN_API
void          pixman_image_composite32        (pixman_op_t        op,
					       pixman_image_t    *src,
					       pixman_image_t    *mask,
					       pixman_image_t    *dest,
					       int32_t            src_x,
					       int32_t            src_y,
					       int32_t            mask_x,
					       int32_t            mask_y,
					       int32_t            dest_x,
					       int32_t            dest_y,
					       int32_t            width,
					       int32_t            height);

/* Fill functions */
PIXMAN_API
pixman_bool_t pixman_fill               (uint32_t           *bits,
					 int                 stride,
					 int                 bpp,
					 int                 x,
					 int                 y,
					 int                 width,
					 int                 height,
					 uint32_t            _xor);

/* Version functions */
PIXMAN_API
int           pixman_version            (void);

PIXMAN_API
const char*   pixman_version_string     (void);

PIXMAN_END_DECLS

#endif /* PIXMAN_H__ */
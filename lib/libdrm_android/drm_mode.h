#ifndef LIBDRM_ANDROID_DRM_MODE_H
#define LIBDRM_ANDROID_DRM_MODE_H

#include <stdint.h>
#include <sys/ioctl.h>
#include "drm.h"

/* Mode-Setting IOCTLs */
#define DRM_IOCTL_MODE_GETRESOURCES    DRM_IOWR(0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCRTC         DRM_IOWR(0xA1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC         DRM_IOWR(0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETCONNECTOR    DRM_IOWR(0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_GETENCODER      DRM_IOWR(0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_CREATE_DUMB     DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB        DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB    DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)
#define DRM_IOCTL_MODE_ADDFB           DRM_IOWR(0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_ADDFB2          DRM_IOWR(0xB8, struct drm_mode_fb_cmd2)
#define DRM_IOCTL_MODE_RMFB            DRM_IOWR(0xAF, uint32_t)
#define DRM_IOCTL_MODE_PAGE_FLIP       DRM_IOWR(0xB0, struct drm_mode_crtc_page_flip)
#define DRM_IOCTL_MODE_GETPROPERTY     DRM_IOWR(0xAA, struct drm_mode_get_property)
#define DRM_IOCTL_MODE_CREATEPROPBLOB  DRM_IOWR(0xBD, struct drm_mode_create_blob)
#define DRM_IOCTL_MODE_DESTROYPROPBLOB DRM_IOWR(0xBE, struct drm_mode_destroy_blob)
#define DRM_IOCTL_MODE_OBJ_GETPROPERTIES DRM_IOWR(0xB9, struct drm_mode_obj_get_properties)
#define DRM_IOCTL_MODE_OBJ_SETPROPERTY   DRM_IOWR(0xBA, struct drm_mode_obj_set_property)
#define DRM_IOCTL_MODE_ATOMIC          DRM_IOWR(0xBC, struct drm_mode_atomic)
#define DRM_IOCTL_MODE_GETPLANERESOURCES DRM_IOWR(0xB5, struct drm_mode_get_plane_res)
#define DRM_IOCTL_MODE_GETPLANE        DRM_IOWR(0xB6, struct drm_mode_get_plane)
#define DRM_IOCTL_MODE_SETPLANE        DRM_IOWR(0xB7, struct drm_mode_set_plane)

/* Connector types */
#define DRM_MODE_CONNECTOR_Unknown      0
#define DRM_MODE_CONNECTOR_VGA          1
#define DRM_MODE_CONNECTOR_VIRTUAL      15

/* Connector status */
#define DRM_MODE_CONNECTED              1

/* Object types */
#define DRM_MODE_OBJECT_CRTC        0xcccccccc
#define DRM_MODE_OBJECT_CONNECTOR   0xc0c0c0c0
#define DRM_MODE_OBJECT_PLANE       0xeeeeeeee

/* Property flags */
#define DRM_MODE_PROP_ENUM           (1<<3)
#define DRM_MODE_PROP_IMMUTABLE      (1<<2)
#define DRM_MODE_PROP_ATOMIC         (1<<31)
#define DRM_MODE_PROP_RANGE          (1<<1)

/* Atomic flags */
#define DRM_MODE_ATOMIC_TEST_ONLY    0x0100

#define DRM_DISPLAY_MODE_LEN 32
#define DRM_PROP_NAME_LEN 32

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[DRM_DISPLAY_MODE_LEN];
};

struct drm_mode_card_res {
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width, max_width;
    uint32_t min_height, max_height;
};

struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x, y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    struct drm_mode_modeinfo mode;
};

struct drm_mode_get_connector {
    uint64_t encoders_ptr;
    uint64_t modes_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mm_width, mm_height;
    uint32_t subpixel;
    uint32_t pad;
};

struct drm_mode_get_encoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
};

struct drm_mode_create_dumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
};

struct drm_mode_map_dumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};

struct drm_mode_destroy_dumb {
    uint32_t handle;
};

struct drm_mode_fb_cmd {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
};

struct drm_mode_fb_cmd2 {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t flags;
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
    uint64_t modifier[4];
};

struct drm_mode_crtc_page_flip {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t user_data;
};

struct drm_mode_get_property {
    uint64_t values_ptr;
    uint64_t enum_blob_ptr;
    uint32_t prop_id;
    uint32_t flags;
    char name[DRM_PROP_NAME_LEN];
    uint32_t count_values;
    uint32_t count_enum_blobs;
};

struct drm_mode_create_blob {
    uint64_t data;
    uint32_t length;
    uint32_t blob_id;
};

struct drm_mode_destroy_blob {
    uint32_t blob_id;
};

struct drm_mode_obj_get_properties {
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_props;
    uint32_t obj_id;
    uint32_t obj_type;
};

struct drm_mode_obj_set_property {
    uint64_t value;
    uint32_t prop_id;
    uint32_t obj_id;
    uint32_t obj_type;
};

struct drm_mode_atomic {
    uint32_t flags;
    uint32_t count_objs;
    uint64_t objs_ptr;
    uint64_t count_props_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint64_t reserved;
    uint64_t user_data;
};

struct drm_mode_get_plane_res {
    uint64_t plane_id_ptr;
    uint32_t count_planes;
};

struct drm_mode_get_plane {
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t possible_crtcs;
    uint32_t gamma_size;
    uint32_t count_format_types;
    uint64_t format_type_ptr;
};

struct drm_mode_set_plane {
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    int32_t crtc_x, crtc_y;
    uint32_t crtc_w, crtc_h;
    uint32_t src_x, src_y;
    uint32_t src_h, src_w;
};

#endif /* LIBDRM_ANDROID_DRM_MODE_H */

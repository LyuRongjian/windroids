#ifndef LIBDRM_ANDROID_DRM_H
#define LIBDRM_ANDROID_DRM_H

#include <stdint.h>
#include <sys/ioctl.h>

/* DRM IOCTLs */
#define DRM_IOCTL_BASE 'd'
#define DRM_IO(nr)             _IO(DRM_IOCTL_BASE, nr)
#define DRM_IOWR(nr,type)      _IOWR(DRM_IOCTL_BASE, nr, type)

/* Master control */
#define DRM_IOCTL_SET_MASTER       DRM_IO(0x1e)
#define DRM_IOCTL_DROP_MASTER      DRM_IO(0x1f)

/* Version */
#define DRM_IOCTL_VERSION          DRM_IOWR(0x00, struct drm_version)

struct drm_version {
    int version_major;
    int version_minor;
    int version_patchlevel;
    size_t name_len;
    char *name;
    size_t date_len;
    char *date;
    size_t desc_len;
    char *desc;
};

/* Capabilities */
#define DRM_IOCTL_GET_CAP          DRM_IOWR(0x0c, struct drm_get_cap)
#define DRM_IOCTL_SET_CLIENT_CAP   DRM_IOWR(0x0d, struct drm_set_client_cap)

#define DRM_CAP_DUMB_BUFFER             0x1
#define DRM_CAP_VBLANK_HIGH_CRTC        0x2
#define DRM_CAP_DUMB_PREFERRED_DEPTH    0x3
#define DRM_CAP_DUMB_PREFER_SHADOW      0x4
#define DRM_CAP_PRIME                   0x5
#define DRM_CAP_TIMESTAMP_MONOTONIC     0x6
#define DRM_CAP_ASYNC_PAGE_FLIP         0x7
#define DRM_CAP_CURSOR_WIDTH            0x8
#define DRM_CAP_CURSOR_HEIGHT           0x9
#define DRM_CAP_ADDFB2_MODIFIERS        0x10
#define DRM_CAP_CRTC_IN_VBLANK_EVENT    0x12
#define DRM_CAP_SYNCOBJ                 0x13

struct drm_get_cap {
    uint64_t capability;
    uint64_t value;
};

struct drm_set_client_cap {
    uint64_t capability;
    uint64_t value;
};

/* GEM */
#define DRM_IOCTL_GEM_CLOSE        DRM_IOWR(0x09, struct drm_gem_close)
#define DRM_IOCTL_GEM_FLINK        DRM_IOWR(0x0a, struct drm_gem_flink)
#define DRM_IOCTL_GEM_OPEN         DRM_IOWR(0x0b, struct drm_gem_open)

struct drm_gem_close {
    uint32_t handle;
    uint32_t pad;
};

struct drm_gem_flink {
    uint32_t handle;
    uint32_t name;
};

struct drm_gem_open {
    uint32_t name;
    uint32_t handle;
    uint64_t size;
};

/* Mode-Setting (前向声明，完整定义在 drm_mode.h) */
struct drm_mode_card_res;
struct drm_mode_crtc;
struct drm_mode_get_connector;
struct drm_mode_get_encoder;
struct drm_mode_create_dumb;
struct drm_mode_map_dumb;
struct drm_mode_destroy_dumb;
struct drm_mode_fb_cmd;
struct drm_mode_fb_cmd2;
struct drm_mode_crtc_page_flip;
struct drm_mode_get_property;
struct drm_mode_create_blob;
struct drm_mode_destroy_blob;
struct drm_mode_obj_get_properties;
struct drm_mode_obj_set_property;
struct drm_mode_atomic;
struct drm_mode_get_plane_res;
struct drm_mode_get_plane;
struct drm_mode_set_plane;
struct drm_mode_modeinfo;

#endif /* LIBDRM_ANDROID_DRM_H */
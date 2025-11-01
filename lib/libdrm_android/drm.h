#ifndef DRM_SHIM_H
#define DRM_SHIM_H

#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>

// ========== DRM ioctl 命令号（与上游 libdrm 一致） ==========
#define DRM_IOCTL_BASE              'd'
#define DRM_IO(nr)                  _IO(DRM_IOCTL_BASE, nr)
#define DRM_IOR(nr,type)            _IOR(DRM_IOCTL_BASE, nr, type)
#define DRM_IOW(nr,type)            _IOW(DRM_IOCTL_BASE, nr, type)
#define DRM_IOWR(nr,type)           _IOWR(DRM_IOCTL_BASE, nr, type)

// 基本DRM ioctl命令
#define DRM_IOCTL_VERSION           DRM_IOWR(0x00, struct drm_version)
#define DRM_IOCTL_GET_UNIQUE        DRM_IOWR(0x01, struct drm_unique)
#define DRM_IOCTL_GET_MAGIC         DRM_IOR( 0x02, struct drm_auth)
#define DRM_IOCTL_IRQ_BUSID         DRM_IOWR(0x03, struct drm_irq_busid)
#define DRM_IOCTL_GET_BUSID         DRM_IOWR(0x04, struct drm_busid)
#define DRM_IOCTL_SET_UNIQUE        DRM_IOW( 0x05, struct drm_unique)
#define DRM_IOCTL_AUTH_MAGIC        DRM_IOW( 0x06, struct drm_auth)

// Mode setting
#define DRM_IOCTL_MODE_GETRESOURCES DRM_IOWR(0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCRTC      DRM_IOWR(0xA1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC      DRM_IOWR(0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETENCODER   DRM_IOWR(0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR DRM_IOWR(0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_GETPROPERTY  DRM_IOWR(0xAA, struct drm_mode_get_property)
#define DRM_IOCTL_MODE_SETPROPERTY  DRM_IOWR(0xAB, struct drm_mode_connector_set_property)
#define DRM_IOCTL_MODE_GETPROPBLOB  DRM_IOWR(0xAC, struct drm_mode_get_blob)
#define DRM_IOCTL_MODE_GETFB        DRM_IOWR(0xAD, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_ADDFB        DRM_IOWR(0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_RMFB         DRM_IOWR(0xAF, unsigned int)
#define DRM_IOCTL_MODE_PAGE_FLIP    DRM_IOWR(0xB0, struct drm_mode_crtc_page_flip)
#define DRM_IOCTL_MODE_DIRTYFB      DRM_IOWR(0xB1, struct drm_mode_fb_dirty_cmd)

// Dumb buffer (软件渲染)
#define DRM_IOCTL_MODE_CREATE_DUMB  DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB     DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)

// Object properties
#define DRM_IOCTL_MODE_OBJ_GETPROPERTIES DRM_IOWR(0xB9, struct drm_mode_obj_get_properties)
#define DRM_IOCTL_MODE_OBJ_SETPROPERTY   DRM_IOWR(0xBA, struct drm_mode_obj_set_property)

// Atomic modesetting
#define DRM_IOCTL_MODE_ATOMIC        DRM_IOWR(0xBC, struct drm_mode_atomic)

// 添加Property Blob相关的ioctl命令
#define DRM_IOCTL_MODE_CREATEPROPBLOB   DRM_IOWR(0xBD, struct drm_mode_create_blob)
#define DRM_IOCTL_MODE_DESTROYPROPBLOB  DRM_IOWR(0xBE, struct drm_mode_destroy_blob)

// GEM (Graphics Execution Manager)
#define DRM_IOCTL_GEM_CLOSE         DRM_IOW(0x09, struct drm_gem_close)
#define DRM_IOCTL_GEM_FLINK         DRM_IOWR(0x0a, struct drm_gem_flink)
#define DRM_IOCTL_GEM_OPEN          DRM_IOWR(0x0b, struct drm_gem_open)

// Master 管理
#define DRM_IOCTL_SET_MASTER        DRM_IO(0x1e)
#define DRM_IOCTL_DROP_MASTER       DRM_IO(0x1f)

// Capabilities
#define DRM_IOCTL_GET_CAP           DRM_IOWR(0x0c, struct drm_get_cap)
#define DRM_IOCTL_SET_CLIENT_CAP    DRM_IOWR(0x0d, struct drm_set_client_cap)

// ========== 数据结构（与上游 libdrm 对齐） ==========
struct drm_version {
    int version_major;      /**< Major version */
    int version_minor;      /**< Minor version */
    int version_patchlevel; /**< Patch level */
    size_t name_len;        /**< Length of name buffer */
    char *name;             /**< Name of driver */
    size_t date_len;        /**< Length of date buffer */
    char *date;             /**< User-space buffer to hold date */
    size_t desc_len;        /**< Length of desc buffer */
    char *desc;             /**< User-space buffer to hold desc */
};

struct drm_unique {
    size_t unique_len;      /**< Length of unique */
    char *unique;           /**< Unique name for driver instantiation */
};

struct drm_auth {
    unsigned int magic;
};

struct drm_irq_busid {
    int irq;        /**< IRQ number */
    int busnum;     /**< bus number */
    int devnum;     /**< device number */
    int funcnum;    /**< function number */
};

struct drm_busid {
    int busnum;     /**< bus number */
    int devnum;     /**< device number */
    int funcnum;    /**< function number */
};

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[32];
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

struct drm_mode_get_encoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
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

struct drm_mode_get_property {
    uint64_t values_ptr;
    uint64_t enum_blob_ptr;
    uint32_t prop_id;
    uint32_t flags;
    char name[32];
    uint32_t count_values;
    uint32_t count_enum_blobs;
};

struct drm_mode_connector_set_property {
    uint64_t value;
    uint32_t prop_id;
    uint32_t connector_id;
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

struct drm_mode_get_blob {
    uint32_t blob_id;
    uint32_t length;
    uint64_t data;
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
    uint32_t width, height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
};

struct drm_mode_crtc_page_flip {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t user_data;
};

struct drm_mode_fb_dirty_cmd {
    uint32_t fb_id;
    uint32_t flags;
    uint32_t color;
    uint32_t num_clips;
    uint64_t clips_ptr;
};

// 添加Property Blob相关的数据结构
struct drm_mode_create_blob {
    uint64_t data;
    uint32_t length;
    uint32_t blob_id;
};

struct drm_mode_destroy_blob {
    uint32_t blob_id;
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

struct drm_get_cap {
    uint64_t capability;
    uint64_t value;
};

struct drm_set_client_cap {
    uint64_t capability;
    uint64_t value;
};

// Page flip flags
#define DRM_MODE_PAGE_FLIP_EVENT    0x01
#define DRM_MODE_PAGE_FLIP_ASYNC    0x02

// Connection status
#define DRM_MODE_CONNECTED          1
#define DRM_MODE_DISCONNECTED       2
#define DRM_MODE_UNKNOWNCONNECTION  3

// Connector types
#define DRM_MODE_CONNECTOR_Unknown      0
#define DRM_MODE_CONNECTOR_VGA          1
#define DRM_MODE_CONNECTOR_DVII         2
#define DRM_MODE_CONNECTOR_DVID         3
#define DRM_MODE_CONNECTOR_DVIA         4
#define DRM_MODE_CONNECTOR_Composite    5
#define DRM_MODE_CONNECTOR_SVIDEO       6
#define DRM_MODE_CONNECTOR_LVDS         7
#define DRM_MODE_CONNECTOR_Component    8
#define DRM_MODE_CONNECTOR_9PinDIN      9
#define DRM_MODE_CONNECTOR_DisplayPort  10
#define DRM_MODE_CONNECTOR_HDMIA        11
#define DRM_MODE_CONNECTOR_HDMIB        12
#define DRM_MODE_CONNECTOR_TV           13
#define DRM_MODE_CONNECTOR_eDP          14
#define DRM_MODE_CONNECTOR_VIRTUAL      15
#define DRM_MODE_CONNECTOR_DSI          16

// Capabilities
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
#define DRM_CAP_PAGE_FLIP_TARGET        0x11
#define DRM_CAP_CRTC_IN_VBLANK_EVENT    0x12
#define DRM_CAP_SYNCOBJ                 0x13
#define DRM_CAP_SYNCOBJ_TIMELINE        0x14

// Object types
#define DRM_MODE_OBJECT_CRTC            0xcccccccc
#define DRM_MODE_OBJECT_CONNECTOR       0xc0c0c0c0
#define DRM_MODE_OBJECT_ENCODER         0xe0e0e0e0
#define DRM_MODE_OBJECT_MODE            0xdededede
#define DRM_MODE_OBJECT_PROPERTY        0xb0b0b0b0
#define DRM_MODE_OBJECT_FB              0xfbfbfbfb
#define DRM_MODE_OBJECT_BLOB            0xbbbbbbbb
#define DRM_MODE_OBJECT_PLANE           0xeeeeeeee

// Atomic flags
#define DRM_MODE_ATOMIC_TEST_ONLY       0x0100
#define DRM_MODE_ATOMIC_NONBLOCK        0x0200
#define DRM_MODE_ATOMIC_ALLOW_MODESET   0x0400
#define DRM_MODE_ATOMIC_FLAGS           (DRM_MODE_ATOMIC_TEST_ONLY | \
                                         DRM_MODE_ATOMIC_NONBLOCK | \
                                         DRM_MODE_ATOMIC_ALLOW_MODESET)

// Plane management (wlroots atomic modesetting 必需)
#define DRM_IOCTL_MODE_GETPLANE         DRM_IOWR(0xB6, struct drm_mode_get_plane)
#define DRM_IOCTL_MODE_SETPLANE         DRM_IOWR(0xB7, struct drm_mode_set_plane)
#define DRM_IOCTL_MODE_GETPLANERESOURCES DRM_IOWR(0xB5, struct drm_mode_get_plane_res)

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

// Property flags (wlroots 需要识别)
#define DRM_MODE_PROP_PENDING           (1<<0)
#define DRM_MODE_PROP_RANGE             (1<<1)
#define DRM_MODE_PROP_IMMUTABLE         (1<<2)
#define DRM_MODE_PROP_ENUM              (1<<3)
#define DRM_MODE_PROP_BLOB              (1<<4)
#define DRM_MODE_PROP_BITMASK           (1<<5)
#define DRM_MODE_PROP_LEGACY_TYPE       (1<<6)
#define DRM_MODE_PROP_ATOMIC            (1<<31)

// 添加 DRM_IOCTL_MODE_ADDFB2 相关的内容
#define DRM_IOCTL_MODE_ADDFB2       DRM_IOWR(0xB8, struct drm_mode_fb_cmd2)

struct drm_mode_fb_cmd2 {
    uint32_t fb_id;
    uint32_t width, height;
    uint32_t pixel_format;  // DRM_FORMAT_*
    uint32_t flags;
    uint32_t handles[4];
    uint32_t pitches[4];
    uint32_t offsets[4];
    uint64_t modifier[4];
};

#endif /* DRM_SHIM_H */
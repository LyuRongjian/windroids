#ifndef _XF86DRM_H_
#define _XF86DRM_H_

#include <stdint.h>
#include "drm.h"

// 定义一些常量
#define DRM_NODE_PRIMARY 0
#define DRM_NODE_RENDER  2

// 定义一些结构体
typedef struct _drmDevice {
    char **nodes;
    int available_nodes;
    int bustype;
    union {
        void *pci;
        void *usb;
        void *platform;
        void *host1x;
    } businfo;
    union {
        void *pci;
        void *usb;
        void *platform;
        void *host1x;
    } deviceinfo;
} drmDevice, *drmDevicePtr;

typedef struct _drmVersion {
    int version_major;      /**< Major version */
    int version_minor;      /**< Minor version */
    int version_patchlevel; /**< Patch level */
    size_t name_len;        /**< Length of name buffer */
    char *name;             /**< Name of driver */
    size_t date_len;        /**< Length of date buffer */
    char *date;             /**< User-space buffer to hold date */
    size_t desc_len;        /**< Length of desc buffer */
    char *desc;             /**< User-space buffer to hold desc */
} drmVersion, *drmVersionPtr;

// ========== Atomic Modesetting API（wlroots 0.18+ 必需） ==========
typedef struct _drmModeAtomicReq drmModeAtomicReq, *drmModeAtomicReqPtr;

drmModeAtomicReqPtr drmModeAtomicAlloc(void);
void drmModeAtomicFree(drmModeAtomicReqPtr req);
int drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id,
                             uint32_t property_id, uint64_t value);
int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data);

// ========== Property API ==========
typedef struct _drmModeProperty {
    uint32_t prop_id;
    uint32_t flags;
    char name[32];
    uint32_t count_values;
    uint64_t *values;
    uint32_t count_enums;
    struct drm_mode_property_enum *enums;
    uint32_t count_blobs;
    uint32_t *blob_ids;
} drmModePropertyRes, *drmModePropertyPtr;

typedef struct _drmModePropertyBlob {
    uint32_t id;
    uint32_t length;
    void *data;
} drmModePropertyBlobRes, *drmModePropertyBlobPtr;

drmModePropertyPtr drmModeGetProperty(int fd, uint32_t propertyId);
void drmModeFreeProperty(drmModePropertyPtr ptr);
drmModePropertyBlobPtr drmModeGetPropertyBlob(int fd, uint32_t blob_id);
void drmModeFreePropertyBlob(drmModePropertyBlobPtr ptr);

// ========== Plane API ==========
typedef struct _drmModePlaneRes {
    uint32_t count_planes;
    uint32_t *planes;
} drmModePlaneRes, *drmModePlaneResPtr;

typedef struct _drmModePlane {
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t possible_crtcs;
    uint32_t gamma_size;
    uint32_t count_formats;
    uint32_t *formats;
} drmModePlane, *drmModePlanePtr;

drmModePlaneResPtr drmModeGetPlaneResources(int fd);
void drmModeFreePlaneResources(drmModePlaneResPtr ptr);
drmModePlanePtr drmModeGetPlane(int fd, uint32_t plane_id);
void drmModeFreePlane(drmModePlanePtr ptr);

// ========== Resource/Connector/Encoder API ==========
typedef struct _drmModeRes {
    uint32_t count_crtcs;
    uint32_t *crtcs;
    uint32_t count_connectors;
    uint32_t *connectors;
    uint32_t count_encoders;
    uint32_t *encoders;
    uint32_t min_width, max_width;
    uint32_t min_height, max_height;
} drmModeRes, *drmModeResPtr;

typedef struct drm_mode_modeinfo drmModeModeInfo, *drmModeModeInfoPtr;

typedef struct _drmModeConnector {
    uint32_t connector_id;
    uint32_t encoder_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mmWidth, mmHeight;
    uint32_t subpixel;
    uint32_t count_modes;
    drmModeModeInfoPtr modes;
    uint32_t count_props;
    uint32_t *props;
    uint64_t *prop_values;
    uint32_t count_encoders;
    uint32_t *encoders;
} drmModeConnector, *drmModeConnectorPtr;

typedef struct _drmModeEncoder {
    uint32_t encoder_id;
    uint32_t crtc_id;
    uint32_t encoder_type;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
} drmModeEncoder, *drmModeEncoderPtr;

drmModeResPtr drmModeGetResources(int fd);
void drmModeFreeResources(drmModeResPtr ptr);
drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connectorId);
void drmModeFreeConnector(drmModeConnectorPtr ptr);
drmModeEncoderPtr drmModeGetEncoder(int fd, uint32_t encoder_id);
void drmModeFreeEncoder(drmModeEncoderPtr ptr);

// ========== CRTC API ==========
typedef struct _drmModeCrtc {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x, y;
    uint32_t width, height;
    int mode_valid;
    drmModeModeInfo mode;
    int gamma_size;
} drmModeCrtc, *drmModeCrtcPtr;

drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t crtcId);
void drmModeFreeCrtc(drmModeCrtcPtr ptr);
int drmModeSetCrtc(int fd, uint32_t crtcId, uint32_t bufferId,
                   uint32_t x, uint32_t y, uint32_t *connectors, int count,
                   drmModeModeInfoPtr mode);

// 最小 API（wlroots 只用到这些）
extern int drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices);
extern void drmFreeDevices(drmDevicePtr devices[], int count);
extern int drmIsMaster(int fd);
extern int drmSetMaster(int fd);
extern int drmDropMaster(int fd);

// 添加更多函数声明
extern int drmOpen(const char *name, const char *busid);
extern int drmClose(int fd);
extern drmVersionPtr drmGetVersion(int fd);
extern void drmFreeVersion(drmVersionPtr version);
extern int drmGetCap(int fd, uint64_t capability, uint64_t *value);
extern int drmSetClientCap(int fd, uint64_t capability, uint64_t value);

// 添加Property Blob相关函数声明
extern int drmModeCreatePropertyBlob(int fd, const void *data, size_t length, uint32_t *id);
extern int drmModeDestroyPropertyBlob(int fd, uint32_t id);

#endif /* _XF86DRM_H_ */
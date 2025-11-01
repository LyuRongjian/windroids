/*
 * Minimal xf86drm API implementation for wlroots
 */

#include "xf86drm.h"
#include "drm.h"  // 添加这一行以引入 struct drm_mode_create_blob 等定义
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <errno.h>
#include <sys/stat.h>

#define LOG_TAG "xf86drm_shim"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 优化 drmGetDevices2：使用内建函数检测常量
__attribute__((hot))
int drmGetDevices2(uint32_t flags, drmDevicePtr devices[], int max_devices) {
    (void)flags;
    
    // 使用分支预测优化
    if (__builtin_expect(max_devices < 1, 0)) {
        return 0;
    }
    
    // 伪造单个 DRM 设备
    drmDevice *dev = calloc(1, sizeof(drmDevice));
    if (__builtin_expect(!dev, 0)) {
        return -1;
    }
    
    dev->available_nodes = 1;
    dev->nodes = calloc(3, sizeof(char*));
    
    // 使用静态常量字符串（编译器优化到 .rodata）
    static const char card0_path[] = "/dev/dri/card0";
    dev->nodes[DRM_NODE_PRIMARY] = strdup(card0_path);
    dev->nodes[DRM_NODE_RENDER] = NULL;
    dev->bustype = 0;
    
    devices[0] = dev;
    
    LOGI("drmGetDevices2: returning 1 fake device (%s)", card0_path);
    return 1;
}

void drmFreeDevices(drmDevicePtr devices[], int count) {
    for (int i = 0; i < count; i++) {
        if (devices[i]) {
            if (devices[i]->nodes) {
                for (int j = 0; j < 3; j++) {
                    free(devices[i]->nodes[j]);
                }
                free(devices[i]->nodes);
            }
            free(devices[i]);
        }
    }
}

__attribute__((const))
__attribute__((pure))  // 添加：告诉编译器函数无副作用（可缓存结果）
int drmIsMaster(int fd) {
    (void)fd;
    return 1;
}

int drmSetMaster(int fd) {
    // 通过 ioctl 劫持实现
    return ioctl(fd, DRM_IOCTL_SET_MASTER);
}

int drmDropMaster(int fd) {
    return ioctl(fd, DRM_IOCTL_DROP_MASTER);
}

// 添加更多函数实现
int drmOpen(const char *name, const char *busid) {
    LOGI("drmOpen: name=%s, busid=%s", name ? name : "NULL", busid ? busid : "NULL");
    
    // 模拟打开DRM设备
    return 100;  // 返回伪fd
}

int drmClose(int fd) {
    LOGI("drmClose: fd=%d", fd);
    
    // 模拟关闭DRM设备
    return 0;
}

__attribute__((malloc))
__attribute__((warn_unused_result))
__attribute__((cold))  // 添加：告诉编译器错误路径不常执行
drmVersionPtr drmGetVersion(int fd) {
    LOGD("drmGetVersion: fd=%d", fd);
    
    drmVersionPtr version = calloc(1, sizeof(drmVersion));
    if (__builtin_expect(!version, 0)) {
        LOGE("drmGetVersion: calloc failed");
        return NULL;
    }
    
    // 填充版本信息
    version->version_major = 2;
    version->version_minor = 4;
    version->version_patchlevel = 0;
    
    // 使用静态常量字符串（编译器优化）
    static const char* const drm_name = "android-drm-shim";
    static const char* const drm_desc = "Android DRM Shim for wlroots";
    
    version->name_len = strlen(drm_name);
    version->name = strdup(drm_name);
    if (__builtin_expect(!version->name, 0)) {
        free(version);
        return NULL;
    }
    
    version->desc_len = strlen(drm_desc);
    version->desc = strdup(drm_desc);
    if (__builtin_expect(!version->desc, 0)) {
        free(version->name);
        free(version);
        return NULL;
    }
    
    return version;
}

void drmFreeVersion(drmVersionPtr version) {
    if (version) {
        free(version->name);
        free(version->desc);
        free(version);
    }
}

__attribute__((pure))  // drmGetCap 结果仅依赖输入参数
int drmGetCap(int fd, uint64_t capability, uint64_t *value) {
    LOGD("drmGetCap: fd=%d, capability=%llu", fd, (unsigned long long)capability);
    
    if (!value) {
        return -EINVAL;
    }
    
    // 使用 __builtin_constant_p 优化常量 capability 查询
    if (__builtin_constant_p(capability)) {
        switch (capability) {
            case DRM_CAP_DUMB_BUFFER:
                *value = 1;
                return 0;
            case DRM_CAP_DUMB_PREFERRED_DEPTH:
                *value = 32;
                return 0;
            case DRM_CAP_TIMESTAMP_MONOTONIC:
                *value = 1;
                return 0;
            default:
                break;
        }
    }
    
    // 运行时查询路径
    switch (capability) {
        case DRM_CAP_DUMB_BUFFER:
            *value = 1;
            break;
        case DRM_CAP_DUMB_PREFERRED_DEPTH:
            *value = 32;
            break;
        case DRM_CAP_DUMB_PREFER_SHADOW:
            *value = 1;
            break;
        case DRM_CAP_PRIME:
            *value = 0;
            break;
        case DRM_CAP_TIMESTAMP_MONOTONIC:
            *value = 1;
            break;
        case DRM_CAP_CURSOR_WIDTH:
            *value = 64;
            break;
        case DRM_CAP_CURSOR_HEIGHT:
            *value = 64;
            break;
        default:
            LOGD("Unknown capability: %llu", (unsigned long long)capability);
            return -EINVAL;
    }
    
    return 0;
}

int drmSetClientCap(int fd, uint64_t capability, uint64_t value) {
    LOGD("drmSetClientCap: fd=%d, capability=%llu, value=%llu", 
         fd, (unsigned long long)capability, (unsigned long long)value);
    
    // 简单实现：接受所有设置
    return 0;
}

// 添加Property Blob相关函数实现
int drmModeCreatePropertyBlob(int fd, const void *data, size_t length, uint32_t *id) {
    struct drm_mode_create_blob create = {
        .data = (uint64_t)(uintptr_t)data,
        .length = (uint32_t)length,  // 修正：显式转换
        .blob_id = 0
    };
    
    int ret = ioctl(fd, DRM_IOCTL_MODE_CREATEPROPBLOB, &create);
    if (ret == 0 && id) {
        *id = create.blob_id;
    }
    return ret;
}

int drmModeDestroyPropertyBlob(int fd, uint32_t id) {
    struct drm_mode_destroy_blob destroy = {
        .blob_id = id
    };
    
    return ioctl(fd, DRM_IOCTL_MODE_DESTROYPROPBLOB, &destroy);
}

// 添加 drmModeAtomicCommit 支持
__attribute__((hot))
int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data) {
    if (!req || fd != 100) {
        return -EINVAL;
    }
    
    // 将 Atomic 请求转换为 DRM_IOCTL_MODE_ATOMIC
    struct drm_mode_atomic atomic = {
        .flags = flags,
        .count_objs = req->cursor,
        .objs_ptr = (uint64_t)(uintptr_t)req->objs,
        .count_props_ptr = (uint64_t)(uintptr_t)req->count_props,
        .props_ptr = (uint64_t)(uintptr_t)req->props,
        .prop_values_ptr = (uint64_t)(uintptr_t)req->values,
        .user_data = (uint64_t)(uintptr_t)user_data
    };
    
    return ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &atomic);
}

// 添加 Atomic 请求管理结构
typedef struct _drmModeAtomicReq {
    uint32_t cursor;
    uint32_t size_objs;
    uint32_t *objs;
    uint32_t *count_props;
    uint32_t size_props;
    uint32_t *props;
    uint64_t *values;
} drmModeAtomicReq;

drmModeAtomicReqPtr drmModeAtomicAlloc(void) {
    drmModeAtomicReqPtr req = calloc(1, sizeof(drmModeAtomicReq));
    if (!req) return NULL;
    
    req->size_objs = 16;
    req->objs = calloc(req->size_objs, sizeof(uint32_t));
    req->count_props = calloc(req->size_objs, sizeof(uint32_t));
    req->size_props = 256;
    req->props = calloc(req->size_props, sizeof(uint32_t));
    req->values = calloc(req->size_props, sizeof(uint64_t));
    
    if (!req->objs || !req->count_props || !req->props || !req->values) {
        drmModeAtomicFree(req);
        return NULL;
    }
    
    return req;
}

void drmModeAtomicFree(drmModeAtomicReqPtr req) {
    if (!req) return;
    free(req->objs);
    free(req->count_props);
    free(req->props);
    free(req->values);
    free(req);
}

int drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id,
                             uint32_t property_id, uint64_t value) {
    if (!req) return -EINVAL;
    
    // 动态扩容（如需）
    if (req->cursor >= req->size_props) {
        uint32_t new_size = req->size_props * 2;
        req->props = realloc(req->props, new_size * sizeof(uint32_t));
        req->values = realloc(req->values, new_size * sizeof(uint64_t));
        req->size_props = new_size;
    }
    
    // 查找或创建对象槽
    int obj_idx = -1;
    for (uint32_t i = 0; i < req->cursor; i++) {
        if (req->objs[i] == object_id) {
            obj_idx = i;
            break;
        }
    }
    
    if (obj_idx < 0) {
        if (req->cursor >= req->size_objs) {
            uint32_t new_size = req->size_objs * 2;
            req->objs = realloc(req->objs, new_size * sizeof(uint32_t));
            req->count_props = realloc(req->count_props, new_size * sizeof(uint32_t));
            req->size_objs = new_size;
        }
        obj_idx = req->cursor;
        req->objs[obj_idx] = object_id;
        req->count_props[obj_idx] = 0;
    }

    req->props[req->cursor] = property_id;
    req->values[req->cursor] = value;
    req->count_props[obj_idx]++;
    req->cursor++;
    
    return 0;
}

// 优化已完成：
// ✅ __attribute__((malloc))
// ✅ __attribute__((warn_unused_result))
// ✅ __builtin_expect() 错误路径标记
// ✅ __builtin_constant_p() 常量折叠
drmModeResPtr drmModeGetResources(int fd) {
    struct drm_mode_card_res res = {0};
    
    // 第一次调用获取数量
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
        return NULL;
    }
    
    drmModeResPtr r = calloc(1, sizeof(drmModeRes));
    if (!r) return NULL;
    
    r->count_crtcs = res.count_crtcs;
    r->count_connectors = res.count_connectors;
    r->count_encoders = res.count_encoders;
    r->min_width = res.min_width;
    r->max_width = res.max_width;
    r->min_height = res.min_height;
    r->max_height = res.max_height;
    
    // 分配并获取 CRTC/Connector/Encoder IDs
    if (r->count_crtcs > 0) {
        r->crtcs = calloc(r->count_crtcs, sizeof(uint32_t));
        res.crtc_id_ptr = (uint64_t)(uintptr_t)r->crtcs;
    }
    if (r->count_connectors > 0) {
        r->connectors = calloc(r->count_connectors, sizeof(uint32_t));
        res.connector_id_ptr = (uint64_t)(uintptr_t)r->connectors;
    }
    if (r->count_encoders > 0) {
        r->encoders = calloc(r->count_encoders, sizeof(uint32_t));
        res.encoder_id_ptr = (uint64_t)(uintptr_t)r->encoders;
    }
    
    ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res);
    
    return r;
}

void drmModeFreeResources(drmModeResPtr ptr) {
    if (!ptr) return;
    free(ptr->crtcs);
    free(ptr->connectors);
    free(ptr->encoders);
    free(ptr);
}

drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connectorId) {
    struct drm_mode_get_connector conn = {
        .connector_id = connectorId
    };
    
    // 第一次调用获取数量
    if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0) {
        return NULL;
    }
    
    drmModeConnectorPtr c = calloc(1, sizeof(drmModeConnector));
    if (!c) return NULL;
    
    c->connector_id = connectorId;
    c->encoder_id = conn.encoder_id;
    c->connector_type = conn.connector_type;
    c->connector_type_id = conn.connector_type_id;
    c->connection = conn.connection;
    c->mmWidth = conn.mm_width;
    c->mmHeight = conn.mm_height;
    c->count_modes = conn.count_modes;
    c->count_encoders = conn.count_encoders;
    
    // 分配modes数组
    if (c->count_modes > 0) {
        c->modes = calloc(c->count_modes, sizeof(drmModeModeInfo));
        conn.modes_ptr = (uint64_t)(uintptr_t)c->modes;
        
        ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn);
    }
    
    return c;
}

void drmModeFreeConnector(drmModeConnectorPtr ptr) {
    if (!ptr) return;
    free(ptr->modes);
    free(ptr->encoders);
    free(ptr->props);
    free(ptr->prop_values);
    free(ptr);
}

drmModeEncoderPtr drmModeGetEncoder(int fd, uint32_t encoder_id) {
    struct drm_mode_get_encoder enc = {
        .encoder_id = encoder_id
    };
    
    if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) != 0) {
        return NULL;
    }
    
    drmModeEncoderPtr e = calloc(1, sizeof(drmModeEncoder));
    if (!e) return NULL;
    
    e->encoder_id = encoder_id;
    e->crtc_id = enc.crtc_id;
    e->encoder_type = enc.encoder_type;
    e->possible_crtcs = enc.possible_crtcs;
    e->possible_clones = enc.possible_clones;
    
    return e;
}

void drmModeFreeEncoder(drmModeEncoderPtr ptr) {
    free(ptr);
}

// ========== Property API 实现 ==========
drmModePropertyPtr drmModeGetProperty(int fd, uint32_t propertyId) {
    struct drm_mode_get_property prop = {
        .prop_id = propertyId,
        .count_values = 0,
        .count_enum_blobs = 0
    };
    
    if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &prop) != 0) {
        return NULL;
    }
    
    drmModePropertyPtr p = calloc(1, sizeof(drmModePropertyRes));
    if (!p) return NULL;
    
    p->prop_id = propertyId;
    p->flags = prop.flags;
    memcpy(p->name, prop.name, sizeof(p->name));
    p->count_values = prop.count_values;
    p->count_enums = prop.count_enum_blobs;
    
    // 分配 values 和 enums 数组（简化实现：不拷贝实际数据）
    if (p->count_values > 0) {
        p->values = calloc(p->count_values, sizeof(uint64_t));
    }
    if (p->count_enums > 0) {
        p->enums = calloc(p->count_enums, sizeof(struct drm_mode_property_enum));
    }
    
    return p;
}

void drmModeFreeProperty(drmModePropertyPtr ptr) {
    if (!ptr) return;
    free(ptr->values);
    free(ptr->enums);
    free(ptr->blob_ids);
    free(ptr);
}

drmModePropertyBlobPtr drmModeGetPropertyBlob(int fd, uint32_t blob_id) {
    struct drm_mode_get_blob blob = {
        .blob_id = blob_id,
        .length = 0,
        .data = 0
    };
    
    // 第一次调用获取长度
    if (ioctl(fd, DRM_IOCTL_MODE_GETPROPBLOB, &blob) != 0) {
        return NULL;
    }
    
    drmModePropertyBlobPtr b = calloc(1, sizeof(drmModePropertyBlobRes) + blob.length);
    if (!b) return NULL;
    
    b->id = blob_id;
    b->length = blob.length;
    b->data = (void *)(b + 1);  // 数据紧跟结构体
    
    // 第二次调用获取数据
    blob.data = (uint64_t)(uintptr_t)b->data;
    ioctl(fd, DRM_IOCTL_MODE_GETPROPBLOB, &blob);
    
    return b;
}

void drmModeFreePropertyBlob(drmModePropertyBlobPtr ptr) {
    free(ptr);
}

// ========== Plane API 实现 ==========
drmModePlaneResPtr drmModeGetPlaneResources(int fd) {
    struct drm_mode_get_plane_res res = {0};
    
    // 第一次调用获取数量
    if (ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &res) != 0) {
        return NULL;
    }
    
    drmModePlaneResPtr r = calloc(1, sizeof(drmModePlaneRes));
    if (!r) return NULL;
    
    r->count_planes = res.count_planes;
    if (r->count_planes > 0) {
        r->planes = calloc(r->count_planes, sizeof(uint32_t));
        res.plane_id_ptr = (uint64_t)(uintptr_t)r->planes;
        
        // 第二次调用获取 plane IDs
        ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &res);
    }
    
    return r;
}

void drmModeFreePlaneResources(drmModePlaneResPtr ptr) {
    if (!ptr) return;
    free(ptr->planes);
    free(ptr);
}

drmModePlanePtr drmModeGetPlane(int fd, uint32_t plane_id) {
    struct drm_mode_get_plane plane = {
        .plane_id = plane_id
    };
    
    if (ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &plane) != 0) {
        return NULL;
    }
    
    drmModePlanePtr p = calloc(1, sizeof(drmModePlane));
    if (!p) return NULL;
    
    p->plane_id = plane.plane_id;
    p->crtc_id = plane.crtc_id;
    p->fb_id = plane.fb_id;
    p->possible_crtcs = plane.possible_crtcs;
    p->gamma_size = plane.gamma_size;
    p->count_formats = plane.count_format_types;
    
    if (p->count_formats > 0) {
        p->formats = calloc(p->count_formats, sizeof(uint32_t));
        plane.format_type_ptr = (uint64_t)(uintptr_t)p->formats;
        
        ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &plane);
    }
    
    return p;
}

void drmModeFreePlane(drmModePlanePtr ptr) {
    if (!ptr) return;
    free(ptr->formats);
    free(ptr);
}

// ========== CRTC API 实现 ==========
drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t crtcId) {
    struct drm_mode_crtc crtc = {
        .crtc_id = crtcId
    };
    
    if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) != 0) {
        return NULL;
    }
    
    drmModeCrtcPtr c = calloc(1, sizeof(drmModeCrtc));
    if (!c) return NULL;
    
    c->crtc_id = crtc.crtc_id;
    c->fb_id = crtc.fb_id;
    c->x = crtc.x;
    c->y = crtc.y;
    c->mode_valid = crtc.mode_valid;
    c->mode = crtc.mode;
    c->gamma_size = crtc.gamma_size;
    
    return c;
}

void drmModeFreeCrtc(drmModeCrtcPtr ptr) {
    free(ptr);
}

int drmModeSetCrtc(int fd, uint32_t crtcId, uint32_t bufferId,
                   uint32_t x, uint32_t y, uint32_t *connectors, int count,
                   drmModeModeInfoPtr mode) {
    struct drm_mode_crtc crtc = {
        .crtc_id = crtcId,
        .fb_id = bufferId,
        .x = x,
        .y = y,
        .set_connectors_ptr = (uint64_t)(uintptr_t)connectors,
        .count_connectors = (uint32_t)count
    };
    
    if (mode) {
        crtc.mode = *mode;
        crtc.mode_valid = 1;
    }
    
    return ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &crtc);
}
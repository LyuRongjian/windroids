/*
 * DRM Shim Layer for Android
 * 
 * Purpose: Intercept wlroots DRM/KMS calls and redirect to ANativeWindow
 * Performance: Zero-copy where possible, NEON-optimized fallback
 * Thread-safe: Uses atomic operations for shared state
 */

#define _GNU_SOURCE
#include "drm.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <android/native_window.h>
#include <android/log.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <sys/ioctl.h>  // ioctl prototype for NDK clang

#ifdef __ANDROID__
#include <time.h>
#include <cpu-features.h>

// ========== 提前定义 CPU 特性检测（避免前向引用错误） ==========
static uint64_t g_cpu_features = 0;
static AndroidCpuFamily g_cpu_family = ANDROID_CPU_FAMILY_UNKNOWN;

__attribute__((constructor))
static void init_cpu_features(void) {
    g_cpu_family = android_getCpuFamily();
    g_cpu_features = android_getCpuFeatures();
    
    if (g_cpu_family == ANDROID_CPU_FAMILY_ARM64) {
        LOGI("Detected ARM64 CPU with features: 0x%llx", 
             (unsigned long long)g_cpu_features);
        
        if (g_cpu_features & ANDROID_CPU_ARM64_FEATURE_ASIMD) {
            LOGI("✓ ASIMD (Advanced SIMD) available");
        }
        if (g_cpu_features & ANDROID_CPU_ARM64_FEATURE_CRC32) {
            LOGI("✓ CRC32 instructions available");
        }
        if (g_cpu_features & ANDROID_CPU_ARM64_FEATURE_PMULL) {
            LOGI("✓ PMULL (Polynomial Multiply) available");
        }
    }
}

#define HAS_NEON() ((g_cpu_family == ANDROID_CPU_FAMILY_ARM64 && \
                     (g_cpu_features & ANDROID_CPU_ARM64_FEATURE_ASIMD)) || \
                    (g_cpu_family == ANDROID_CPU_FAMILY_ARM && \
                     (g_cpu_features & ANDROID_CPU_ARM_FEATURE_NEON)))
#else
#define HAS_NEON() 1
#endif


#define LOG_TAG "drm_shim"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
// 在文件开头添加错误处理宏
#define HANDLE_ERROR(err, msg) do { \
    LOGE("%s: %s", msg, strerror(err)); \
    errno = err; \
    return -1; \
} while(0)

// ========== 添加分支预测宏 ==========
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// ========== 全局状态（线程安全） ==========
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int (*real_open)(const char*, int, ...) = NULL;
static int (*real_ioctl)(int, unsigned long, ...) = NULL;
static void* (*real_mmap)(void*, size_t, int, int, int, off_t) = NULL;

// 窗口管理
static ANativeWindow* g_window = NULL;
static ANativeWindow_Buffer g_buffer;
static int g_window_width = 1920;
static int g_window_height = 1080;

// DRM 对象池
#define MAX_DUMB_BUFFERS 8
typedef struct {
    uint32_t handle;
    uint32_t width, height, bpp;
    uint64_t size;
    uint32_t pitch;
    void*    vaddr;
    int      in_use;
    int      is_mapped;  // 标记是否已 mmap
    void* ahb;  // AHardwareBuffer* 指针
} dumb_buffer_t;

static dumb_buffer_t g_dumb_buffers[MAX_DUMB_BUFFERS] = {0};

// 使用 atomic_uint_fast32_t 替代 _Atomic uint32_t（性能更好）
static atomic_uint_fast32_t g_next_handle = ATOMIC_VAR_INIT(1);
static atomic_uint_fast32_t g_next_fb_id = ATOMIC_VAR_INIT(1);
static atomic_uint_fast32_t g_next_blob_id = ATOMIC_VAR_INIT(1000);

// 添加 blob 管理结构
typedef struct {
    uint32_t id;
    void* data;
    size_t length;
    int in_use;
} blob_t;

#define MAX_BLOBS 64
static blob_t g_blobs[MAX_BLOBS] = {0};

// 添加 Plane 管理
static uint32_t g_plane_id = 1;

// ========== 简化：单显示器 + 多窗口支持 ==========
// 移除 g_num_displays，始终使用单个 CRTC/Connector
static uint32_t g_crtc_id = 1;
static uint32_t g_connector_id = 1;
static uint32_t g_encoder_id = 1;
static uint32_t g_current_fb_handle = 0;  // 添加这一行

// 窗口栈管理（用于多窗口渲染）
#define MAX_WINDOWS 8
typedef struct {
    uint32_t fb_id;
    int      x, y;           // 在 ANativeWindow 中的位置
    int      width, height;
    int      z_order;        // 渲染顺序（0=最底层）
    int      active;
} window_t;

static window_t g_windows[MAX_WINDOWS] = {0};
static int g_num_windows = 0;

// 移除 init_virtual_displays()，使用简化的初始化
__attribute__((constructor))
static void drm_shim_init(void) {
    real_open = dlsym(RTLD_NEXT, "open");
    real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    real_mmap = dlsym(RTLD_NEXT, "mmap");
    
    // 使用 NDK 内存屏障确保初始化完成
    __sync_synchronize();  // 全内存屏障
    
    if (!real_open || !real_ioctl || !real_mmap) {
        LOGE("Failed to find real open/ioctl/mmap");
    }
    
    // 不再初始化多显示器，仅保留单 CRTC/Connector
    LOGI("DRM shim initialized (single display, multi-window support)");
}

// 窗口管理函数（优化：按访问频率排序）
static window_t* find_window_by_fb(uint32_t fb_id) {
    // 热路径优化：最近访问的窗口通常在前面
    for (int i = 0; i < g_num_windows; i++) {
        if (likely(g_windows[i].active) && g_windows[i].fb_id == fb_id) {
            // 预取下一个窗口（减少 cache miss）
            if (i + 1 < g_num_windows) {
                __builtin_prefetch(&g_windows[i + 1], 0, 3);
            }
            return &g_windows[i];
        }
    }
    return NULL;
}

static window_t* alloc_window(uint32_t fb_id) {
    if (g_num_windows >= MAX_WINDOWS) {
        LOGE("Window pool exhausted");
        return NULL;
    }
    
    window_t* win = &g_windows[g_num_windows];
    win->fb_id = fb_id;
    win->x = win->y = 0;
    win->width = g_window_width;
    win->height = g_window_height;
    win->z_order = g_num_windows;
    win->active = 1;
    g_num_windows++;
    
    LOGI("Allocated window: fb_id=%u z_order=%d", fb_id, win->z_order);
    return win;
}

// ========== Dumb Buffer 管理 ==========
static dumb_buffer_t* find_dumb_buffer(uint32_t handle) {
    for (int i = 0; i < MAX_DUMB_BUFFERS; i++) {
        if (g_dumb_buffers[i].in_use && g_dumb_buffers[i].handle == handle) {
            return &g_dumb_buffers[i];
        }
    }
    return NULL;
}

static dumb_buffer_t* alloc_dumb_buffer(void) {
    // 使用位掩码快速查找空闲槽
    uint32_t free_mask = 0;
    for (int i = 0; i < MAX_DUMB_BUFFERS; i++) {
        if (!g_dumb_buffers[i].in_use) {
            free_mask |= (1U << i);
        }
    }
    
    if (free_mask == 0) {
        return NULL;
    }
    
    // 优化：使用 __builtin_ctz (Count Trailing Zeros) 找到第一个空闲位
    int idx = __builtin_ctz(free_mask);
    
    // 预取下一个可能的空闲槽（提升后续分配速度）
    if (__builtin_popcountll(free_mask) > 1) {  // 如果还有多个空闲槽
        uint32_t next_free = free_mask & ~(1U << idx);
        int next_idx = __builtin_ctz(next_free);
        __builtin_prefetch(&g_dumb_buffers[next_idx], 0, 1);
    }
    
    memset(&g_dumb_buffers[idx], 0, sizeof(dumb_buffer_t));
    g_dumb_buffers[idx].in_use = 1;
    return &g_dumb_buffers[idx];
}

// ========== 使用 NDK __builtin_clz 优化 buffer 查找 ==========
static dumb_buffer_t* find_dumb_buffer_optimized(uint32_t handle) {
    // 使用位操作优化：假设 handle 连续分配
    if (unlikely(handle == 0 || handle >= g_next_handle)) {
        return NULL;
    }
    
    // 直接索引查找（假设 handle 从 1 开始且连续）
    uint32_t index = handle - 1;
    if (likely(index < MAX_DUMB_BUFFERS && g_dumb_buffers[index].in_use)) {
        return &g_dumb_buffers[index];
    }
    
    // 回退到线性查找
    for (int i = 0; i < MAX_DUMB_BUFFERS; i++) {
        if (g_dumb_buffers[i].in_use && g_dumb_buffers[i].handle == handle) {
            return &g_dumb_buffers[i];
        }
    }
    return NULL;
}

// 替换原有的 find_dumb_buffer 调用
#define find_dumb_buffer find_dumb_buffer_optimized

// ========== NEON 优化的拷贝（利用 NDK 编译器特性） ==========
#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>

__attribute__((hot))
__attribute__((always_inline))  // 强制内联以减少函数调用开销
static inline void neon_copy_optimized(const uint32_t* __restrict src, 
                                      uint32_t* __restrict dst, 
                                      int count) {
    if (!HAS_NEON()) {
        memcpy(dst, src, count * 4);
        return;
    }
    
    // 移除错误的对齐假设（ANativeWindow buffer 可能未对齐）
    // src = (const uint32_t*)__builtin_assume_aligned(src, 16);
    // dst = (uint32_t*)__builtin_assume_aligned(dst, 16);
    
    // 使用 __builtin_prefetch 优化缓存
    __builtin_prefetch(src, 0, 3);
    __builtin_prefetch(dst, 1, 3);
    
    int n = count;
    
    // 利用 __restrict 让编译器假设 src 和 dst 不重叠（提升向量化）
    for (; n >= 64; n -= 64, src += 64, dst += 64) {
        __builtin_prefetch(src + 128, 0, 3);
        
        // 使用 v
        #if __ARM_FEATURE_DOTPROD  // ARMv8.2+
        uint32x4x4_t v0 = vld1q_u32_x4(src);
        uint32x4x4_t v1 = vld1q_u32_x4(src + 16);
        uint32x4x4_t v2 = vld1q_u32_x4(src + 32);
        uint32x4x4_t v3 = vld1q_u32_x4(src + 48);
        
        vst1q_u32_x4(dst, v0);
        vst1q_u32_x4(dst + 16, v1);
        vst1q_u32_x4(dst + 32, v2);
        vst1q_u32_x4(dst + 48, v3);
        #else
        // 回退到逐个加载（ARM64 标准）
        uint32x4_t v0  = vld1q_u32(src);
        uint32x4_t v1  = vld1q_u32(src + 4);
        uint32x4_t v2  = vld1q_u32(src + 8);
        uint32x4_t v3  = vld1q_u32(src + 12);
        uint32x4_t v4  = vld1q_u32(src + 16);
        uint32x4_t v5  = vld1q_u32(src + 20);
        uint32x4_t v6  = vld1q_u32(src + 24);
        uint32x4_t v7  = vld1q_u32(src + 28);
        uint32x4_t v8  = vld1q_u32(src + 32);
        uint32x4_t v9  = vld1q_u32(src + 36);
        uint32x4_t v10 = vld1q_u32(src + 40);
        uint32x4_t v11 = vld1q_u32(src + 44);
        uint32x4_t v12 = vld1q_u32(src + 48);
        uint32x4_t v13 = vld1q_u32(src + 52);
        uint32x4_t v14 = vld1q_u32(src + 56);
        uint32x4_t v15 = vld1q_u32(src + 60);
        
        // 使用 Non-temporal store（绕过 cache，适用于大块拷贝）
        vst1q_u32(dst, v0);
        vst1q_u32(dst + 4, v1);
        vst1q_u32(dst + 8, v2);
        vst1q_u32(dst + 12, v3);
        vst1q_u32(dst + 16, v4);
        vst1q_u32(dst + 20, v5);
        vst1q_u32(dst + 24, v6);
        vst1q_u32(dst + 28, v7);
        vst1q_u32(dst + 32, v8);
        vst1q_u32(dst + 36, v9);
        vst1q_u32(dst + 40, v10);
        vst1q_u32(dst + 44, v11);
        vst1q_u32(dst + 48, v12);
        vst1q_u32(dst + 52, v13);
        vst1q_u32(dst + 56, v14);
        vst1q_u32(dst + 60, v15);
        #endif
    }
    
    // 32 像素并行
    for (; n >= 32; n -= 32, src += 32, dst += 32) {
        __builtin_prefetch(src + 64, 0, 3);
        
        uint32x4_t v0 = vld1q_u32(src);
        uint32x4_t v1 = vld1q_u32(src + 4);
        uint32x4_t v2 = vld1q_u32(src + 8);
        uint32x4_t v3 = vld1q_u32(src + 12);
        uint32x4_t v4 = vld1q_u32(src + 16);
        uint32x4_t v5 = vld1q_u32(src + 20);
        uint32x4_t v6 = vld1q_u32(src + 24);
        uint32x4_t v7 = vld1q_u32(src + 28);
        
        vst1q_u32(dst, v0);
        vst1q_u32(dst + 4, v1);
        vst1q_u32(dst + 8, v2);
        vst1q_u32(dst + 12, v3);
        vst1q_u32(dst + 16, v4);
        vst1q_u32(dst + 20, v5);
        vst1q_u32(dst + 24, v6);
        vst1q_u32(dst + 28, v7);
    }
    
    // 16 像素并行
    for (; n >= 16; n -= 16, src += 16, dst += 16) {
        __builtin_prefetch(src + 16, 0, 3);
        uint32x4_t v0 = vld1q_u32(src);
        uint32x4_t v1 = vld1q_u32(src + 4);
        uint32x4_t v2 = vld1q_u32(src + 8);
        uint32x4_t v3 = vld1q_u32(src + 12);
        vst1q_u32(dst, v0);
        vst1q_u32(dst + 4, v1);
        vst1q_u32(dst + 8, v2);
        vst1q_u32(dst + 12, v3);
    }
    
    // 4 像素并行
    for (; n >= 4; n -= 4, src += 4, dst += 4) {
        vst1q_u32(dst, vld1q_u32(src));
    }
    
    // 剩余像素（标量处理）
    while (n--) *dst++ = *src++;
}
#else
static void neon_copy_optimized(const uint32_t* src, uint32_t* dst, int count) {
    memcpy(dst, src, count * 4);
}
#endif

// NEON 优化的 memcpy（用于 CREATEPROPBLOB）
#if defined(__ARM_NEON) || defined(__aarch64__)
static inline void neon_memcpy_aligned(void* dst, const void* src, size_t size) {
    if (!HAS_NEON() || size < 64) {
        memcpy(dst, src, size);
        return;
    }
    
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    size_t n = size;
    
    // 64 字节并行
    for (; n >= 64; n -= 64, s += 64, d += 64) {
        __builtin_prefetch(s + 128, 0, 3);
        
        uint8x16_t v0 = vld1q_u8(s);
        uint8x16_t v1 = vld1q_u8(s + 16);
        uint8x16_t v2 = vld1q_u8(s + 32);
        uint8x16_t v3 = vld1q_u8(s + 48);
        
        vst1q_u8(d, v0);
        vst1q_u8(d + 16, v1);
        vst1q_u8(d + 32, v2);
        vst1q_u8(d + 48, v3);
    }
    
    // 剩余部分
    if (n > 0) {
        memcpy(d, s, n);
    }
}
#else
#define neon_memcpy_aligned memcpy
#endif

// ========== open() 劫持 ==========
int open(const char* path, int flags, ...) {
    if (!real_open) {
        real_open = dlsym(RTLD_NEXT, "open");
        if (!real_open) {
            LOGE("Failed to find real open");
            errno = ENOSYS;
            return -1;
        }
    }
    
    // 拦截 DRM 设备
    if (path && (strcmp(path, "/dev/dri/card0") == 0 || 
                 strcmp(path, "/dev/dri/card1") == 0 ||
                 strncmp(path, "/dev/dri/renderD", 16) == 0)) {
        LOGI("Intercepted open(%s) -> fd=100", path);
        return 100;  // 伪 fd
    }
    
    // 处理可变参数（mode）
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
        return real_open(path, flags, mode);
    }
    
    return real_open(path, flags);
}

// 使用简单的哈希表（blob_id % MAX_BLOBS）加速查找
static blob_t* find_blob(uint32_t id) {
    // 热路径优化：直接哈希查找
    uint32_t hash = id % MAX_BLOBS;
    
    // 预取哈希槽（减少 cache miss）
    __builtin_prefetch(&g_blobs[hash], 0, 3);
    
    // 使用 __builtin_clz 优化探测步长（减少冲突）
    uint32_t step = 1 + (__builtin_clz(id) & 0x3);  // 步长范围 1-4
    
    // 从哈希槽开始以 step 步长探测（减少聚集）
    for (int i = 0; i < MAX_BLOBS; i++) {
        uint32_t idx = (hash + i * step) % MAX_BLOBS;
        
        // 预取下一个槽
        if (i + 1 < MAX_BLOBS) {
            uint32_t next_idx = (hash + (i + 1) * step) % MAX_BLOBS;
            __builtin_prefetch(&g_blobs[next_idx], 0, 3);
        }
        
        if (g_blobs[idx].in_use && g_blobs[idx].id == id) {
            return &g_blobs[idx];
        }
        
        // 空槽：停止搜索
        if (!g_blobs[idx].in_use) {
            break;
        }
    }
    return NULL;
}

// 创建新的blob
static blob_t* create_blob() {
    for (int i = 0; i < MAX_BLOBS; i++) {
        if (!g_blobs[i].in_use) {
            g_blobs[i].in_use = 1;
            g_blobs[i].id = atomic_fetch_add_explicit(&g_next_blob_id, 1, memory_order_relaxed);
            return &g_blobs[i];
        }
    }
    return NULL;
}

// ========== mmap() 劫持（关键：wlroots 会调用 mmap 映射 dumb buffer） ==========
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (!real_mmap) {
        real_mmap = dlsym(RTLD_NEXT, "mmap");
    }
    
    // 拦截 DRM fd 的 mmap（offset 是 MAP_DUMB 返回的虚拟地址）
    if (fd == 100 && offset != 0) {
        pthread_mutex_lock(&g_lock);
        
        // offset 实际是 vaddr（见 MAP_DUMB 实现）
        void* vaddr = (void*)(uintptr_t)offset;
        
        // 验证是否是合法的 dumb buffer
        dumb_buffer_t* buf = NULL;
        for (int i = 0; i < MAX_DUMB_BUFFERS; i++) {
            if (g_dumb_buffers[i].in_use && g_dumb_buffers[i].vaddr == vaddr) {
                buf = &g_dumb_buffers[i];
                break;
            }
        }
        
        pthread_mutex_unlock(&g_lock);
        
        if (buf) {
            LOGD("mmap: DRM buffer handle=%u vaddr=%p size=%llu", 
                 buf->handle, vaddr, (unsigned long long)buf->size);
            buf->is_mapped = 1;
            return vaddr;  // 直接返回已分配的内存
        }
        
        LOGE("mmap: invalid DRM buffer offset=%lx", (unsigned long)offset);
        errno = EINVAL;
        return MAP_FAILED;
    }
    
    // 其他 mmap 调用转发
    return real_mmap(addr, length, prot, flags, fd, offset);
}

// ========== close() 劫持（资源清理）==========
int close(int fd) {
    static int (*real_close)(int) = NULL;
    if (!real_close) {
        real_close = dlsym(RTLD_NEXT, "close");
    }
    
    if (fd == 100) {
        pthread_mutex_lock(&g_lock);
        
        // 清理所有 dumb buffers
        for (int i = 0; i < MAX_DUMB_BUFFERS; i++) {
            if (g_dumb_buffers[i].in_use) {
                if (g_dumb_buffers[i].vaddr) {
                    free(g_dumb_buffers[i].vaddr);
                }
                g_dumb_buffers[i].in_use = 0;
            }
        }
        
        // 清理所有 blobs
        for (int i = 0; i < MAX_BLOBS; i++) {
            if (g_blobs[i].in_use) {
                free(g_blobs[i].data);
                g_blobs[i].in_use = 0;
            }
        }
        
        LOGI("DRM fd 100 closed, all resources freed");
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    
    return real_close(fd);
}

// ========== ioctl() 劫持（修正版）==========
__attribute__((hot))
int ioctl(int fd, unsigned long request, ...) {
    if (!real_ioctl) {
        real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    }
    
    // 利用分支预测：DRM fd 是热路径
    if (unlikely(fd != 100)) {
        va_list args;
        va_start(args, request);
        void* argp = va_arg(args, void*);
        va_end(args);
        return real_ioctl(fd, request, argp);
    }
    
    va_list args;
    va_start(args, request);
    void* argp = va_arg(args, void*);
    va_end(args);
    
    pthread_mutex_lock(&g_lock);
    int ret = 0;
    
    switch (request) {
        case DRM_IOCTL_SET_MASTER:
            LOGD("SET_MASTER: fd=%d (fake success)", fd);
            ret = 0;
            break;
        
        case DRM_IOCTL_DROP_MASTER:
            LOGD("DROP_MASTER: fd=%d (fake success)", fd);
            ret = 0;
            break;
        
        case DRM_IOCTL_MODE_GETRESOURCES: {
            struct drm_mode_card_res* res = (struct drm_mode_card_res*)argp;
            
            // 始终返回单个 CRTC/Connector（简化）
            if (res->crtc_id_ptr) {
                ((uint32_t*)(uintptr_t)res->crtc_id_ptr)[0] = g_crtc_id;
            }
            if (res->connector_id_ptr) {
                ((uint32_t*)(uintptr_t)res->connector_id_ptr)[0] = g_connector_id;
            }
            if (res->encoder_id_ptr) {
                ((uint32_t*)(uintptr_t)res->encoder_id_ptr)[0] = g_encoder_id;
            }
            
            res->count_fbs = 0;
            res->count_crtcs = 1;
            res->count_connectors = 1;
            res->count_encoders = 1;
            res->min_width = res->max_width = g_window_width;
            res->min_height = res->max_height = g_window_height;
            
            LOGD("GETRESOURCES -> 1 CRTC/connector (%dx%d)", 
                 g_window_width, g_window_height);
            break;
        }
        
        case DRM_IOCTL_MODE_GETCRTC: {
            struct drm_mode_crtc* crtc = (struct drm_mode_crtc*)argp;
            
            // 简化：直接返回当前 FB（不查询虚拟显示器）
            crtc->crtc_id = g_crtc_id;
            crtc->fb_id = (g_next_fb_id > 1) ? (g_next_fb_id - 1) : 0;  // 修复：补全三元表达式
            crtc->x = crtc->y = 0;
            crtc->mode_valid = 1;
            crtc->mode.hdisplay = g_window_width;
            crtc->mode.vdisplay = g_window_height;
            crtc->mode.vrefresh = 60;
            snprintf(crtc->mode.name, sizeof(crtc->mode.name), 
                    "%dx%d@60", g_window_width, g_window_height);
            
            LOGD("GETCRTC: crtc_id=%u fb_id=%u", crtc->crtc_id, crtc->fb_id);
            break;
        }
        
        case DRM_IOCTL_MODE_SETCRTC: {
            struct drm_mode_crtc* crtc = (struct drm_mode_crtc*)argp;
            
            if (crtc->fb_id) {
                g_current_fb_handle = crtc->fb_id;
            }
            
            LOGD("SETCRTC: crtc_id=%u fb_id=%u", crtc->crtc_id, crtc->fb_id);
            break;
        }
        
        case DRM_IOCTL_MODE_GETCONNECTOR: {
            struct drm_mode_get_connector* conn = (struct drm_mode_get_connector*)argp;
            
            // 简化：始终返回单个连接的虚拟显示器
            conn->encoder_id = g_encoder_id;
            conn->connector_type = DRM_MODE_CONNECTOR_VIRTUAL;
            conn->connector_type_id = 1;
            conn->connection = DRM_MODE_CONNECTED;
            conn->mm_width = conn->mm_height = 0;
            conn->subpixel = 0;
            
            if (conn->count_modes == 0) {
                conn->count_modes = 1;
            } else if (conn->modes_ptr) {
                struct drm_mode_modeinfo* mode = (struct drm_mode_modeinfo*)(uintptr_t)conn->modes_ptr;
                memset(mode, 0, sizeof(*mode));
                mode->hdisplay = g_window_width;
                mode->vdisplay = g_window_height;
                mode->vrefresh = 60;
                snprintf(mode->name, sizeof(mode->name), "%dx%d@60", 
                        g_window_width, g_window_height);
            }
            
            LOGD("GETCONNECTOR: conn_id=%u -> CONNECTED, %dx%d", 
                 conn->connector_id, g_window_width, g_window_height);
            break;
        }
        
        case DRM_IOCTL_MODE_GETENCODER: {
            struct drm_mode_get_encoder* enc = (struct drm_mode_get_encoder*)argp;
            enc->crtc_id = g_crtc_id;
            enc->encoder_type = 1;
            enc->possible_crtcs = 1;
            enc->possible_clones = 0;
            break;
        }
        
        case DRM_IOCTL_MODE_CREATE_DUMB: {
            TRACE_BEGIN("drm_create_dumb");  // 添加追踪点
            
            struct drm_mode_create_dumb* dumb = (struct drm_mode_create_dumb*)argp;
            dumb_buffer_t* buf = alloc_dumb_buffer();
            if (unlikely(!buf)) {
                LOGE("CREATE_DUMB: buffer pool exhausted");
                ret = -ENOMEM;
                TRACE_END();
                break;
            }
            
            // 修复：使用 memory_order_relaxed（无同步开销）
            buf->handle = atomic_fetch_add_explicit(&g_next_handle, 1, memory_order_relaxed);
            buf->width = dumb->width;
            buf->height = dumb->height;
            buf->bpp = dumb->bpp;
            buf->pitch = ((dumb->width * dumb->bpp + 31) / 32) * 4;  // 对齐到 4 字节
            buf->size = (uint64_t)buf->pitch * dumb->height;
            buf->vaddr = NULL;
            
            dumb->handle = buf->handle;
            dumb->pitch = buf->pitch;
            dumb->size = buf->size;
            LOGI("CREATE_DUMB: %ux%u bpp=%u -> handle=%u pitch=%u size=%llu",
                 dumb->width, dumb->height, dumb->bpp, buf->handle, 
                 buf->pitch, (unsigned long long)buf->size);
            
            TRACE_END();
            break;
        }
        
        case DRM_IOCTL_MODE_MAP_DUMB: {
            struct drm_mode_map_dumb* map = (struct drm_mode_map_dumb*)argp;
            dumb_buffer_t* buf = find_dumb_buffer(map->handle);
            if (!buf) {
                LOGE("MAP_DUMB: invalid handle=%u", map->handle);
                ret = -EINVAL;
                break;
            }
            
            if (!buf->vaddr) {
#ifdef __ANDROID__
#if __ANDROID_API__ >= 26
                // 优先使用 Gralloc（零拷贝 + GPU 加速）
                buf->vaddr = android_alloc_hardware_buffer_v2((size_t)buf->size, 
                                                             buf->width, buf->height, buf);
#else
                buf->vaddr = android_alloc_large_buffer((size_t)buf->size);
#endif
#else
                buf->vaddr = android_alloc_large_buffer((size_t)buf->size);
#endif
                if (!buf->vaddr) {
                    LOGE("MAP_DUMB: allocation failed for %llu bytes", 
                         (unsigned long long)buf->size);
                    ret = -ENOMEM;
                    break;
                }
                memset(buf->vaddr, 0, (size_t)buf->size);
            }
            
            // 返回虚拟地址（mmap 会验证）
            map->offset = (uint64_t)(uintptr_t)buf->vaddr;
            LOGD("MAP_DUMB: handle=%u -> offset=%llx (vaddr=%p)", 
                 map->handle, (unsigned long long)map->offset, buf->vaddr);
            break;
        }
        
        case DRM_IOCTL_MODE_ADDFB: {
            struct drm_mode_fb_cmd* fb = (struct drm_mode_fb_cmd*)argp;
            fb->fb_id = atomic_fetch_add_explicit(&g_next_fb_id, 1, memory_order_relaxed);
            
            dumb_buffer_t* buf = find_dumb_buffer(fb->handle);
            if (buf) {
                g_current_fb_handle = fb->handle;
            }
            LOGI("ADDFB: handle=%u -> fb_id=%u", fb->handle, fb->fb_id);
            break;
        }
        
        case DRM_IOCTL_MODE_ADDFB2: {
            struct drm_mode_fb_cmd2* fb = (struct drm_mode_fb_cmd2*)argp;
            fb->fb_id = atomic_fetch_add_explicit(&g_next_fb_id, 1, memory_order_relaxed);
            
            dumb_buffer_t* buf = find_dumb_buffer(fb->handles[0]);
            if (buf) {
                g_current_fb_handle = fb->handles[0];
            }
            
            LOGI("ADDFB2: handle=%u pixel_format=0x%x -> fb_id=%u", 
                 fb->handles[0], fb->pixel_format, fb->fb_id);
            break;
        }
        
        case DRM_IOCTL_MODE_RMFB: {
            uint32_t fb_id = *(uint32_t*)argp;
            LOGD("RMFB: fb_id=%u", fb_id);
            break;
        }
        
        case DRM_IOCTL_MODE_PAGE_FLIP: {
#ifdef __ANDROID__
            struct timespec start, end;
            clock_gettime(CLOCK_MONOTONIC, &start);
#endif
            
            TRACE_BEGIN("drm_page_flip");
            
            struct drm_mode_crtc_page_flip* flip = (struct drm_mode_crtc_page_flip*)argp;
            
            if (unlikely(!g_window)) {
                LOGE("PAGE_FLIP: no ANativeWindow set");
                ret = -EINVAL;
                TRACE_END();
                break;
            }
            
            // 锁定 ANativeWindow buffer（一次性）
            if (unlikely(ANativeWindow_lock(g_window, &g_buffer, NULL) < 0)) {
                LOGE("PAGE_FLIP: ANativeWindow_lock failed");
                ret = -EIO;
                TRACE_END();
                break;
            }
            
            // 按 z_order 从低到高渲染所有窗口
            int rendered = 0;
            int total_pixels = 0;
            
            // 告诉编译器窗口数量上限（优化循环展开）
            __builtin_assume(g_num_windows >= 0 && g_num_windows <= MAX_WINDOWS);
            
            for (int z = 0; z < g_num_windows; z++) {
                window_t* win = NULL;
                for (int i = 0; i < g_num_windows; i++) {
                    if (g_windows[i].active && g_windows[i].z_order == z) {
                        win = &g_windows[i];
                        break;
                    }
                }
                
                if (!win) continue;
                
                dumb_buffer_t* buf = find_dumb_buffer(win->fb_id);
                if (!buf || !buf->vaddr) {
                    LOGW("PAGE_FLIP: skip invalid window fb_id=%u", win->fb_id);
                    continue;
                }
                
                // 计算窗口在 ANativeWindow 中的渲染区域
                int dst_x = win->x;
                int dst_y = win->y;
                int dst_w = win->width;
                int dst_h = win->height;
                
                // 裁剪到 ANativeWindow 边界
                if (dst_x < 0) { dst_w += dst_x; dst_x = 0; }
                if (dst_y < 0) { dst_h += dst_y; dst_y = 0; }
                if (dst_x + dst_w > g_buffer.width) dst_w = g_buffer.width - dst_x;
                if (dst_y + dst_h > g_buffer.height) dst_h = g_buffer.height - dst_y;
                
                if (dst_w <= 0 || dst_h <= 0) continue;
                
                // 源 buffer 参数
                int src_stride = buf->pitch / 4;
                int dst_stride = g_buffer.stride;
                int copy_width = (buf->width < (uint32_t)dst_w) ? buf->width : dst_w;
                int copy_height = (buf->height < (uint32_t)dst_h) ? buf->height : dst_h;

                const uint32_t* src_base = (const uint32_t*)buf->vaddr;
                uint32_t* dst_base = (uint32_t*)g_buffer.bits;

                for (int y = 0; y < copy_height; y++) {
                    const uint32_t* src_row = src_base + y * src_stride;
                    uint32_t* dst_row = dst_base + (dst_y + y) * dst_stride + dst_x;
                    neon_copy_optimized(src_row, dst_row, copy_width);
                }
                
                rendered++;
                total_pixels += copy_width * copy_height;
                LOGD("PAGE_FLIP: rendered window fb_id=%u at (%d,%d)+%dx%d z=%d", 
                     win->fb_id, dst_x, dst_y, copy_width, copy_height, win->z_order);
            }
            
            ANativeWindow_unlockAndPost(g_window);
            
            if (unlikely(rendered == 0)) {
                LOGW("PAGE_FLIP: no windows rendered (flip->fb_id=%u)", flip->fb_id);
            } else {
                LOGD("PAGE_FLIP: composited %d windows to ANativeWindow", rendered);
            }
            
            TRACE_END();
            
#ifdef __ANDROID__
            clock_gettime(CLOCK_MONOTONIC, &end);
            uint64_t elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL + 
                                 (end.tv_nsec - start.tv_nsec);
            
            g_perf_stats.page_flip_count++;
            g_perf_stats.total_time_ns += elapsed_ns;
            g_perf_stats.total_pixels += (uint64_t)total_pixels;
#endif
            
            break;
        }
        
        case DRM_IOCTL_MODE_DESTROY_DUMB: {
            struct drm_mode_destroy_dumb* destroy = (struct drm_mode_destroy_dumb*)argp;
            dumb_buffer_t* buf = find_dumb_buffer(destroy->handle);
            if (buf) {
                if (buf->vaddr) {
                    free(buf->vaddr);
                    buf->vaddr = NULL;
                }
                buf->in_use = 0;
                LOGD("DESTROY_DUMB: handle=%u", destroy->handle);
            }
            break;
        }
        
        case DRM_IOCTL_GEM_CLOSE: {
            struct drm_gem_close* gem = (struct drm_gem_close*)argp;
            LOGD("GEM_CLOSE: handle=%u", gem->handle);
            break;
        }

        // 添加更多ioctl处理
        case DRM_IOCTL_VERSION: {
            struct drm_version* ver = (struct drm_version*)argp;
            const char* name = "android-drm-shim";
            const char* date = "20240101";
            const char* desc = "Android DRM Shim for wlroots/xwayland";
            
            ver->version_major = 2;
            ver->version_minor = 4;
            ver->version_patchlevel = 120;
            
            if (ver->name && ver->name_len > 0) {
                strncpy(ver->name, name, ver->name_len - 1);
                ver->name[ver->name_len - 1] = '\0';
                ver->name_len = strlen(name);
            }
            
            if (ver->date && ver->date_len > 0) {
                strncpy(ver->date, date, ver->date_len - 1);
                ver->date[ver->date_len - 1] = '\0';
                ver->date_len = strlen(date);
            }
            
            if (ver->desc && ver->desc_len > 0) {
                strncpy(ver->desc, desc, ver->desc_len - 1);
                ver->desc[ver->desc_len - 1] = '\0';
                ver->desc_len = strlen(desc);
            }
            
            LOGD("VERSION: %d.%d.%d (%s)", ver->version_major, 
                 ver->version_minor, ver->version_patchlevel, name);
            break;
        }
        
        case DRM_IOCTL_GET_CAP: {
            struct drm_get_cap* cap = (struct drm_get_cap*)argp;  // 修复：补全变量声明
            
            switch (cap->capability) {
                case DRM_CAP_DUMB_BUFFER:
                    cap->value = 1;
                    break;
                case DRM_CAP_DUMB_PREFERRED_DEPTH:
                    cap->value = 32;
                    break;
                case DRM_CAP_DUMB_PREFER_SHADOW:
                    cap->value = 1;
                    break;
                case DRM_CAP_VBLANK_HIGH_CRTC:
                    cap->value = 1;
                    break;
                case DRM_CAP_TIMESTAMP_MONOTONIC:
                    cap->value = 1;
                    break;
                case DRM_CAP_ASYNC_PAGE_FLIP:
                    cap->value = 0;  // 不支持异步翻页
                    break;
                case DRM_CAP_CURSOR_WIDTH:
                    cap->value = 64;
                    break;
                case DRM_CAP_CURSOR_HEIGHT:
                    cap->value = 64;
                    break;
                case DRM_CAP_ADDFB2_MODIFIERS:
                    cap->value = 0;  // 不支持 modifier
                    break;
                case DRM_CAP_PRIME:
                    cap->value = 0;  // 不支持 PRIME
                    break;
                case DRM_CAP_SYNCOBJ:
                    cap->value = 0;
                    break;
                case DRM_CAP_CRTC_IN_VBLANK_EVENT:
                    cap->value = 1;
                    break;
                default:
                    LOGD("GET_CAP: unknown capability %llu", 
                         (unsigned long long)cap->capability);
                    ret = -EINVAL;
                    break;
            }
            
            if (ret == 0) {
                LOGD("GET_CAP: capability=%llu value=%llu", 
                     (unsigned long long)cap->capability, 
                     (unsigned long long)cap->value);
            }
            break;
        }
        
        case DRM_IOCTL_SET_CLIENT_CAP: {
            struct drm_set_client_cap* cap = (struct drm_set_client_cap*)argp;
            LOGD("SET_CLIENT_CAP: capability=%llu value=%llu", 
                 (unsigned long long)cap->capability, 
                 (unsigned long long)cap->value);
            // 接受所有客户端能力设置
            ret = 0;
            break;
        }
        
        case DRM_IOCTL_MODE_GETPROPERTY: {
            struct drm_mode_get_property* prop = (struct drm_mode_get_property*)argp;
            
            // 伪造常见属性（wlroots 会查询这些）
            switch (prop->prop_id) {
                case 1: // "type"
                    strncpy(prop->name, "type", sizeof(prop->name) - 1);
                    prop->name[sizeof(prop->name) - 1] = '\0';
                    prop->flags = DRM_MODE_PROP_ENUM | DRM_MODE_PROP_IMMUTABLE;
                    prop->count_values = 0;
                    prop->count_enum_blobs = 2;
                    
                    if (prop->enum_blob_ptr) {
                        struct drm_mode_property_enum {
                            uint64_t value;
                            char name[32];
                        } *enums = (void*)(uintptr_t)prop->enum_blob_ptr;
                        
                        enums[0].value = DRM_MODE_CONNECTOR_VIRTUAL;
                        strncpy(enums[0].name, "Virtual", sizeof(enums[0].name));
                        enums[1].value = DRM_MODE_CONNECTOR_Unknown;
                        strncpy(enums[1].name, "Unknown", sizeof(enums[1].name));
                    }
                    break;
                    
                case 2: // "DPMS"
                    strncpy(prop->name, "DPMS", sizeof(prop->name) - 1);
                    prop->name[sizeof(prop->name) - 1] = '\0';
                    prop->flags = DRM_MODE_PROP_ENUM;
                    prop->count_values = 1;
                    prop->count_enum_blobs = 1;
                    
                    if (prop->values_ptr) {
                        ((uint64_t*)(uintptr_t)prop->values_ptr)[0] = 0; // DRM_MODE_DPMS_ON
                    }
                    break;
                    
                case 3: // "CRTC_ID"
                    strncpy(prop->name, "CRTC_ID", sizeof(prop->name) - 1);
                    prop->name[sizeof(prop->name) - 1] = '\0';
                    prop->flags = DRM_MODE_PROP_ATOMIC;
                    prop->count_values = 1;
                    prop->count_enum_blobs = 0;
                    
                    if (prop->values_ptr) {
                        ((uint64_t*)(uintptr_t)prop->values_ptr)[0] = 0;
                    }
                    break;
                    
                default:
                    LOGD("GETPROPERTY: unknown prop_id=%u, returning generic", prop->prop_id);
                    snprintf(prop->name, sizeof(prop->name), "prop_%u", prop->prop_id);
                    prop->flags = DRM_MODE_PROP_RANGE;
                    prop->count_values = 0;
                    prop->count_enum_blobs = 0;
                    ret = 0;  // 不返回错误，避免 wlroots 失败
                    break;
            }
            
            if (ret == 0) {
                LOGD("GETPROPERTY: prop_id=%u name='%s' flags=0x%x", 
                     prop->prop_id, prop->name, prop->flags);
            }
            break;
        }
        
        case DRM_IOCTL_MODE_CREATEPROPBLOB: {
            struct drm_mode_create_blob* create = (struct drm_mode_create_blob*)argp;
            blob_t* blob = create_blob();
            
            if (!blob) {
                ret = -ENOMEM;
                break;
            }
            
            blob->data = malloc(create->length);
            if (!blob->data) {
                blob->in_use = 0;
                ret = -ENOMEM;
                break;
            }
            
            if (create->data) {
                // 使用 NEON 优化的 memcpy
                neon_memcpy_aligned(blob->data, (void*)(uintptr_t)create->data, 
                                   create->length);
            }
            
            blob->length = create->length;
            create->blob_id = blob->id;
            
            LOGD("CREATEPROPBLOB: blob_id=%u length=%zu", blob->id, blob->length);
            break;
        }
        
        case DRM_IOCTL_MODE_DESTROYPROPBLOB: {
            struct drm_mode_destroy_blob* destroy = (struct drm_mode_destroy_blob*)argp;
            blob_t* blob = find_blob(destroy->blob_id);
            
            if (blob) {
                free(blob->data);
                blob->in_use = 0;
                LOGD("DESTROYPROPBLOB: blob_id=%u", destroy->blob_id);
            } else {
                LOGW("DESTROYPROPBLOB: blob %u not found", destroy->blob_id);
                ret = -ENOENT;
            }
            break;
        }
        
        case DRM_IOCTL_MODE_OBJ_GETPROPERTIES: {
            struct drm_mode_obj_get_properties* props = (struct drm_mode_obj_get_properties*)argp;
            
            // 根据对象类型返回属性
            switch (props->obj_type) {
                case DRM_MODE_OBJECT_CONNECTOR:
                    if (props->props_ptr && props->count_props >= 1) {
                        ((uint32_t*)(uintptr_t)props->props_ptr)[0] = 1; // "type" 属性
                    }
                    if (props->prop_values_ptr && props->count_props >= 1) {
                        ((uint64_t*)(uintptr_t)props->prop_values_ptr)[0] = DRM_MODE_CONNECTOR_VIRTUAL;
                    }
                    props->count_props = 1;
                    break;
                    
                case DRM_MODE_OBJECT_CRTC:
                    props->count_props = 0;
                    break;
                    
                case DRM_MODE_OBJECT_PLANE:
                    props->count_props = 0;
                    break;
                    
                default:
                    LOGW("OBJ_GETPROPERTIES: unknown obj_type=%u", props->obj_type);
                    ret = -EINVAL;
                    break;
            }
            
            LOGD("OBJ_GETPROPERTIES: obj_id=%u obj_type=0x%x count=%u", 
                 props->obj_id, props->obj_type, props->count_props);
            break;
        }
        
        case DRM_IOCTL_MODE_OBJ_SETPROPERTY: {
            struct drm_mode_obj_set_property* prop = (struct drm_mode_obj_set_property*)argp;
            
            LOGD("OBJ_SETPROPERTY: obj_id=%u obj_type=0x%x prop_id=%u value=%llu",
                 prop->obj_id, prop->obj_type, prop->prop_id,
                 (unsigned long long)prop->value);
            
            // 简化实现：记录属性设置但不实际存储（wlroots 通常在 MODE_ATOMIC 中批量提交）
            switch (prop->obj_type) {
                case DRM_MODE_OBJECT_CRTC:
                    // 常见属性：MODE_ID, ACTIVE, GAMMA_LUT 等
                    if (prop->prop_id == 3) { // 假设 prop_id=3 是 "ACTIVE"
                        LOGD("  CRTC %u: ACTIVE=%llu", prop->obj_id, 
                             (unsigned long long)prop->value);
                    }
                    break;
                    
                case DRM_MODE_OBJECT_CONNECTOR:
                    // 常见属性：CRTC_ID, DPMS 等
                    if (prop->prop_id == 3) { // "CRTC_ID"
                        LOGD("  Connector %u: CRTC_ID=%llu", prop->obj_id,
                             (unsigned long long)prop->value);
                    }
                    break;
                    
                case DRM_MODE_OBJECT_PLANE:
                    // 常见属性：FB_ID, CRTC_ID, CRTC_X, CRTC_Y 等
                    LOGD("  Plane %u: prop_id=%u value=%llu", prop->obj_id,
                         prop->prop_id, (unsigned long long)prop->value);
                    break;
                    
                default:
                    LOGW("OBJ_SETPROPERTY: unsupported obj_type=0x%x", prop->obj_type);
                    ret = -EINVAL;
                    break;
            }
            
            // 返回成功（实际应用由 MODE_ATOMIC 或 SETCRTC 执行）
            ret = 0;
            break;
        }
        
        case DRM_IOCTL_MODE_ATOMIC: {
            struct drm_mode_atomic* atomic = (struct drm_mode_atomic*)argp;
            
            LOGD("MODE_ATOMIC: flags=0x%x count_objs=%u", 
                 atomic->flags, atomic->count_objs);
            
            // 解析原子操作（简化实现：仅记录，不实际执行）
            if (atomic->count_objs > 0 && atomic->objs_ptr) {
                uint32_t* objs = (uint32_t*)(uintptr_t)atomic->objs_ptr;
                uint32_t* count_props = (uint32_t*)(uintptr_t)atomic->count_props_ptr;
                
                for (uint32_t i = 0; i < atomic->count_objs; i++) {
                    LOGD("  obj[%u]: id=%u props_count=%u", 
                         i, objs[i], count_props[i]);
                }
            }
            
            // TEST_ONLY 模式下不实际应用
            if (atomic->flags & DRM_MODE_ATOMIC_TEST_ONLY) {
                LOGD("MODE_ATOMIC: test-only, returning success");
            }
            
            ret = 0;
            break;
        }
        
        case DRM_IOCTL_GEM_FLINK: {
            struct drm_gem_flink* flink = (struct drm_gem_flink*)argp;
            // 简化：handle 即 name
            flink->name = flink->handle;
            LOGD("GEM_FLINK: handle=%u -> name=%u", flink->handle, flink->name);
            break;
        }
        
        case DRM_IOCTL_GEM_OPEN: {
            struct drm_gem_open* open_gem = (struct drm_gem_open*)argp;
            open_gem->handle = open_gem->name;
            
            dumb_buffer_t* buf = find_dumb_buffer(open_gem->handle);
            open_gem->size = buf ? buf->size : 0;
            
            LOGD("GEM_OPEN: name=%u -> handle=%u size=%llu", 
                 open_gem->name, open_gem->handle, 
                 (unsigned long long)open_gem->size);
            break;
        }
        case DRM_IOCTL_MODE_GETPLANERESOURCES: {
            struct drm_mode_get_plane_res* res = (struct drm_mode_get_plane_res*)argp;
            
            if (res->plane_id_ptr && res->count_planes >= 1) {
                ((uint32_t*)(uintptr_t)res->plane_id_ptr)[0] = g_plane_id;
            }
            res->count_planes = 1;  // 伪造单个 overlay plane
            
            LOGD("GETPLANERESOURCES: 1 plane (id=%u)", g_plane_id);
            break;
        }
        
        case DRM_IOCTL_MODE_GETPLANE: {
            struct drm_mode_get_plane* plane = (struct drm_mode_get_plane*)argp;
            
            plane->plane_id = g_plane_id;
            plane->crtc_id = g_crtc_id;
            plane->fb_id = 0;
            plane->possible_crtcs = 1;
            plane->gamma_size = 0;
            
            // 支持的像素格式
            plane->count_format_types = 2;
            if (plane->format_type_ptr) {
                uint32_t* formats = (uint32_t*)(uintptr_t)plane->format_type_ptr;
                formats[0] = 0x34325258;  // DRM_FORMAT_XRGB8888
                formats[1] = 0x34324152;  // DRM_FORMAT_ARGB8888
            }
            
            LOGD("GETPLANE: plane_id=%u crtc_id=%u", plane->plane_id, plane->crtc_id);
            break;
        }
        
        case DRM_IOCTL_MODE_SETPLANE: {
            struct drm_mode_set_plane* plane = (struct drm_mode_set_plane*)argp;
            
            LOGD("SETPLANE: plane_id=%u crtc_id=%u fb_id=%u (%ux%u -> %ux%u)", 
                 plane->plane_id, plane->crtc_id, plane->fb_id,
                 plane->src_w >> 16, plane->src_h >> 16,
                 plane->crtc_w, plane->crtc_h);
            
            // 简化实现：接受但不实际操作
            ret = 0;
            break;
        }
        
        default:
            LOGE("Unhandled ioctl: 0x%lx", request);
            ret = -ENOSYS;
            break;  // 移除 __builtin_unreachable()，保留 break
    }
    
    pthread_mutex_unlock(&g_lock);
    return ret;
}

// ========== ANativeWindow 注入（可选，用于测试） ==========
void drm_shim_set_window(ANativeWindow* win) {
    pthread_mutex_lock(&g_lock);
    if (g_window) {
        ANativeWindow_release(g_window);
    }
    g_window = win;
    if (g_window) {
        ANativeWindow_acquire(g_window);
        g_window_width = ANativeWindow_getWidth(g_window);
        g_window_height = ANativeWindow_getHeight(g_window);
        ANativeWindow_setBuffersGeometry(g_window, g_window_width, g_window_height, 
                                         WINDOW_FORMAT_RGBA_8888);
        LOGI("Window set: %dx%d", g_window_width, g_window_height);
    }
    pthread_mutex_unlock(&g_lock);
}

// ========== 窗口管理 API（用于 wlroots surface 映射） ==========

// 设置窗口位置（用于分屏场景）
void drm_shim_set_window_geometry(uint32_t fb_id, int x, int y, int width, int height) {
    pthread_mutex_lock(&g_lock);
    
    window_t* win = find_window_by_fb(fb_id);
    if (win) {
        win->x = x;
        win->y = y;
        win->width = width;
        win->height = height;
        LOGI("Window geometry set: fb_id=%u x=%d y=%d width=%d height=%d", 
             fb_id, x, y, width, height);
    } else {
        LOGE("Failed to set window geometry: fb_id=%u not found", fb_id);
    }
    
    pthread_mutex_unlock(&g_lock);
}

// 设置窗口 Z 顺序（用于分屏场景）
void drm_shim_set_window_z_order(uint32_t fb_id, int z_order) {
    pthread_mutex_lock(&g_lock);
    
    window_t* win = find_window_by_fb(fb_id);
    if (win) {
        win->z_order = z_order;
        LOGI("Window z_order set: fb_id=%u z_order=%d", fb_id, z_order);
    } else {
        LOGE("Failed to set window z_order: fb_id=%u not found", fb_id);
    }
    
    pthread_mutex_unlock(&g_lock);
}

// 销毁窗口（释放资源）
void drm_shim_destroy_window(uint32_t fb_id) {
    pthread_mutex_lock(&g_lock);
    
    for (int i = 0; i < g_num_windows; i++) {
        if (g_windows[i].fb_id == fb_id) {
            // 释放关联的 dumb buffer
            dumb_buffer_t* buf = find_dumb_buffer(fb_id);
            if (buf) {
                if (buf->vaddr) {
                    free(buf->vaddr);
                    buf->vaddr = NULL;
                }
                buf->in_use = 0;
                LOGD("DESTROY_WINDOW: freed dumb buffer fb_id=%u", fb_id);
            }
            
            // 移动最后一个窗口到当前位置（保持窗口数组紧凑）
            if (i < g_num_windows - 1) {
                g_windows[i] = g_windows[g_num_windows - 1];
                g_windows[g_num_windows - 1].active = 0;  // 标记为非活动
            }
            g_num_windows--;
            
            LOGI("Window destroyed: fb_id=%u", fb_id);
            break;
        }
    }
    
    pthread_mutex_unlock(&g_lock);
}

// ========== 性能统计 ==========
#ifdef __ANDROID__
#include <time.h>

// 性能统计
static struct {
    uint64_t page_flip_count;
    uint64_t total_pixels;
    uint64_t total_time_ns;
} g_perf_stats = {0};

__attribute__((destructor))
static void drm_shim_print_stats(void) {
    if (g_perf_stats.page_flip_count > 0) {
        double avg_time_ms = (double)g_perf_stats.total_time_ns / 
                            g_perf_stats.page_flip_count / 1000000.0;
        double avg_pixels = (double)g_perf_stats.total_pixels / 
                           g_perf_stats.page_flip_count;
        double throughput_mpixels_s = (double)g_perf_stats.total_pixels / 
                                     (g_perf_stats.total_time_ns / 1000000000.0) / 
                                     1000000.0;
        
        LOGI("========== DRM Shim Performance Stats ==========");
        LOGI("  Page flips: %llu", (unsigned long long)g_perf_stats.page_flip_count);
        LOGI("  Avg time: %.2f ms/flip", avg_time_ms);
        LOGI("  Avg pixels: %.0f pixels/flip", avg_pixels);
        LOGI("  Throughput: %.2f MPixels/s", throughput_mpixels_s);
        LOGI("=================================================");
    }
}
#endif

// ========== Android Shared Memory (ashmem) ==========
#ifdef __ANDROID__
#include <sys/mman.h>

// Android ashmem ioctl 宏（NDK 头文件可能不包含）
#ifndef ASHMEM_SET_NAME
#define ASHMEM_NAME_LEN 256
#define __ASHMEMIOC 0x77
#define ASHMEM_SET_NAME _IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE _IOW(__ASHMEMIOC, 3, size_t)
#endif

// 使用 ashmem 分配大块内存（> 4MB）
static void* android_alloc_large_buffer(size_t size) {
    if (size < 4 * 1024 * 1024) {
        // 小于 4MB，使用标准分配
        void* buf = NULL;
        if (posix_memalign(&buf, 16, size) == 0) {
            return buf;
        }
        return malloc(size);
    }
    
    // 大块内存使用 ashmem（Android 共享内存）
    int fd = open("/dev/ashmem", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        LOGW("ashmem open failed, fallback to malloc");
        return malloc(size);
    }
    
    char name[64];  // 修复：添加缺失的 name 变量
    snprintf(name, sizeof(name), "drm_shim_buf_%zu", size);
    ioctl(fd, ASHMEM_SET_NAME, name);
    ioctl(fd, ASHMEM_SET_SIZE, size);
    
    void* buf = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);  // mmap 后可以关闭 fd
    
    if (buf == MAP_FAILED) {
        LOGW("ashmem mmap failed, fallback to malloc");
        return malloc(size);
    }
    
    LOGD("Allocated %zu bytes via ashmem", size);
    return buf;
}

#if __ANDROID_API__ >= 26
#include <android/hardware_buffer.h>
#include <cutils/native_handle.h>

// 使用 Gralloc 零拷贝（GPU 可直接访问）
static void* android_alloc_hardware_buffer_v2(size_t size, uint32_t width, uint32_t height, dumb_buffer_t* buf) {
    AHardwareBuffer_Desc desc = {
        .width = width,
        .height = height,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | 
                 AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                 AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                 AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER,  // 添加 GPU framebuffer 支持
        .stride = 0,
        .rfu0 = 0,
        .rfu1 = 0
    };
    
    AHardwareBuffer* ahb = NULL;
    if (AHardwareBuffer_allocate(&desc, &ahb) != 0) {
        LOGW("AHardwareBuffer_allocate failed, fallback to ashmem");
        return android_alloc_large_buffer(size);
    }
    
    void* vaddr = NULL;
    if (AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, 
                            -1, NULL, &vaddr) != 0) {
        AHardwareBuffer_release(ahb);
        LOGW("AHardwareBuffer_lock failed, fallback to ashmem");
        return android_alloc_large_buffer(size);
    }
    
    // 存储 AHardwareBuffer 指针（用于后续 unlock）
    buf->ahb = (void*)ahb;
    
    LOGI("Allocated %zu bytes via AHardwareBuffer (Gralloc zero-copy)", size);
    return vaddr;
}
#endif
#else
static void* android_alloc_large_buffer(size_t size) {
    void* buf = NULL;
    if (posix_memalign(&buf, 16, size) == 0) {
        return buf;
    }
    return malloc(size);
}
#endif

// ========== Android Trace API ==========
#ifdef __ANDROID__
#include <android/trace.h>

#if __ANDROID_API__ >= 23
#define TRACE_BEGIN(name) ATrace_beginSection(name)
#define TRACE_END() ATrace_endSection()
#else
#define TRACE_BEGIN(name)
#define TRACE_END()
#endif
#else
#define TRACE_BEGIN(name)
#define TRACE_END()
#endif
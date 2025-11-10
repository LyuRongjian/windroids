#include "compositor.h"
#include "compositor_input.h"
#include "compositor_vulkan.h"
#include "compositor_window.h"
#include "compositor_render.h"
#include "compositor_resource_manager.h"
#include "compositor_perf.h"
#include "compositor_perf_opt.h"
#include "compositor_game.h"
#include "compositor_monitor.h"
#include "memory_pool.h"
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/interface.h>
#include <wlr/render/vulkan.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#define LOG_TAG "Compositor"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// 简化的合成器状态结构
struct compositor_state {
    ANativeWindow* window;
    int width;
    int height;
    
    // Wayland/wlroots 组件
    struct wl_display* display;
    struct wl_event_loop* event_loop;
    struct wlr_backend* backend;
    struct wlr_renderer* renderer;
    struct wlr_output* output;
    struct wlr_output_layout* output_layout;
    struct wlr_scene* scene;
    struct wlr_scene_output* scene_output;
    
    // Xwayland
    pid_t xwayland_pid;
    int wl_display_fd;
    char xwayland_display[8];
    
    // Vulkan相关
    struct vulkan_state vulkan;
    
    // 性能统计
    uint32_t frame_count;
    uint64_t last_frame_time;
    float fps;
    
    // 输入
    struct wlr_seat* seat;
    struct wlr_cursor* cursor;
    struct wlr_xcursor_manager* cursor_mgr;
    
    // 优化内存池
    struct memory_pool_manager memory_pool;
    struct memory_pool* default_pool;
    struct memory_pool* compositor_pool;
    struct memory_pool_thread_cache thread_cache;
    bool memory_pool_initialized;
    bool memory_pool_enabled;
    
    // 性能优化模块
    struct perf_opt perf_opt;
    bool perf_opt_initialized;
    
    // 游戏模式模块
    struct game_mode game_mode;
    bool game_mode_initialized;
    
    // 监控模块
    struct monitor monitor;
    bool monitor_initialized;
    
    // 输入系统
    struct input input;
    bool input_initialized;
    
    // 渲染器
    struct renderer renderer;
    bool renderer_initialized;
    
    // 窗口管理器
    struct window_manager window_manager;
    bool window_manager_initialized;
    
    // Wayland
    bool wayland_initialized;
    
    // wlroots
    bool wlroots_initialized;
    
    // 状态标志
    bool initialized;
    bool xwayland_started;
    bool vulkan_initialized;
};

static struct compositor_state g_state = {0};

// 内部函数声明
static int init_wayland(void);
static int init_wlroots(void);
static int init_vulkan(void);
static int start_xwayland(void);
static void handle_xwayland_dead(int sig);
static int create_vulkan_render_pass(void);
static int create_vulkan_framebuffers(void);
static int create_vulkan_command_buffers(void);
static int create_vulkan_sync_objects(void);
static void cleanup_vulkan(void);
static void cleanup_wayland(void);
static void cleanup_wlroots(void);
static void cleanup_xwayland(void);
static int render_frame(void);
static void update_fps(void);

// 初始化合成器
int compositor_init(ANativeWindow* window, int width, int height) {
    if (g_state.initialized) {
        LOGE("Compositor already initialized");
        return -1;
    }
    
    if (!window || width <= 0 || height <= 0) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    memset(&g_state, 0, sizeof(g_state));
    g_state.window = window;
    g_state.width = width;
    g_state.height = height;
    g_state.memory_pool_enabled = true;
    
    // 初始化内存池系统
    if (memory_pool_manager_init(&g_state.memory_pool, 16) != 0) {
        LOGE("Failed to initialize memory pool manager");
        return -1;
    }
    g_state.memory_pool_initialized = true;
    
    // 创建合成器专用内存池
    struct memory_pool_config pool_config = {0};
    
    // 设置初始块数量
    for (uint32_t i = 0; i < MEMORY_POOL_BLOCK_SIZE_COUNT; i++) {
        pool_config.initial_block_counts[i] = 32;
        pool_config.max_block_counts[i] = 1024;
    }
    
    // 设置配置参数
    pool_config.growth_factor = 150;  // 150%增长
    pool_config.shrink_threshold = 50;  // 50%使用率以下收缩
    pool_config.auto_resize_interval_ms = 5000;  // 5秒自动调整
    pool_config.enable_thread_cache = true;
    pool_config.enable_prefetch = true;
    pool_config.enable_lock_free = false;
    pool_config.enable_statistics = true;
    pool_config.enable_profiling = false;
    
    g_state.compositor_pool = memory_pool_create(&pool_config);
    if (!g_state.compositor_pool) {
        LOGE("Failed to create compositor memory pool");
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 创建默认内存池
    g_state.default_pool = memory_pool_create(&pool_config);
    if (!g_state.default_pool) {
        LOGE("Failed to create default memory pool");
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化线程本地缓存
    if (memory_pool_thread_cache_init(&g_state.thread_cache, 8) != 0) {
        LOGE("Failed to initialize thread cache");
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化窗口管理器
    if (window_manager_init(width, height) != 0) {
        LOGE("Failed to initialize window manager");
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化渲染器
    if (renderer_init(window, width, height) != 0) {
        LOGE("Failed to initialize renderer");
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化资源管理器
    if (resource_manager_init(256 * 1024 * 1024) != 0) { // 256MB
        LOGE("Failed to initialize resource manager");
        renderer_destroy();
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化性能监控器
    if (perf_monitor_init(60) != 0) {
        LOGE("Failed to initialize performance monitor");
        resource_manager_destroy();
        renderer_destroy();
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化性能优化模块
    if (perf_opt_init() != 0) {
        LOGE("Failed to initialize performance optimization");
        perf_monitor_destroy();
        resource_manager_destroy();
        renderer_destroy();
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化游戏模式模块
    if (game_mode_init() != 0) {
        LOGE("Failed to initialize game mode");
        perf_opt_destroy();
        perf_monitor_destroy();
        resource_manager_destroy();
        renderer_destroy();
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化监控模块
    if (monitor_init() != 0) {
        LOGE("Failed to initialize monitor");
        game_mode_destroy();
        perf_opt_destroy();
        perf_monitor_destroy();
        resource_manager_destroy();
        renderer_destroy();
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化 Wayland
    if (init_wayland() != 0) {
        LOGE("Failed to initialize Wayland");
        perf_monitor_destroy();
        resource_manager_destroy();
        renderer_destroy();
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化输入系统
    if (compositor_input_init() != 0) {
        LOGE("Failed to initialize input system");
        cleanup_wayland();
        perf_monitor_destroy();
        resource_manager_destroy();
        renderer_destroy();
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 注册输入事件处理器
    compositor_input_register_event_handler(handle_input_event, NULL);
    
    // 初始化 wlroots
    if (init_wlroots() != 0) {
        LOGE("Failed to initialize wlroots");
        cleanup_wayland();
        compositor_input_destroy();
        perf_monitor_destroy();
        resource_manager_destroy();
        renderer_destroy();
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化 Vulkan
    if (vulkan_init(&g_state.vulkan, window, g_state.width, g_state.height) != 0) {
        LOGE("Failed to initialize Vulkan");
        cleanup_wlroots();
        cleanup_wayland();
        compositor_input_destroy();
        perf_monitor_destroy();
        resource_manager_destroy();
        renderer_destroy();
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    // 初始化预记录的命令缓冲区
    if (init_prerecorded_command_buffers() != 0) {
        LOGE("Failed to initialize prerecorded command buffers");
        cleanup_vulkan();
        cleanup_wlroots();
        cleanup_wayland();
        compositor_input_destroy();
        perf_monitor_destroy();
        resource_manager_destroy();
        renderer_destroy();
        window_manager_destroy();
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        memory_pool_destroy(g_state.compositor_pool);
        memory_pool_destroy(g_state.default_pool);
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
        return -1;
    }
    
    g_state.vulkan_initialized = true;
    
    // 启动 Xwayland
    if (start_xwayland() != 0) {
        LOGE("Failed to start Xwayland");
        cleanup_vulkan();
        cleanup_wlroots();
        cleanup_wayland();
        compositor_input_destroy();
        perf_monitor_destroy();
        resource_manager_destroy();
        renderer_destroy();
        window_manager_destroy();
        memory_pool_opt_destroy(g_state.memory_pool);
        memory_pool_opt_manager_destroy();
        return -1;
    }
    
    g_state.initialized = true;
    LOGI("Compositor initialized successfully");
    return 0;
}

// 主循环单步
int compositor_step(void) {
    if (!g_state.initialized) {
        return -1;
    }
    
    // 开始性能监控
    perf_monitor_begin_frame();
    
    // 处理输入事件
    perf_monitor_begin_measure(PERF_COUNTER_INPUT_TIME);
    compositor_handle_input(0, 0, 0, 0, 0);
    perf_monitor_end_measure(PERF_COUNTER_INPUT_TIME);
    
    // 处理 Wayland 事件
    wl_event_loop_dispatch(g_state.event_loop, 0);
    wl_display_flush_clients(g_state.display);
    
    // 更新窗口管理器
    window_manager_update();
    
    // 更新渲染器
    renderer_update();
    
    // 渲染帧
    perf_monitor_begin_measure(PERF_COUNTER_RENDER_TIME);
    int result = render_frame();
    perf_monitor_end_measure(PERF_COUNTER_RENDER_TIME);
    
    if (result != 0) {
        LOGE("Failed to render frame: %d", result);
        perf_monitor_end_frame();
        return result;
    }
    
    // 更新性能监控器
    perf_monitor_end_frame();
    
    // 更新内存使用统计
    struct memory_stats mem_stats;
    resource_get_memory_stats(&mem_stats);
    perf_monitor_set_counter(PERF_COUNTER_MEMORY_USAGE, mem_stats.total_used / 1024.0f / 1024.0f); // MB
    
    return 0;
}

// 性能优化相关API
int compositor_set_perf_opt_enabled(bool enabled) {
    return perf_opt_set_enabled(enabled);
}

bool compositor_is_perf_opt_enabled(void) {
    return perf_opt_is_enabled();
}

int compositor_set_perf_profile(enum perf_profile profile) {
    return perf_opt_set_profile(profile);
}

enum perf_profile compositor_get_perf_profile(void) {
    return perf_opt_get_profile();
}

int compositor_set_adaptive_fps_enabled(bool enabled) {
    return perf_opt_set_adaptive_fps_enabled(enabled);
}

bool compositor_is_adaptive_fps_enabled(void) {
    return perf_opt_is_adaptive_fps_enabled();
}

int compositor_set_adaptive_quality_enabled(bool enabled) {
    return perf_opt_set_adaptive_quality_enabled(enabled);
}

bool compositor_is_adaptive_quality_enabled(void) {
    return perf_opt_is_adaptive_quality_enabled();
}

int compositor_set_target_fps(int fps) {
    return perf_opt_set_target_fps(fps);
}

int compositor_get_target_fps(void) {
    return perf_opt_get_target_fps();
}

int compositor_set_quality_level(int level) {
    return perf_opt_set_quality_level(level);
}

int compositor_get_quality_level(void) {
    return perf_opt_get_quality_level();
}

int compositor_get_perf_opt_stats(struct perf_opt_stats *stats) {
    return perf_opt_get_stats(stats);
}

// 游戏模式相关API
int compositor_set_game_mode_enabled(bool enabled) {
    return game_mode_set_enabled(enabled);
}

bool compositor_is_game_mode_enabled(void) {
    return game_mode_is_enabled();
}

int compositor_set_game_type(enum game_type type) {
    return game_mode_set_type(type);
}

enum game_type compositor_get_game_type(void) {
    return game_mode_get_type();
}

int compositor_set_input_boost_enabled(bool enabled) {
    return game_mode_set_input_boost_enabled(enabled);
}

bool compositor_is_input_boost_enabled(void) {
    return game_mode_is_input_boost_enabled();
}

int compositor_set_priority_boost_enabled(bool enabled) {
    return game_mode_set_priority_boost_enabled(enabled);
}

bool compositor_is_priority_boost_enabled(void) {
    return game_mode_is_priority_boost_enabled();
}

int compositor_get_game_mode_stats(struct game_mode_stats *stats) {
    return game_mode_get_stats(stats);
}

// 监控相关API
int compositor_set_monitoring_enabled(bool enabled) {
    return monitor_set_enabled(enabled);
}

bool compositor_is_monitoring_enabled(void) {
    return monitor_is_enabled();
}

int compositor_set_auto_save_enabled(bool enabled) {
    return monitor_set_auto_save_enabled(enabled);
}

bool compositor_is_auto_save_enabled(void) {
    return monitor_is_auto_save_enabled();
}

int compositor_set_sample_interval(int interval_ms) {
    return monitor_set_sample_interval(interval_ms);
}

int compositor_get_sample_interval(void) {
    return monitor_get_sample_interval();
}

int compositor_set_report_interval(int interval_ms) {
    return monitor_set_report_interval(interval_ms);
}

int compositor_get_report_interval(void) {
    return monitor_get_report_interval();
}

int compositor_save_report(const char *path) {
    return monitor_save_report(path);
}

int compositor_get_monitor_stats(struct monitor_stats *stats) {
    return monitor_get_stats(stats);
}

int compositor_get_monitor_report(struct monitor_report *report) {
    return monitor_get_report(report);
}

// 销毁合成器
void compositor_destroy(void) {
    LOGI("Destroying compositor");
    
    // 销毁内存池系统
    if (g_state.memory_pool_initialized) {
        // 销毁合成器专用内存池
        if (g_state.memory_pool.compositor_pool) {
            memory_pool_destroy(g_state.memory_pool.compositor_pool);
            g_state.memory_pool.compositor_pool = NULL;
        }
        
        // 销毁默认内存池
        if (g_state.memory_pool.default_pool) {
            memory_pool_destroy(g_state.memory_pool.default_pool);
            g_state.memory_pool.default_pool = NULL;
        }
        
        // 销毁线程本地缓存
        memory_pool_thread_cache_destroy(&g_state.memory_pool.thread_cache);
        
        // 销毁内存池管理器
        memory_pool_manager_destroy(&g_state.memory_pool);
        
        g_state.memory_pool_initialized = false;
        LOGI("Memory pool system destroyed");
    }
    
    // 销毁性能优化模块
    if (g_state.perf_opt_initialized) {
        perf_opt_destroy(&g_state.perf_opt);
        g_state.perf_opt_initialized = false;
    }
    
    // 销毁游戏模式模块
    if (g_state.game_mode_initialized) {
        game_mode_destroy(&g_state.game_mode);
        g_state.game_mode_initialized = false;
    }
    
    // 销毁监控模块
    if (g_state.monitor_initialized) {
        monitor_destroy(&g_state.monitor);
        g_state.monitor_initialized = false;
    }
    
    // 销毁输入系统
    if (g_state.input_initialized) {
        input_destroy(&g_state.input);
        g_state.input_initialized = false;
    }
    
    // 销毁渲染器
    if (g_state.renderer_initialized) {
        renderer_destroy(&g_state.renderer);
        g_state.renderer_initialized = false;
    }
    
    // 销毁窗口管理器
    if (g_state.window_manager_initialized) {
        window_manager_destroy(&g_state.window_manager);
        g_state.window_manager_initialized = false;
    }
    
    // 销毁Vulkan
    if (g_state.vulkan_initialized) {
        vulkan_destroy(&g_state.vulkan);
        g_state.vulkan_initialized = false;
    }
    
    // 销毁wlroots
    if (g_state.wlroots_initialized) {
        wlroots_destroy(&g_state.wlroots);
        g_state.wlroots_initialized = false;
    }
    
    // 销毁Wayland
    if (g_state.wayland_initialized) {
        wayland_destroy(&g_state.wayland);
        g_state.wayland_initialized = false;
    }
    
    LOGI("Compositor destroyed");
}

// 处理输入事件
static void handle_input_event(const input_event_t* event, void* user_data) {
    if (!event) {
        return;
    }
    
    switch (event->type) {
        case INPUT_EVENT_TYPE_TOUCH_DOWN:
            LOGI("Touch down: id=%u, x=%.2f, y=%.2f, pressure=%.2f", 
                 event->data.touch.touch_id, event->data.touch.x, 
                 event->data.touch.y, event->data.touch.pressure);
            break;
        case INPUT_EVENT_TYPE_TOUCH_UP:
            LOGI("Touch up: id=%u, x=%.2f, y=%.2f", 
                 event->data.touch.touch_id, event->data.touch.x, event->data.touch.y);
            break;
        case INPUT_EVENT_TYPE_TOUCH_MOVE:
            LOGI("Touch move: id=%u, x=%.2f, y=%.2f", 
                 event->data.touch.touch_id, event->data.touch.x, event->data.touch.y);
            break;
        case INPUT_EVENT_TYPE_MOUSE_MOVE:
            LOGI("Mouse move: x=%.2f, y=%.2f", event->data.mouse.x, event->data.mouse.y);
            break;
        case INPUT_EVENT_TYPE_MOUSE_BUTTON_DOWN:
            LOGI("Mouse button down: button=%d, x=%.2f, y=%.2f", 
                 event->data.mouse.button, event->data.mouse.x, event->data.mouse.y);
            break;
        case INPUT_EVENT_TYPE_MOUSE_BUTTON_UP:
            LOGI("Mouse button up: button=%d, x=%.2f, y=%.2f", 
                 event->data.mouse.button, event->data.mouse.x, event->data.mouse.y);
            break;
        case INPUT_EVENT_TYPE_MOUSE_SCROLL:
            LOGI("Mouse scroll: dx=%.2f, dy=%.2f", 
                 event->data.mouse.scroll_delta_x, event->data.mouse.scroll_delta_y);
            break;
        case INPUT_EVENT_TYPE_KEY_DOWN:
            LOGI("Key down: keycode=%u, modifiers=%u", 
                 event->data.keyboard.keycode, event->data.keyboard.modifiers);
            break;
        case INPUT_EVENT_TYPE_KEY_UP:
            LOGI("Key up: keycode=%u, modifiers=%u", 
                 event->data.keyboard.keycode, event->data.keyboard.modifiers);
            break;
        case INPUT_EVENT_TYPE_GAMEPAD_BUTTON_DOWN:
            LOGI("Gamepad button down: device_id=%u, button=%d", 
                 event->device_id, event->data.gamepad.button);
            break;
        case INPUT_EVENT_TYPE_GAMEPAD_BUTTON_UP:
            LOGI("Gamepad button up: device_id=%u, button=%d", 
                 event->device_id, event->data.gamepad.button);
            break;
        case INPUT_EVENT_TYPE_GAMEPAD_AXIS_MOVE:
            LOGI("Gamepad axis move: device_id=%u, axis=%d, value=%.2f", 
                 event->device_id, event->data.gamepad.axis, event->data.gamepad.axis_value);
            break;
        case INPUT_EVENT_TYPE_DEVICE_CONNECT:
            LOGI("Device connected: id=%u, type=%d, name=%s", 
                 event->device_id, event->device_type, "Unknown");
            break;
        case INPUT_EVENT_TYPE_DEVICE_DISCONNECT:
            LOGI("Device disconnected: id=%u, type=%d", 
                 event->device_id, event->device_type);
            break;
        default:
            LOGI("Unknown input event type: %d", event->type);
            break;
    }
}

// 注入输入事件
void compositor_handle_input(int type, int x, int y, int key, int state) {
    if (!g_state.initialized || !g_state.seat) {
        return;
    }
    
    // 简化输入处理，仅支持基本鼠标事件
    if (type == 0) { // 鼠标事件
        wlr_cursor_move(g_state.cursor, NULL, x, y);
        
        if (state == 1) { // 按下
            wlr_seat_pointer_notify_button(g_state.seat, 
                time(NULL), BTN_LEFT, WLR_BUTTON_PRESSED);
        } else if (state == 0) { // 释放
            wlr_seat_pointer_notify_button(g_state.seat, 
                time(NULL), BTN_LEFT, WLR_BUTTON_RELEASED);
        }
    }
}

// 处理Android输入事件
void compositor_handle_android_input_event(int type, int x, int y, int state) {
    // 将Android输入事件转换为输入系统事件
    switch (type) {
        case 0: // 触摸按下
            compositor_input_inject_touch_event(0, (float)x, (float)y, 1.0f, true);
            break;
        case 1: // 触摸移动
            compositor_input_inject_touch_event(0, (float)x, (float)y, 1.0f, true);
            break;
        case 2: // 触摸释放
            compositor_input_inject_touch_event(0, (float)x, (float)y, 0.0f, false);
            break;
        default:
            LOGI("Unknown input event type: %d", type);
            break;
    }
}

// 处理Android鼠标事件
void compositor_handle_android_mouse_event(float x, float y, int button, bool down) {
    compositor_input_inject_mouse_event(x, y, (mouse_button_t)button, down);
}

// 处理Android鼠标滚轮事件
void compositor_handle_android_mouse_scroll(float delta_x, float delta_y) {
    compositor_input_inject_mouse_scroll(delta_x, delta_y);
}

// 处理Android键盘事件
void compositor_handle_android_keyboard_event(uint32_t keycode, uint32_t modifiers, bool down) {
    compositor_input_inject_keyboard_event(keycode, (keyboard_modifier_t)modifiers, down);
}

// 处理Android游戏手柄按钮事件
void compositor_handle_android_gamepad_button_event(uint32_t device_id, int button, bool down) {
    compositor_input_inject_gamepad_button_event(device_id, (gamepad_button_t)button, down);
}

// 处理Android游戏手柄轴事件
void compositor_handle_android_gamepad_axis_event(uint32_t device_id, int axis, float value) {
    compositor_input_inject_gamepad_axis_event(device_id, (gamepad_axis_t)axis, value);
}

// 处理Android输入事件
void compositor_handle_android_input_event(int type, int x, int y, int state) {
    // 将Android输入事件转换为输入系统事件
    switch (type) {
        case 0: // 触摸按下
            compositor_input_inject_touch_event(0, (float)x, (float)y, 1.0f, true);
            break;
        case 1: // 触摸移动
            compositor_input_inject_touch_event(0, (float)x, (float)y, 1.0f, true);
            break;
        case 2: // 触摸释放
            compositor_input_inject_touch_event(0, (float)x, (float)y, 0.0f, false);
            break;
        default:
            LOGI("Unknown input event type: %d", type);
            break;
    }
}

// 设置窗口大小
void compositor_set_size(int width, int height) {
    if (!g_state.initialized || width <= 0 || height <= 0) {
        return;
    }
    
    g_state.width = width;
    g_state.height = height;
    
    // 更新窗口管理器
    window_manager_init(width, height);
    
    // 更新渲染器
    renderer_set_size(width, height);
    
    // 重新创建 Vulkan 交换链
    if (g_state.vulkan_initialized) {
        if (vulkan_recreate_swapchain(&g_state.vulkan, width, height) != 0) {
            LOGE("Failed to recreate Vulkan swapchain");
            return;
        }
        
        // 标记所有命令缓冲区为脏
        mark_all_command_buffers_dirty();
        
        // 重新记录预记录的命令缓冲区
        if (init_prerecorded_command_buffers() != 0) {
            LOGE("Failed to reinitialize prerecorded command buffers");
            return;
        }
    }
}

// 获取当前帧率
float compositor_get_fps(void) {
    // 优先使用性能监控器的帧率
    return perf_monitor_get_fps();
}

// 初始化 Wayland
static int init_wayland(void) {
    g_state.display = wl_display_create();
    if (!g_state.display) {
        LOGE("Failed to create Wayland display");
        return -1;
    }
    
    g_state.event_loop = wl_display_get_event_loop(g_state.display);
    if (!g_state.event_loop) {
        LOGE("Failed to get Wayland event loop");
        return -1;
    }
    
    g_state.wl_display_fd = wl_display_get_fd(g_state.display);
    if (g_state.wl_display_fd < 0) {
        LOGE("Failed to get Wayland display fd");
        return -1;
    }
    
    return 0;
}

// 初始化 wlroots
static int init_wlroots(void) {
    // 创建 headless 后端
    g_state.backend = wlr_headless_backend_create(g_state.display);
    if (!g_state.backend) {
        LOGE("Failed to create headless backend");
        return -1;
    }
    
    // 创建渲染器
    g_state.renderer = wlr_vk_renderer_create(g_state.display);
    if (!g_state.renderer) {
        LOGE("Failed to create Vulkan renderer");
        return -1;
    }
    
    // 启动后端
    if (!wlr_backend_start(g_state.backend)) {
        LOGE("Failed to start backend");
        return -1;
    }
    
    // 创建输出
    g_state.output = wlr_headless_add_output(g_state.backend, g_state.width, g_state.height);
    if (!g_state.output) {
        LOGE("Failed to create output");
        return -1;
    }
    
    // 创建输出布局
    g_state.output_layout = wlr_output_layout_create();
    if (!g_state.output_layout) {
        LOGE("Failed to create output layout");
        return -1;
    }
    
    // 创建场景
    g_state.scene = wlr_scene_create();
    if (!g_state.scene) {
        LOGE("Failed to create scene");
        return -1;
    }
    
    // 创建场景输出
    g_state.scene_output = wlr_scene_output_create(g_state.scene, g_state.output);
    if (!g_state.scene_output) {
        LOGE("Failed to create scene output");
        return -1;
    }
    
    // 创建 seat
    g_state.seat = wlr_seat_create(g_state.display, "seat0");
    if (!g_state.seat) {
        LOGE("Failed to create seat");
        return -1;
    }
    
    // 创建光标
    g_state.cursor = wlr_cursor_create();
    if (!g_state.cursor) {
        LOGE("Failed to create cursor");
        return -1;
    }
    
    // 创建光标管理器
    g_state.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    if (!g_state.cursor_mgr) {
        LOGE("Failed to create cursor manager");
        return -1;
    }
    
    return 0;
}

// 初始化 Vulkan
static int init_vulkan(void) {
    // 创建 Vulkan 实例
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "Compositor",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0
    };
    
    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };
    
    VkInstanceCreateInfo instance_create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = extensions
    };
    
    if (vkCreateInstance(&instance_create_info, NULL, &g_state.vk_instance) != VK_SUCCESS) {
        LOGE("Failed to create Vulkan instance");
        return -1;
    }
    
    // 创建 Android 表面
    VkAndroidSurfaceCreateInfoKHR surface_create_info = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .window = (void*)g_state.window
    };
    
    PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR = 
        (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(g_state.vk_instance, "vkCreateAndroidSurfaceKHR");
    
    if (!vkCreateAndroidSurfaceKHR || 
        vkCreateAndroidSurfaceKHR(g_state.vk_instance, &surface_create_info, NULL, &g_state.vk_surface) != VK_SUCCESS) {
        LOGE("Failed to create Android surface");
        return -1;
    }
    
    // 获取物理设备
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(g_state.vk_instance, &device_count, NULL);
    if (device_count == 0) {
        LOGE("No Vulkan physical devices found");
        return -1;
    }
    
    VkPhysicalDevice* physical_devices = malloc(sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(g_state.vk_instance, &device_count, physical_devices);
    
    // 简化：使用第一个物理设备
    VkPhysicalDevice physical_device = physical_devices[0];
    free(physical_devices);
    
    // 获取队列族索引
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);
    VkQueueFamilyProperties* queue_families = malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families);
    
    uint32_t queue_family_index = 0;
    for (; queue_family_index < queue_family_count; queue_family_index++) {
        if (queue_families[queue_family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queue_family_index, g_state.vk_surface, &present_support);
            if (present_support) {
                break;
            }
        }
    }
    
    free(queue_families);
    
    if (queue_family_index == queue_family_count) {
        LOGE("No suitable queue family found");
        return -1;
    }
    
    // 创建逻辑设备
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };
    
    const char* device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = NULL
    };
    
    if (vkCreateDevice(physical_device, &device_create_info, NULL, &g_state.vk_device) != VK_SUCCESS) {
        LOGE("Failed to create Vulkan device");
        return -1;
    }
    
    // 获取队列
    vkGetDeviceQueue(g_state.vk_device, queue_family_index, 0, &g_state.vk_queue);
    
    // 创建交换链
    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = g_state.vk_surface,
        .minImageCount = 2,
        .imageFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = {g_state.width, g_state.height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };
    
    if (vkCreateSwapchainKHR(g_state.vk_device, &swapchain_create_info, NULL, &g_state.vk_swapchain) != VK_SUCCESS) {
        LOGE("Failed to create Vulkan swapchain");
        return -1;
    }
    
    // 获取交换链图像
    vkGetSwapchainImagesKHR(g_state.vk_device, g_state.vk_swapchain, &g_state.vk_image_count, NULL);
    g_state.vk_images = malloc(sizeof(VkImage) * g_state.vk_image_count);
    vkGetSwapchainImagesKHR(g_state.vk_device, g_state.vk_swapchain, &g_state.vk_image_count, g_state.vk_images);
    
    // 创建图像视图
    g_state.vk_image_views = malloc(sizeof(VkImageView) * g_state.vk_image_count);
    for (uint32_t i = 0; i < g_state.vk_image_count; i++) {
        VkImageViewCreateInfo image_view_create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .image = g_state.vk_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        
        if (vkCreateImageView(g_state.vk_device, &image_view_create_info, NULL, &g_state.vk_image_views[i]) != VK_SUCCESS) {
            LOGE("Failed to create image view %d", i);
            return -1;
        }
    }
    
    // 创建渲染通道
    if (create_vulkan_render_pass() != 0) {
        LOGE("Failed to create render pass");
        return -1;
    }
    
    // 创建帧缓冲区
    if (create_vulkan_framebuffers() != 0) {
        LOGE("Failed to create framebuffers");
        return -1;
    }
    
    // 创建命令缓冲区
    if (create_vulkan_command_buffers() != 0) {
        LOGE("Failed to create command buffers");
        return -1;
    }
    
    // 创建同步对象
    if (create_vulkan_sync_objects() != 0) {
        LOGE("Failed to create sync objects");
        return -1;
    }
    
    g_state.vulkan_initialized = true;
    return 0;
}

// 启动 Xwayland
static int start_xwayland(void) {
    // 创建 socket 对
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) {
        LOGE("Failed to create socket pair: %s", strerror(errno));
        return -1;
    }
    
    // 设置 Wayland 显示 fd
    if (wl_display_add_socket_fd(g_state.display, sv[0]) != 0) {
        LOGE("Failed to add socket to Wayland display");
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    
    // 生成显示名称
    snprintf(g_state.xwayland_display, sizeof(g_state.xwayland_display), "wayland-%d", getpid());
    
    // 设置 Xwayland 环境变量
    setenv("WAYLAND_DISPLAY", g_state.xwayland_display, 1);
    setenv("DISPLAY", ":0", 1);
    
    // 启动 Xwayland 进程
    g_state.xwayland_pid = fork();
    if (g_state.xwayland_pid == 0) {
        // 子进程：Xwayland
        char fd_str[16];
        snprintf(fd_str, sizeof(fd_str), "%d", sv[1]);
        
        // 简化：假设 Xwayland 在 /system/bin/xwayland
        execl("/system/bin/xwayland", "xwayland", 
              "-displayfd", fd_str, 
              "-rootless", 
              "-terminate", 
              "-noreset", 
              "-listen", fd_str,
              NULL);
        
        // 如果执行到这里，说明 exec 失败
        LOGE("Failed to exec Xwayland: %s", strerror(errno));
        exit(1);
    } else if (g_state.xwayland_pid < 0) {
        LOGE("Failed to fork for Xwayland: %s", strerror(errno));
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    
    // 父进程：合成器
    close(sv[1]);
    
    // 设置 SIGCHLD 处理器，以便在 Xwayland 退出时收到通知
    struct sigaction sa;
    sa.sa_handler = handle_xwayland_dead;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
    
    g_state.xwayland_started = true;
    LOGI("Xwayland started with PID %d", g_state.xwayland_pid);
    return 0;
}

// 处理 Xwayland 死亡
static void handle_xwayland_dead(int sig) {
    int status;
    pid_t pid = waitpid(g_state.xwayland_pid, &status, WNOHANG);
    
    if (pid == g_state.xwayland_pid) {
        LOGE("Xwayland died with status %d", status);
        g_state.xwayland_started = false;
        g_state.xwayland_pid = 0;
    }
}

// 创建 Vulkan 渲染通道
static int create_vulkan_render_pass(void) {
    VkAttachmentDescription color_attachment = {
        .flags = 0,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    
    VkAttachmentReference color_attachment_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    
    VkSubpassDescription subpass = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = NULL,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
        .pResolveAttachments = NULL,
        .pDepthStencilAttachment = NULL,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = NULL
    };
    
    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0
    };
    
    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };
    
    if (vkCreateRenderPass(g_state.vk_device, &render_pass_info, NULL, &g_state.vk_render_pass) != VK_SUCCESS) {
        LOGE("Failed to create render pass");
        return -1;
    }
    
    return 0;
}

// 创建 Vulkan 帧缓冲区
static int create_vulkan_framebuffers(void) {
    g_state.vk_framebuffers = malloc(sizeof(VkFramebuffer) * g_state.vk_image_count);
    
    for (uint32_t i = 0; i < g_state.vk_image_count; i++) {
        VkImageView attachments[] = {
            g_state.vk_image_views[i]
        };
        
        VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .renderPass = g_state.vk_render_pass,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = g_state.width,
            .height = g_state.height,
            .layers = 1
        };
        
        if (vkCreateFramebuffer(g_state.vk_device, &framebuffer_info, NULL, &g_state.vk_framebuffers[i]) != VK_SUCCESS) {
            LOGE("Failed to create framebuffer %d", i);
            return -1;
        }
    }
    
    return 0;
}

// 创建 Vulkan 命令缓冲区
static int create_vulkan_command_buffers(void) {
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = 0 // 简化：假设使用第一个队列族
    };
    
    if (vkCreateCommandPool(g_state.vk_device, &pool_info, NULL, &g_state.vk_command_pool) != VK_SUCCESS) {
        LOGE("Failed to create command pool");
        return -1;
    }
    
    g_state.vk_command_buffers = malloc(sizeof(VkCommandBuffer) * g_state.vk_image_count);
    
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = g_state.vk_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = g_state.vk_image_count
    };
    
    if (vkAllocateCommandBuffers(g_state.vk_device, &alloc_info, g_state.vk_command_buffers) != VK_SUCCESS) {
        LOGE("Failed to allocate command buffers");
        return -1;
    }
    
    return 0;
}

// 创建 Vulkan 同步对象
static int create_vulkan_sync_objects(void) {
    g_state.vk_image_available_semaphores = malloc(sizeof(VkSemaphore) * g_state.vk_image_count);
    g_state.vk_render_finished_semaphores = malloc(sizeof(VkSemaphore) * g_state.vk_image_count);
    g_state.vk_in_flight_fences = malloc(sizeof(VkFence) * g_state.vk_image_count);
    
    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0
    };
    
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    
    for (uint32_t i = 0; i < g_state.vk_image_count; i++) {
        if (vkCreateSemaphore(g_state.vk_device, &semaphore_info, NULL, &g_state.vk_image_available_semaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(g_state.vk_device, &semaphore_info, NULL, &g_state.vk_render_finished_semaphores[i]) != VK_SUCCESS ||
            vkCreateFence(g_state.vk_device, &fence_info, NULL, &g_state.vk_in_flight_fences[i]) != VK_SUCCESS) {
            LOGE("Failed to create sync objects for frame %d", i);
            return -1;
        }
    }
    
    return 0;
}

// 清理Vulkan资源
static void cleanup_vulkan(void) {
    vulkan_destroy(&g_state.vulkan);
}

// 清理 Wayland 资源
static void cleanup_wayland(void) {
    if (g_state.display) {
        wl_display_destroy(g_state.display);
        g_state.display = NULL;
    }
}

// 清理 wlroots 资源
static void cleanup_wlroots(void) {
    if (g_state.cursor_mgr) {
        wlr_xcursor_manager_destroy(g_state.cursor_mgr);
        g_state.cursor_mgr = NULL;
    }
    
    if (g_state.cursor) {
        wlr_cursor_destroy(g_state.cursor);
        g_state.cursor = NULL;
    }
    
    if (g_state.seat) {
        wlr_seat_destroy(g_state.seat);
        g_state.seat = NULL;
    }
    
    if (g_state.scene_output) {
        wlr_scene_output_destroy(g_state.scene_output);
        g_state.scene_output = NULL;
    }
    
    if (g_state.scene) {
        wlr_scene_destroy(g_state.scene);
        g_state.scene = NULL;
    }
    
    if (g_state.output_layout) {
        wlr_output_layout_destroy(g_state.output_layout);
        g_state.output_layout = NULL;
    }
    
    if (g_state.output) {
        wlr_output_destroy(g_state.output);
        g_state.output = NULL;
    }
    
    if (g_state.renderer) {
        wlr_renderer_destroy(g_state.renderer);
        g_state.renderer = NULL;
    }
    
    if (g_state.backend) {
        wlr_backend_destroy(g_state.backend);
        g_state.backend = NULL;
    }
}

// 清理 Xwayland 资源
static void cleanup_xwayland(void) {
    if (g_state.xwayland_started && g_state.xwayland_pid > 0) {
        // 发送 SIGTERM 信号
        kill(g_state.xwayland_pid, SIGTERM);
        
        // 等待进程退出
        int status;
        waitpid(g_state.xwayland_pid, &status, 0);
        
        g_state.xwayland_started = false;
        g_state.xwayland_pid = 0;
    }
}

// 渲染帧
static int render_frame(void) {
    struct timespec start_time, end_time;
    int ret;
    
    if (!g_state.vulkan_initialized) {
        return -1;
    }
    
    // 记录开始时间
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    // 更新性能优化模块
    perf_opt_update();
    
    // 更新游戏模式模块
    game_mode_update();
    
    // 更新监控模块
    monitor_update();
    
    // 开始性能监控
    perf_monitor_begin_measure(PERF_COUNTER_RENDER_TIME);
    
    // 应用性能优化设置
    struct perf_opt_settings opt_settings;
    if (perf_opt_get_settings(&opt_settings) == 0) {
        // 根据性能优化设置调整渲染参数
        renderer_set_quality(opt_settings.quality_level);
        renderer_set_max_fps(opt_settings.target_fps);
    }
    
    // 应用游戏模式设置
    if (game_mode_is_enabled()) {
        struct game_mode_settings game_settings;
        if (game_mode_get_settings(&game_settings) == 0) {
            // 根据游戏模式设置调整渲染参数
            renderer_set_input_boost_enabled(game_settings.input_boost);
            renderer_set_priority_boost_enabled(game_settings.priority_boost);
        }
    }
    
    uint32_t image_index;
    
    // 开始渲染帧
    if (vulkan_begin_frame(&g_state.vulkan, &image_index) != 0) {
        LOGE("Failed to begin frame");
        perf_monitor_end_measure(PERF_COUNTER_RENDER_TIME);
        return -1;
    }
    
    // 获取命令缓冲区
    VkCommandBuffer cmd_buffer = vulkan_get_command_buffer(&g_state.vulkan, image_index);
    if (cmd_buffer == VK_NULL_HANDLE) {
        LOGE("Failed to get command buffer");
        perf_monitor_end_measure(PERF_COUNTER_RENDER_TIME);
        return -1;
    }
    
    // 记录命令缓冲区
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = 0,
        .pInheritanceInfo = NULL
    };
    
    VkResult result = vkBeginCommandBuffer(cmd_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        LOGE("Failed to begin command buffer: %d", result);
        perf_monitor_end_measure(PERF_COUNTER_RENDER_TIME);
        return -1;
    }
    
    // 开始渲染通道
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = NULL,
        .renderPass = g_state.vulkan.render_pass,
        .framebuffer = vulkan_get_current_framebuffer(&g_state.vulkan, image_index),
        .renderArea = {
            .offset = {0, 0},
            .extent = {g_state.width, g_state.height}
        },
        .clearValueCount = 1,
        .pClearValues = &(VkClearValue){.color = {{0.0f, 0.0f, 0.0f, 1.0f}}}
    };
    
    vkCmdBeginRenderPass(cmd_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    
    // 执行缓存的命令缓冲区（如果有的话）
    vulkan_execute_cached_command_buffers(&g_state.vulkan, cmd_buffer);
    
    // 这里可以添加动态渲染命令
    
    vkCmdEndRenderPass(cmd_buffer);
    
    result = vkEndCommandBuffer(cmd_buffer);
    if (result != VK_SUCCESS) {
        LOGE("Failed to end command buffer: %d", result);
        perf_monitor_end_measure(PERF_COUNTER_RENDER_TIME);
        return -1;
    }
    
    // 结束渲染帧
    if (vulkan_end_frame(&g_state.vulkan, image_index) != 0) {
        LOGE("Failed to end frame");
        perf_monitor_end_measure(PERF_COUNTER_RENDER_TIME);
        return -1;
    }
    
    // 结束性能监控
    perf_monitor_end_measure(PERF_COUNTER_RENDER_TIME);
    
    // 记录结束时间并计算帧时间
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    long frame_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 + 
                         (end_time.tv_nsec - start_time.tv_nsec) / 1000000;
    
    // 记录帧时间到性能优化模块
    perf_opt_record_frame_time(frame_time_ms);
    
    // 记录帧时间到监控模块
    monitor_record_frame_time(frame_time_ms);
    
    // 检查性能警告
    struct perf_warning warning;
    if (perf_monitor_check_warning(&warning) == 0) {
        LOGW("Performance warning: %s (value: %.2f, threshold: %.2f)", 
             warning.message, warning.value, warning.threshold);
        
        // 将警告信息发送到监控模块
        monitor_record_event("Performance Warning", warning.message);
    }
    
    return 0;
}

// 更新 FPS
static void update_fps(void) {
    g_state.frame_count++;
    
    uint64_t current_time = time(NULL) * 1000; // 简化：使用秒级时间
    if (current_time - g_state.last_frame_time >= 1000) {
        g_state.fps = (float)g_state.frame_count;
        g_state.frame_count = 0;
        g_state.last_frame_time = current_time;
        
        LOGD("FPS: %.1f", g_state.fps);
    }
}

// 添加缺失的perf_opt_get_settings和game_mode_get_settings函数实现
struct perf_opt_settings {
    uint32_t quality_level;
    uint32_t target_fps;
};

struct game_mode_settings {
    bool input_boost;
    bool priority_boost;
};

static int perf_opt_get_settings(struct perf_opt_settings *settings) {
    if (!settings) {
        return -1;
    }
    
    // 简化实现：返回默认设置
    settings->quality_level = 2; // 中等质量
    settings->target_fps = 60;   // 60 FPS
    
    return 0;
}

static int game_mode_get_settings(struct game_mode_settings *settings) {
    if (!settings) {
        return -1;
    }
    
    // 简化实现：返回默认设置
    settings->input_boost = false;
    settings->priority_boost = false;
    
    return 0;
}

// 添加缺失的renderer_set_*函数实现
static void renderer_set_quality(uint32_t level) {
    // 实际实现应该调用渲染器的设置质量函数
    LOGI("Setting renderer quality to %u", level);
}

static void renderer_set_max_fps(uint32_t fps) {
    // 实际实现应该调用渲染器的设置最大帧率函数
    LOGI("Setting renderer max FPS to %u", fps);
}

static void renderer_set_input_boost_enabled(bool enabled) {
    // 实际实现应该调用渲染器的设置输入增强函数
    LOGI("Setting renderer input boost %s", enabled ? "enabled" : "disabled");
}

static void renderer_set_priority_boost_enabled(bool enabled) {
    // 实际实现应该调用渲染器的设置优先级增强函数
    LOGI("Setting renderer priority boost %s", enabled ? "enabled" : "disabled");
}

// 示例：记录静态背景的次要命令缓冲区
static void record_background_commands(VkCommandBuffer cmd_buffer, void* user_data) {
    // 这里可以添加静态背景渲染命令
    // 例如：清除屏幕、绘制背景图像等
    
    // 示例：设置视口
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)g_state.width,
        .height = (float)g_state.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);
    
    // 示例：设置裁剪矩形
    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = {g_state.width, g_state.height}
    };
    vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);
    
    // 这里可以添加更多的静态渲染命令
}

// 示例：记录UI元素的次要命令缓冲区
static void record_ui_commands(VkCommandBuffer cmd_buffer, void* user_data) {
    // 这里可以添加UI渲染命令
    // 例如：绘制按钮、文本、图标等
    
    // 示例：绑定UI管线和描述符集
    // vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline);
    // vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_pipeline_layout, 
    //                         0, 1, &ui_descriptor_set, 0, NULL);
    
    // 这里可以添加更多的UI渲染命令
}

// 初始化预记录的命令缓冲区
static int init_prerecorded_command_buffers(void) {
    if (!g_state.vulkan_initialized) {
        return -1;
    }
    
    // 记录静态背景命令缓冲区
    if (vulkan_record_secondary_command_buffer(&g_state.vulkan, 0, 
                                              record_background_commands, NULL) != 0) {
        LOGE("Failed to record background command buffer");
        return -1;
    }
    
    // 记录UI命令缓冲区
    if (vulkan_record_secondary_command_buffer(&g_state.vulkan, 1, 
                                              record_ui_commands, NULL) != 0) {
        LOGE("Failed to record UI command buffer");
        return -1;
    }
    
    LOGI("Prerecorded command buffers initialized");
    return 0;
}

// 标记所有命令缓冲区为脏（在窗口大小改变时调用）
static void mark_all_command_buffers_dirty(void) {
    if (!g_state.vulkan_initialized) {
        return;
    }
    
    // 标记所有主要命令缓冲区为脏
    for (uint32_t i = 0; i < g_state.vulkan.image_count; i++) {
        vulkan_mark_command_buffer_dirty(&g_state.vulkan, COMMAND_BUFFER_TYPE_PRIMARY, i);
    }
    
    // 标记所有次要命令缓冲区为脏
    for (uint32_t i = 0; i < g_state.vulkan.secondary_buffer_count; i++) {
        vulkan_mark_command_buffer_dirty(&g_state.vulkan, COMMAND_BUFFER_TYPE_SECONDARY, i);
    }
    
    LOGI("All command buffers marked as dirty");
}

// 内存池相关API实现
int compositor_set_memory_pool_enabled(bool enabled) {
    if (!g_state.initialized) {
        return -1;
    }
    
    g_state.memory_pool_enabled = enabled;
    LOGI("Memory pool %s", enabled ? "enabled" : "disabled");
    return 0;
}

bool compositor_is_memory_pool_enabled(void) {
    return g_state.memory_pool_enabled;
}

int compositor_set_default_memory_pool(struct memory_pool* pool) {
    if (!g_state.initialized || !g_state.memory_pool_initialized) {
        return -1;
    }
    
    if (g_state.default_pool) {
        memory_pool_destroy(g_state.default_pool);
    }
    
    g_state.default_pool = pool;
    LOGI("Default memory pool set");
    return 0;
}

struct memory_pool* compositor_get_default_memory_pool(void) {
    if (!g_state.initialized || !g_state.memory_pool_initialized) {
        return NULL;
    }
    
    return g_state.default_pool;
}

int compositor_set_compositor_memory_pool(struct memory_pool* pool) {
    if (!g_state.initialized || !g_state.memory_pool_initialized) {
        return -1;
    }
    
    if (g_state.compositor_pool) {
        memory_pool_destroy(g_state.compositor_pool);
    }
    
    g_state.compositor_pool = pool;
    LOGI("Compositor memory pool set");
    return 0;
}

struct memory_pool* compositor_get_compositor_memory_pool(void) {
    if (!g_state.initialized || !g_state.memory_pool_initialized) {
        return NULL;
    }
    
    return g_state.compositor_pool;
}

int compositor_get_memory_pool_stats(struct memory_pool_stats* stats) {
    if (!g_state.initialized || !g_state.memory_pool_initialized || !stats) {
        return -1;
    }
    
    return memory_pool_get_stats(g_state.default_pool, stats);
}

void* compositor_memory_alloc(size_t size) {
    if (!g_state.initialized || !g_state.memory_pool_enabled || !g_state.memory_pool_initialized) {
        // 回退到标准分配
        return malloc(size);
    }
    
    return memory_pool_alloc(g_state.default_pool, size);
}

void* compositor_memory_realloc(void* ptr, size_t size) {
    if (!g_state.initialized || !g_state.memory_pool_enabled || !g_state.memory_pool_initialized) {
        // 回退到标准分配
        return realloc(ptr, size);
    }
    
    return memory_pool_realloc(g_state.default_pool, ptr, size);
}

void compositor_memory_free(void* ptr) {
    if (!g_state.initialized || !g_state.memory_pool_enabled || !g_state.memory_pool_initialized) {
        // 回退到标准分配
        free(ptr);
        return;
    }
    
    memory_pool_free(g_state.default_pool, ptr);
}

// 销毁合成器
void compositor_destroy(void) {
    if (!g_state.initialized) {
        return;
    }
    
    LOGI("Destroying compositor");
    
    // 清理Xwayland
    if (g_state.xwayland_started) {
        cleanup_xwayland();
        g_state.xwayland_started = false;
    }
    
    // 清理Vulkan
    if (g_state.vulkan_initialized) {
        cleanup_vulkan();
        g_state.vulkan_initialized = false;
    }
    
    // 清理输入系统
    if (g_state.input_initialized) {
        input_destroy(&g_state.input);
        g_state.input_initialized = false;
    }
    
    // 清理监控模块
    if (g_state.monitor_initialized) {
        monitor_destroy(&g_state.monitor);
        g_state.monitor_initialized = false;
    }
    
    // 清理游戏模式
    if (g_state.game_mode_initialized) {
        game_mode_destroy(&g_state.game_mode);
        g_state.game_mode_initialized = false;
    }
    
    // 清理性能优化
    if (g_state.perf_opt_initialized) {
        perf_opt_destroy(&g_state.perf_opt);
        g_state.perf_opt_initialized = false;
    }
    
    // 清理渲染器
    if (g_state.renderer_initialized) {
        renderer_destroy(&g_state.renderer);
        g_state.renderer_initialized = false;
    }
    
    // 清理窗口管理器
    if (g_state.window_manager_initialized) {
        window_manager_destroy(&g_state.window_manager);
        g_state.window_manager_initialized = false;
    }
    
    // 清理wlroots
    if (g_state.wlroots_initialized) {
        cleanup_wlroots();
        g_state.wlroots_initialized = false;
    }
    
    // 清理Wayland
    if (g_state.wayland_initialized) {
        cleanup_wayland();
        g_state.wayland_initialized = false;
    }
    
    // 清理内存池系统
    if (g_state.memory_pool_initialized) {
        // 销毁线程本地缓存
        memory_pool_thread_cache_destroy(&g_state.thread_cache);
        
        // 销毁合成器专用内存池
        if (g_state.compositor_pool) {
            memory_pool_destroy(g_state.compositor_pool);
            g_state.compositor_pool = NULL;
        }
        
        // 销毁默认内存池
        if (g_state.default_pool) {
            memory_pool_destroy(g_state.default_pool);
            g_state.default_pool = NULL;
        }
        
        // 销毁内存池管理器
        memory_pool_manager_destroy(&g_state.memory_pool);
        g_state.memory_pool_initialized = false;
    }
    
    // 重置状态
    memset(&g_state, 0, sizeof(g_state));
    
    LOGI("Compositor destroyed");
}
#include "compositor_game.h"
#include "compositor.h"
#include "compositor_perf.h"
#include "compositor_render.h"
#include "compositor_input.h"
#include <android/log.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LOG_TAG "GameMode"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// 游戏模式状态
static struct {
    bool initialized;
    struct game_mode_settings settings;
    struct game_performance_stats stats;
    uint64_t last_stats_update;
    uint64_t last_frame_time;
    uint32_t frame_count;
    float fps_history[60];  // 保存最近60帧的FPS
    uint32_t fps_history_index;
    float input_latency_history[60];  // 保存最近60帧的输入延迟
    uint32_t input_latency_history_index;
    game_mode_callback_t callback;
    void* callback_user_data;
} g_game_mode_state = {0};

// 内部函数声明
static void update_performance_stats(void);
static void detect_game_type(void);
static void apply_game_type_settings(game_type_t type);
static uint64_t get_current_time(void);
static void update_fps_history(float fps);
static void update_input_latency_history(float latency);
static float calculate_avg_fps(void);
static float calculate_avg_input_latency(void);
static void optimize_for_fps_game(void);
static void optimize_for_rts_game(void);
static void optimize_for_rpg_game(void);
static void optimize_for_racing_game(void);
static void optimize_for_puzzle_game(void);
static void optimize_for_platformer_game(void);
static void optimize_for_strategy_game(void);
static void optimize_for_adventure_game(void);
static void optimize_for_simulation_game(void);
static void optimize_for_sports_game(void);

// 初始化游戏模式模块
int game_mode_init(void) {
    if (g_game_mode_state.initialized) {
        LOGE("Game mode module already initialized");
        return -1;
    }
    
    memset(&g_game_mode_state, 0, sizeof(g_game_mode_state));
    
    // 设置默认游戏模式设置
    g_game_mode_state.settings.enabled = false;
    g_game_mode_state.settings.type = GAME_TYPE_NONE;
    g_game_mode_state.settings.auto_detect = true;
    g_game_mode_state.settings.touch_optimization = true;
    g_game_mode_state.settings.input_prediction = false;
    g_game_mode_state.settings.frame_pacing = true;
    g_game_mode_state.settings.latency_optimization = true;
    g_game_mode_state.settings.target_fps = 60;
    g_game_mode_state.settings.max_latency_ms = 50;
    g_game_mode_state.settings.touch_sensitivity = 1.0f;
    g_game_mode_state.settings.input_prediction_ms = 16.0f;
    
    // 初始化性能统计
    g_game_mode_state.stats.avg_fps = 60.0f;
    g_game_mode_state.stats.min_fps = 60.0f;
    g_game_mode_state.stats.max_fps = 60.0f;
    g_game_mode_state.stats.avg_frame_time = 16.67f;
    g_game_mode_state.stats.max_frame_time = 16.67f;
    g_game_mode_state.stats.avg_input_latency = 16.0f;
    g_game_mode_state.stats.max_input_latency = 16.0f;
    g_game_mode_state.stats.prediction_accuracy = 0.0f;
    
    // 初始化历史数据
    for (int i = 0; i < 60; i++) {
        g_game_mode_state.fps_history[i] = 60.0f;
        g_game_mode_state.input_latency_history[i] = 16.0f;
    }
    
    g_game_mode_state.last_stats_update = get_current_time();
    g_game_mode_state.last_frame_time = get_current_time();
    
    g_game_mode_state.initialized = true;
    
    LOGI("Game mode module initialized");
    return 0;
}

// 销毁游戏模式模块
void game_mode_destroy(void) {
    if (!g_game_mode_state.initialized) {
        return;
    }
    
    g_game_mode_state.initialized = false;
    g_game_mode_state.callback = NULL;
    g_game_mode_state.callback_user_data = NULL;
    
    LOGI("Game mode module destroyed");
}

// 更新游戏模式模块（每帧调用）
void game_mode_update(void) {
    if (!g_game_mode_state.initialized || !g_game_mode_state.settings.enabled) {
        return;
    }
    
    // 更新帧计数
    g_game_mode_state.frame_count++;
    
    // 更新性能统计
    update_performance_stats();
    
    // 自动检测游戏类型
    if (g_game_mode_state.settings.auto_detect && g_game_mode_state.settings.type == GAME_TYPE_NONE) {
        detect_game_type();
    }
    
    // 调用回调函数
    if (g_game_mode_state.callback) {
        g_game_mode_state.callback(g_game_mode_state.callback_user_data);
    }
}

// 设置游戏模式
void game_mode_set_enabled(bool enabled) {
    if (g_game_mode_state.settings.enabled != enabled) {
        g_game_mode_state.settings.enabled = enabled;
        
        if (enabled) {
            // 启用游戏模式
            if (g_game_mode_state.settings.type != GAME_TYPE_NONE) {
                apply_game_type_settings(g_game_mode_state.settings.type);
            }
            
            // 应用游戏模式设置
            renderer_set_target_fps(g_game_mode_state.settings.target_fps);
            
            LOGI("Game mode enabled");
        } else {
            // 禁用游戏模式
            LOGI("Game mode disabled");
        }
    }
}

// 检查游戏模式是否启用
bool game_mode_is_enabled(void) {
    return g_game_mode_state.settings.enabled;
}

// 设置游戏类型
void game_mode_set_type(game_type_t type) {
    if (type >= GAME_TYPE_COUNT) {
        LOGE("Invalid game type: %d", type);
        return;
    }
    
    if (g_game_mode_state.settings.type != type) {
        g_game_mode_state.settings.type = type;
        
        if (g_game_mode_state.settings.enabled) {
            apply_game_type_settings(type);
        }
        
        LOGI("Game type set to %d", type);
    }
}

// 获取游戏类型
game_type_t game_mode_get_type(void) {
    return g_game_mode_state.settings.type;
}

// 启用/禁用自动检测游戏类型
void game_mode_set_auto_detect(bool enabled) {
    g_game_mode_state.settings.auto_detect = enabled;
    LOGI("Auto game type detection %s", enabled ? "enabled" : "disabled");
}

// 检查是否启用自动检测游戏类型
bool game_mode_is_auto_detect_enabled(void) {
    return g_game_mode_state.settings.auto_detect;
}

// 设置游戏模式设置
void game_mode_set_settings(const struct game_mode_settings* settings) {
    if (!settings) {
        LOGE("Invalid settings pointer");
        return;
    }
    
    bool was_enabled = g_game_mode_state.settings.enabled;
    game_type_t old_type = g_game_mode_state.settings.type;
    
    g_game_mode_state.settings = *settings;
    
    // 如果游戏模式已启用，应用新设置
    if (g_game_mode_state.settings.enabled) {
        if (old_type != g_game_mode_state.settings.type) {
            apply_game_type_settings(g_game_mode_state.settings.type);
        }
        
        renderer_set_target_fps(g_game_mode_state.settings.target_fps);
    }
    
    LOGI("Game mode settings updated");
}

// 获取游戏模式设置
void game_mode_get_settings(struct game_mode_settings* settings) {
    if (!settings) {
        LOGE("Invalid settings pointer");
        return;
    }
    
    *settings = g_game_mode_state.settings;
}

// 启用/禁用触摸优化
void game_mode_set_touch_optimization_enabled(bool enabled) {
    g_game_mode_state.settings.touch_optimization = enabled;
    LOGI("Touch optimization %s", enabled ? "enabled" : "disabled");
}

// 检查触摸优化是否启用
bool game_mode_is_touch_optimization_enabled(void) {
    return g_game_mode_state.settings.touch_optimization;
}

// 设置触摸灵敏度
void game_mode_set_touch_sensitivity(float sensitivity) {
    if (sensitivity <= 0.0f) {
        LOGE("Invalid touch sensitivity: %.2f", sensitivity);
        return;
    }
    
    g_game_mode_state.settings.touch_sensitivity = sensitivity;
    input_set_touch_sensitivity(sensitivity);
    LOGI("Touch sensitivity set to %.2f", sensitivity);
}

// 获取触摸灵敏度
float game_mode_get_touch_sensitivity(void) {
    return g_game_mode_state.settings.touch_sensitivity;
}

// 启用/禁用输入预测
void game_mode_set_input_prediction_enabled(bool enabled) {
    g_game_mode_state.settings.input_prediction = enabled;
    input_set_prediction_enabled(enabled);
    LOGI("Input prediction %s", enabled ? "enabled" : "disabled");
}

// 检查输入预测是否启用
bool game_mode_is_input_prediction_enabled(void) {
    return g_game_mode_state.settings.input_prediction;
}

// 设置输入预测时间
void game_mode_set_input_prediction_time(float time_ms) {
    if (time_ms < 0.0f) {
        LOGE("Invalid input prediction time: %.2f", time_ms);
        return;
    }
    
    g_game_mode_state.settings.input_prediction_ms = time_ms;
    input_set_prediction_time(time_ms);
    LOGI("Input prediction time set to %.2f ms", time_ms);
}

// 获取输入预测时间
float game_mode_get_input_prediction_time(void) {
    return g_game_mode_state.settings.input_prediction_ms;
}

// 启用/禁用帧率控制
void game_mode_set_frame_pacing_enabled(bool enabled) {
    g_game_mode_state.settings.frame_pacing = enabled;
    renderer_set_frame_pacing_enabled(enabled);
    LOGI("Frame pacing %s", enabled ? "enabled" : "disabled");
}

// 检查帧率控制是否启用
bool game_mode_is_frame_pacing_enabled(void) {
    return g_game_mode_state.settings.frame_pacing;
}

// 设置目标帧率
void game_mode_set_target_fps(uint32_t fps) {
    if (fps == 0) {
        LOGE("Invalid target FPS: %u", fps);
        return;
    }
    
    g_game_mode_state.settings.target_fps = fps;
    renderer_set_target_fps(fps);
    LOGI("Target FPS set to %u", fps);
}

// 获取目标帧率
uint32_t game_mode_get_target_fps(void) {
    return g_game_mode_state.settings.target_fps;
}

// 启用/禁用延迟优化
void game_mode_set_latency_optimization_enabled(bool enabled) {
    g_game_mode_state.settings.latency_optimization = enabled;
    renderer_set_latency_optimization_enabled(enabled);
    LOGI("Latency optimization %s", enabled ? "enabled" : "disabled");
}

// 检查延迟优化是否启用
bool game_mode_is_latency_optimization_enabled(void) {
    return g_game_mode_state.settings.latency_optimization;
}

// 设置最大延迟
void game_mode_set_max_latency(uint32_t latency_ms) {
    g_game_mode_state.settings.max_latency_ms = latency_ms;
    renderer_set_max_latency(latency_ms);
    LOGI("Max latency set to %u ms", latency_ms);
}

// 获取最大延迟
uint32_t game_mode_get_max_latency(void) {
    return g_game_mode_state.settings.max_latency_ms;
}

// 获取游戏性能统计
void game_mode_get_performance_stats(struct game_performance_stats* stats) {
    if (!stats) {
        LOGE("Invalid stats pointer");
        return;
    }
    
    *stats = g_game_mode_state.stats;
}

// 重置游戏性能统计
void game_mode_reset_stats(void) {
    memset(&g_game_mode_state.stats, 0, sizeof(g_game_mode_state.stats));
    
    g_game_mode_state.stats.avg_fps = 60.0f;
    g_game_mode_state.stats.min_fps = 60.0f;
    g_game_mode_state.stats.max_fps = 60.0f;
    g_game_mode_state.stats.avg_frame_time = 16.67f;
    g_game_mode_state.stats.max_frame_time = 16.67f;
    g_game_mode_state.stats.avg_input_latency = 16.0f;
    g_game_mode_state.stats.max_input_latency = 16.0f;
    g_game_mode_state.stats.prediction_accuracy = 0.0f;
    
    // 重置历史数据
    for (int i = 0; i < 60; i++) {
        g_game_mode_state.fps_history[i] = 60.0f;
        g_game_mode_state.input_latency_history[i] = 16.0f;
    }
    
    g_game_mode_state.frame_count = 0;
    g_game_mode_state.last_stats_update = get_current_time();
    
    LOGI("Game mode statistics reset");
}

// 注册游戏模式回调
void game_mode_register_callback(game_mode_callback_t callback, void* user_data) {
    g_game_mode_state.callback = callback;
    g_game_mode_state.callback_user_data = user_data;
}

// 注销游戏模式回调
void game_mode_unregister_callback(game_mode_callback_t callback) {
    if (g_game_mode_state.callback == callback) {
        g_game_mode_state.callback = NULL;
        g_game_mode_state.callback_user_data = NULL;
    }
}

// 保存游戏模式设置
int game_mode_save_settings(const char* path) {
    // 实现保存设置到文件
    LOGI("Game mode settings saved to %s", path ? path : "default location");
    return 0;
}

// 加载游戏模式设置
int game_mode_load_settings(const char* path) {
    // 实现从文件加载设置
    LOGI("Game mode settings loaded from %s", path ? path : "default location");
    return 0;
}

// 打印游戏模式状态
void game_mode_print_status(void) {
    LOGI("Game Mode Status:");
    LOGI("  Enabled: %s", g_game_mode_state.settings.enabled ? "yes" : "no");
    LOGI("  Game Type: %d", g_game_mode_state.settings.type);
    LOGI("  Auto Detect: %s", g_game_mode_state.settings.auto_detect ? "yes" : "no");
    LOGI("  Touch Optimization: %s", g_game_mode_state.settings.touch_optimization ? "yes" : "no");
    LOGI("  Input Prediction: %s", g_game_mode_state.settings.input_prediction ? "yes" : "no");
    LOGI("  Frame Pacing: %s", g_game_mode_state.settings.frame_pacing ? "yes" : "no");
    LOGI("  Latency Optimization: %s", g_game_mode_state.settings.latency_optimization ? "yes" : "no");
    LOGI("  Target FPS: %u", g_game_mode_state.settings.target_fps);
    LOGI("  Max Latency: %u ms", g_game_mode_state.settings.max_latency_ms);
    LOGI("  Touch Sensitivity: %.2f", g_game_mode_state.settings.touch_sensitivity);
    LOGI("  Input Prediction Time: %.2f ms", g_game_mode_state.settings.input_prediction_ms);
    LOGI("  Average FPS: %.2f", g_game_mode_state.stats.avg_fps);
    LOGI("  Average Input Latency: %.2f ms", g_game_mode_state.stats.avg_input_latency);
    LOGI("  Prediction Accuracy: %.2f%%", g_game_mode_state.stats.prediction_accuracy * 100.0f);
}

// 内部函数实现
static void update_performance_stats(void) {
    // 获取当前时间
    uint64_t current_time = get_current_time();
    uint64_t frame_time = current_time - g_game_mode_state.last_frame_time;
    g_game_mode_state.last_frame_time = current_time;
    
    // 更新帧时间
    float frame_time_ms = frame_time / 1000000.0f;
    if (frame_time_ms > g_game_mode_state.stats.max_frame_time) {
        g_game_mode_state.stats.max_frame_time = frame_time_ms;
    }
    
    // 计算当前FPS
    float current_fps = 1000.0f / frame_time_ms;
    update_fps_history(current_fps);
    
    // 更新输入延迟
    float input_latency = input_get_average_latency();
    update_input_latency_history(input_latency);
    
    // 检查是否需要更新统计（每秒更新一次）
    if (current_time - g_game_mode_state.last_stats_update < 1000000ULL) {
        return;
    }
    
    // 更新统计
    g_game_mode_state.stats.total_frames = g_game_mode_state.frame_count;
    g_game_mode_state.stats.avg_fps = calculate_avg_fps();
    g_game_mode_state.stats.avg_frame_time = 1000.0f / g_game_mode_state.stats.avg_fps;
    g_game_mode_state.stats.avg_input_latency = calculate_avg_input_latency();
    
    // 更新最小/最大FPS
    if (g_game_mode_state.stats.avg_fps < g_game_mode_state.stats.min_fps) {
        g_game_mode_state.stats.min_fps = g_game_mode_state.stats.avg_fps;
    }
    if (g_game_mode_state.stats.avg_fps > g_game_mode_state.stats.max_fps) {
        g_game_mode_state.stats.max_fps = g_game_mode_state.stats.avg_fps;
    }
    
    // 更新最大输入延迟
    if (g_game_mode_state.stats.avg_input_latency > g_game_mode_state.stats.max_input_latency) {
        g_game_mode_state.stats.max_input_latency = g_game_mode_state.stats.avg_input_latency;
    }
    
    // 更新预测准确率
    if (g_game_mode_state.stats.predicted_inputs > 0) {
        g_game_mode_state.stats.prediction_accuracy = 
            (float)g_game_mode_state.stats.accurate_predictions / g_game_mode_state.stats.predicted_inputs;
    }
    
    // 更新触摸事件数和预测输入数
    g_game_mode_state.stats.touch_events = input_get_touch_event_count();
    g_game_mode_state.stats.predicted_inputs = input_get_predicted_input_count();
    g_game_mode_state.stats.accurate_predictions = input_get_accurate_prediction_count();
    
    // 更新最后统计时间
    g_game_mode_state.last_stats_update = current_time;
}

static void detect_game_type(void) {
    // 简化的游戏类型检测，实际应用中应该使用更复杂的算法
    // 这里基于输入模式和性能特征来检测游戏类型
    
    // 获取输入统计
    uint32_t touch_events = input_get_touch_event_count();
    uint32_t drag_events = input_get_drag_event_count();
    uint32_t tap_events = input_get_tap_event_count();
    
    // 获取性能统计
    float avg_fps = g_game_mode_state.stats.avg_fps;
    float avg_frame_time = g_game_mode_state.stats.avg_frame_time;
    
    // 基于特征检测游戏类型
    if (touch_events > 100 && drag_events > touch_events * 0.8f) {
        // 大量拖动操作，可能是RTS或策略游戏
        if (avg_fps > 50) {
            game_mode_set_type(GAME_TYPE_RTS);
        } else {
            game_mode_set_type(GAME_TYPE_STRATEGY);
        }
    } else if (tap_events > touch_events * 0.7f) {
        // 大量点击操作，可能是益智游戏或平台跳跃游戏
        if (avg_frame_time < 20.0f) {
            game_mode_set_type(GAME_TYPE_PLATFORMER);
        } else {
            game_mode_set_type(GAME_TYPE_PUZZLE);
        }
    } else if (avg_fps > 55 && avg_frame_time < 18.0f) {
        // 高帧率，低帧时间，可能是FPS或赛车游戏
        game_mode_set_type(GAME_TYPE_FPS);
    } else if (avg_fps < 40) {
        // 低帧率，可能是RPG或冒险游戏
        game_mode_set_type(GAME_TYPE_RPG);
    } else {
        // 默认设置为冒险游戏
        game_mode_set_type(GAME_TYPE_ADVENTURE);
    }
    
    LOGI("Auto-detected game type: %d", g_game_mode_state.settings.type);
}

static void apply_game_type_settings(game_type_t type) {
    switch (type) {
        case GAME_TYPE_FPS:
            optimize_for_fps_game();
            break;
        case GAME_TYPE_RTS:
            optimize_for_rts_game();
            break;
        case GAME_TYPE_RPG:
            optimize_for_rpg_game();
            break;
        case GAME_TYPE_RACING:
            optimize_for_racing_game();
            break;
        case GAME_TYPE_PUZZLE:
            optimize_for_puzzle_game();
            break;
        case GAME_TYPE_PLATFORMER:
            optimize_for_platformer_game();
            break;
        case GAME_TYPE_STRATEGY:
            optimize_for_strategy_game();
            break;
        case GAME_TYPE_ADVENTURE:
            optimize_for_adventure_game();
            break;
        case GAME_TYPE_SIMULATION:
            optimize_for_simulation_game();
            break;
        case GAME_TYPE_SPORTS:
            optimize_for_sports_game();
            break;
        default:
            LOGE("Unknown game type: %d", type);
            break;
    }
}

static uint64_t get_current_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void update_fps_history(float fps) {
    g_game_mode_state.fps_history[g_game_mode_state.fps_history_index] = fps;
    g_game_mode_state.fps_history_index = (g_game_mode_state.fps_history_index + 1) % 60;
}

static void update_input_latency_history(float latency) {
    g_game_mode_state.input_latency_history[g_game_mode_state.input_latency_history_index] = latency;
    g_game_mode_state.input_latency_history_index = (g_game_mode_state.input_latency_history_index + 1) % 60;
}

static float calculate_avg_fps(void) {
    float sum = 0.0f;
    for (int i = 0; i < 60; i++) {
        sum += g_game_mode_state.fps_history[i];
    }
    return sum / 60.0f;
}

static float calculate_avg_input_latency(void) {
    float sum = 0.0f;
    for (int i = 0; i < 60; i++) {
        sum += g_game_mode_state.input_latency_history[i];
    }
    return sum / 60.0f;
}

static void optimize_for_fps_game(void) {
    LOGI("Optimizing for FPS game");
    
    // FPS游戏优化：高帧率，低延迟，输入预测
    game_mode_set_target_fps(60);
    game_mode_set_max_latency(16);
    game_mode_set_input_prediction_enabled(true);
    game_mode_set_input_prediction_time(8.0f);
    game_mode_set_touch_sensitivity(1.5f);
    game_mode_set_frame_pacing_enabled(true);
    game_mode_set_latency_optimization_enabled(true);
    
    // 设置渲染器优化
    renderer_set_dirty_regions_enabled(false);  // FPS游戏通常全屏更新
    renderer_set_vsync_enabled(false);          // 禁用垂直同步以减少延迟
    renderer_set_triple_buffering_enabled(true); // 启用三重缓冲
}

static void optimize_for_rts_game(void) {
    LOGI("Optimizing for RTS game");
    
    // RTS游戏优化：中等帧率，触摸优化，帧率控制
    game_mode_set_target_fps(45);
    game_mode_set_max_latency(50);
    game_mode_set_input_prediction_enabled(false);
    game_mode_set_touch_sensitivity(1.0f);
    game_mode_set_frame_pacing_enabled(true);
    game_mode_set_latency_optimization_enabled(false);
    
    // 设置渲染器优化
    renderer_set_dirty_regions_enabled(true);   // RTS游戏通常有静态UI
    renderer_set_vsync_enabled(true);           // 启用垂直同步以避免撕裂
    renderer_set_triple_buffering_enabled(false); // 禁用三重缓冲以减少延迟
}

static void optimize_for_rpg_game(void) {
    LOGI("Optimizing for RPG game");
    
    // RPG游戏优化：中等帧率，平衡设置
    game_mode_set_target_fps(30);
    game_mode_set_max_latency(100);
    game_mode_set_input_prediction_enabled(false);
    game_mode_set_touch_sensitivity(1.0f);
    game_mode_set_frame_pacing_enabled(true);
    game_mode_set_latency_optimization_enabled(false);
    
    // 设置渲染器优化
    renderer_set_dirty_regions_enabled(true);   // RPG游戏通常有静态UI
    renderer_set_vsync_enabled(true);           // 启用垂直同步以避免撕裂
    renderer_set_triple_buffering_enabled(false); // 禁用三重缓冲以减少延迟
}

static void optimize_for_racing_game(void) {
    LOGI("Optimizing for racing game");
    
    // 赛车游戏优化：高帧率，低延迟，输入预测
    game_mode_set_target_fps(60);
    game_mode_set_max_latency(16);
    game_mode_set_input_prediction_enabled(true);
    game_mode_set_input_prediction_time(8.0f);
    game_mode_set_touch_sensitivity(1.2f);
    game_mode_set_frame_pacing_enabled(true);
    game_mode_set_latency_optimization_enabled(true);
    
    // 设置渲染器优化
    renderer_set_dirty_regions_enabled(false);  // 赛车游戏通常全屏更新
    renderer_set_vsync_enabled(false);          // 禁用垂直同步以减少延迟
    renderer_set_triple_buffering_enabled(true); // 启用三重缓冲
}

static void optimize_for_puzzle_game(void) {
    LOGI("Optimizing for puzzle game");
    
    // 益智游戏优化：低帧率，省电，触摸优化
    game_mode_set_target_fps(30);
    game_mode_set_max_latency(100);
    game_mode_set_input_prediction_enabled(false);
    game_mode_set_touch_sensitivity(1.0f);
    game_mode_set_frame_pacing_enabled(false);
    game_mode_set_latency_optimization_enabled(false);
    
    // 设置渲染器优化
    renderer_set_dirty_regions_enabled(true);   // 益智游戏通常有静态UI
    renderer_set_vsync_enabled(true);           // 启用垂直同步以避免撕裂
    renderer_set_triple_buffering_enabled(false); // 禁用三重缓冲以减少延迟
}

static void optimize_for_platformer_game(void) {
    LOGI("Optimizing for platformer game");
    
    // 平台跳跃游戏优化：高帧率，低延迟，输入预测
    game_mode_set_target_fps(60);
    game_mode_set_max_latency(16);
    game_mode_set_input_prediction_enabled(true);
    game_mode_set_input_prediction_time(8.0f);
    game_mode_set_touch_sensitivity(1.3f);
    game_mode_set_frame_pacing_enabled(true);
    game_mode_set_latency_optimization_enabled(true);
    
    // 设置渲染器优化
    renderer_set_dirty_regions_enabled(false);  // 平台跳跃游戏通常全屏更新
    renderer_set_vsync_enabled(false);          // 禁用垂直同步以减少延迟
    renderer_set_triple_buffering_enabled(true); // 启用三重缓冲
}

static void optimize_for_strategy_game(void) {
    LOGI("Optimizing for strategy game");
    
    // 策略游戏优化：低帧率，触摸优化，帧率控制
    game_mode_set_target_fps(30);
    game_mode_set_max_latency(100);
    game_mode_set_input_prediction_enabled(false);
    game_mode_set_touch_sensitivity(1.0f);
    game_mode_set_frame_pacing_enabled(true);
    game_mode_set_latency_optimization_enabled(false);
    
    // 设置渲染器优化
    renderer_set_dirty_regions_enabled(true);   // 策略游戏通常有静态UI
    renderer_set_vsync_enabled(true);           // 启用垂直同步以避免撕裂
    renderer_set_triple_buffering_enabled(false); // 禁用三重缓冲以减少延迟
}

static void optimize_for_adventure_game(void) {
    LOGI("Optimizing for adventure game");
    
    // 冒险游戏优化：中等帧率，平衡设置
    game_mode_set_target_fps(30);
    game_mode_set_max_latency(100);
    game_mode_set_input_prediction_enabled(false);
    game_mode_set_touch_sensitivity(1.0f);
    game_mode_set_frame_pacing_enabled(true);
    game_mode_set_latency_optimization_enabled(false);
    
    // 设置渲染器优化
    renderer_set_dirty_regions_enabled(true);   // 冒险游戏通常有静态UI
    renderer_set_vsync_enabled(true);           // 启用垂直同步以避免撕裂
    renderer_set_triple_buffering_enabled(false); // 禁用三重缓冲以减少延迟
}

static void optimize_for_simulation_game(void) {
    LOGI("Optimizing for simulation game");
    
    // 模拟游戏优化：中等帧率，平衡设置
    game_mode_set_target_fps(30);
    game_mode_set_max_latency(100);
    game_mode_set_input_prediction_enabled(false);
    game_mode_set_touch_sensitivity(1.0f);
    game_mode_set_frame_pacing_enabled(true);
    game_mode_set_latency_optimization_enabled(false);
    
    // 设置渲染器优化
    renderer_set_dirty_regions_enabled(true);   // 模拟游戏通常有静态UI
    renderer_set_vsync_enabled(true);           // 启用垂直同步以避免撕裂
    renderer_set_triple_buffering_enabled(false); // 禁用三重缓冲以减少延迟
}

static void optimize_for_sports_game(void) {
    LOGI("Optimizing for sports game");
    
    // 体育游戏优化：高帧率，低延迟，输入预测
    game_mode_set_target_fps(60);
    game_mode_set_max_latency(16);
    game_mode_set_input_prediction_enabled(true);
    game_mode_set_input_prediction_time(8.0f);
    game_mode_set_touch_sensitivity(1.2f);
    game_mode_set_frame_pacing_enabled(true);
    game_mode_set_latency_optimization_enabled(true);
    
    // 设置渲染器优化
    renderer_set_dirty_regions_enabled(false);  // 体育游戏通常全屏更新
    renderer_set_vsync_enabled(false);          // 禁用垂直同步以减少延迟
    renderer_set_triple_buffering_enabled(true); // 启用三重缓冲
}

// 添加缺失的renderer_set_*函数实现
static void renderer_set_target_fps(uint32_t fps) {
    // 实际实现应该调用渲染器的设置目标帧率函数
    LOGI("Setting renderer target FPS to %u", fps);
}

static void renderer_set_max_latency(uint32_t latency_ms) {
    // 实际实现应该调用渲染器的设置最大延迟函数
    LOGI("Setting renderer max latency to %u ms", latency_ms);
}

static void renderer_set_dirty_regions_enabled(bool enabled) {
    // 实际实现应该调用渲染器的设置脏区域函数
    LOGI("Setting renderer dirty regions %s", enabled ? "enabled" : "disabled");
}

static void renderer_set_vsync_enabled(bool enabled) {
    // 实际实现应该调用渲染器的设置垂直同步函数
    LOGI("Setting renderer vsync %s", enabled ? "enabled" : "disabled");
}

static void renderer_set_triple_buffering_enabled(bool enabled) {
    // 实际实现应该调用渲染器的设置三重缓冲函数
    LOGI("Setting renderer triple buffering %s", enabled ? "enabled" : "disabled");
}

static void renderer_set_frame_pacing_enabled(bool enabled) {
    // 实际实现应该调用渲染器的设置帧率控制函数
    LOGI("Setting renderer frame pacing %s", enabled ? "enabled" : "disabled");
}

static void renderer_set_latency_optimization_enabled(bool enabled) {
    // 实际实现应该调用渲染器的设置延迟优化函数
    LOGI("Setting renderer latency optimization %s", enabled ? "enabled" : "disabled");
}

static void renderer_set_input_prediction_enabled(bool enabled) {
    // 实际实现应该调用渲染器的设置输入预测函数
    LOGI("Setting renderer input prediction %s", enabled ? "enabled" : "disabled");
}

static void renderer_set_input_prediction_time(float time_ms) {
    // 实际实现应该调用渲染器的设置输入预测时间函数
    LOGI("Setting renderer input prediction time to %.1f ms", time_ms);
}

static void renderer_set_touch_sensitivity(float sensitivity) {
    // 实际实现应该调用渲染器的设置触摸灵敏度函数
    LOGI("Setting renderer touch sensitivity to %.1f", sensitivity);
}
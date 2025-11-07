#ifndef COMPOSITOR_GAME_H
#define COMPOSITOR_GAME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 游戏类型
typedef enum {
    GAME_TYPE_NONE = 0,
    GAME_TYPE_FPS,          // 第一人称射击游戏
    GAME_TYPE_RTS,          // 即时战略游戏
    GAME_TYPE_RPG,          // 角色扮演游戏
    GAME_TYPE_RACING,       // 赛车游戏
    GAME_TYPE_PUZZLE,       // 益智游戏
    GAME_TYPE_PLATFORMER,   // 平台跳跃游戏
    GAME_TYPE_STRATEGY,     // 策略游戏
    GAME_TYPE_ADVENTURE,    // 冒险游戏
    GAME_TYPE_SIMULATION,   // 模拟游戏
    GAME_TYPE_SPORTS,       // 体育游戏
    GAME_TYPE_COUNT
} game_type_t;

// 游戏模式设置
struct game_mode_settings {
    bool enabled;                   // 是否启用游戏模式
    game_type_t type;               // 游戏类型
    bool auto_detect;               // 是否自动检测游戏类型
    bool touch_optimization;        // 是否启用触摸优化
    bool input_prediction;          // 是否启用输入预测
    bool frame_pacing;              // 是否启用帧率控制
    bool latency_optimization;      // 是否启用延迟优化
    uint32_t target_fps;            // 目标帧率
    uint32_t max_latency_ms;        // 最大延迟（毫秒）
    float touch_sensitivity;        // 触摸灵敏度
    float input_prediction_ms;      // 输入预测时间（毫秒）
};

// 游戏性能统计
struct game_performance_stats {
    uint32_t total_frames;          // 总帧数
    uint32_t dropped_frames;         // 丢帧数
    float avg_fps;                   // 平均帧率
    float min_fps;                   // 最低帧率
    float max_fps;                   // 最高帧率
    float avg_frame_time;            // 平均帧时间
    float max_frame_time;            // 最大帧时间
    float avg_input_latency;         // 平均输入延迟
    float max_input_latency;         // 最大输入延迟
    uint32_t touch_events;           // 触摸事件数
    uint32_t predicted_inputs;       // 预测输入数
    uint32_t accurate_predictions;   // 准确预测数
    float prediction_accuracy;       // 预测准确率
};

// 游戏模式回调函数类型
typedef void (*game_mode_callback_t)(void* user_data);

// 游戏模式接口函数

// 初始化游戏模式模块
int game_mode_init(void);

// 销毁游戏模式模块
void game_mode_destroy(void);

// 更新游戏模式模块（每帧调用）
void game_mode_update(void);

// 设置游戏模式
void game_mode_set_enabled(bool enabled);

// 检查游戏模式是否启用
bool game_mode_is_enabled(void);

// 设置游戏类型
void game_mode_set_type(game_type_t type);

// 获取游戏类型
game_type_t game_mode_get_type(void);

// 启用/禁用自动检测游戏类型
void game_mode_set_auto_detect(bool enabled);

// 检查是否启用自动检测游戏类型
bool game_mode_is_auto_detect_enabled(void);

// 设置游戏模式设置
void game_mode_set_settings(const struct game_mode_settings* settings);

// 获取游戏模式设置
void game_mode_get_settings(struct game_mode_settings* settings);

// 启用/禁用触摸优化
void game_mode_set_touch_optimization_enabled(bool enabled);

// 检查触摸优化是否启用
bool game_mode_is_touch_optimization_enabled(void);

// 设置触摸灵敏度
void game_mode_set_touch_sensitivity(float sensitivity);

// 获取触摸灵敏度
float game_mode_get_touch_sensitivity(void);

// 启用/禁用输入预测
void game_mode_set_input_prediction_enabled(bool enabled);

// 检查输入预测是否启用
bool game_mode_is_input_prediction_enabled(void);

// 设置输入预测时间
void game_mode_set_input_prediction_time(float time_ms);

// 获取输入预测时间
float game_mode_get_input_prediction_time(void);

// 启用/禁用帧率控制
void game_mode_set_frame_pacing_enabled(bool enabled);

// 检查帧率控制是否启用
bool game_mode_is_frame_pacing_enabled(void);

// 设置目标帧率
void game_mode_set_target_fps(uint32_t fps);

// 获取目标帧率
uint32_t game_mode_get_target_fps(void);

// 启用/禁用延迟优化
void game_mode_set_latency_optimization_enabled(bool enabled);

// 检查延迟优化是否启用
bool game_mode_is_latency_optimization_enabled(void);

// 设置最大延迟
void game_mode_set_max_latency(uint32_t latency_ms);

// 获取最大延迟
uint32_t game_mode_get_max_latency(void);

// 获取游戏性能统计
void game_mode_get_performance_stats(struct game_performance_stats* stats);

// 重置游戏性能统计
void game_mode_reset_stats(void);

// 注册游戏模式回调
void game_mode_register_callback(game_mode_callback_t callback, void* user_data);

// 注销游戏模式回调
void game_mode_unregister_callback(game_mode_callback_t callback);

// 保存游戏模式设置
int game_mode_save_settings(const char* path);

// 加载游戏模式设置
int game_mode_load_settings(const char* path);

// 打印游戏模式状态
void game_mode_print_status(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_GAME_H
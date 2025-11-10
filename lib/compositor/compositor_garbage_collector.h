#ifndef COMPOSITOR_GARBAGE_COLLECTOR_H
#define COMPOSITOR_GARBAGE_COLLECTOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// 日志宏定义（避免依赖Android日志）
#ifndef LOG_TAG
#define LOG_TAG "GarbageCollector"
#endif

#ifdef ANDROID
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <stdio.h>
#define LOGI(...) printf("[INFO] " LOG_TAG ": " __VA_ARGS__)
#define LOGE(...) printf("[ERROR] " LOG_TAG ": " __VA_ARGS__)
#endif

// 垃圾回收策略
typedef enum {
    GC_STRATEGY_BASIC,          // 基础GC
    GC_STRATEGY_GENERATIONAL,   // 分代GC
    GC_STRATEGY_INCREMENTAL,    // 增量GC
    GC_STRATEGY_CONCURRENT,     // 并发GC
    GC_STRATEGY_ADAPTIVE        // 自适应GC
} gc_strategy_t;

// GC状态
typedef enum {
    GC_STATE_IDLE,              // 空闲
    GC_STATE_MARKING,           // 标记阶段
    GC_STATE_SWEEPING,          // 清除阶段
    GC_STATE_COMPACTING,        // 压缩阶段
    GC_STATE_FINALIZING         // 终结阶段
} gc_state_t;

// 对象类别（用于自适应GC）
typedef enum {
    GC_OBJECT_CLASS_SHORT_LIVED,   // 短生命周期对象
    GC_OBJECT_CLASS_MEDIUM_LIVED,  // 中生命周期对象
    GC_OBJECT_CLASS_LONG_LIVED,    // 长生命周期对象
    GC_OBJECT_CLASS_STATIC         // 静态对象
} gc_object_class_t;

// 代际（用于分代GC）
typedef enum {
    GC_GENERATION_YOUNG,        // 新生代
    GC_GENERATION_MIDDLE,       // 中年代
    GC_GENERATION_OLD,          // 老年代
    GC_GENERATION_PERMANENT,    // 终身代
    GC_GENERATION_COUNT         // 代际数量
} gc_generation_t;

// 增量GC阶段
typedef enum {
    GC_INCREMENTAL_PHASE_MARK,  // 标记阶段
    GC_INCREMENTAL_PHASE_SWEEP  // 清除阶段
} gc_incremental_phase_t;

// 对象状态
typedef enum {
    GC_OBJ_STATE_UNREACHABLE,   // 不可达
    GC_OBJ_STATE_REACHABLE,     // 可达
    GC_OBJ_STATE_FINALIZABLE,   // 可终结
    GC_OBJ_STATE_FINALIZED      // 已终结
} gc_obj_state_t;

// 对象颜色（用于三色标记算法）
typedef enum {
    GC_COLOR_WHITE,             // 白色（未被标记）
    GC_COLOR_GRAY,              // 灰色（已标记但引用未处理）
    GC_COLOR_BLACK              // 黑色（已标记且引用已处理）
} gc_color_t;

// 对象年龄（用于分代GC）
typedef enum {
    GC_AGE_YOUNG,               // 年轻代
    GC_AGE_MIDDLE,              // 中年代
    GC_AGE_OLD                  // 老年代
} gc_age_t;

// GC对象
struct gc_object {
    void* resource;              // 资源指针
    size_t size;                // 对象大小
    uint64_t creation_time;     // 创建时间
    uint64_t last_access_time;  // 最后访问时间
    uint32_t ref_count;         // 引用计数
    gc_obj_state_t state;       // 对象状态
    gc_color_t color;           // 对象颜色
    gc_age_t age;               // 对象年龄
    gc_object_class_t class;    // 对象类别（用于自适应GC）
    bool marked;                // 是否已标记
    bool finalized;             // 是否已终结
    bool pinned;                // 是否固定
    struct gc_object* next;     // 下一个对象
    struct gc_object* prev;     // 前一个对象
    uint32_t generation;        // 所在代
    uint32_t access_count;      // 访问次数
    float access_frequency;     // 访问频率
};

// 分代GC统计
struct generational_gc_stats {
    uint32_t young_collections;  // 年轻代回收次数
    uint32_t middle_collections; // 中年代回收次数
    uint32_t old_collections;   // 老年代回收次数
    uint32_t promotions;         // 晋升次数
    uint32_t young_survivors;    // 年轻代存活对象数
    uint32_t middle_survivors;   // 中年代存活对象数
    uint32_t old_survivors;     // 老年代存活对象数
    uint64_t young_time;         // 年轻代回收时间
    uint64_t middle_time;        // 中年代回收时间
    uint64_t old_time;           // 老年代回收时间
    uint64_t total_time;         // 总回收时间
    float young_efficiency;     // 年轻代回收效率
    float middle_efficiency;    // 中年代回收效率
    float old_efficiency;       // 老年代回收效率
};

// 增量GC状态
struct incremental_gc_state {
    gc_state_t phase;            // 当前阶段
    uint32_t step_count;         // 步数
    uint32_t max_steps;          // 最大步数
    uint32_t step_size;          // 步长
    uint64_t time_budget;        // 时间预算
    uint64_t step_time;          // 步时间
    bool marking_complete;       // 标记是否完成
    bool sweeping_complete;      // 清除是否完成
    bool compacting_complete;    // 压缩是否完成
    struct gc_object* current_object; // 当前处理对象
    uint32_t objects_per_step;   // 每步处理对象数
    uint32_t marked_objects;     // 已标记对象数
    uint32_t swept_objects;      // 已清除对象数
    uint32_t compacted_objects;  // 已压缩对象数
};

// 并发GC状态
struct concurrent_gc_state {
    pthread_t gc_thread;         // GC线程
    bool gc_thread_running;      // GC线程是否运行
    bool gc_requested;           // 是否请求GC
    bool gc_in_progress;         // GC是否进行中
    pthread_mutex_t gc_mutex;    // GC互斥锁
    pthread_cond_t gc_cond;      // GC条件变量
    pthread_mutex_t request_mutex; // 请求互斥锁
    pthread_cond_t request_cond; // 请求条件变量
    uint64_t gc_interval;        // GC间隔
    uint64_t last_gc_time;       // 上次GC时间
    uint32_t gc_count;           // GC次数
    uint64_t total_gc_time;      // 总GC时间
    uint64_t max_gc_time;        // 最大GC时间
    uint64_t avg_gc_time;        // 平均GC时间
};

// 自适应GC状态
struct adaptive_gc_state {
    gc_strategy_t current_strategy; // 当前策略
    gc_strategy_t previous_strategy; // 上一个策略
    uint64_t strategy_switch_time;  // 策略切换时间
    uint32_t strategy_switch_count;  // 策略切换次数
    float efficiency_threshold;      // 效率阈值
    float pause_time_threshold;      // 暂停时间阈值
    float memory_pressure_threshold; // 内存压力阈值
    uint32_t evaluation_interval;     // 评估间隔
    uint64_t last_evaluation_time;   // 上次评估时间
    bool auto_adjust;                 // 自动调整
    uint32_t min_heap_size;           // 最小堆大小
    uint32_t max_heap_size;           // 最大堆大小
    uint32_t target_heap_size;        // 目标堆大小
    float gc_ratio;                   // GC比率
    float promotion_rate;             // 晋升率
    float survival_rate;              // 存活率
};

// 智能垃圾回收器
struct smart_gc {
    gc_strategy_t strategy;       // GC策略
    gc_state_t state;             // GC状态
    struct gc_object* objects;    // 对象列表
    uint32_t object_count;        // 对象数量
    size_t total_size;            // 总大小
    size_t used_size;             // 已使用大小
    uint64_t last_gc_time;        // 上次GC时间
    uint32_t gc_count;            // GC次数
    uint64_t total_gc_time;       // 总GC时间
    uint64_t max_gc_time;         // 最大GC时间
    uint64_t avg_gc_time;         // 平均GC时间
    uint32_t collected_objects;   // 已收集对象数
    size_t collected_size;        // 已收集大小
    struct generational_gc_stats gen_stats; // 分代GC统计
    struct incremental_gc_state inc_state; // 增量GC状态
    struct concurrent_gc_state conc_state;  // 并发GC状态
    struct adaptive_gc_state adapt_state;   // 自适应GC状态
    pthread_mutex_t gc_mutex;     // GC互斥锁
    pthread_cond_t gc_cond;       // GC条件变量
    pthread_mutex_t mutex;        // 互斥锁
    bool gc_enabled;              // GC是否启用
    bool auto_gc;                 // 是否自动GC
    uint64_t gc_interval;         // GC间隔
    uint32_t gc_threshold;        // GC阈值
    float gc_ratio;               // GC比率
    uint32_t max_pause_time;      // 最大暂停时间
    bool incremental_mode;        // 是否增量模式
    bool concurrent_mode;         // 是否并发模式
    bool adaptive_mode;           // 是否自适应模式
    void (*gc_callback)(void*);   // GC回调函数
    void* gc_callback_data;       // GC回调数据
    struct gc_object* young_generation;   // 年轻代
    struct gc_object* middle_generation;  // 中年代
    struct gc_object* old_generation;     // 老年代
    struct gc_object* permanent_generation; // 终身代
    uint32_t generation_counts[GC_GENERATION_COUNT]; // 各代对象数量
    size_t generation_sizes[GC_GENERATION_COUNT];    // 各代大小
    uint32_t promotion_thresholds[GC_GENERATION_COUNT-1]; // 晋升阈值
    uint32_t collection_counts[GC_GENERATION_COUNT]; // 各代回收次数
    uint64_t collection_times[GC_GENERATION_COUNT];  // 各代回收时间
    float collection_efficiencies[GC_GENERATION_COUNT]; // 各代回收效率
    uint32_t young_count;          // 年轻代对象数
    uint32_t middle_count;         // 中年代对象数
    uint32_t old_count;            // 老年代对象数
    size_t young_size;             // 年轻代大小
    size_t middle_size;            // 中年代大小
    size_t old_size;               // 老年代大小
    uint32_t young_threshold;      // 年轻代阈值
    uint32_t middle_threshold;     // 中年代阈值
    uint32_t old_threshold;        // 老年代阈值
    uint32_t promotion_age;        // 晋升年龄
    bool compacting;               // 是否压缩
    uint32_t compact_threshold;    // 压缩阈值
    uint32_t compact_interval;     // 压缩间隔
    uint64_t last_compact_time;     // 上次压缩时间
    bool finalizing;               // 是否终结
    uint32_t finalizer_queue_size; // 终结器队列大小
    struct gc_object* finalizer_queue; // 终结器队列
    pthread_mutex_t finalizer_mutex; // 终结器互斥锁
    pthread_cond_t finalizer_cond;   // 终结器条件变量
    pthread_t finalizer_thread;      // 终结器线程
    bool finalizer_thread_running;  // 终结器线程是否运行
};

// ==================== 基础垃圾回收API ====================

// 初始化垃圾回收器
int smart_gc_init(gc_strategy_t strategy, bool auto_gc, uint64_t gc_interval, uint32_t gc_threshold);

// 销毁垃圾回收器
void smart_gc_destroy(void);

// 添加资源到GC管理
int smart_gc_add_resource(void* resource, size_t size);

// 从GC管理中移除资源
void smart_gc_remove_resource(void* resource);

// 执行垃圾回收
void smart_gc_collect(void);

// 增加资源引用
void smart_gc_add_ref(void* resource);

// 减少资源引用
void smart_gc_release(void* resource);

// 设置GC策略
void smart_gc_set_strategy(gc_strategy_t strategy);

// 启用/禁用自动GC
void smart_gc_set_auto_gc(bool enabled);

// 设置GC间隔
void smart_gc_set_gc_interval(uint64_t interval);

// 设置GC阈值
void smart_gc_set_gc_threshold(uint32_t threshold);

// 获取GC统计
void smart_gc_get_stats(uint32_t* gc_count, uint64_t* total_time, uint64_t* max_time, uint64_t* avg_time,
                       uint32_t* collected_objects, size_t* collected_size);

// 重置GC统计
void smart_gc_reset_stats(void);

// 打印GC统计
void smart_gc_print_stats(void);

// 更新GC（每帧调用）
void smart_gc_update(void);

// ==================== 分代垃圾回收API ====================

// 初始化分代GC
int generational_gc_init(uint32_t young_threshold, uint32_t middle_threshold, uint32_t old_threshold,
                         uint32_t promotion_age);

// 销毁分代GC
void generational_gc_destroy(void);

// 执行年轻代GC
void generational_gc_collect_young(void);

// 执行中年代GC
void generational_gc_collect_middle(void);

// 执行老年代GC
void generational_gc_collect_old(void);

// 执行完整GC
void generational_gc_collect_full(void);

// 设置晋升年龄
void generational_gc_set_promotion_age(uint32_t age);

// 获取分代GC统计
void generational_gc_get_stats(struct generational_gc_stats* stats);

// 重置分代GC统计
void generational_gc_reset_stats(void);

// 打印分代GC统计
void generational_gc_print_stats(void);

// ==================== 增量垃圾回收API ====================

// 初始化增量GC
int incremental_gc_init(uint32_t max_steps, uint32_t step_size, uint64_t time_budget);

// 销毁增量GC
void incremental_gc_destroy(void);

// 执行增量GC步骤
bool incremental_gc_step(void);

// 设置最大步数
void incremental_gc_set_max_steps(uint32_t steps);

// 设置步长
void incremental_gc_set_step_size(uint32_t size);

// 设置时间预算
void incremental_gc_set_time_budget(uint64_t budget);

// 获取增量GC状态
void incremental_gc_get_state(struct incremental_gc_state* state);

// ==================== 并发垃圾回收API ====================

// 初始化并发GC
int concurrent_gc_init(uint64_t gc_interval);

// 销毁并发GC
void concurrent_gc_destroy(void);

// 请求并发GC
void concurrent_gc_request(void);

// 等待并发GC完成
void concurrent_gc_wait(void);

// 设置GC间隔
void concurrent_gc_set_interval(uint64_t interval);

// 获取并发GC状态
void concurrent_gc_get_state(struct concurrent_gc_state* state);

// ==================== 自适应垃圾回收API ====================

// 初始化自适应GC
int adaptive_gc_init(float efficiency_threshold, float pause_time_threshold, float memory_pressure_threshold,
                    uint32_t evaluation_interval, bool auto_adjust, uint32_t min_heap_size, uint32_t max_heap_size);

// 销毁自适应GC
void adaptive_gc_destroy(void);

// 评估GC策略
void adaptive_gc_evaluate(void);

// 设置效率阈值
void adaptive_gc_set_efficiency_threshold(float threshold);

// 设置暂停时间阈值
void adaptive_gc_set_pause_time_threshold(float threshold);

// 设置内存压力阈值
void adaptive_gc_set_memory_pressure_threshold(float threshold);

// 设置评估间隔
void adaptive_gc_set_evaluation_interval(uint32_t interval);

// 启用/禁用自动调整
void adaptive_gc_set_auto_adjust(bool enabled);

// 设置最小堆大小
void adaptive_gc_set_min_heap_size(uint32_t size);

// 设置最大堆大小
void adaptive_gc_set_max_heap_size(uint32_t size);

// 获取自适应GC状态
void adaptive_gc_get_state(struct adaptive_gc_state* state);

#ifdef __cplusplus
}
#endif

#endif // GARBAGE_COLLECTOR_H
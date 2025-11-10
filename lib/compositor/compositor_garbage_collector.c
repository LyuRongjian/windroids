#include "compositor_garbage_collector.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>

#define LOG_TAG "GarbageCollector"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 全局垃圾回收器实例
static struct optimized_gc g_gc = {0};

// ==================== 工具函数 ====================

// 获取当前时间（毫秒）
uint64_t gc_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ==================== 基础垃圾回收实现 ====================

// 初始化垃圾回收器
int gc_init(gc_strategy_t strategy) {
    // 初始化互斥锁
    if (pthread_mutex_init(&g_gc.mutex, NULL) != 0) {
        LOGE("初始化垃圾回收器互斥锁失败");
        return -1;
    }
    
    // 初始化条件变量
    if (pthread_cond_init(&g_gc.cond, NULL) != 0) {
        LOGE("初始化垃圾回收器条件变量失败");
        pthread_mutex_destroy(&g_gc.mutex);
        return -1;
    }
    
    // 初始化垃圾回收器
    memset(&g_gc, 0, sizeof(struct optimized_gc));
    g_gc.strategy = strategy;
    g_gc.state = GC_STATE_IDLE;
    g_gc.auto_gc = true;
    g_gc.gc_interval = 1000; // 默认1秒
    g_gc.last_gc_time = gc_get_time();
    g_gc.total_gc_time = 0;
    g_gc.total_gc_count = 0;
    g_gc.total_objects_freed = 0;
    g_gc.total_memory_freed = 0;
    
    // 初始化分代GC
    for (int i = 0; i < GC_GENERATION_COUNT; i++) {
        g_gc.generational_gc.generations[i].objects = NULL;
        g_gc.generational_gc.generations[i].object_count = 0;
        g_gc.generational_gc.generations[i].total_size = 0;
        g_gc.generational_gc.generations[i].threshold = (i == 0) ? 10 : (i == 1) ? 100 : 1000;
    }
    
    // 初始化增量GC
    g_gc.incremental_gc.phase = GC_INCREMENTAL_PHASE_MARK;
    g_gc.incremental_gc.work_units = 0;
    g_gc.incremental_gc.total_work_units = 100;
    g_gc.incremental_gc.mark_stack = NULL;
    g_gc.incremental_gc.mark_stack_size = 0;
    g_gc.incremental_gc.mark_stack_capacity = 1000;
    
    // 初始化并发GC
    g_gc.concurrent_gc.running = false;
    g_gc.concurrent_gc.thread = 0;
    g_gc.concurrent_gc.mark_queue = NULL;
    g_gc.concurrent_gc.mark_queue_size = 0;
    g_gc.concurrent_gc.mark_queue_capacity = 1000;
    
    // 初始化自适应GC
    g_gc.adaptive_gc.heap_size_threshold = 10 * 1024 * 1024; // 默认10MB
    g_gc.adaptive_gc.allocation_rate_threshold = 1024 * 1024; // 默认1MB/s
    g_gc.adaptive_gc.gc_time_threshold = 16; // 默认16ms
    g_gc.adaptive_gc.heap_size = 0;
    g_gc.adaptive_gc.allocation_rate = 0;
    g_gc.adaptive_gc.last_gc_time = gc_get_time();
    g_gc.adaptive_gc.last_allocation_count = 0;
    g_gc.adaptive_gc.last_gc_duration = 0;
    
    LOGI("垃圾回收器初始化完成，策略: %d", strategy);
    
    return 0;
}

// 销毁垃圾回收器
void gc_destroy(void) {
    // 停止并发GC
    if (g_gc.concurrent_gc.running) {
        gc_stop_concurrent_gc();
    }
    
    // 销毁互斥锁和条件变量
    pthread_mutex_destroy(&g_gc.mutex);
    pthread_cond_destroy(&g_gc.cond);
    
    // 释放增量GC标记栈
    if (g_gc.incremental_gc.mark_stack) {
        free(g_gc.incremental_gc.mark_stack);
    }
    
    // 释放并发GC标记队列
    if (g_gc.concurrent_gc.mark_queue) {
        free(g_gc.concurrent_gc.mark_queue);
    }
    
    LOGI("垃圾回收器已销毁");
}

// 添加对象到垃圾回收器
int gc_add_object(void* obj, size_t size, gc_object_class_t obj_class) {
    if (!obj) {
        LOGE("对象不能为空");
        return -1;
    }
    
    pthread_mutex_lock(&g_gc.mutex);
    
    // 创建GC对象
    struct gc_object* gc_obj = malloc(sizeof(struct gc_object));
    if (!gc_obj) {
        pthread_mutex_unlock(&g_gc.mutex);
        LOGE("分配GC对象失败");
        return -1;
    }
    
    // 初始化GC对象
    memset(gc_obj, 0, sizeof(struct gc_object));
    gc_obj->obj = obj;
    gc_obj->size = size;
    gc_obj->obj_class = obj_class;
    gc_obj->state = GC_OBJECT_STATE_ALLOCATED;
    gc_obj->color = GC_COLOR_WHITE;
    gc_obj->age = 0;
    gc_obj->marked = false;
    gc_obj->finalized = false;
    gc_obj->creation_time = gc_get_time();
    gc_obj->last_access_time = gc_obj->creation_time;
    gc_obj->access_count = 0;
    
    // 根据策略添加对象
    switch (g_gc.strategy) {
        case GC_STRATEGY_BASIC:
            // 基础策略，不特殊处理
            break;
            
        case GC_STRATEGY_GENERATIONAL:
            // 分代策略，添加到新生代
            gc_obj->generation = GC_GENERATION_YOUNG;
            gc_obj->next = g_gc.generational_gc.generations[GC_GENERATION_YOUNG].objects;
            g_gc.generational_gc.generations[GC_GENERATION_YOUNG].objects = gc_obj;
            g_gc.generational_gc.generations[GC_GENERATION_YOUNG].object_count++;
            g_gc.generational_gc.generations[GC_GENERATION_YOUNG].total_size += size;
            break;
            
        case GC_STRATEGY_INCREMENTAL:
            // 增量策略，标记为白色
            gc_obj->color = GC_COLOR_WHITE;
            break;
            
        case GC_STRATEGY_CONCURRENT:
            // 并发策略，标记为白色
            gc_obj->color = GC_COLOR_WHITE;
            break;
            
        case GC_STRATEGY_ADAPTIVE:
            // 自适应策略，根据对象类别设置代际
            if (obj_class == GC_OBJECT_CLASS_SHORT_LIVED) {
                gc_obj->generation = GC_GENERATION_YOUNG;
            } else if (obj_class == GC_OBJECT_CLASS_MEDIUM_LIVED) {
                gc_obj->generation = GC_GENERATION_MIDDLE;
            } else {
                gc_obj->generation = GC_GENERATION_OLD;
            }
            
            gc_obj->next = g_gc.generational_gc.generations[gc_obj->generation].objects;
            g_gc.generational_gc.generations[gc_obj->generation].objects = gc_obj;
            g_gc.generational_gc.generations[gc_obj->generation].object_count++;
            g_gc.generational_gc.generations[gc_obj->generation].total_size += size;
            break;
            
        default:
            LOGE("未知的垃圾回收策略: %d", g_gc.strategy);
            free(gc_obj);
            pthread_mutex_unlock(&g_gc.mutex);
            return -1;
    }
    
    // 更新自适应GC统计
    g_gc.adaptive_gc.heap_size += size;
    
    pthread_mutex_unlock(&g_gc.mutex);
    
    LOGI("添加对象到垃圾回收器: 对象=%p, 大小=%zu, 类别=%d", obj, size, obj_class);
    
    return 0;
}

// 从垃圾回收器移除对象
int gc_remove_object(void* obj) {
    if (!obj) {
        LOGE("对象不能为空");
        return -1;
    }
    
    pthread_mutex_lock(&g_gc.mutex);
    
    // 查找对象
    struct gc_object* gc_obj = NULL;
    struct gc_object* prev = NULL;
    
    // 在各代中查找对象
    for (int i = 0; i < GC_GENERATION_COUNT; i++) {
        gc_obj = g_gc.generational_gc.generations[i].objects;
        prev = NULL;
        
        while (gc_obj) {
            if (gc_obj->obj == obj) {
                // 从链表中移除
                if (prev) {
                    prev->next = gc_obj->next;
                } else {
                    g_gc.generational_gc.generations[i].objects = gc_obj->next;
                }
                
                // 更新统计
                g_gc.generational_gc.generations[i].object_count--;
                g_gc.generational_gc.generations[i].total_size -= gc_obj->size;
                g_gc.adaptive_gc.heap_size -= gc_obj->size;
                
                // 释放GC对象
                free(gc_obj);
                
                pthread_mutex_unlock(&g_gc.mutex);
                
                LOGI("从垃圾回收器移除对象: 对象=%p", obj);
                
                return 0;
            }
            
            prev = gc_obj;
            gc_obj = gc_obj->next;
        }
    }
    
    pthread_mutex_unlock(&g_gc.mutex);
    
    LOGE("未找到要移除的对象: %p", obj);
    
    return -1;
}

// 标记对象为可达
int gc_mark_object(void* obj) {
    if (!obj) {
        LOGE("对象不能为空");
        return -1;
    }
    
    pthread_mutex_lock(&g_gc.mutex);
    
    // 查找对象
    struct gc_object* gc_obj = NULL;
    
    // 在各代中查找对象
    for (int i = 0; i < GC_GENERATION_COUNT; i++) {
        gc_obj = g_gc.generational_gc.generations[i].objects;
        
        while (gc_obj) {
            if (gc_obj->obj == obj) {
                // 标记对象
                gc_obj->marked = true;
                gc_obj->color = GC_COLOR_BLACK;
                gc_obj->last_access_time = gc_get_time();
                gc_obj->access_count++;
                
                pthread_mutex_unlock(&g_gc.mutex);
                
                LOGI("标记对象为可达: 对象=%p", obj);
                
                return 0;
            }
            
            gc_obj = gc_obj->next;
        }
    }
    
    pthread_mutex_unlock(&g_gc.mutex);
    
    LOGE("未找到要标记的对象: %p", obj);
    
    return -1;
}

// 执行垃圾回收
int gc_collect(void) {
    pthread_mutex_lock(&g_gc.mutex);
    
    // 检查是否已经在进行垃圾回收
    if (g_gc.state != GC_STATE_IDLE) {
        pthread_mutex_unlock(&g_gc.mutex);
        LOGI("垃圾回收已在进行中");
        return 0;
    }
    
    // 设置状态为回收中
    g_gc.state = GC_STATE_MARKING;
    uint64_t start_time = gc_get_time();
    
    // 根据策略执行垃圾回收
    int result = 0;
    switch (g_gc.strategy) {
        case GC_STRATEGY_BASIC:
            result = gc_collect_basic();
            break;
            
        case GC_STRATEGY_GENERATIONAL:
            result = gc_collect_generational();
            break;
            
        case GC_STRATEGY_INCREMENTAL:
            result = gc_collect_incremental();
            break;
            
        case GC_STRATEGY_CONCURRENT:
            result = gc_collect_concurrent();
            break;
            
        case GC_STRATEGY_ADAPTIVE:
            result = gc_collect_adaptive();
            break;
            
        default:
            LOGE("未知的垃圾回收策略: %d", g_gc.strategy);
            result = -1;
            break;
    }
    
    // 更新统计
    uint64_t end_time = gc_get_time();
    uint64_t duration = end_time - start_time;
    
    g_gc.last_gc_time = end_time;
    g_gc.total_gc_time += duration;
    g_gc.total_gc_count++;
    g_gc.adaptive_gc.last_gc_duration = duration;
    
    // 设置状态为空闲
    g_gc.state = GC_STATE_IDLE;
    
    pthread_mutex_unlock(&g_gc.mutex);
    
    LOGI("垃圾回收完成，耗时: %llu ms", (unsigned long long)duration);
    
    return result;
}

// 基础垃圾回收
int gc_collect_basic(void) {
    LOGI("执行基础垃圾回收");
    
    // 标记阶段
    gc_mark_phase_basic();
    
    // 清除阶段
    gc_sweep_phase_basic();
    
    return 0;
}

// 标记阶段（基础）
void gc_mark_phase_basic(void) {
    // 基础标记阶段，需要外部调用gc_mark_object来标记根对象
    LOGI("执行基础标记阶段");
}

// 清除阶段（基础）
void gc_sweep_phase_basic(void) {
    LOGI("执行基础清除阶段");
    
    uint32_t objects_freed = 0;
    size_t memory_freed = 0;
    
    // 遍历所有对象
    for (int i = 0; i < GC_GENERATION_COUNT; i++) {
        struct gc_object* gc_obj = g_gc.generational_gc.generations[i].objects;
        struct gc_object* prev = NULL;
        
        while (gc_obj) {
            struct gc_object* next = gc_obj->next;
            
            // 如果对象未被标记，释放它
            if (!gc_obj->marked) {
                // 从链表中移除
                if (prev) {
                    prev->next = next;
                } else {
                    g_gc.generational_gc.generations[i].objects = next;
                }
                
                // 更新统计
                g_gc.generational_gc.generations[i].object_count--;
                g_gc.generational_gc.generations[i].total_size -= gc_obj->size;
                g_gc.adaptive_gc.heap_size -= gc_obj->size;
                
                objects_freed++;
                memory_freed += gc_obj->size;
                
                // 调用析构函数
                if (gc_obj->finalizer) {
                    gc_obj->finalizer(gc_obj->obj);
                }
                
                // 释放对象
                free(gc_obj->obj);
                
                // 释放GC对象
                free(gc_obj);
            } else {
                // 重置标记
                gc_obj->marked = false;
                gc_obj->color = GC_COLOR_WHITE;
                prev = gc_obj;
            }
            
            gc_obj = next;
        }
    }
    
    // 更新全局统计
    g_gc.total_objects_freed += objects_freed;
    g_gc.total_memory_freed += memory_freed;
    
    LOGI("基础清除阶段完成，释放对象数: %u, 释放内存: %zu", objects_freed, memory_freed);
}

// ==================== 分代垃圾回收实现 ====================

// 分代垃圾回收
int gc_collect_generational(void) {
    LOGI("执行分代垃圾回收");
    
    // 首先回收新生代
    gc_collect_generation(GC_GENERATION_YOUNG);
    
    // 检查是否需要回收老年代
    if (g_gc.generational_gc.generations[GC_GENERATION_OLD].object_count > 
        g_gc.generational_gc.generations[GC_GENERATION_OLD].threshold) {
        gc_collect_generation(GC_GENERATION_OLD);
    }
    
    // 检查是否需要回收终身代
    if (g_gc.generational_gc.generations[GC_GENERATION_PERMANENT].object_count > 
        g_gc.generational_gc.generations[GC_GENERATION_PERMANENT].threshold) {
        gc_collect_generation(GC_GENERATION_PERMANENT);
    }
    
    return 0;
}

// 回收指定代
int gc_collect_generation(gc_generation_t generation) {
    if (generation < 0 || generation >= GC_GENERATION_COUNT) {
        LOGE("无效的代: %d", generation);
        return -1;
    }
    
    LOGI("回收代: %d", generation);
    
    uint32_t objects_freed = 0;
    size_t memory_freed = 0;
    
    // 标记阶段
    gc_mark_phase_generation(generation);
    
    // 清除阶段
    gc_sweep_phase_generation(generation, &objects_freed, &memory_freed);
    
    // 更新统计
    g_gc.generational_gc.stats.total_collections++;
    g_gc.generational_gc.stats.total_objects_freed += objects_freed;
    g_gc.generational_gc.stats.total_memory_freed += memory_freed;
    g_gc.generational_gc.stats.last_collection_time = gc_get_time();
    
    LOGI("代回收完成: %d, 释放对象数: %u, 释放内存: %zu", 
         generation, objects_freed, memory_freed);
    
    return 0;
}

// 标记阶段（分代）
void gc_mark_phase_generation(gc_generation_t generation) {
    LOGI("执行分代标记阶段: %d", generation);
    
    // 分代标记阶段，需要外部调用gc_mark_object来标记根对象
    // 这里可以添加特定代的标记逻辑
}

// 清除阶段（分代）
void gc_sweep_phase_generation(gc_generation_t generation, uint32_t* objects_freed, size_t* memory_freed) {
    LOGI("执行分代清除阶段: %d", generation);
    
    struct gc_object* gc_obj = g_gc.generational_gc.generations[generation].objects;
    struct gc_object* prev = NULL;
    
    while (gc_obj) {
        struct gc_object* next = gc_obj->next;
        
        // 如果对象未被标记，释放它
        if (!gc_obj->marked) {
            // 从链表中移除
            if (prev) {
                prev->next = next;
            } else {
                g_gc.generational_gc.generations[generation].objects = next;
            }
            
            // 更新统计
            g_gc.generational_gc.generations[generation].object_count--;
            g_gc.generational_gc.generations[generation].total_size -= gc_obj->size;
            g_gc.adaptive_gc.heap_size -= gc_obj->size;
            
            (*objects_freed)++;
            (*memory_freed) += gc_obj->size;
            
            // 调用析构函数
            if (gc_obj->finalizer) {
                gc_obj->finalizer(gc_obj->obj);
            }
            
            // 释放对象
            free(gc_obj->obj);
            
            // 释放GC对象
            free(gc_obj);
        } else {
            // 重置标记
            gc_obj->marked = false;
            gc_obj->color = GC_COLOR_WHITE;
            
            // 增加年龄
            gc_obj->age++;
            
            // 如果是新生代对象且年龄达到阈值，晋升到下一代
            if (generation == GC_GENERATION_YOUNG && 
                gc_obj->age >= g_gc.generational_gc.generations[GC_GENERATION_YOUNG].threshold) {
                // 从当前代移除
                if (prev) {
                    prev->next = next;
                } else {
                    g_gc.generational_gc.generations[generation].objects = next;
                }
                
                g_gc.generational_gc.generations[generation].object_count--;
                g_gc.generational_gc.generations[generation].total_size -= gc_obj->size;
                
                // 添加到下一代
                gc_obj->generation = GC_GENERATION_MIDDLE;
                gc_obj->next = g_gc.generational_gc.generations[GC_GENERATION_MIDDLE].objects;
                g_gc.generational_gc.generations[GC_GENERATION_MIDDLE].objects = gc_obj;
                g_gc.generational_gc.generations[GC_GENERATION_MIDDLE].object_count++;
                g_gc.generational_gc.generations[GC_GENERATION_MIDDLE].total_size += gc_obj->size;
                
                LOGI("对象晋升: %p, 从新生代到中年代", gc_obj->obj);
            } 
            // 如果是中年代对象且年龄达到阈值，晋升到老年代
            else if (generation == GC_GENERATION_MIDDLE && 
                     gc_obj->age >= g_gc.generational_gc.generations[GC_GENERATION_MIDDLE].threshold) {
                // 从当前代移除
                if (prev) {
                    prev->next = next;
                } else {
                    g_gc.generational_gc.generations[generation].objects = next;
                }
                
                g_gc.generational_gc.generations[generation].object_count--;
                g_gc.generational_gc.generations[generation].total_size -= gc_obj->size;
                
                // 添加到老年代
                gc_obj->generation = GC_GENERATION_OLD;
                gc_obj->next = g_gc.generational_gc.generations[GC_GENERATION_OLD].objects;
                g_gc.generational_gc.generations[GC_GENERATION_OLD].objects = gc_obj;
                g_gc.generational_gc.generations[GC_GENERATION_OLD].object_count++;
                g_gc.generational_gc.generations[GC_GENERATION_OLD].total_size += gc_obj->size;
                
                LOGI("对象晋升: %p, 从中年代到老年代", gc_obj->obj);
            } else {
                prev = gc_obj;
            }
        }
        
        gc_obj = next;
    }
}

// ==================== 增量垃圾回收实现 ====================

// 增量垃圾回收
int gc_collect_incremental(void) {
    LOGI("执行增量垃圾回收");
    
    // 增量垃圾回收分为多个阶段
    switch (g_gc.incremental_gc.phase) {
        case GC_INCREMENTAL_PHASE_MARK:
            gc_incremental_mark_phase();
            break;
            
        case GC_INCREMENTAL_PHASE_SWEEP:
            gc_incremental_sweep_phase();
            break;
            
        default:
            LOGE("未知的增量垃圾回收阶段: %d", g_gc.incremental_gc.phase);
            return -1;
    }
    
    return 0;
}

// 增量标记阶段
void gc_incremental_mark_phase(void) {
    LOGI("执行增量标记阶段");
    
    // 初始化标记栈（如果尚未初始化）
    if (!g_gc.incremental_gc.mark_stack) {
        g_gc.incremental_gc.mark_stack_capacity = 1000;
        g_gc.incremental_gc.mark_stack = malloc(sizeof(void*) * g_gc.incremental_gc.mark_stack_capacity);
        if (!g_gc.incremental_gc.mark_stack) {
            LOGE("分配增量标记栈失败");
            return;
        }
        g_gc.incremental_gc.mark_stack_size = 0;
    }
    
    // 执行增量标记工作
    uint32_t work_units_per_step = 10; // 每步执行10个工作单元
    uint32_t work_units_done = 0;
    
    // 如果标记栈为空，需要从根对象开始标记
    if (g_gc.incremental_gc.mark_stack_size == 0) {
        // 这里应该添加根对象到标记栈
        // 简化实现，直接进入清除阶段
        g_gc.incremental_gc.phase = GC_INCREMENTAL_PHASE_SWEEP;
        g_gc.incremental_gc.work_units = 0;
        return;
    }
    
    // 处理标记栈中的对象
    while (g_gc.incremental_gc.mark_stack_size > 0 && work_units_done < work_units_per_step) {
        // 从标记栈弹出对象
        void* obj = g_gc.incremental_gc.mark_stack[--g_gc.incremental_gc.mark_stack_size];
        
        // 查找对应的GC对象
        struct gc_object* gc_obj = NULL;
        for (int i = 0; i < GC_GENERATION_COUNT; i++) {
            gc_obj = g_gc.generational_gc.generations[i].objects;
            while (gc_obj) {
                if (gc_obj->obj == obj) {
                    break;
                }
                gc_obj = gc_obj->next;
            }
            if (gc_obj) {
                break;
            }
        }
        
        if (gc_obj && gc_obj->color == GC_COLOR_GRAY) {
            // 标记为黑色
            gc_obj->color = GC_COLOR_BLACK;
            
            // 这里应该遍历对象引用，将引用的对象添加到标记栈
            // 简化实现，不处理对象引用
            
            work_units_done++;
        }
    }
    
    // 更新工作进度
    g_gc.incremental_gc.work_units += work_units_done;
    
    // 如果标记栈为空，进入清除阶段
    if (g_gc.incremental_gc.mark_stack_size == 0) {
        g_gc.incremental_gc.phase = GC_INCREMENTAL_PHASE_SWEEP;
        g_gc.incremental_gc.work_units = 0;
    }
}

// 增量清除阶段
void gc_incremental_sweep_phase(void) {
    LOGI("执行增量清除阶段");
    
    // 执行增量清除工作
    uint32_t work_units_per_step = 10; // 每步执行10个工作单元
    uint32_t work_units_done = 0;
    
    // 遍历所有对象
    for (int i = 0; i < GC_GENERATION_COUNT && work_units_done < work_units_per_step; i++) {
        struct gc_object* gc_obj = g_gc.generational_gc.generations[i].objects;
        struct gc_object* prev = NULL;
        
        while (gc_obj && work_units_done < work_units_per_step) {
            struct gc_object* next = gc_obj->next;
            
            // 如果对象是白色，释放它
            if (gc_obj->color == GC_COLOR_WHITE) {
                // 从链表中移除
                if (prev) {
                    prev->next = next;
                } else {
                    g_gc.generational_gc.generations[i].objects = next;
                }
                
                // 更新统计
                g_gc.generational_gc.generations[i].object_count--;
                g_gc.generational_gc.generations[i].total_size -= gc_obj->size;
                g_gc.adaptive_gc.heap_size -= gc_obj->size;
                
                g_gc.total_objects_freed++;
                g_gc.total_memory_freed += gc_obj->size;
                
                // 调用析构函数
                if (gc_obj->finalizer) {
                    gc_obj->finalizer(gc_obj->obj);
                }
                
                // 释放对象
                free(gc_obj->obj);
                
                // 释放GC对象
                free(gc_obj);
                
                work_units_done++;
            } else {
                // 重置颜色为白色
                gc_obj->color = GC_COLOR_WHITE;
                prev = gc_obj;
            }
            
            gc_obj = next;
        }
    }
    
    // 更新工作进度
    g_gc.incremental_gc.work_units += work_units_done;
    
    // 如果工作完成，回到标记阶段
    if (g_gc.incremental_gc.work_units >= g_gc.incremental_gc.total_work_units) {
        g_gc.incremental_gc.phase = GC_INCREMENTAL_PHASE_MARK;
        g_gc.incremental_gc.work_units = 0;
    }
}

// ==================== 并发垃圾回收实现 ====================

// 并发垃圾回收
int gc_collect_concurrent(void) {
    LOGI("执行并发垃圾回收");
    
    // 如果并发GC未运行，启动它
    if (!g_gc.concurrent_gc.running) {
        return gc_start_concurrent_gc();
    }
    
    return 0;
}

// 启动并发垃圾回收
int gc_start_concurrent_gc(void) {
    if (g_gc.concurrent_gc.running) {
        LOGI("并发垃圾回收已在运行");
        return 0;
    }
    
    // 初始化标记队列（如果尚未初始化）
    if (!g_gc.concurrent_gc.mark_queue) {
        g_gc.concurrent_gc.mark_queue_capacity = 1000;
        g_gc.concurrent_gc.mark_queue = malloc(sizeof(void*) * g_gc.concurrent_gc.mark_queue_capacity);
        if (!g_gc.concurrent_gc.mark_queue) {
            LOGE("分配并发标记队列失败");
            return -1;
        }
        g_gc.concurrent_gc.mark_queue_size = 0;
    }
    
    // 创建并发GC线程
    if (pthread_create(&g_gc.concurrent_gc.thread, NULL, gc_concurrent_thread_func, NULL) != 0) {
        LOGE("创建并发垃圾回收线程失败");
        return -1;
    }
    
    g_gc.concurrent_gc.running = true;
    
    LOGI("并发垃圾回收已启动");
    
    return 0;
}

// 停止并发垃圾回收
int gc_stop_concurrent_gc(void) {
    if (!g_gc.concurrent_gc.running) {
        LOGI("并发垃圾回收未运行");
        return 0;
    }
    
    // 通知线程退出
    g_gc.concurrent_gc.running = false;
    pthread_cond_signal(&g_gc.cond);
    
    // 等待线程退出
    pthread_join(g_gc.concurrent_gc.thread, NULL);
    
    LOGI("并发垃圾回收已停止");
    
    return 0;
}

// 并发垃圾回收线程函数
void* gc_concurrent_thread_func(void* arg) {
    (void)arg; // 避免未使用参数警告
    
    LOGI("并发垃圾回收线程已启动");
    
    while (g_gc.concurrent_gc.running) {
        pthread_mutex_lock(&g_gc.mutex);
        
        // 等待工作或退出信号
        while (g_gc.concurrent_gc.running && g_gc.concurrent_gc.mark_queue_size == 0) {
            pthread_cond_wait(&g_gc.cond, &g_gc.mutex);
        }
        
        if (!g_gc.concurrent_gc.running) {
            pthread_mutex_unlock(&g_gc.mutex);
            break;
        }
        
        // 处理标记队列中的对象
        while (g_gc.concurrent_gc.mark_queue_size > 0) {
            // 从标记队列取出对象
            void* obj = g_gc.concurrent_gc.mark_queue[--g_gc.concurrent_gc.mark_queue_size];
            
            // 查找对应的GC对象
            struct gc_object* gc_obj = NULL;
            for (int i = 0; i < GC_GENERATION_COUNT; i++) {
                gc_obj = g_gc.generational_gc.generations[i].objects;
                while (gc_obj) {
                    if (gc_obj->obj == obj) {
                        break;
                    }
                    gc_obj = gc_obj->next;
                }
                if (gc_obj) {
                    break;
                }
            }
            
            if (gc_obj && gc_obj->color == GC_COLOR_GRAY) {
                // 标记为黑色
                gc_obj->color = GC_COLOR_BLACK;
                
                // 这里应该遍历对象引用，将引用的对象添加到标记队列
                // 简化实现，不处理对象引用
            }
        }
        
        pthread_mutex_unlock(&g_gc.mutex);
        
        // 短暂休眠，避免占用过多CPU
        usleep(1000); // 1ms
    }
    
    LOGI("并发垃圾回收线程已退出");
    
    return NULL;
}

// ==================== 自适应垃圾回收实现 ====================

// 自适应垃圾回收
int gc_collect_adaptive(void) {
    LOGI("执行自适应垃圾回收");
    
    // 计算分配速率
    uint64_t current_time = gc_get_time();
    uint64_t time_diff = current_time - g_gc.adaptive_gc.last_gc_time;
    
    if (time_diff > 0) {
        // 计算当前堆大小与上次GC时的差值
        size_t heap_size_diff = g_gc.adaptive_gc.heap_size - g_gc.adaptive_gc.last_heap_size;
        
        // 计算分配速率（字节/秒）
        g_gc.adaptive_gc.allocation_rate = (heap_size_diff * 1000) / time_diff;
    }
    
    // 根据当前情况选择垃圾回收策略
    gc_strategy_t strategy;
    
    // 如果堆大小超过阈值，使用分代GC
    if (g_gc.adaptive_gc.heap_size > g_gc.adaptive_gc.heap_size_threshold) {
        strategy = GC_STRATEGY_GENERATIONAL;
        LOGI("选择分代垃圾回收策略: 堆大小=%zu, 阈值=%zu", 
             g_gc.adaptive_gc.heap_size, g_gc.adaptive_gc.heap_size_threshold);
    }
    // 如果分配速率超过阈值，使用并发GC
    else if (g_gc.adaptive_gc.allocation_rate > g_gc.adaptive_gc.allocation_rate_threshold) {
        strategy = GC_STRATEGY_CONCURRENT;
        LOGI("选择并发垃圾回收策略: 分配速率=%zu, 阈值=%zu", 
             g_gc.adaptive_gc.allocation_rate, g_gc.adaptive_gc.allocation_rate_threshold);
    }
    // 如果上次GC时间超过阈值，使用增量GC
    else if (g_gc.adaptive_gc.last_gc_duration > g_gc.adaptive_gc.gc_time_threshold) {
        strategy = GC_STRATEGY_INCREMENTAL;
        LOGI("选择增量垃圾回收策略: 上次GC时间=%llu, 阈值=%u", 
             (unsigned long long)g_gc.adaptive_gc.last_gc_duration, g_gc.adaptive_gc.gc_time_threshold);
    }
    // 否则使用基础GC
    else {
        strategy = GC_STRATEGY_BASIC;
        LOGI("选择基础垃圾回收策略");
    }
    
    // 临时切换策略
    gc_strategy_t old_strategy = g_gc.strategy;
    g_gc.strategy = strategy;
    
    // 执行垃圾回收
    int result = gc_collect();
    
    // 恢复原策略
    g_gc.strategy = old_strategy;
    
    // 更新统计
    g_gc.adaptive_gc.last_gc_time = current_time;
    g_gc.adaptive_gc.last_heap_size = g_gc.adaptive_gc.heap_size;
    
    return result;
}

// ==================== 垃圾回收器控制接口 ====================

// 设置垃圾回收策略
int gc_set_strategy(gc_strategy_t strategy) {
    if (strategy < 0 || strategy >= GC_STRATEGY_COUNT) {
        LOGE("无效的垃圾回收策略: %d", strategy);
        return -1;
    }
    
    pthread_mutex_lock(&g_gc.mutex);
    
    // 如果当前正在运行并发GC，需要先停止
    if (g_gc.concurrent_gc.running) {
        pthread_mutex_unlock(&g_gc.mutex);
        gc_stop_concurrent_gc();
        pthread_mutex_lock(&g_gc.mutex);
    }
    
    g_gc.strategy = strategy;
    
    pthread_mutex_unlock(&g_gc.mutex);
    
    LOGI("设置垃圾回收策略: %d", strategy);
    
    return 0;
}

// 获取垃圾回收策略
gc_strategy_t gc_get_strategy(void) {
    pthread_mutex_lock(&g_gc.mutex);
    gc_strategy_t strategy = g_gc.strategy;
    pthread_mutex_unlock(&g_gc.mutex);
    
    return strategy;
}

// 启用自动垃圾回收
void gc_enable_auto_gc(void) {
    pthread_mutex_lock(&g_gc.mutex);
    g_gc.auto_gc = true;
    pthread_mutex_unlock(&g_gc.mutex);
    
    LOGI("启用自动垃圾回收");
}

// 禁用自动垃圾回收
void gc_disable_auto_gc(void) {
    pthread_mutex_lock(&g_gc.mutex);
    g_gc.auto_gc = false;
    pthread_mutex_unlock(&g_gc.mutex);
    
    LOGI("禁用自动垃圾回收");
}

// 设置垃圾回收间隔
void gc_set_interval(uint32_t interval_ms) {
    pthread_mutex_lock(&g_gc.mutex);
    g_gc.gc_interval = interval_ms;
    pthread_mutex_unlock(&g_gc.mutex);
    
    LOGI("设置垃圾回收间隔: %u ms", interval_ms);
}

// 更新垃圾回收器（每帧调用）
void gc_update(void) {
    pthread_mutex_lock(&g_gc.mutex);
    
    // 检查是否启用自动GC
    if (!g_gc.auto_gc) {
        pthread_mutex_unlock(&g_gc.mutex);
        return;
    }
    
    // 检查是否需要执行GC
    uint64_t current_time = gc_get_time();
    if (current_time - g_gc.last_gc_time >= g_gc.gc_interval) {
        pthread_mutex_unlock(&g_gc.mutex);
        gc_collect();
        return;
    }
    
    // 如果是增量GC，执行一步
    if (g_gc.strategy == GC_STRATEGY_INCREMENTAL) {
        pthread_mutex_unlock(&g_gc.mutex);
        gc_collect_incremental();
        return;
    }
    
    pthread_mutex_unlock(&g_gc.mutex);
}

// 获取垃圾回收器统计
void gc_get_stats(struct gc_stats* stats) {
    if (!stats) {
        return;
    }
    
    pthread_mutex_lock(&g_gc.mutex);
    
    stats->strategy = g_gc.strategy;
    stats->state = g_gc.state;
    stats->auto_gc = g_gc.auto_gc;
    stats->gc_interval = g_gc.gc_interval;
    stats->last_gc_time = g_gc.last_gc_time;
    stats->total_gc_time = g_gc.total_gc_time;
    stats->total_gc_count = g_gc.total_gc_count;
    stats->total_objects_freed = g_gc.total_objects_freed;
    stats->total_memory_freed = g_gc.total_memory_freed;
    
    pthread_mutex_unlock(&g_gc.mutex);
}

// 打印垃圾回收器统计
void gc_print_stats(void) {
    struct gc_stats stats;
    gc_get_stats(&stats);
    
    LOGI("=== 垃圾回收器统计 ===");
    LOGI("策略: %d", stats.strategy);
    LOGI("状态: %d", stats.state);
    LOGI("自动GC: %s", stats.auto_gc ? "启用" : "禁用");
    LOGI("GC间隔: %u ms", stats.gc_interval);
    LOGI("上次GC时间: %llu", (unsigned long long)stats.last_gc_time);
    LOGI("总GC时间: %llu ms", (unsigned long long)stats.total_gc_time);
    LOGI("总GC次数: %u", stats.total_gc_count);
    LOGI("总释放对象数: %u", stats.total_objects_freed);
    LOGI("总释放内存: %zu", stats.total_memory_freed);
    
    if (stats.total_gc_count > 0) {
        LOGI("平均GC时间: %.2f ms", (float)stats.total_gc_time / stats.total_gc_count);
    }
    
    LOGI("=====================");
}
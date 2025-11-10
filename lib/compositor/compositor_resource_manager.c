#include "compositor_resource_manager.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>

#define LOG_TAG "ResourceManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 全局资源管理器实例
static struct resource_manager g_resource_manager = {0};

// ==================== 工具函数 ====================

// 获取当前时间（毫秒）
uint64_t resource_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ==================== 基础资源管理实现 ====================

// 初始化资源管理器
int resource_manager_init(size_t memory_limit) {
    // 初始化互斥锁
    if (pthread_mutex_init(&g_resource_manager.preload_mgr.mutex, NULL) != 0) {
        LOGE("初始化预加载管理器互斥锁失败");
        return -1;
    }
    
    if (pthread_mutex_init(&g_resource_manager.texture_cache_mgr.mutex, NULL) != 0) {
        LOGE("初始化纹理缓存管理器互斥锁失败");
        pthread_mutex_destroy(&g_resource_manager.preload_mgr.mutex);
        return -1;
    }
    
    if (pthread_mutex_init(&g_resource_manager.async_queue.mutex, NULL) != 0) {
        LOGE("初始化异步加载队列互斥锁失败");
        pthread_mutex_destroy(&g_resource_manager.preload_mgr.mutex);
        pthread_mutex_destroy(&g_resource_manager.texture_cache_mgr.mutex);
        return -1;
    }
    
    if (pthread_mutex_init(&g_resource_manager.thread_pool.mutex, NULL) != 0) {
        LOGE("初始化线程池互斥锁失败");
        pthread_mutex_destroy(&g_resource_manager.preload_mgr.mutex);
        pthread_mutex_destroy(&g_resource_manager.texture_cache_mgr.mutex);
        pthread_mutex_destroy(&g_resource_manager.async_queue.mutex);
        return -1;
    }
    
    if (pthread_mutex_init(&g_resource_manager.pending_tasks_mutex, NULL) != 0) {
        LOGE("初始化待处理任务队列互斥锁失败");
        pthread_mutex_destroy(&g_resource_manager.preload_mgr.mutex);
        pthread_mutex_destroy(&g_resource_manager.texture_cache_mgr.mutex);
        pthread_mutex_destroy(&g_resource_manager.async_queue.mutex);
        pthread_mutex_destroy(&g_resource_manager.thread_pool.mutex);
        return -1;
    }
    
    // 初始化资源列表
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        g_resource_manager.resources[i] = NULL;
    }
    
    // 初始化其他字段
    g_resource_manager.next_resource_id = 1;
    g_resource_manager.memory_limit = memory_limit;
    g_resource_manager.current_time = resource_get_time();
    
    // 初始化统计信息
    memset(&g_resource_manager.stats, 0, sizeof(struct resource_manager_stats));
    
    // 初始化预加载管理器
    g_resource_manager.preload_mgr.descriptors = NULL;
    g_resource_manager.preload_mgr.descriptor_count = 0;
    g_resource_manager.preload_mgr.critical_loaded = 0;
    g_resource_manager.preload_mgr.total_loaded = 0;
    g_resource_manager.preload_mgr.total_size = 0;
    g_resource_manager.preload_mgr.loaded_size = 0;
    g_resource_manager.preload_mgr.start_time = 0;
    g_resource_manager.preload_mgr.critical_time = 0;
    g_resource_manager.preload_mgr.total_time = 0;
    g_resource_manager.preload_mgr.enabled = true;
    g_resource_manager.preload_mgr.in_progress = false;
    g_resource_manager.preload_mgr.max_concurrent_loads = 3;
    g_resource_manager.preload_mgr.current_loads = 0;
    
    // 初始化纹理缓存管理器
    g_resource_manager.texture_cache_mgr.items = NULL;
    g_resource_manager.texture_cache_mgr.lru_list = NULL;
    g_resource_manager.texture_cache_mgr.mru_list = NULL;
    g_resource_manager.texture_cache_mgr.capacity = 128; // 默认128MB
    g_resource_manager.texture_cache_mgr.current_size = 0;
    g_resource_manager.texture_cache_mgr.max_items = 1024; // 默认最大1024项
    g_resource_manager.texture_cache_mgr.item_count = 0;
    g_resource_manager.texture_cache_mgr.hash_size = 256; // 默认哈希表大小
    g_resource_manager.texture_cache_mgr.compression_enabled = false;
    g_resource_manager.texture_cache_mgr.streaming_enabled = false;
    g_resource_manager.texture_cache_mgr.compression_level = 6; // 默认压缩级别
    g_resource_manager.texture_cache_mgr.eviction_threshold = 80; // 默认淘汰阈值80%
    g_resource_manager.texture_cache_mgr.lru_update_interval = 60; // 默认60帧更新一次LRU
    g_resource_manager.texture_cache_mgr.frame_counter = 0;
    memset(&g_resource_manager.texture_cache_mgr.stats, 0, sizeof(struct texture_cache_stats));
    
    // 初始化异步加载队列
    g_resource_manager.async_queue.high_priority_head = NULL;
    g_resource_manager.async_queue.high_priority_tail = NULL;
    g_resource_manager.async_queue.normal_priority_head = NULL;
    g_resource_manager.async_queue.normal_priority_tail = NULL;
    g_resource_manager.async_queue.max_concurrent_loads = 3;
    g_resource_manager.async_queue.current_loads = 0;
    g_resource_manager.async_queue.processing = false;
    
    // 初始化线程池
    g_resource_manager.thread_pool.threads = NULL;
    g_resource_manager.thread_pool.thread_count = 0;
    g_resource_manager.thread_pool.shutdown = false;
    
    // 初始化待处理任务队列
    g_resource_manager.pending_tasks = NULL;
    
    LOGI("资源管理器初始化完成，内存限制: %zu MB", memory_limit / (1024 * 1024));
    
    return 0;
}

// 销毁资源管理器
void resource_manager_destroy(void) {
    // 销毁所有资源
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        struct resource* resource = g_resource_manager.resources[i];
        while (resource) {
            struct resource* next = resource->next;
            resource_destroy(resource);
            resource = next;
        }
        g_resource_manager.resources[i] = NULL;
    }
    
    // 销毁预加载管理器
    resource_preload_shutdown();
    
    // 销毁纹理缓存管理器
    texture_cache_shutdown();
    
    // 销毁线程池
    async_load_thread_pool_shutdown();
    
    // 清理待处理任务队列
    pthread_mutex_lock(&g_resource_manager.pending_tasks_mutex);
    struct async_load_task* task = g_resource_manager.pending_tasks;
    while (task) {
        struct async_load_task* next = task->next;
        free(task);
        task = next;
    }
    g_resource_manager.pending_tasks = NULL;
    pthread_mutex_unlock(&g_resource_manager.pending_tasks_mutex);
    
    // 销毁互斥锁
    pthread_mutex_destroy(&g_resource_manager.preload_mgr.mutex);
    pthread_mutex_destroy(&g_resource_manager.texture_cache_mgr.mutex);
    pthread_mutex_destroy(&g_resource_manager.async_queue.mutex);
    pthread_mutex_destroy(&g_resource_manager.thread_pool.mutex);
    pthread_mutex_destroy(&g_resource_manager.pending_tasks_mutex);
    
    LOGI("资源管理器已销毁");
}

// 创建资源
struct resource* resource_create(resource_type_t type, const char* name, size_t size) {
    if (!name) {
        LOGE("资源名称不能为空");
        return NULL;
    }
    
    // 检查是否已存在同名资源
    if (resource_find_by_name(name)) {
        LOGE("资源已存在: %s", name);
        return NULL;
    }
    
    // 分配资源结构
    struct resource* resource = malloc(sizeof(struct resource));
    if (!resource) {
        LOGE("分配资源结构失败");
        return NULL;
    }
    
    // 分配资源数据
    void* data = malloc(size);
    if (!data) {
        LOGE("分配资源数据失败");
        free(resource);
        return NULL;
    }
    
    // 初始化资源
    memset(resource, 0, sizeof(struct resource));
    resource->id = g_resource_manager.next_resource_id++;
    resource->type = type;
    resource->state = RESOURCE_STATE_UNLOADED;
    strncpy(resource->name, name, sizeof(resource->name) - 1);
    resource->name[sizeof(resource->name) - 1] = '\0';
    resource->size = size;
    resource->data = data;
    resource->async_loading = false;
    resource->high_priority = false;
    resource->load_progress = 0;
    
    // 初始化使用统计
    resource->usage.ref_count = 1;
    resource->usage.last_used = resource_get_time();
    resource->usage.use_count = 0;
    resource->usage.total_time = 0;
    
    // 添加到资源列表
    resource->next = g_resource_manager.resources[type];
    g_resource_manager.resources[type] = resource;
    
    // 更新统计
    g_resource_manager.stats.total_resources++;
    g_resource_manager.stats.total_memory += size;
    
    LOGI("创建资源: %s (ID: %u, 类型: %d, 大小: %zu)", 
         name, resource->id, type, size);
    
    return resource;
}

// 销毁资源
void resource_destroy(struct resource* resource) {
    if (!resource) {
        return;
    }
    
    // 从资源列表中移除
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        struct resource* prev = NULL;
        struct resource* curr = g_resource_manager.resources[i];
        
        while (curr) {
            if (curr == resource) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    g_resource_manager.resources[i] = curr->next;
                }
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    
    // 释放资源数据
    if (resource->data) {
        free(resource->data);
    }
    
    // 更新统计
    g_resource_manager.stats.total_resources--;
    g_resource_manager.stats.total_memory -= resource->size;
    
    if (resource->state == RESOURCE_STATE_LOADED) {
        g_resource_manager.stats.loaded_resources--;
        g_resource_manager.stats.used_memory -= resource->size;
    } else if (resource->state == RESOURCE_STATE_ERROR) {
        g_resource_manager.stats.error_resources--;
    }
    
    LOGI("销毁资源: %s (ID: %u)", resource->name, resource->id);
    
    // 释放资源结构
    free(resource);
}

// 查找资源
struct resource* resource_find(uint32_t id) {
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        struct resource* resource = g_resource_manager.resources[i];
        while (resource) {
            if (resource->id == id) {
                return resource;
            }
            resource = resource->next;
        }
    }
    return NULL;
}

struct resource* resource_find_by_name(const char* name) {
    if (!name) {
        return NULL;
    }
    
    for (int i = 0; i < RESOURCE_TYPE_COUNT; i++) {
        struct resource* resource = g_resource_manager.resources[i];
        while (resource) {
            if (strcmp(resource->name, name) == 0) {
                return resource;
            }
            resource = resource->next;
        }
    }
    return NULL;
}

// 加载资源
int resource_load(struct resource* resource) {
    if (!resource) {
        LOGE("资源不能为空");
        return -1;
    }
    
    if (resource->state == RESOURCE_STATE_LOADED) {
        LOGI("资源已加载: %s", resource->name);
        return 0;
    }
    
    if (resource->state == RESOURCE_STATE_LOADING) {
        LOGI("资源正在加载中: %s", resource->name);
        return 0;
    }
    
    // 设置状态为加载中
    resource->state = RESOURCE_STATE_LOADING;
    
    // 根据资源类型加载资源
    int result = 0;
    switch (resource->type) {
        case RESOURCE_TYPE_TEXTURE:
            // 模拟纹理加载
            LOGI("加载纹理资源: %s", resource->name);
            // 这里应该调用实际的纹理加载函数
            break;
        case RESOURCE_TYPE_BUFFER:
            // 模拟缓冲区加载
            LOGI("加载缓冲区资源: %s", resource->name);
            // 这里应该调用实际的缓冲区加载函数
            break;
        case RESOURCE_TYPE_SHADER:
            // 模拟着色器加载
            LOGI("加载着色器资源: %s", resource->name);
            // 这里应该调用实际的着色器加载函数
            break;
        case RESOURCE_TYPE_PIPELINE:
            // 模拟渲染管线加载
            LOGI("加载渲染管线资源: %s", resource->name);
            // 这里应该调用实际的渲染管线加载函数
            break;
        case RESOURCE_TYPE_MEMORY:
            // 模拟内存资源加载
            LOGI("加载内存资源: %s", resource->name);
            // 这里应该调用实际的内存资源加载函数
            break;
        default:
            LOGE("未知资源类型: %d", resource->type);
            result = -1;
            break;
    }
    
    if (result == 0) {
        // 加载成功
        resource->state = RESOURCE_STATE_LOADED;
        resource->load_progress = 100;
        
        // 更新统计
        g_resource_manager.stats.loaded_resources++;
        g_resource_manager.stats.used_memory += resource->size;
        
        if (g_resource_manager.stats.used_memory > g_resource_manager.stats.peak_memory) {
            g_resource_manager.stats.peak_memory = g_resource_manager.stats.used_memory;
        }
        
        LOGI("资源加载成功: %s", resource->name);
    } else {
        // 加载失败
        resource->state = RESOURCE_STATE_ERROR;
        resource->load_progress = 0;
        
        // 更新统计
        g_resource_manager.stats.error_resources++;
        
        LOGE("资源加载失败: %s", resource->name);
    }
    
    return result;
}

// 异步加载资源
int resource_load_async(struct resource* resource, bool high_priority) {
    if (!resource) {
        LOGE("资源不能为空");
        return -1;
    }
    
    if (resource->state == RESOURCE_STATE_LOADED) {
        LOGI("资源已加载: %s", resource->name);
        return 0;
    }
    
    if (resource->state == RESOURCE_STATE_LOADING) {
        LOGI("资源正在加载中: %s", resource->name);
        return 0;
    }
    
    // 设置异步加载标志
    resource->async_loading = true;
    resource->high_priority = high_priority;
    resource->state = RESOURCE_STATE_LOADING;
    resource->load_progress = 0;
    
    // 添加到异步加载队列
    pthread_mutex_lock(&g_resource_manager.async_queue.mutex);
    
    if (high_priority) {
        // 添加到高优先级队列
        if (g_resource_manager.async_queue.high_priority_tail) {
            g_resource_manager.async_queue.high_priority_tail->next = resource;
            g_resource_manager.async_queue.high_priority_tail = resource;
        } else {
            g_resource_manager.async_queue.high_priority_head = resource;
            g_resource_manager.async_queue.high_priority_tail = resource;
        }
    } else {
        // 添加到普通优先级队列
        if (g_resource_manager.async_queue.normal_priority_tail) {
            g_resource_manager.async_queue.normal_priority_tail->next = resource;
            g_resource_manager.async_queue.normal_priority_tail = resource;
        } else {
            g_resource_manager.async_queue.normal_priority_head = resource;
            g_resource_manager.async_queue.normal_priority_tail = resource;
        }
    }
    
    pthread_mutex_unlock(&g_resource_manager.async_queue.mutex);
    
    LOGI("资源已添加到异步加载队列: %s (优先级: %s)", 
         resource->name, high_priority ? "高" : "普通");
    
    return 0;
}

// 异步加载资源（带回调）
int resource_load_async_with_callback(struct resource* resource, bool high_priority,
                                     resource_load_callback_t callback, void* user_data) {
    if (!resource || !callback) {
        LOGE("资源或回调函数不能为空");
        return -1;
    }
    
    // 创建异步加载任务
    struct async_load_task* task = malloc(sizeof(struct async_load_task));
    if (!task) {
        LOGE("分配异步加载任务失败");
        return -1;
    }
    
    task->resource = resource;
    task->callback = callback;
    task->user_data = user_data;
    task->next = NULL;
    
    // 添加到待处理任务队列
    pthread_mutex_lock(&g_resource_manager.pending_tasks_mutex);
    
    task->next = g_resource_manager.pending_tasks;
    g_resource_manager.pending_tasks = task;
    
    pthread_mutex_unlock(&g_resource_manager.pending_tasks_mutex);
    
    // 添加到异步加载队列
    return resource_load_async(resource, high_priority);
}

// 卸载资源
void resource_unload(struct resource* resource) {
    if (!resource) {
        return;
    }
    
    if (resource->state != RESOURCE_STATE_LOADED) {
        LOGI("资源未加载: %s", resource->name);
        return;
    }
    
    // 取消异步加载
    if (resource->async_loading) {
        resource_cancel_async_load(resource);
    }
    
    // 根据资源类型卸载资源
    switch (resource->type) {
        case RESOURCE_TYPE_TEXTURE:
            // 模拟纹理卸载
            LOGI("卸载纹理资源: %s", resource->name);
            // 这里应该调用实际的纹理卸载函数
            break;
        case RESOURCE_TYPE_BUFFER:
            // 模拟缓冲区卸载
            LOGI("卸载缓冲区资源: %s", resource->name);
            // 这里应该调用实际的缓冲区卸载函数
            break;
        case RESOURCE_TYPE_SHADER:
            // 模拟着色器卸载
            LOGI("卸载着色器资源: %s", resource->name);
            // 这里应该调用实际的着色器卸载函数
            break;
        case RESOURCE_TYPE_PIPELINE:
            // 模拟渲染管线卸载
            LOGI("卸载渲染管线资源: %s", resource->name);
            // 这里应该调用实际的渲染管线卸载函数
            break;
        case RESOURCE_TYPE_MEMORY:
            // 模拟内存资源卸载
            LOGI("卸载内存资源: %s", resource->name);
            // 这里应该调用实际的内存资源卸载函数
            break;
        default:
            LOGE("未知资源类型: %d", resource->type);
            break;
    }
    
    // 更新状态
    resource->state = RESOURCE_STATE_UNLOADED;
    resource->load_progress = 0;
    
    // 更新统计
    g_resource_manager.stats.loaded_resources--;
    g_resource_manager.stats.used_memory -= resource->size;
    
    LOGI("资源卸载完成: %s", resource->name);
}

// 获取资源加载进度
uint32_t resource_get_load_progress(struct resource* resource) {
    if (!resource) {
        return 0;
    }
    
    return resource->load_progress;
}

// 取消异步加载
void resource_cancel_async_load(struct resource* resource) {
    if (!resource || !resource->async_loading) {
        return;
    }
    
    // 从异步加载队列中移除
    pthread_mutex_lock(&g_resource_manager.async_queue.mutex);
    
    // 检查高优先级队列
    struct resource* prev = NULL;
    struct resource* curr = g_resource_manager.async_queue.high_priority_head;
    
    while (curr) {
        if (curr == resource) {
            if (prev) {
                prev->next = curr->next;
            } else {
                g_resource_manager.async_queue.high_priority_head = curr->next;
            }
            
            if (curr == g_resource_manager.async_queue.high_priority_tail) {
                g_resource_manager.async_queue.high_priority_tail = prev;
            }
            
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    
    // 如果未在高优先级队列中找到，检查普通优先级队列
    if (!curr) {
        prev = NULL;
        curr = g_resource_manager.async_queue.normal_priority_head;
        
        while (curr) {
            if (curr == resource) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    g_resource_manager.async_queue.normal_priority_head = curr->next;
                }
                
                if (curr == g_resource_manager.async_queue.normal_priority_tail) {
                    g_resource_manager.async_queue.normal_priority_tail = prev;
                }
                
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    
    pthread_mutex_unlock(&g_resource_manager.async_queue.mutex);
    
    // 重置异步加载标志
    resource->async_loading = false;
    resource->state = RESOURCE_STATE_UNLOADED;
    resource->load_progress = 0;
    
    LOGI("取消异步加载: %s", resource->name);
}

// 处理异步加载队列
void resource_process_async_loads(void) {
    pthread_mutex_lock(&g_resource_manager.async_queue.mutex);
    
    // 检查是否已达到最大并发加载数
    if (g_resource_manager.async_queue.current_loads >= g_resource_manager.async_queue.max_concurrent_loads) {
        pthread_mutex_unlock(&g_resource_manager.async_queue.mutex);
        return;
    }
    
    // 优先处理高优先级队列
    while (g_resource_manager.async_queue.current_loads < g_resource_manager.async_queue.max_concurrent_loads) {
        struct resource* resource = NULL;
        
        // 从高优先级队列取出资源
        if (g_resource_manager.async_queue.high_priority_head) {
            resource = g_resource_manager.async_queue.high_priority_head;
            g_resource_manager.async_queue.high_priority_head = resource->next;
            
            if (g_resource_manager.async_queue.high_priority_head == NULL) {
                g_resource_manager.async_queue.high_priority_tail = NULL;
            }
        } 
        // 如果高优先级队列为空，从普通优先级队列取出资源
        else if (g_resource_manager.async_queue.normal_priority_head) {
            resource = g_resource_manager.async_queue.normal_priority_head;
            g_resource_manager.async_queue.normal_priority_head = resource->next;
            
            if (g_resource_manager.async_queue.normal_priority_head == NULL) {
                g_resource_manager.async_queue.normal_priority_tail = NULL;
            }
        }
        
        if (!resource) {
            break;
        }
        
        resource->next = NULL;
        g_resource_manager.async_queue.current_loads++;
        
        // 创建异步加载任务
        struct async_load_task* task = malloc(sizeof(struct async_load_task));
        if (task) {
            // 查找是否有对应的回调任务
            pthread_mutex_lock(&g_resource_manager.pending_tasks_mutex);
            
            struct async_load_task* pending_task = g_resource_manager.pending_tasks;
            struct async_load_task* prev_task = NULL;
            
            while (pending_task) {
                if (pending_task->resource == resource) {
                    if (prev_task) {
                        prev_task->next = pending_task->next;
                    } else {
                        g_resource_manager.pending_tasks = pending_task->next;
                    }
                    break;
                }
                prev_task = pending_task;
                pending_task = pending_task->next;
            }
            
            pthread_mutex_unlock(&g_resource_manager.pending_tasks_mutex);
            
            if (pending_task) {
                // 使用已有的回调任务
                task->callback = pending_task->callback;
                task->user_data = pending_task->user_data;
                free(pending_task);
            } else {
                // 没有回调任务，使用默认回调
                task->callback = NULL;
                task->user_data = NULL;
            }
            
            task->resource = resource;
            task->next = NULL;
            
            // 添加到线程池任务队列
            async_load_task_push(task);
        }
    }
    
    pthread_mutex_unlock(&g_resource_manager.async_queue.mutex);
}

// 增加资源引用
void resource_add_ref(struct resource* resource) {
    if (!resource) {
        return;
    }
    
    resource->usage.ref_count++;
    resource_update_usage(resource);
    
    LOGI("增加资源引用: %s (引用计数: %u)", resource->name, resource->usage.ref_count);
}

// 减少资源引用
void resource_release(struct resource* resource) {
    if (!resource) {
        return;
    }
    
    if (resource->usage.ref_count == 0) {
        LOGE("资源引用计数已为0: %s", resource->name);
        return;
    }
    
    resource->usage.ref_count--;
    resource_update_usage(resource);
    
    LOGI("减少资源引用: %s (引用计数: %u)", resource->name, resource->usage.ref_count);
    
    // 如果引用计数为0，可以考虑卸载资源
    if (resource->usage.ref_count == 0 && resource->state == RESOURCE_STATE_LOADED) {
        // 这里可以实现延迟卸载逻辑
        LOGI("资源引用计数为0，可以考虑卸载: %s", resource->name);
    }
}

// 更新资源使用统计
void resource_update_usage(struct resource* resource) {
    if (!resource) {
        return;
    }
    
    uint64_t current_time = resource_get_time();
    uint64_t time_diff = current_time - resource->usage.last_used;
    
    resource->usage.last_used = current_time;
    resource->usage.use_count++;
    resource->usage.total_time += time_diff;
    
    LOGI("更新资源使用统计: %s (使用次数: %u, 总使用时间: %llu ms)", 
         resource->name, resource->usage.use_count, (unsigned long long)resource->usage.total_time);
}

// 设置内存限制
void resource_set_memory_limit(size_t limit) {
    g_resource_manager.memory_limit = limit;
    LOGI("设置内存限制: %zu MB", limit / (1024 * 1024));
}

// 获取资源管理器统计
void resource_get_stats(struct resource_manager_stats* stats) {
    if (!stats) {
        return;
    }
    
    *stats = g_resource_manager.stats;
    
    // 计算空闲内存
    stats->free_memory = stats->total_memory - stats->used_memory;
}

// 重置资源管理器统计
void resource_reset_stats(void) {
    memset(&g_resource_manager.stats, 0, sizeof(struct resource_manager_stats));
    LOGI("重置资源管理器统计");
}

// 更新资源管理器（每帧调用）
void resource_manager_update(void) {
    // 更新当前时间
    g_resource_manager.current_time = resource_get_time();
    
    // 处理异步加载队列
    resource_process_async_loads();
    
    // 更新预加载管理器
    resource_preload_update();
    
    // 更新纹理缓存
    texture_cache_update();
    
    // 这里可以添加其他需要每帧更新的逻辑
}

// 打印资源使用情况
void resource_print_usage(void) {
    LOGI("=== 资源使用情况 ===");
    LOGI("总资源数: %u", g_resource_manager.stats.total_resources);
    LOGI("已加载资源数: %u", g_resource_manager.stats.loaded_resources);
    LOGI("错误资源数: %u", g_resource_manager.stats.error_resources);
    LOGI("总内存: %zu MB", g_resource_manager.stats.total_memory / (1024 * 1024));
    LOGI("已使用内存: %zu MB", g_resource_manager.stats.used_memory / (1024 * 1024));
    LOGI("空闲内存: %zu MB", (g_resource_manager.stats.total_memory - g_resource_manager.stats.used_memory) / (1024 * 1024));
    LOGI("峰值内存: %zu MB", g_resource_manager.stats.peak_memory / (1024 * 1024));
    LOGI("分配次数: %u", g_resource_manager.stats.allocation_count);
    LOGI("释放次数: %u", g_resource_manager.stats.free_count);
    LOGI("==================");
}

// ==================== 异步加载实现 ====================

// 线程池管理
int async_load_thread_pool_init(uint32_t thread_count) {
    if (thread_count == 0) {
        LOGE("线程数量不能为0");
        return -1;
    }
    
    // 初始化线程池互斥锁和条件变量
    if (pthread_mutex_init(&g_resource_manager.thread_pool.queue_mutex, NULL) != 0) {
        LOGE("初始化线程池队列互斥锁失败");
        return -1;
    }
    
    if (pthread_cond_init(&g_resource_manager.thread_pool.queue_cond, NULL) != 0) {
        LOGE("初始化线程池队列条件变量失败");
        pthread_mutex_destroy(&g_resource_manager.thread_pool.queue_mutex);
        return -1;
    }
    
    if (pthread_cond_init(&g_resource_manager.thread_pool.finished_cond, NULL) != 0) {
        LOGE("初始化线程池完成条件变量失败");
        pthread_mutex_destroy(&g_resource_manager.thread_pool.queue_mutex);
        pthread_cond_destroy(&g_resource_manager.thread_pool.queue_cond);
        return -1;
    }
    
    // 分配线程数组
    g_resource_manager.thread_pool.threads = malloc(sizeof(pthread_t) * thread_count);
    if (!g_resource_manager.thread_pool.threads) {
        LOGE("分配线程数组失败");
        pthread_mutex_destroy(&g_resource_manager.thread_pool.queue_mutex);
        pthread_cond_destroy(&g_resource_manager.thread_pool.queue_cond);
        pthread_cond_destroy(&g_resource_manager.thread_pool.finished_cond);
        return -1;
    }
    
    // 创建线程
    g_resource_manager.thread_pool.thread_count = thread_count;
    g_resource_manager.thread_pool.shutdown = false;
    
    for (uint32_t i = 0; i < thread_count; i++) {
        if (pthread_create(&g_resource_manager.thread_pool.threads[i], NULL, 
                          async_load_thread_func, NULL) != 0) {
            LOGE("创建异步加载线程失败");
            
            // 销毁已创建的线程
            g_resource_manager.thread_pool.shutdown = true;
            pthread_cond_broadcast(&g_resource_manager.thread_pool.queue_cond);
            
            for (uint32_t j = 0; j < i; j++) {
                pthread_join(g_resource_manager.thread_pool.threads[j], NULL);
            }
            
            free(g_resource_manager.thread_pool.threads);
            g_resource_manager.thread_pool.threads = NULL;
            g_resource_manager.thread_pool.thread_count = 0;
            
            pthread_mutex_destroy(&g_resource_manager.thread_pool.queue_mutex);
            pthread_cond_destroy(&g_resource_manager.thread_pool.queue_cond);
            pthread_cond_destroy(&g_resource_manager.thread_pool.finished_cond);
            
            return -1;
        }
    }
    
    LOGI("异步加载线程池初始化完成，线程数: %u", thread_count);
    
    return 0;
}

// 销毁线程池
void async_load_thread_pool_shutdown(void) {
    if (!g_resource_manager.thread_pool.threads) {
        return;
    }
    
    // 通知所有线程关闭
    g_resource_manager.thread_pool.shutdown = true;
    pthread_cond_broadcast(&g_resource_manager.thread_pool.queue_cond);
    
    // 等待所有线程退出
    for (uint32_t i = 0; i < g_resource_manager.thread_pool.thread_count; i++) {
        pthread_join(g_resource_manager.thread_pool.threads[i], NULL);
    }
    
    // 释放线程数组
    free(g_resource_manager.thread_pool.threads);
    g_resource_manager.thread_pool.threads = NULL;
    g_resource_manager.thread_pool.thread_count = 0;
    
    // 销毁互斥锁和条件变量
    pthread_mutex_destroy(&g_resource_manager.thread_pool.queue_mutex);
    pthread_cond_destroy(&g_resource_manager.thread_pool.queue_cond);
    pthread_cond_destroy(&g_resource_manager.thread_pool.finished_cond);
    
    LOGI("异步加载线程池已关闭");
}

// 异步加载线程函数
void* async_load_thread_func(void* arg) {
    while (!g_resource_manager.thread_pool.shutdown) {
        // 从任务队列取出任务
        struct async_load_task* task = async_load_task_pop();
        if (!task) {
            // 没有任务，等待
            pthread_mutex_lock(&g_resource_manager.thread_pool.queue_mutex);
            pthread_cond_wait(&g_resource_manager.thread_pool.queue_cond, 
                            &g_resource_manager.thread_pool.queue_mutex);
            pthread_mutex_unlock(&g_resource_manager.thread_pool.queue_mutex);
            continue;
        }
        
        // 执行任务
        async_load_task_execute(task);
        
        // 释放任务
        free(task);
    }
    
    return NULL;
}

// 任务队列管理
int async_load_task_push(struct async_load_task* task) {
    if (!task) {
        return -1;
    }
    
    // 这里简化实现，直接执行任务
    // 实际实现应该维护一个任务队列
    async_load_task_execute(task);
    
    return 0;
}

struct async_load_task* async_load_task_pop(void) {
    // 这里简化实现，返回NULL表示没有任务
    // 实际实现应该从任务队列中取出任务
    return NULL;
}

void async_load_task_execute(struct async_load_task* task) {
    if (!task || !task->resource) {
        return;
    }
    
    // 加载资源
    int result = resource_load(task->resource);
    
    // 更新异步加载状态
    pthread_mutex_lock(&g_resource_manager.async_queue.mutex);
    g_resource_manager.async_queue.current_loads--;
    pthread_mutex_unlock(&g_resource_manager.async_queue.mutex);
    
    // 调用回调函数
    if (task->callback) {
        task->callback(task->resource, result == 0, task->user_data);
    }
    
    LOGI("异步加载任务完成: %s (结果: %s)", 
         task->resource->name, result == 0 ? "成功" : "失败");
}
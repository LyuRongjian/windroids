#ifndef COMPOSITOR_VULKAN_TEXTURE_H
#define COMPOSITOR_VULKAN_TEXTURE_H

#include "compositor_vulkan_core.h"

// 纹理缓存项结构体
typedef struct TextureCacheEntry {
    VulkanTexture* texture;
    char* name;
    uint64_t last_access_time;
    uint64_t size_bytes;
    bool is_compressed;
    int access_count;
} TextureCacheEntry;

// 纹理缓存结构体
typedef struct TextureCache {
    TextureCacheEntry* textures;
    size_t texture_count;
    size_t max_textures;
    VkDevice device;
    size_t max_size_bytes;
    size_t current_size_bytes;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t eviction_count;
    uint64_t last_maintenance_time;
} TextureCache;

// 基于surface的纹理缓存项
typedef struct SurfaceTextureCacheItem {
    void* surface;
    uint32_t texture_id;
    uint64_t last_used;
    struct SurfaceTextureCacheItem* next;
} SurfaceTextureCacheItem;

// 纹理管理函数
int create_texture_from_surface(VulkanState* vulkan, void* surface, uint32_t* out_texture_id);
int update_texture_from_surface(VulkanState* vulkan, uint32_t texture_id, void* surface);
int destroy_texture(VulkanState* vulkan, uint32_t texture_id);
int get_texture(VulkanState* vulkan, uint32_t texture_id, VulkanTexture** out_texture);

// 纹理缓存管理函数
int init_texture_cache(VulkanState* vulkan, size_t max_size_bytes);
int init_surface_texture_cache(VulkanState* vulkan);
void cleanup_texture_cache(VulkanState* vulkan);
void cleanup_surface_texture_cache(VulkanState* vulkan);
uint32_t get_cached_texture_by_surface(VulkanState* vulkan, void* surface);

#endif // COMPOSITOR_VULKAN_TEXTURE_H
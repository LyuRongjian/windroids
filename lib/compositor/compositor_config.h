#ifndef COMPOSITOR_CONFIG_H
#define COMPOSITOR_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 配置值类型
typedef enum {
    CONFIG_TYPE_INT,
    CONFIG_TYPE_FLOAT,
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_STRING
} config_type_t;

// 配置项
struct config_item {
    char name[64];              // 配置项名称
    config_type_t type;         // 值类型
    union {
        int int_val;
        float float_val;
        bool bool_val;
        char* string_val;
    } value;
    union {
        int int_default;
        float float_default;
        bool bool_default;
        char* string_default;
    } default_value;
    bool modified;               // 是否已修改
    struct config_item* next;   // 链表下一项
};

// 配置管理器
struct config_manager {
    struct config_item* items;  // 配置项链表
    char config_file[256];      // 配置文件路径
    bool loaded;                // 是否已加载
    bool auto_save;             // 是否自动保存
};

// 初始化配置管理器
int config_init(const char* config_file);

// 销毁配置管理器
void config_destroy(void);

// 从文件加载配置
int config_load(void);

// 保存配置到文件
int config_save(void);

// 设置是否自动保存
void config_set_auto_save(bool auto_save);

// 注册整数配置项
int config_register_int(const char* name, int default_val);

// 注册浮点数配置项
int config_register_float(const char* name, float default_val);

// 注册布尔配置项
int config_register_bool(const char* name, bool default_val);

// 注册字符串配置项
int config_register_string(const char* name, const char* default_val);

// 获取整数配置值
int config_get_int(const char* name, int* value);

// 获取浮点数配置值
int config_get_float(const char* name, float* value);

// 获取布尔配置值
int config_get_bool(const char* name, bool* value);

// 获取字符串配置值
int config_get_string(const char* name, char* value, size_t max_len);

// 设置整数配置值
int config_set_int(const char* name, int value);

// 设置浮点数配置值
int config_set_float(const char* name, float value);

// 设置布尔配置值
int config_set_bool(const char* name, bool value);

// 设置字符串配置值
int config_set_string(const char* name, const char* value);

// 重置配置项为默认值
int config_reset(const char* name);

// 重置所有配置项为默认值
void config_reset_all(void);

// 检查配置项是否存在
bool config_exists(const char* name);

// 检查配置项是否已修改
bool config_is_modified(const char* name);

// 应用配置更改
int config_apply(void);

// 打印所有配置项
void config_print_all(void);

#ifdef __cplusplus
}
#endif

#endif // COMPOSITOR_CONFIG_H
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libcompositor
LOCAL_SRC_FILES := \
    compositor.c \
    compositor_input.c \
    compositor_window.c \
    compositor_render.c \
    compositor_resource.c \
    compositor_perf.c \
    compositor_perf_opt.c \
    compositor_game.c \
    compositor_monitor.c \
    compositor_vulkan.c

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    $(ANDROID_BUILD_TOP)/external/wlroots/include \
    $(ANDROID_BUILD_TOP)/external/wlroots/include/wlr \
    $(ANDROID_BUILD_TOP)/external/wayland/include \
    $(ANDROID_BUILD_TOP)/external/vulkan/include \
    $(ANDROID_BUILD_TOP)/external/vulkan-headers/include

LOCAL_CFLAGS := \
    -Wall -Werror -Wextra \
    -Wno-unused-parameter \
    -Wno-missing-field-initializers \
    -DWLR_USE_UNSTABLE

LOCAL_SHARED_LIBRARIES := \
    libwayland-server \
    libwlroots \
    libvulkan \
    liblog \
    libandroid

LOCAL_LDLIBS := -llog -landroid

include $(BUILD_SHARED_LIBRARY)
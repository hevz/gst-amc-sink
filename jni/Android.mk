# Android.mk

LOCAL_PATH := $(call my-dir)
 
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := libgstamcsink
LOCAL_SRC_FILES := \
    src/gst-amc-sink-plugin.c \
    src/gst-amc.c \
    src/gst-jni-utils.c \
    src/gst-amc-sink.c \
    src/gst-amc-video-decoder.c \
    src/gst-amc-video-sink.c

ifndef GSTREAMER_ROOT_ANDROID
$(error GSTREAMER_ROOT_ANDROID is not defined!)
endif

ifeq ($(TARGET_ARCH_ABI),armeabi)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/arm
else ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/armv7
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/arm64
else ifeq ($(TARGET_ARCH_ABI),x86)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/x86
else ifeq ($(TARGET_ARCH_ABI),x86_64)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/x86_64
else
$(error Target arch ABI not supported: $(TARGET_ARCH_ABI))
endif

LOCAL_C_INCLUDES := $(GSTREAMER_ROOT)/include/gstreamer-1.0 \
                    $(GSTREAMER_ROOT)/include/glib-2.0 \
                    $(GSTREAMER_ROOT)/include \
                    $(GSTREAMER_ROOT)/include/libxml2 \
                    $(GSTREAMER_ROOT)/lib/glib-2.0/include
LOCAL_CFLAGS += -DGST_PLUGIN_BUILD_STATIC

ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
LOCAL_CFLAGS += -mfpu=neon
endif

include $(BUILD_STATIC_LIBRARY)

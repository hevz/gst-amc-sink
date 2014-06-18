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

LOCAL_C_INCLUDES := $(GSTREAMER_SDK_ROOT_ANDROID)/include/gstreamer-1.0 \
		    $(GSTREAMER_SDK_ROOT_ANDROID)/include/glib-2.0 \
		    $(GSTREAMER_SDK_ROOT_ANDROID)/include \
		    $(GSTREAMER_SDK_ROOT_ANDROID)/include/libxml2 \
		    $(GSTREAMER_SDK_ROOT_ANDROID)/lib/glib-2.0/include
LOCAL_CFLAGS += -DGST_PLUGIN_BUILD_STATIC

include $(BUILD_STATIC_LIBRARY)
  

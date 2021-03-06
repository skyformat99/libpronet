PRO_JNI_DIR  := $(call my-dir)
PRO_ROOT_DIR := $(PRO_JNI_DIR)/../../..

APP_PROJECT_PATH := $(PRO_JNI_DIR)/..
APP_BUILD_SCRIPT := $(PRO_JNI_DIR)/Android.mk
APP_MODULES      := mbedtls         \
                    pro_shared      \
                    pro_util        \
                    pro_net         \
                    pro_rtp         \
                    pro_service_hub \
                    rtp_msg_server  \
                    rtp_msg_c2s     \
                    test_msg_client \
                    test_rtp        \
                    test_tcp_server \
                    test_tcp_client

APP_CFLAGS   := -D_DEBUG          \
                -D_GNU_SOURCE     \
		-D_LIBC_REENTRANT \
		-D_REENTRANT      \
		-Wall
APP_CPPFLAGS := -fno-rtti -fexceptions
APP_OPTIM    := debug
APP_LDFLAGS  :=

APP_PLATFORM := android-16
APP_STL      := gnustl_static

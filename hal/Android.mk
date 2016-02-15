#
# Copyright (C) 2016 The Android Open-Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)

# A static library providing glue logic that simplifies creation of NVRAM HAL
# modules.
include $(CLEAR_VARS)
LOCAL_MODULE := libnvram-hal
LOCAL_SRC_FILES := \
	nvram_device_adapter.cpp
LOCAL_SHARED_LIBRARIES := libnvram-messages
LOCAL_CFLAGS := -Wall -Werror -Wextra
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
include $(BUILD_STATIC_LIBRARY)

# nvram.testing is the software-only testing NVRAM HAL module.
include $(CLEAR_VARS)
LOCAL_MODULE := nvram.testing
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := \
	memory_storage.cpp \
	testing_module.c \
	testing_nvram_implementation.cpp
LOCAL_STATIC_LIBRARIES := libnvram-core libnvram-hal libmincrypt liblog
LOCAL_SHARED_LIBRARIES := libnvram-messages
LOCAL_CFLAGS := -Wall -Werror -Wextra -fvisibility=hidden
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

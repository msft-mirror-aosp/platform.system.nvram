/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <string.h>

#include <hardware/nvram.h>

#include <nvram/core/nvram_manager.h>
#include <nvram/hal/nvram_device_adapter.h>
#include <nvram/messages/nvram_messages.h>

namespace {

// This instantiates an |NvramManager| with the storage interface wired up with
// an in-memory implementation. This *DOES NOT* meet the persistence and tamper
// evidence requirements of the HAL, but is useful for demonstration and running
// tests against the |NvramManager| implementation.
class TestingNvramImplementation : public nvram::NvramImplementation {
 public:
  ~TestingNvramImplementation() override = default;

  void Execute(const nvram::Request& request,
               nvram::Response* response) override {
    nvram::Blob request_blob;
    if (!nvram::Encode(request, &request_blob)) {
      response->result = NV_RESULT_INVALID_PARAMETER;
      return;
    }
    nvram::Blob response_blob;
    if (!ExecuteEncoded(request_blob, &response_blob)) {
      response->result = NV_RESULT_INTERNAL_ERROR;
      return;
    }
    if (!nvram::Decode(response_blob.data(), response_blob.size(), response)) {
      response->result = NV_RESULT_INTERNAL_ERROR;
      return;
    }
  }

 private:
  // Mixing encoding into the Execute flow emulates scenarios where requests are
  // sent to another component for processing.
  bool ExecuteEncoded(const nvram::Blob& request_blob,
                      nvram::Blob* response_blob) {
    nvram::Request request;
    if (!nvram::Decode(request_blob.data(), request_blob.size(), &request)) {
      return false;
    }
    nvram::Response response;
    nvram_manager_.Dispatch(request, &response);
    if (!nvram::Encode(response, response_blob)) {
      return false;
    }
    return true;
  }
  nvram::NvramManager nvram_manager_;
};

}  // namespace

extern "C" int testing_nvram_open(const hw_module_t* module,
                                  const char* device_id,
                                  hw_device_t** device_ptr) {
  if (strcmp(NVRAM_HARDWARE_DEVICE_ID, device_id) != 0) {
    return -EINVAL;
  }

  nvram::NvramDeviceAdapter* adapter =
      new nvram::NvramDeviceAdapter(module, new TestingNvramImplementation);
  *device_ptr = adapter->as_device();
  return 0;
}

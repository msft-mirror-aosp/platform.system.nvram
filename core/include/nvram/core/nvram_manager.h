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

#ifndef NVRAM_CORE_NVRAM_MANAGER_H_
#define NVRAM_CORE_NVRAM_MANAGER_H_

#include <nvram/messages/nvram_messages.h>

#include <nvram/core/persistence.h>

namespace nvram {

// |NvramManager| implements the core functionality of the access-controlled
// NVRAM HAL backend. It keeps track of the allocated spaces and their state,
// including the transient state that is held per boot. It provides operations
// for querying, creating, deleting, reading and writing spaces. It deals with
// persistent storage objects in the form of |NvramHeader| and |NvramSpace|
// objects and uses the persistence layer to read and write them from persistent
// storage.
class NvramManager {
 private:
  // Holds transient state corresponding to an allocated NVRAM space, i.e. meta
  // data valid for a single boot. One instance of this struct is kept in memory
  // in the |spaces_| array for each of the spaces that are currently allocated.
  struct SpaceListEntry {
    uint32_t index;
    bool write_locked = false;
    bool read_locked = false;
  };

  // Initializes |header_| from storage if that hasn't happened already. Returns
  // true if NvramManager object is initialized and ready to serve requests. May
  // be called again after failure to attempt initialization again.
  bool Initialize();

  // Writes the header to storage and returns a suitable status code.
  nvram_result_t WriteHeader(Optional<uint32_t> provisional_index);

  // Maximum number of NVRAM spaces we're willing to allocate.
  static constexpr size_t kMaxSpaces = 32;

  bool initialized_ = false;
  bool disable_create_ = false;

  // Bookkeeping information for allocated spaces.
  size_t num_spaces_ = 0;
  SpaceListEntry spaces_[kMaxSpaces];
};

}  // namespace nvram

#endif  // NVRAM_CORE_NVRAM_MANAGER_H_

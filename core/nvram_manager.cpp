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

#include "nvram/core/nvram_manager.h"

#include <nvram/core/logger.h>

namespace nvram {

bool NvramManager::Initialize() {
  if (initialized_)
    return true;

  NvramHeader header;
  switch (persistence::LoadHeader(&header)) {
    case storage::Status::kStorageError:
      NVRAM_LOG_ERR("Init failed to load header.");
      return false;
    case storage::Status::kNotFound:
      // No header in storage. This happens the very first time we initialize
      // on a fresh device where the header isn't present yet. The first write
      // will flush the fresh header to storage.
      initialized_ = true;
      return true;
    case storage::Status::kSuccess:
      if (header.version > NvramHeader::kVersion) {
        NVRAM_LOG_ERR("Storage format %u is more recent than %u, aborting.",
                      header.version, NvramHeader::kVersion);
        return false;
      }
      break;
  }

  // Check the state of the provisional space if applicable.
  const Optional<uint32_t>& provisional_index = header.provisional_index;
  bool provisional_space_in_storage = false;
  if (provisional_index.valid()) {
    NvramSpace space;
    switch (persistence::LoadSpace(provisional_index.value(), &space)) {
      case storage::Status::kStorageError:
        // Log an error but leave the space marked as allocated. This will allow
        // initialization to complete, so other spaces can be accessed.
        // Operations on the bad space will fail however. The choice of keeping
        // the bad space around (as opposed to dropping it) is intentional:
        //  * Failing noisily reduces the chances of bugs going undetected.
        //  * Keeping the index allocated prevents it from being accidentally
        //    clobbered due to appearing absent after transient storage errors.
        NVRAM_LOG_ERR("Failed to load provisional space 0x%x.",
                      provisional_index.value());
        provisional_space_in_storage = true;
        break;
      case storage::Status::kNotFound:
        break;
      case storage::Status::kSuccess:
        provisional_space_in_storage = true;
        break;
    }
  }

  // If there are more spaces allocated than this build supports, fail
  // initialization. This may seem a bit drastic, but the alternatives aren't
  // acceptable:
  //  * If we continued with just a subset of the spaces, that may lead to wrong
  //    conclusions about the system state in consumers. Furthermore, consumers
  //    might delete a space to make room and then create a space that appears
  //    free but is present in storage. This would clobber the existing space
  //    data and potentially violate its access control rules.
  //  * We could just try to allocate more memory to hold the larger number of
  //    spaces. That'd render the memory footprint of the NVRAM implementation
  //    unpredictable. One variation that may work is to allow a maximum number
  //    of existing spaces larger than kMaxSpaces, but still within sane limits.
  if (header.allocated_indices.size() > kMaxSpaces) {
    NVRAM_LOG_ERR("Excess spaces %zu in header.",
                  header.allocated_indices.size());
    return false;
  }

  // Initialize the transient space bookkeeping data.
  bool delete_provisional_space = provisional_index.valid();
  for (uint32_t index : header.allocated_indices) {
    if (provisional_index.valid() && provisional_index.value() == index) {
      // The provisional space index refers to a created space. If it isn't
      // valid, pretend it was never created.
      if (!provisional_space_in_storage) {
        continue;
      }

      // The provisional space index corresponds to a created space that is
      // present in storage. Retain the space.
      delete_provisional_space = false;
    }

    spaces_[num_spaces_].index = index;
    spaces_[num_spaces_].write_locked = false;
    spaces_[num_spaces_].read_locked = false;
    ++num_spaces_;
  }

  // If the provisional space data is present in storage, but the index wasn't
  // in |header.allocated_indices|, it refers to half-deleted space. Destroy the
  // space in that case.
  if (delete_provisional_space) {
    switch (persistence::DeleteSpace(provisional_index.value())) {
      case storage::Status::kStorageError:
        NVRAM_LOG_ERR("Failed to delete provisional space 0x%x data.",
                      provisional_index.value());
        return false;
      case storage::Status::kNotFound:
        NVRAM_LOG_ERR("Provisional space 0x%x absent on deletion.",
                      provisional_index.value());
        return false;
      case storage::Status::kSuccess:
        break;
    }
  }

  disable_create_ = header.HasFlag(NvramHeader::kFlagDisableCreate);
  initialized_ = true;

  // Write the header to clear the provisional index if necessary. It's actually
  // not a problem if this fails, because the state is consistent regardless. We
  // still do this opportunistically in order to avoid loading the provisional
  // space data for each reboot after a crash.
  if (provisional_index.valid()) {
    WriteHeader(Optional<uint32_t>());
  }

  return true;
}

nvram_result_t NvramManager::WriteHeader(Optional<uint32_t> provisional_index) {
  NvramHeader header;
  header.version = NvramHeader::kVersion;
  if (disable_create_) {
    header.SetFlag(NvramHeader::kFlagDisableCreate);
  }

  if (!header.allocated_indices.Resize(num_spaces_)) {
    NVRAM_LOG_ERR("Allocation failure.");
    return NV_RESULT_INTERNAL_ERROR;
  }
  for (size_t i = 0; i < num_spaces_; ++i) {
    header.allocated_indices[i] = spaces_[i].index;
  }

  header.provisional_index = provisional_index;

  if (persistence::StoreHeader(header) != storage::Status::kSuccess) {
    NVRAM_LOG_ERR("Failed to store header.");
    return NV_RESULT_INTERNAL_ERROR;
  }

  return NV_RESULT_SUCCESS;
}

}  // namespace nvram

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

extern "C" {
#include <string.h>
}  // extern "C"

using namespace nvram::storage;

namespace nvram {

namespace {

// Maximum size of a single space's contents.
constexpr size_t kMaxSpaceSize = 1024;

// Maximum authorization blob size;
constexpr size_t kMaxAuthSize = 32;

// The bitmask of all supported control flags.
constexpr uint32_t kSupportedControlsMask =
    (1 << NV_CONTROL_PERSISTENT_WRITE_LOCK) |
    (1 << NV_CONTROL_BOOT_WRITE_LOCK) |
    (1 << NV_CONTROL_BOOT_READ_LOCK) |
    (1 << NV_CONTROL_WRITE_AUTHORIZATION) |
    (1 << NV_CONTROL_READ_AUTHORIZATION) |
    (1 << NV_CONTROL_WRITE_EXTEND);

// Convert the |space.controls| bitmask to vector representation.
nvram_result_t GetControlsVector(const NvramSpace& space,
                                 Vector<nvram_control_t>* controls) {
  for (size_t control = 0; control < sizeof(uint32_t) * 8; ++control) {
    if (space.HasControl(control)) {
      if (!controls->Resize(controls->size() + 1)) {
        NVRAM_LOG_ERR("Allocation failure.");
        return NV_RESULT_INTERNAL_ERROR;
      }
      (*controls)[controls->size() - 1] = static_cast<nvram_control_t>(control);
    }
  }
  return NV_RESULT_SUCCESS;
}

}  // namespace

nvram_result_t NvramManager::GetInfo(const GetInfoRequest& /* request */,
                                     GetInfoResponse* response) {
  NVRAM_LOG_INFO("GetInfo");

  if (!Initialize())
    return NV_RESULT_INTERNAL_ERROR;

  // TODO: Get better values for total and available size from the storage
  // layer.
  response->total_size = kMaxSpaceSize * kMaxSpaces;
  response->available_size = kMaxSpaceSize * (kMaxSpaces - num_spaces_);
  response->max_spaces = kMaxSpaces;
  Vector<uint32_t>& space_list = response->space_list;
  if (!space_list.Resize(num_spaces_)) {
    NVRAM_LOG_ERR("Allocation failure.");
    return NV_RESULT_INTERNAL_ERROR;
  }
  for (size_t i = 0; i < num_spaces_; ++i) {
    space_list[i] = spaces_[i].index;
  }

  return NV_RESULT_SUCCESS;
}

nvram_result_t NvramManager::CreateSpace(const CreateSpaceRequest& request,
                                         CreateSpaceResponse* /* response */) {
  const uint32_t index = request.index;
  NVRAM_LOG_INFO("CreateSpace Ox%x", index);

  if (!Initialize())
    return NV_RESULT_INTERNAL_ERROR;

  if (disable_create_) {
    NVRAM_LOG_INFO("Creation of further spaces is disabled.");
    return NV_RESULT_OPERATION_DISABLED;
  }

  if (FindSpace(index) != kMaxSpaces) {
    NVRAM_LOG_INFO("Space 0x%x already exists.", index);
    return NV_RESULT_SPACE_ALREADY_EXISTS;
  }

  if (num_spaces_ + 1 > kMaxSpaces) {
    NVRAM_LOG_INFO("Too many spaces.");
    return NV_RESULT_INVALID_PARAMETER;
  }

  if (request.size > kMaxSpaceSize) {
    NVRAM_LOG_INFO("Create request exceeds max space size.");
    return NV_RESULT_INVALID_PARAMETER;
  }

  if (request.authorization_value.size() > kMaxAuthSize) {
    NVRAM_LOG_INFO("Authorization blob too large.");
    return NV_RESULT_INVALID_PARAMETER;
  }

  uint32_t controls = 0;
  for (uint32_t control : request.controls) {
    controls |= (1 << control);
  }
  if ((controls & ~kSupportedControlsMask) != 0) {
    NVRAM_LOG_INFO("Bad controls.");
    return NV_RESULT_INVALID_PARAMETER;
  }
  if ((controls & (1 << NV_CONTROL_PERSISTENT_WRITE_LOCK)) != 0 &&
      (controls & (1 << NV_CONTROL_BOOT_WRITE_LOCK)) != 0) {
    NVRAM_LOG_INFO("Write lock controls are exclusive.");
    return NV_RESULT_INVALID_PARAMETER;
  }

  // Mark the index as allocated.
  spaces_[num_spaces_].index = index;
  spaces_[num_spaces_].write_locked = false;
  spaces_[num_spaces_].read_locked = false;
  ++num_spaces_;

  // Create a space record.
  NvramSpace space;
  space.flags = 0;
  space.controls = controls;

  // Copy the auth blob.
  if (space.HasControl(NV_CONTROL_WRITE_AUTHORIZATION) ||
      space.HasControl(NV_CONTROL_READ_AUTHORIZATION)) {
    if (!space.authorization_value.Assign(request.authorization_value.data(),
                                          request.authorization_value.size())) {
      NVRAM_LOG_ERR("Allocation failure.");
      return NV_RESULT_INTERNAL_ERROR;
    }
  }

  // Initialize the space content.
  if (!space.contents.Resize(request.size)) {
    NVRAM_LOG_ERR("Allocation failure.");
    return NV_RESULT_INTERNAL_ERROR;
  }
  memset(space.contents.data(), 0, request.size);

  // Write the header before the space data. This ensures that all space
  // definitions present in storage are also recorded in the header. Thus, the
  // set of spaces present in the header is always a superset of the set of
  // spaces that have state in storage. If there's a crash after writing the
  // header but before writing the space information, the space data will be
  // missing in storage. The initialization code handles this by checking the
  // for the space data corresponding to the index marked as provisional in the
  // header.
  nvram_result_t result;
  if ((result = WriteHeader(Optional<uint32_t>(index))) != NV_RESULT_SUCCESS ||
      (result = WriteSpace(index, space)) != NV_RESULT_SUCCESS) {
    --num_spaces_;
  }
  return result;
}

nvram_result_t NvramManager::GetSpaceInfo(const GetSpaceInfoRequest& request,
                                          GetSpaceInfoResponse* response) {
  const uint32_t index = request.index;
  NVRAM_LOG_INFO("GetSpaceInfo Ox%x", index);

  if (!Initialize())
    return NV_RESULT_INTERNAL_ERROR;

  SpaceRecord space_record;
  nvram_result_t result;
  if (!LoadSpaceRecord(index, &space_record, &result)) {
    return result;
  }

  response->size = space_record.persistent.contents.size();

  result = GetControlsVector(space_record.persistent, &response->controls);
  if (result != NV_RESULT_SUCCESS) {
    return NV_RESULT_INTERNAL_ERROR;
  }

  if (space_record.persistent.HasControl(NV_CONTROL_BOOT_READ_LOCK)) {
    response->read_locked = space_record.transient->read_locked;
  }

  if (space_record.persistent.HasControl(NV_CONTROL_PERSISTENT_WRITE_LOCK)) {
    response->write_locked =
        space_record.persistent.HasFlag(NvramSpace::kFlagWriteLocked);
  } else if (space_record.persistent.HasControl(NV_CONTROL_BOOT_WRITE_LOCK)) {
    response->write_locked = space_record.transient->write_locked;
  }

  return NV_RESULT_SUCCESS;
}

nvram_result_t NvramManager::DisableCreate(
    const DisableCreateRequest& /* request */,
    DisableCreateResponse* /* response */) {
  NVRAM_LOG_INFO("DisableCreate");

  if (!Initialize())
    return NV_RESULT_INTERNAL_ERROR;

  // Set the |disable_create_| flag and call |WriteHeader| to persist the flag
  // such that it remains effective after a reboot.
  disable_create_ = true;
  return WriteHeader(Optional<uint32_t>());
}

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

size_t NvramManager::FindSpace(uint32_t space_index) {
  for (size_t i = 0; i < num_spaces_; ++i) {
    if (spaces_[i].index == space_index) {
      return i;
    }
  }

  return kMaxSpaces;
}

bool NvramManager::LoadSpaceRecord(uint32_t index,
                                   SpaceRecord* space_record,
                                   nvram_result_t* result) {
  space_record->array_index = FindSpace(index);
  if (space_record->array_index == kMaxSpaces) {
    *result = NV_RESULT_SPACE_DOES_NOT_EXIST;
    return false;
  }

  space_record->transient = &spaces_[space_record->array_index];

  switch (persistence::LoadSpace(index, &space_record->persistent)) {
    case storage::Status::kStorageError:
      NVRAM_LOG_ERR("Failed to load space 0x%x data.", index);
      *result = NV_RESULT_INTERNAL_ERROR;
      return false;
    case storage::Status::kNotFound:
      // This should never happen if the header contains the index.
      NVRAM_LOG_ERR("Space index 0x%x present in header, but data missing.",
                    index);
      *result = NV_RESULT_INTERNAL_ERROR;
      return false;
    case storage::Status::kSuccess:
      *result = NV_RESULT_SUCCESS;
      return true;
  }

  *result = NV_RESULT_INTERNAL_ERROR;
  return false;
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

nvram_result_t NvramManager::WriteSpace(uint32_t index,
                                        const NvramSpace& space) {
  if (persistence::StoreSpace(index, space) != storage::Status::kSuccess) {
    NVRAM_LOG_ERR("Failed to store space 0x%x.", index);
    return NV_RESULT_INTERNAL_ERROR;
  }

  return NV_RESULT_SUCCESS;
}

}  // namespace nvram

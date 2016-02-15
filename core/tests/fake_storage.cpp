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

#include "fake_storage.h"

#include <nvram/messages/blob.h>
#include <nvram/messages/compiler.h>

#define countof(a) (sizeof(a) / sizeof((a)[0]))

namespace nvram {
namespace storage {

namespace {

class StorageSlot {
 public:
  Status Load(Blob* blob) {
    if (error_) {
      return Status::kStorageError;
    }

    if (!present_) {
      return Status::kNotFound;
    }

    NVRAM_CHECK(blob->Assign(blob_.data(), blob_.size()));
    return Status::kSuccess;
  }

  Status Store(const Blob& blob) {
    if (error_) {
      return Status::kStorageError;
    }

    NVRAM_CHECK(blob_.Assign(blob.data(), blob.size()));
    present_ = true;
    return Status::kSuccess;
  }

  void Clear() {
    NVRAM_CHECK(blob_.Resize(0));
    present_ = false;
  }

  bool present() const { return present_; }
  void set_present(bool present) { present_ = present; }
  void set_error(bool error) { error_ = error; }

 private:
  bool present_ = false;
  bool error_ = false;
  Blob blob_;
};

// Header storage.
StorageSlot g_header;

// Space blob storage.
struct SpaceStorageSlot {
  uint32_t index;
  StorageSlot slot;
};

SpaceStorageSlot g_spaces[256];

// Find the position in |g_spaces| corresponding to a given space |index|.
// Returns the slot pointer or |nullptr| if not found.
StorageSlot* FindSlotForIndex(uint32_t index) {
  for (size_t i = 0; i < countof(g_spaces); ++i) {
    if (g_spaces[i].slot.present() && g_spaces[i].index == index) {
      return &g_spaces[i].slot;
    }
  }

  return nullptr;
}

// Finds or creates the slot for |index|. Returns the slot pointer or |nullptr|
// if not found.
StorageSlot* FindOrCreateSlotForIndex(uint32_t index) {
  StorageSlot* slot = FindSlotForIndex(index);
  if (slot) {
    return slot;
  }


  for (size_t i = 0; i < countof(g_spaces); ++i) {
    if (!g_spaces[i].slot.present()) {
      g_spaces[i].index = index;
      return &g_spaces[i].slot;
    }
  }

  return nullptr;
}

}  // namespace

Status LoadHeader(Blob* blob) {
  return g_header.Load(blob);
}

Status StoreHeader(const Blob& blob) {
  return g_header.Store(blob);
}

void SetHeaderError(bool error) {
  g_header.set_error(error);
}

Status LoadSpace(uint32_t index, Blob* blob) {
  StorageSlot* slot = FindSlotForIndex(index);
  return slot ? slot->Load(blob) : Status::kNotFound;
}

Status StoreSpace(uint32_t index, const Blob& blob) {
  StorageSlot* slot = FindOrCreateSlotForIndex(index);
  return slot ? slot->Store(blob) : Status::kStorageError;
}

Status DeleteSpace(uint32_t index) {
  StorageSlot* slot = FindSlotForIndex(index);
  if (slot) {
    slot->Clear();
  }

  return Status::kSuccess;
}

void Clear() {
  g_header.Clear();
  for (size_t i = 0; i < countof(g_spaces); ++i) {
    g_spaces[i].slot.Clear();
  }
}

void SetSpaceError(uint32_t index, bool error) {
  StorageSlot* slot = FindOrCreateSlotForIndex(index);
  if (slot) {
    slot->set_error(error);
  }
}

}  // namespace storage
}  // namespace nvram

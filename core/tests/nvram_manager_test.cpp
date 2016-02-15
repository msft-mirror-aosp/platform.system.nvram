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

#include <gtest/gtest.h>

#include <nvram/core/nvram_manager.h>
#include <nvram/core/persistence.h>

#include "fake_storage.h"

namespace nvram {
namespace {

class NvramManagerTest : public testing::Test {
 protected:
  NvramManagerTest() {
    storage::Clear();
  }

  void SetupHeader(uint32_t header_version, uint32_t index) {
    NvramHeader header;
    header.version = header_version;
    ASSERT_TRUE(header.allocated_indices.Resize(1));
    header.allocated_indices[0] = index;
    ASSERT_EQ(storage::Status::kSuccess, persistence::StoreHeader(header));
  }

  static uint32_t GetControlsMask(const Vector<nvram_control_t>& controls) {
    uint32_t mask = 0;
    for (nvram_control_t control : controls) {
      mask |= (1 << control);
    }
    return mask;
  }
};

TEST_F(NvramManagerTest, Init_FromScratch) {
  NvramManager nvram;

  GetSpaceInfoRequest get_space_info_request;
  get_space_info_request.index = 1;
  GetSpaceInfoResponse get_space_info_response;
  EXPECT_EQ(
      NV_RESULT_SPACE_DOES_NOT_EXIST,
      nvram.GetSpaceInfo(get_space_info_request, &get_space_info_response));
}

TEST_F(NvramManagerTest, Init_TrailingStorageBytes) {
  // Set up a pre-existing space and add some trailing bytes.
  NvramSpace space;
  ASSERT_TRUE(space.contents.Resize(10));
  ASSERT_EQ(storage::Status::kSuccess, persistence::StoreSpace(1, space));
  Blob space_blob;
  ASSERT_EQ(storage::Status::kSuccess, storage::LoadSpace(1, &space_blob));
  ASSERT_TRUE(space_blob.Resize(space_blob.size() + 10));
  ASSERT_EQ(storage::Status::kSuccess, storage::StoreSpace(1, space_blob));

  // Produce a matching header and append some trailing bytes.
  NvramHeader header;
  header.version = NvramHeader::kVersion;
  ASSERT_TRUE(header.allocated_indices.Resize(1));
  header.allocated_indices[0] = 1;
  ASSERT_EQ(storage::Status::kSuccess, persistence::StoreHeader(header));
  Blob header_blob;
  ASSERT_EQ(storage::Status::kSuccess, storage::LoadHeader(&header_blob));
  ASSERT_TRUE(header_blob.Resize(header_blob.size() + 10));
  ASSERT_EQ(storage::Status::kSuccess, storage::StoreHeader(header_blob));

  // Initialize the |NvramManager| and check that the header and space blobs get
  // loaded successfully.
  NvramManager nvram;

  GetInfoRequest get_info_request;
  GetInfoResponse get_info_response;
  EXPECT_EQ(NV_RESULT_SUCCESS,
            nvram.GetInfo(get_info_request, &get_info_response));
  ASSERT_EQ(1U, get_info_response.space_list.size());
  EXPECT_EQ(1U, get_info_response.space_list[0]);

  GetSpaceInfoRequest get_space_info_request;
  get_space_info_request.index = 1;
  GetSpaceInfoResponse get_space_info_response;
  EXPECT_EQ(NV_RESULT_SUCCESS, nvram.GetSpaceInfo(get_space_info_request,
                                                  &get_space_info_response));
  EXPECT_EQ(10U, get_space_info_response.size);
}

TEST_F(NvramManagerTest, Init_SpacesPresent) {
  // Set up two pre-existing spaces.
  NvramSpace space;
  ASSERT_TRUE(space.contents.Resize(10));
  ASSERT_EQ(storage::Status::kSuccess, persistence::StoreSpace(1, space));
  ASSERT_TRUE(space.contents.Resize(20));
  ASSERT_EQ(storage::Status::kSuccess, persistence::StoreSpace(2, space));

  // Indicate 3 present spaces in the header, including one that doesn't have
  // space data in storage.
  NvramHeader header;
  header.version = NvramHeader::kVersion;
  ASSERT_TRUE(header.allocated_indices.Resize(3));
  header.allocated_indices[0] = 1;
  header.allocated_indices[1] = 2;
  header.allocated_indices[2] = 3;
  header.provisional_index.Activate() = 4;
  ASSERT_EQ(storage::Status::kSuccess, persistence::StoreHeader(header));

  NvramManager nvram;

  // Check that the spaces are correctly recovered.
  GetSpaceInfoRequest get_space_info_request;
  get_space_info_request.index = 1;
  GetSpaceInfoResponse get_space_info_response;
  EXPECT_EQ(NV_RESULT_SUCCESS, nvram.GetSpaceInfo(get_space_info_request,
                                                  &get_space_info_response));
  EXPECT_EQ(10u, get_space_info_response.size);

  get_space_info_request.index = 2;
  EXPECT_EQ(NV_RESULT_SUCCESS, nvram.GetSpaceInfo(get_space_info_request,
                                                  &get_space_info_response));
  EXPECT_EQ(20u, get_space_info_response.size);

  get_space_info_request.index = 3;
  EXPECT_EQ(
      NV_RESULT_INTERNAL_ERROR,
      nvram.GetSpaceInfo(get_space_info_request, &get_space_info_response));

  get_space_info_request.index = 4;
  EXPECT_EQ(
      NV_RESULT_SPACE_DOES_NOT_EXIST,
      nvram.GetSpaceInfo(get_space_info_request, &get_space_info_response));
}

TEST_F(NvramManagerTest, Init_BadSpacePresent) {
  // Set up a good and a bad NVRAM space.
  NvramSpace space;
  ASSERT_TRUE(space.contents.Resize(10));
  ASSERT_EQ(storage::Status::kSuccess, persistence::StoreSpace(1, space));
  const uint8_t kBadSpaceData[] = {0xba, 0xad};
  Blob bad_space_blob;
  ASSERT_TRUE(bad_space_blob.Assign(kBadSpaceData, sizeof(kBadSpaceData)));
  ASSERT_EQ(storage::Status::kSuccess,
            storage::StoreSpace(2, bad_space_blob));

  NvramHeader header;
  header.version = NvramHeader::kVersion;
  ASSERT_TRUE(header.allocated_indices.Resize(2));
  header.allocated_indices[0] = 1;
  header.allocated_indices[1] = 2;
  ASSERT_EQ(storage::Status::kSuccess, persistence::StoreHeader(header));

  NvramManager nvram;

  // The bad index will fail requests.
  GetSpaceInfoRequest get_space_info_request;
  get_space_info_request.index = 2;
  GetSpaceInfoResponse get_space_info_response;
  nvram_result_t result =
      nvram.GetSpaceInfo(get_space_info_request, &get_space_info_response);
  EXPECT_NE(NV_RESULT_SUCCESS, result);
  EXPECT_NE(NV_RESULT_SPACE_DOES_NOT_EXIST, result);

  // A request to get info for the good index should succeed.
  get_space_info_request.index = 1;
  EXPECT_EQ(NV_RESULT_SUCCESS, nvram.GetSpaceInfo(get_space_info_request,
                                                  &get_space_info_response));
  EXPECT_EQ(10u, get_space_info_response.size);
}

TEST_F(NvramManagerTest, Init_NewerStorageVersion) {
  // Set up an NVRAM space.
  NvramSpace space;
  ASSERT_TRUE(space.contents.Resize(10));
  ASSERT_EQ(storage::Status::kSuccess, persistence::StoreSpace(1, space));

  SetupHeader(NvramHeader::kVersion + 1, 1);

  NvramManager nvram;

  // Requests should fail due to version mismatch.
  GetSpaceInfoRequest get_space_info_request;
  get_space_info_request.index = 1;
  GetSpaceInfoResponse get_space_info_response;
  EXPECT_EQ(
      NV_RESULT_INTERNAL_ERROR,
      nvram.GetSpaceInfo(get_space_info_request, &get_space_info_response));
}

TEST_F(NvramManagerTest, Init_StorageObjectTypeMismatch) {
  // Set up an NVRAM space.
  NvramSpace space;
  ASSERT_TRUE(space.contents.Resize(10));
  ASSERT_EQ(storage::Status::kSuccess, persistence::StoreSpace(1, space));

  // Copy the space blob to the header storage.
  Blob space_blob;
  ASSERT_EQ(storage::Status::kSuccess, storage::LoadSpace(1, &space_blob));
  ASSERT_EQ(storage::Status::kSuccess, storage::StoreHeader(space_blob));

  NvramManager nvram;

  // Initialization should detect that the header storage object doesn't look
  // like a header, so initialization should fail.
  GetInfoRequest get_info_request;
  GetInfoResponse get_info_response;
  EXPECT_EQ(NV_RESULT_INTERNAL_ERROR,
            nvram.GetInfo(get_info_request, &get_info_response));
}

TEST_F(NvramManagerTest, CreateSpace_Success) {
  NvramManager nvram;

  // Make a call to CreateSpace, which should succeed.
  CreateSpaceRequest create_space_request;
  create_space_request.index = 1;
  create_space_request.size = 16;
  ASSERT_TRUE(create_space_request.controls.Resize(5));
  create_space_request.controls[0] = NV_CONTROL_BOOT_WRITE_LOCK;
  create_space_request.controls[1] = NV_CONTROL_BOOT_READ_LOCK;
  create_space_request.controls[2] = NV_CONTROL_WRITE_AUTHORIZATION;
  create_space_request.controls[3] = NV_CONTROL_READ_AUTHORIZATION;
  create_space_request.controls[4] = NV_CONTROL_WRITE_EXTEND;

  CreateSpaceResponse create_space_response;
  EXPECT_EQ(NV_RESULT_SUCCESS,
            nvram.CreateSpace(create_space_request, &create_space_response));

  // GetSpaceInfo should reflect the space parameters set during creation.
  GetSpaceInfoRequest get_space_info_request;
  get_space_info_request.index = 1;
  GetSpaceInfoResponse get_space_info_response;
  EXPECT_EQ(NV_RESULT_SUCCESS, nvram.GetSpaceInfo(get_space_info_request,
                                                  &get_space_info_response));

  EXPECT_EQ(16u, get_space_info_response.size);
  EXPECT_EQ(GetControlsMask(create_space_request.controls),
            GetControlsMask(get_space_info_response.controls));
  EXPECT_EQ(false, get_space_info_response.read_locked);
  EXPECT_EQ(false, get_space_info_response.write_locked);
}

TEST_F(NvramManagerTest, CreateSpace_Existing) {
  // Set up an NVRAM space.
  NvramSpace space;
  ASSERT_TRUE(space.contents.Resize(10));
  ASSERT_EQ(storage::Status::kSuccess, persistence::StoreSpace(1, space));

  SetupHeader(NvramHeader::kVersion, 1);

  NvramManager nvram;

  // A request to create another space with the same index should fail.
  CreateSpaceRequest create_space_request;
  create_space_request.index = 1;
  create_space_request.size = 16;

  CreateSpaceResponse create_space_response;
  EXPECT_EQ(NV_RESULT_SPACE_ALREADY_EXISTS,
            nvram.CreateSpace(create_space_request, &create_space_response));
}

TEST_F(NvramManagerTest, CreateSpace_TooLarge) {
  NvramManager nvram;

  // A request to create a space with a too large content size should fail.
  CreateSpaceRequest create_space_request;
  create_space_request.index = 1;
  create_space_request.size = 16384;

  CreateSpaceResponse create_space_response;
  EXPECT_EQ(NV_RESULT_INVALID_PARAMETER,
            nvram.CreateSpace(create_space_request, &create_space_response));
}

TEST_F(NvramManagerTest, CreateSpace_AuthTooLarge) {
  NvramManager nvram;

  // A request to create a space with a too large authorization value size
  // should fail.
  CreateSpaceRequest create_space_request;
  create_space_request.index = 1;
  ASSERT_TRUE(create_space_request.authorization_value.Resize(256));

  CreateSpaceResponse create_space_response;
  EXPECT_EQ(NV_RESULT_INVALID_PARAMETER,
            nvram.CreateSpace(create_space_request, &create_space_response));
}

TEST_F(NvramManagerTest, CreateSpace_BadControl) {
  NvramManager nvram;

  // A request to create a space with an unknown control value should fail.
  CreateSpaceRequest create_space_request;
  create_space_request.index = 1;
  create_space_request.size = 16;
  ASSERT_TRUE(create_space_request.controls.Resize(2));
  create_space_request.controls[0] = NV_CONTROL_BOOT_WRITE_LOCK;
  create_space_request.controls[1] = 17;

  CreateSpaceResponse create_space_response;
  EXPECT_EQ(NV_RESULT_INVALID_PARAMETER,
            nvram.CreateSpace(create_space_request, &create_space_response));
}

TEST_F(NvramManagerTest, CreateSpace_ControlWriteLockExclusive) {
  NvramManager nvram;

  // Spaces may not be created with conflicting write lock modes.
  CreateSpaceRequest create_space_request;
  create_space_request.index = 1;
  create_space_request.size = 16;
  ASSERT_TRUE(create_space_request.controls.Resize(2));
  create_space_request.controls[0] = NV_CONTROL_BOOT_WRITE_LOCK;
  create_space_request.controls[1] = NV_CONTROL_PERSISTENT_WRITE_LOCK;

  CreateSpaceResponse create_space_response;
  EXPECT_EQ(NV_RESULT_INVALID_PARAMETER,
            nvram.CreateSpace(create_space_request, &create_space_response));
}

TEST_F(NvramManagerTest, CreateSpace_HeaderWriteError) {
  // Initialize the |NvramManager|.
  NvramManager nvram;
  GetInfoRequest get_info_request;
  GetInfoResponse get_info_response;
  EXPECT_EQ(NV_RESULT_SUCCESS,
            nvram.GetInfo(get_info_request, &get_info_response));
  EXPECT_EQ(0U, get_info_response.space_list.size());

  // If the header fails to get written to storage, the creation request should
  // fail.
  storage::SetHeaderError(true);

  CreateSpaceRequest create_space_request;
  create_space_request.index = 1;
  create_space_request.size = 16;

  CreateSpaceResponse create_space_response;
  EXPECT_EQ(NV_RESULT_INTERNAL_ERROR,
            nvram.CreateSpace(create_space_request, &create_space_response));

  // The space shouldn't be present.
  EXPECT_EQ(NV_RESULT_SUCCESS,
            nvram.GetInfo(get_info_request, &get_info_response));
  EXPECT_EQ(0U, get_info_response.space_list.size());

  // Creation of the space after clearing the error should work.
  storage::SetHeaderError(false);
  EXPECT_EQ(NV_RESULT_SUCCESS,
            nvram.CreateSpace(create_space_request, &create_space_response));

  // The space should be reported as allocated now.
  EXPECT_EQ(NV_RESULT_SUCCESS,
            nvram.GetInfo(get_info_request, &get_info_response));
  ASSERT_EQ(1U, get_info_response.space_list.size());
  EXPECT_EQ(1U, get_info_response.space_list[0]);
}

TEST_F(NvramManagerTest, CreateSpace_SpaceWriteError) {
  storage::SetSpaceError(1, true);
  NvramManager nvram;

  // A request to create another space with the same index should fail.
  CreateSpaceRequest create_space_request;
  create_space_request.index = 1;
  create_space_request.size = 16;

  CreateSpaceResponse create_space_response;
  EXPECT_EQ(NV_RESULT_INTERNAL_ERROR,
            nvram.CreateSpace(create_space_request, &create_space_response));

  // Reloading the state after a crash should not show any traces of the space.
  storage::SetSpaceError(1, false);
  NvramManager nvram2;

  // The space shouldn't exist in the space list.
  GetInfoRequest get_info_request;
  GetInfoResponse get_info_response;
  EXPECT_EQ(NV_RESULT_SUCCESS,
            nvram2.GetInfo(get_info_request, &get_info_response));

  EXPECT_EQ(0U, get_info_response.space_list.size());

  // The space info request should indicate the space doesn't exist.
  GetSpaceInfoRequest get_space_info_request;
  get_space_info_request.index = 1;
  GetSpaceInfoResponse get_space_info_response;
  EXPECT_EQ(
      NV_RESULT_SPACE_DOES_NOT_EXIST,
      nvram2.GetSpaceInfo(get_space_info_request, &get_space_info_response));
}

}  // namespace
}  // namespace nvram

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

#ifndef NVRAM_NVRAM_MESSAGES_H_
#define NVRAM_NVRAM_MESSAGES_H_

#include <hardware/nvram_defs.h>

#include <nvram/blob.h>
#include <nvram/compiler.h>
#include <nvram/struct.h>
#include <nvram/tagged_union.h>
#include <nvram/vector.h>

namespace nvram {

enum Command {
  COMMAND_GET_INFO = 1,
  COMMAND_CREATE_SPACE = 2,
  COMMAND_GET_SPACE_INFO = 3,
  COMMAND_DELETE_SPACE = 4,
  COMMAND_DISABLE_CREATE = 5,
  COMMAND_WRITE_SPACE = 6,
  COMMAND_READ_SPACE = 7,
  COMMAND_LOCK_SPACE_WRITE = 8,
  COMMAND_LOCK_SPACE_READ = 9,
};

// COMMAND_GET_INFO request/response.
struct GetInfoRequest {};

struct GetInfoResponse {
  uint64_t total_size = 0;
  uint64_t available_size = 0;
  uint32_t max_spaces = 0;
  Vector<uint32_t> space_list;
};

// COMMAND_CREATE_SPACE request/response.
struct CreateSpaceRequest {
  uint32_t index = 0;
  uint64_t size = 0;
  Vector<nvram_control_t> controls;
  Blob authorization_value;
};

struct CreateSpaceResponse {};

// COMMAND_GET_SPACE_INFO request/response.
struct GetSpaceInfoRequest {
  uint32_t index = 0;
};

struct GetSpaceInfoResponse {
  uint64_t size = 0;
  Vector<nvram_control_t> controls;
  bool read_locked = false;
  bool write_locked = false;
};

// COMMAND_DELETE_SPACE request/response.
struct DeleteSpaceRequest {
  uint32_t index = 0;
  Blob authorization_value;
};

struct DeleteSpaceResponse {};

// COMMAND_DISABLE_CREATE request/response.
struct DisableCreateRequest {};

struct DisableCreateResponse {};

// COMMAND_WRITE_SPACE request/response.
struct WriteSpaceRequest {
  uint32_t index = 0;
  Blob buffer;
  Blob authorization_value;
};

struct WriteSpaceResponse {};

// COMMAND_READ_SPACE request/response.
struct ReadSpaceRequest {
  uint32_t index = 0;
  Blob authorization_value;
};

struct ReadSpaceResponse {
  Blob buffer;
};

// COMMAND_LOCK_SPACE_WRITE request/response.
struct LockSpaceWriteRequest {
  uint32_t index = 0;
  Blob authorization_value;
};

struct LockSpaceWriteResponse {};

// COMMAND_LOCK_SPACE_READ request/response.
struct LockSpaceReadRequest {
  uint32_t index = 0;
  Blob authorization_value;
};

struct LockSpaceReadResponse {};

// Generic request message, carrying command-specific payload. The slot set in
// the payload determines the requested command.
using RequestUnion = TaggedUnion<
    Command,
    TaggedUnionMember<COMMAND_GET_INFO, GetInfoRequest>,
    TaggedUnionMember<COMMAND_CREATE_SPACE, CreateSpaceRequest>,
    TaggedUnionMember<COMMAND_GET_SPACE_INFO, GetSpaceInfoRequest>,
    TaggedUnionMember<COMMAND_DELETE_SPACE, DeleteSpaceRequest>,
    TaggedUnionMember<COMMAND_DISABLE_CREATE, DisableCreateRequest>,
    TaggedUnionMember<COMMAND_WRITE_SPACE, WriteSpaceRequest>,
    TaggedUnionMember<COMMAND_READ_SPACE, ReadSpaceRequest>,
    TaggedUnionMember<COMMAND_LOCK_SPACE_WRITE, LockSpaceWriteRequest>,
    TaggedUnionMember<COMMAND_LOCK_SPACE_READ, LockSpaceReadRequest>>;
struct Request {
  RequestUnion payload;
};

// Generic response message, carrying a result code and command-specific
// payload.
using ResponseUnion = TaggedUnion<
    Command,
    TaggedUnionMember<COMMAND_GET_INFO, GetInfoResponse>,
    TaggedUnionMember<COMMAND_CREATE_SPACE, CreateSpaceResponse>,
    TaggedUnionMember<COMMAND_GET_SPACE_INFO, GetSpaceInfoResponse>,
    TaggedUnionMember<COMMAND_DELETE_SPACE, DeleteSpaceResponse>,
    TaggedUnionMember<COMMAND_DISABLE_CREATE, DisableCreateResponse>,
    TaggedUnionMember<COMMAND_WRITE_SPACE, WriteSpaceResponse>,
    TaggedUnionMember<COMMAND_READ_SPACE, ReadSpaceResponse>,
    TaggedUnionMember<COMMAND_LOCK_SPACE_WRITE, LockSpaceWriteResponse>,
    TaggedUnionMember<COMMAND_LOCK_SPACE_READ, LockSpaceReadResponse>>;
struct Response {
  nvram_result_t result = NV_RESULT_SUCCESS;
  ResponseUnion payload;
};

// Encoding and decoding functions. Template instantiations are provided for the
// |Request| and |Response| wrapper types declared above.

// Encode |msg| to |blob|. Returns true if successful.
template <typename Message>
bool Encode(const Message& msg, Blob* blob);

// Decode |msg| from the |data| buffer, which contains |size| bytes. Returns
// true if successful.
template <typename Message>
bool Decode(const uint8_t* data, size_t size, Message* msg);

}  // namespace nvram

#endif  // NVRAM_NVRAM_MESSAGES_H_

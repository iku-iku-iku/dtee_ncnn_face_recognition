/*
 * Copyright (c) 2023 IPADS, Shanghai Jiao Tong University.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CLIENTSERVERTYPES_H_
#define CLIENTSERVERTYPES_H_

#include <cerrno>
#include <cstring>
#include <vector>
#include "fastrtps/TopicDataType.h"
#include "fastrtps/rtps/common/all_common.h"

inline int memcpy_s(void *dest, size_t destSize, const void *src, size_t count)
{
    if (dest == nullptr || src == nullptr)
        return EINVAL;
    if (destSize < count)
        return ERANGE;
    memmove(dest, src, count);
    return 0;
}

inline int memset_s(void *dest, size_t destSize, int ch, size_t count)
{
    if (dest == nullptr || ch < 0) {
        return EINVAL;
    }
    if (destSize < count) {
        return ERANGE;
    }
    memset(dest, ch, count);
    return 0;
}

namespace clientserver {
#define THREE 3
#define GUID_SIZE 16
#define GUID_PREFIX_SIZE 12
#define GUID_ENTITYID_SIZE 4

#define MAX_TRANSFER_VECTOR_SIZE 200000

#define NOTIFICATION_MESSAGE 0
#define NORMAL_MESSAGE 1
#define DUMMY_MESSAGE 2

#define ENCLAVE_UNRELATED (-1)
#define ENCLAVE_UNKNOWN 0

class Operation {
public:
    eprosima::fastrtps::rtps::GUID_t m_guid;
    int m_type; // 0: notification message, 1: normal message
    int m_vector_size;
    int m_enclave_id =
        ENCLAVE_UNRELATED; // -1: not related to enclave, 0: the enclave_id is
                           // unknown, positive number: the id of the enclave,
                           // which needs to be bound to the corresponding
                           // server.
    std::vector<char> m_vector;
    int fragment_idx = 0, total_fragment = 1;
    Operation() : m_vector_size(0)
    {
    }

    ~Operation()
    {
    }
};

class OperationDataType : public eprosima::fastrtps::TopicDataType {
public:
    OperationDataType()
    {
        setName("Operation");
        m_typeSize = GUID_SIZE + THREE * sizeof(int) +
                     MAX_TRANSFER_VECTOR_SIZE * sizeof(char);
        m_isGetKeyDefined = false;
    }

    ~OperationDataType()
    {
    }

    bool serialize(void *data,
                   eprosima::fastrtps::rtps::SerializedPayload_t *payload);
    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t *payload,
                     void *data);
    std::function<uint32_t()> getSerializedSizeProvider(void *data);
    bool getKey(void *, eprosima::fastrtps::rtps::InstanceHandle_t *, bool)
    {
        return false;
    }

    void *createData();
    void deleteData(void *data);
};

class Result {
public:
    eprosima::fastrtps::rtps::GUID_t m_guid;
    int m_type; // 0: notification message, 1: normal message
    int m_vector_size;
    int m_enclave_id = ENCLAVE_UNRELATED;
    int ack_idx = 0;
    std::vector<char> m_vector;
    Result() : m_vector_size(0)
    {
    }

    ~Result()
    {
    }
};

class ResultDataType : public eprosima::fastrtps::TopicDataType {
public:
    ResultDataType()
    {
        setName("Result");
        m_typeSize = GUID_SIZE + THREE * sizeof(int) +
                     MAX_TRANSFER_VECTOR_SIZE * sizeof(char);
        m_isGetKeyDefined = false;
    }

    ~ResultDataType()
    {
    }

    bool serialize(void *data,
                   eprosima::fastrtps::rtps::SerializedPayload_t *payload);
    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t *payload,
                     void *data);
    std::function<uint32_t()> getSerializedSizeProvider(void *data);
    bool getKey(void *, eprosima::fastrtps::rtps::InstanceHandle_t *, bool)
    {
        return false;
    }

    void *createData();
    void deleteData(void *data);
};
} // namespace clientserver
#endif /* CLIENTSERVERTYPES_H_ */

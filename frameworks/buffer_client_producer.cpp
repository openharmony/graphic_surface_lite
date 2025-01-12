/*
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd.
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

#include "buffer_client_producer.h"
#include "ipc_skeleton.h"

#include "buffer_common.h"
#include "buffer_manager.h"
#include "buffer_queue.h"
#include "surface_buffer_impl.h"

namespace OHOS {
const int32_t DEFAULT_IPC_SIZE = 200;
BufferClientProducer::BufferClientProducer(const SvcIdentity& sid) : sid_(sid)
{
}

BufferClientProducer::~BufferClientProducer()
{
}

SurfaceBufferImpl* BufferClientProducer::RequestBuffer(uint8_t wait)
{
    IpcIo requestIo;
    uint8_t requestIoData[DEFAULT_IPC_SIZE];
    IpcIoInit(&requestIo, requestIoData, DEFAULT_IPC_SIZE, 0);
    WriteUint8(&requestIo, wait);
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    int32_t ret = SendRequest(sid_, REQUEST_BUFFER, &requestIo, &reply, option, &ptr);
    if (ret != 0) {
        GRAPHIC_LOGW("RequestBuffer SendRequest failed");
        return nullptr;
    }
    ReadInt32(&reply, &ret);
    if (ret != 0) {
        GRAPHIC_LOGW("RequestBuffer generic failed code=%d", ret);
        FreeBuffer(reinterpret_cast<void *>(ptr));
        return nullptr;
    }

    SurfaceBufferImpl* buffer = new SurfaceBufferImpl();
    if (buffer == nullptr) {
        GRAPHIC_LOGW("SurfaceBufferImpl buffer is null");
        FreeBuffer(reinterpret_cast<void *>(ptr));
        return nullptr;
    }
    buffer->ReadFromIpcIo(reply);
    BufferManager* manager = BufferManager::GetInstance();
    if (manager == nullptr) {
        GRAPHIC_LOGW("BufferManager is null, usage(%d)", buffer->GetUsage());
        delete buffer;
        FreeBuffer(reinterpret_cast<void *>(ptr));
        return nullptr;
    }

    if (!manager->MapBuffer(*buffer)) {
        Cancel(buffer);
        FreeBuffer(reinterpret_cast<void *>(ptr));
        return nullptr;
    }
    FreeBuffer(reinterpret_cast<void *>(ptr));
    return buffer;
}

int32_t BufferClientProducer::FlushBuffer(SurfaceBufferImpl* buffer)
{
    RETURN_VAL_IF_FAIL(buffer, -1);
    BufferManager* manager = BufferManager::GetInstance();
    RETURN_VAL_IF_FAIL(manager, -1);
    int32_t ret = SURFACE_ERROR_OK;
    if (buffer->GetUsage() == BUFFER_CONSUMER_USAGE_HARDWARE_PRODUCER_CACHE) {
        ret = manager->FlushCache(*buffer);
        if (ret != SURFACE_ERROR_OK) {
            GRAPHIC_LOGW("Flush buffer failed, ret=%d", ret);
            return ret;
        }
    }
    IpcIo requestIo;
    uint8_t requestIoData[DEFAULT_IPC_SIZE];
    IpcIoInit(&requestIo, requestIoData, DEFAULT_IPC_SIZE, 0);
    buffer->WriteToIpcIo(requestIo);
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    ret = SendRequest(sid_, FLUSH_BUFFER, &requestIo, &reply, option, &ptr);
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("FlushBuffer failed");
        return ret;
    }
    ReadInt32(&reply, &ret);
    FreeBuffer(reinterpret_cast<void *>(ptr));
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("FlushBuffer failed code=%d", ret);
        return -1;
    }
    manager->UnmapBuffer(*buffer);
    delete buffer;
    return ret;
}

void BufferClientProducer::Cancel(SurfaceBufferImpl* buffer)
{
    if (buffer == nullptr) {
        return;
    }
    IpcIo requestIo;
    uint8_t requestIoData[DEFAULT_IPC_SIZE];
    IpcIoInit(&requestIo, requestIoData, DEFAULT_IPC_SIZE, 0);
    buffer->WriteToIpcIo(requestIo);
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    int32_t ret = SendRequest(sid_, CANCEL_BUFFER, &requestIo, &reply, option, &ptr);
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("Cancel buffer failed");
    } else {
        FreeBuffer(reinterpret_cast<void *>(ptr));
    }
    BufferManager* manager = BufferManager::GetInstance();
    RETURN_IF_FAIL(manager);
    manager->UnmapBuffer(*buffer);
    delete buffer;
}

void BufferClientProducer::SetQueueSize(uint8_t queueSize)
{
    IpcIo requestIo;
    uint8_t requestIoData[DEFAULT_IPC_SIZE];
    IpcIoInit(&requestIo, requestIoData, DEFAULT_IPC_SIZE, 0);
    WriteUint8(&requestIo, queueSize);
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    int32_t ret = SendRequest(sid_, SET_QUEUE_SIZE, &requestIo, &reply, option, &ptr);
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("Set Attr(%d:%u) failed", SET_QUEUE_SIZE, queueSize);
    }
    FreeBuffer(reinterpret_cast<void *>(ptr));
}

uint8_t BufferClientProducer::GetQueueSize()
{
    IpcIo requestIo;
    uint8_t requestIoData[DEFAULT_IPC_SIZE];
    IpcIoInit(&requestIo, requestIoData, DEFAULT_IPC_SIZE, 0);
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    int32_t ret = SendRequest(sid_, GET_QUEUE_SIZE, &requestIo, &reply, option, &ptr);
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("GetAttr SendRequest failed, errno=%d", ret);
        return 0;
    }
    ReadInt32(&reply, &ret);
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("GetAttr failed code=%d", GET_QUEUE_SIZE);
        FreeBuffer(reinterpret_cast<void *>(ptr));
        return 0;
    }
    uint8_t queueSize;
    ReadUint8(&reply, &queueSize);
    FreeBuffer(reinterpret_cast<void *>(ptr));
    return queueSize;
}

void BufferClientProducer::SetWidthAndHeight(uint32_t width, uint32_t height)
{
    IpcIo requestIo;
    uint8_t requestIoData[DEFAULT_IPC_SIZE];
    IpcIoInit(&requestIo, requestIoData, DEFAULT_IPC_SIZE, 0);
    WriteUint32(&requestIo, width);
    WriteUint32(&requestIo, height);
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    int32_t ret = SendRequest(sid_, SET_WIDTH_AND_HEIGHT, &requestIo, &reply, option, &ptr);
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("SetWidthAndHeight failed");
    } else {
        FreeBuffer(reinterpret_cast<void *>(ptr));
    }
    return;
}

uint32_t BufferClientProducer::GetWidth()
{
    return GetAttr(GET_WIDTH);
}

uint32_t BufferClientProducer::GetHeight()
{
    return GetAttr(GET_HEIGHT);
}

void BufferClientProducer::SetFormat(uint32_t format)
{
    SetAttr(SET_FORMAT, format);
}

uint32_t BufferClientProducer::GetFormat()
{
    return GetAttr(GET_FORMAT);
}

void BufferClientProducer::SetStrideAlignment(uint32_t strideAlignment)
{
    SetAttr(SET_STRIDE_ALIGNMENT, strideAlignment);
}

uint32_t BufferClientProducer::GetStrideAlignment()
{
    return GetAttr(GET_STRIDE_ALIGNMENT);
}

uint32_t BufferClientProducer::GetStride()
{
    return GetAttr(GET_STRIDE);
}

void BufferClientProducer::SetSize(uint32_t size)
{
    SetAttr(SET_SIZE, size);
}

uint32_t BufferClientProducer::GetSize()
{
    return GetAttr(GET_SIZE);
}

void BufferClientProducer::SetUsage(uint32_t usage)
{
    SetAttr(SET_USAGE, usage);
}

uint32_t BufferClientProducer::GetUsage()
{
    uint32_t usage = GetAttr(GET_USAGE);
    return usage;
}

void BufferClientProducer::SetUserData(const std::string& key, const std::string& value)
{
    IpcIo requestIo;
    uint8_t requestIoData[DEFAULT_IPC_SIZE];
    IpcIoInit(&requestIo, requestIoData, DEFAULT_IPC_SIZE, 0);
    WriteString(&requestIo, key.c_str());
    WriteString(&requestIo, value.c_str());
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    int32_t ret = SendRequest(sid_, SET_USER_DATA, &requestIo, &reply, option, &ptr);
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("Get user data(%s) failed", key.c_str());
    } else {
        FreeBuffer(reinterpret_cast<void *>(ptr));
    }
}

std::string BufferClientProducer::GetUserData(const std::string& key)
{
    std::string sValue = std::string();
    IpcIo requestIo;
    uint8_t requestIoData[DEFAULT_IPC_SIZE];
    IpcIoInit(&requestIo, requestIoData, DEFAULT_IPC_SIZE, 0);
    WriteString(&requestIo, key.c_str());
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    int32_t ret = SendRequest(sid_, GET_USER_DATA, &requestIo, &reply, option, &ptr);
    if (ret != SURFACE_ERROR_OK) {
        return sValue;
    } else {
        size_t len = 0;
        const char* value = reinterpret_cast<char *>(ReadString(&reply, &len));
        if (value == nullptr || len == 0) {
            GRAPHIC_LOGW("Get user data failed");
        } else {
            sValue = value;
        }
        FreeBuffer(reinterpret_cast<void *>(ptr));
        return sValue;
    }
}

void BufferClientProducer::SetAttr(uint32_t code, uint32_t value)
{
    IpcIo requestIo;
    uint8_t requestIoData[DEFAULT_IPC_SIZE];
    IpcIoInit(&requestIo, requestIoData, DEFAULT_IPC_SIZE, 0);
    WriteUint32(&requestIo, value);
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    int32_t ret = SendRequest(sid_, code, &requestIo, &reply, option, &ptr);
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("Set Attr(%u:%u) failed", code, value);
    } else {
        FreeBuffer(reinterpret_cast<void *>(ptr));
    }
}

uint32_t BufferClientProducer::GetAttr(uint32_t code)
{
    IpcIo requestIo;
    uint8_t requestIoData[DEFAULT_IPC_SIZE];
    IpcIoInit(&requestIo, requestIoData, DEFAULT_IPC_SIZE, 0);
    IpcIo reply;
    uintptr_t ptr;
    MessageOption option;
    MessageOptionInit(&option);
    int32_t ret = SendRequest(sid_, code, &requestIo, &reply, option, &ptr);
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("GetAttr SendRequest failed, errno=%d", ret);
        return 0;
    }
    ReadInt32(&reply, &ret);
    if (ret != SURFACE_ERROR_OK) {
        GRAPHIC_LOGW("GetAttr failed code=%d", code);
        FreeBuffer(reinterpret_cast<void *>(ptr));
        return 0;
    }
    uint32_t attr;
    ReadUint32(&reply, &attr);
    FreeBuffer(reinterpret_cast<void *>(ptr));
    return attr;
}
} // end namespace

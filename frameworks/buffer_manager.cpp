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

#include "buffer_manager.h"

#include "buffer_common.h"
#include "securec.h"
#include "surface_buffer.h"

namespace OHOS {
BufferManager* BufferManager::GetInstance()
{
    static BufferManager instance;
    return &instance;
}

bool BufferManager::Init()
{
    if (grallocFucs_ != nullptr) {
        GRAPHIC_LOGI("BufferManager has init succeed.");
        return true;
    }
    if (GrallocInitialize(&grallocFucs_) != DISPLAY_SUCCESS) {
        return false;
    }
    return true;
}

bool BufferManager::ConvertUsage(uint64_t& destUsage, uint32_t srcUsage) const
{
    switch (srcUsage) {
        case BUFFER_CONSUMER_USAGE_SORTWARE:
            destUsage = HBM_USE_MEM_SHARE;
            break;
        case BUFFER_CONSUMER_USAGE_HARDWARE:
        case BUFFER_CONSUMER_USAGE_HARDWARE_PRODUCER_CACHE:
            destUsage = HBM_USE_MEM_MMZ;
            break;
        case BUFFER_CONSUMER_USAGE_HARDWARE_CONSUMER_CACHE:
            destUsage = HBM_USE_MEM_MMZ_CACHE;
            break;
        default:
            GRAPHIC_LOGW("Conversion usage failed.");
            return false;
    }
    return true;
}

bool BufferManager::ConvertFormat(PixelFormat& destFormat, uint32_t srcFormat) const
{
    switch (srcFormat) {
        case IMAGE_PIXEL_FORMAT_NONE:
            destFormat = PIXEL_FMT_BUTT;
            break;
        case IMAGE_PIXEL_FORMAT_RGB565:
            destFormat = PIXEL_FMT_RGB_565;
            break;
        case IMAGE_PIXEL_FORMAT_ARGB1555:
            destFormat = PIXEL_FMT_RGBA_5551;
            break;
        case IMAGE_PIXEL_FORMAT_RGB888:
            destFormat = PIXEL_FMT_RGB_888;
            break;
        case IMAGE_PIXEL_FORMAT_ARGB8888:
            destFormat = PIXEL_FMT_RGBA_8888;
            break;
        case IMAGE_PIXEL_FORMAT_NV12:
            destFormat = PIXEL_FMT_YCBCR_420_SP;
            break;
        case IMAGE_PIXEL_FORMAT_NV21:
            destFormat = PIXEL_FMT_YCRCB_420_SP;
            break;
        case IMAGE_PIXEL_FORMAT_YUV420:
            destFormat = PIXEL_FMT_YCBCR_420_P;
            break;
        case IMAGE_PIXEL_FORMAT_YVU420:
            destFormat = PIXEL_FMT_YCRCB_420_P;
            break;
        default:
            GRAPHIC_LOGW("Conversion format failed.");
            return false;
    }
    return true;
}

BufferHandle* BufferManager::AllocateBufferHandle(SurfaceBufferImpl& buffer) const
{
    uint32_t total = buffer.GetReserveFds() + buffer.GetReserveInts() + sizeof(BufferHandle);
    BufferHandle* bufferHandle = static_cast<BufferHandle *>(malloc(total));
    if (bufferHandle != nullptr) {
        bufferHandle->fd = buffer.GetKey();
        bufferHandle->phyAddr = buffer.GetPhyAddr();
        bufferHandle->size = buffer.GetSize();
        if (!ConvertUsage(bufferHandle->usage, buffer.GetUsage())) {
            GRAPHIC_LOGW("Conversion usage failed.");
            free(bufferHandle);
            return nullptr;
        }
        bufferHandle->virAddr = buffer.GetVirAddr();
        bufferHandle->reserveFds = buffer.GetReserveFds();
        bufferHandle->reserveInts = buffer.GetReserveInts();
        for (uint32_t i = 0; i < (buffer.GetReserveFds() + buffer.GetReserveInts()); i++) {
            buffer.GetInt32(i, bufferHandle->reserve[i]);
        }
        return bufferHandle;
    }
    return nullptr;
}

SurfaceBufferImpl* BufferManager::AllocBuffer(AllocInfo info)
{
    RETURN_VAL_IF_FAIL((grallocFucs_ != nullptr), nullptr);
    BufferHandle* bufferHandle = nullptr;
    if ((grallocFucs_->AllocMem == nullptr) || (grallocFucs_->AllocMem(&info, &bufferHandle) != DISPLAY_SUCCESS)) {
        GRAPHIC_LOGE("Alloc graphic buffer failed");
        return nullptr;
    }
    SurfaceBufferImpl* buffer = new SurfaceBufferImpl();
    if (buffer != nullptr) {
        buffer->SetMaxSize(bufferHandle->size);
        buffer->SetVirAddr(bufferHandle->virAddr);
        buffer->SetKey(bufferHandle->fd);
        buffer->SetPhyAddr(bufferHandle->phyAddr);
        buffer->SetStride(bufferHandle->stride);
        buffer->SetReserveFds(bufferHandle->reserveFds);
        buffer->SetReserveInts(bufferHandle->reserveInts);
        for (uint32_t i = 0; i < (bufferHandle->reserveFds + bufferHandle->reserveInts); i++) {
            buffer->SetInt32(i, bufferHandle->reserve[i]);
        }
        BufferKey key = {bufferHandle->fd, bufferHandle->phyAddr};
        bufferHandleMap_.insert(std::make_pair(key, bufferHandle));
        GRAPHIC_LOGD("Alloc buffer succeed to shared memory segment.");
    } else {
        grallocFucs_->FreeMem(bufferHandle);
        GRAPHIC_LOGW("Alloc buffer failed to shared memory segment.");
    }
    return buffer;
}

SurfaceBufferImpl* BufferManager::AllocBuffer(uint32_t size, uint32_t usage)
{
    RETURN_VAL_IF_FAIL((grallocFucs_ != nullptr), nullptr);
    AllocInfo info = {0};
    info.expectedSize = size;
    info.format = PIXEL_FMT_RGB_565;
    if (!ConvertUsage(info.usage, usage)) {
        GRAPHIC_LOGW("Alloc graphic buffer failed --- conversion usage.");
        return nullptr;
    }
    info.usage |= HBM_USE_ASSIGN_SIZE;
    SurfaceBufferImpl* buffer = AllocBuffer(info);
    if (buffer == nullptr) {
        GRAPHIC_LOGE("Alloc graphic buffer failed");
        return nullptr;
    }
    buffer->SetUsage(usage);
    return buffer;
}

SurfaceBufferImpl* BufferManager::AllocBuffer(uint32_t width, uint32_t height, uint32_t format, uint32_t usage)
{
    RETURN_VAL_IF_FAIL((grallocFucs_ != nullptr), nullptr);
    AllocInfo info = {0};
    info.width = width;
    info.height = height;
    if (!ConvertUsage(info.usage, usage)) {
        GRAPHIC_LOGW("Alloc graphic buffer failed --- conversion usage.");
        return nullptr;
    }
    if (!ConvertFormat(info.format, format)) {
        GRAPHIC_LOGW("Alloc graphic buffer failed --- conversion format.");
        return nullptr;
    }
    SurfaceBufferImpl* buffer = AllocBuffer(info);
    if (buffer == nullptr) {
        GRAPHIC_LOGE("Alloc graphic buffer failed");
        return nullptr;
    }
    buffer->SetUsage(usage);
    return buffer;
}

void BufferManager::FreeBuffer(SurfaceBufferImpl** buffer)
{
    RETURN_IF_FAIL((grallocFucs_ != nullptr));
    if ((*buffer) == nullptr) {
        GRAPHIC_LOGW("Input param buffer is null.");
        return;
    }
    BufferKey key = {(*buffer)->GetKey(), (*buffer)->GetPhyAddr()};
    auto iter = bufferHandleMap_.find(key);
    if (iter == bufferHandleMap_.end()) {
        return;
    }
    BufferHandle* bufferHandle = iter->second;
    if (grallocFucs_->FreeMem != nullptr) {
        grallocFucs_->FreeMem(bufferHandle);
        bufferHandleMap_.erase(key);
        delete *buffer;
        *buffer = nullptr;
        GRAPHIC_LOGD("Free buffer succeed.");
    }
}

bool BufferManager::MapBuffer(SurfaceBufferImpl& buffer) const
{
    RETURN_VAL_IF_FAIL((grallocFucs_ != nullptr), false);
    void* virAddr = nullptr;
    BufferHandle* bufferHandle = AllocateBufferHandle(buffer);
    if (bufferHandle == nullptr) {
        return false;
    }
    if (buffer.GetUsage() == BUFFER_CONSUMER_USAGE_HARDWARE ||
        buffer.GetUsage() == BUFFER_CONSUMER_USAGE_HARDWARE_CONSUMER_CACHE ||
        buffer.GetUsage() == BUFFER_CONSUMER_USAGE_SORTWARE) {
        if (grallocFucs_->Mmap != nullptr) {
            virAddr = grallocFucs_->Mmap(bufferHandle);
        }
    } else if (buffer.GetUsage() == BUFFER_CONSUMER_USAGE_HARDWARE_PRODUCER_CACHE) {
        if (grallocFucs_->MmapCache != nullptr) {
            virAddr = grallocFucs_->MmapCache(bufferHandle);
        }
    } else {
        GRAPHIC_LOGE("No support usage.");
        free(bufferHandle);
        return false;
    }
    if (virAddr == nullptr) {
        GRAPHIC_LOGE("Map Buffer error.");
        free(bufferHandle);
        return false;
    }
    buffer.SetVirAddr(virAddr);
    GRAPHIC_LOGD("Map Buffer succeed.");
    free(bufferHandle);
    return true;
}

void BufferManager::UnmapBuffer(SurfaceBufferImpl& buffer) const
{
    RETURN_IF_FAIL((grallocFucs_ != nullptr));
    BufferHandle* bufferHandle = AllocateBufferHandle(buffer);
    if (bufferHandle == nullptr) {
        return;
    }
    if ((grallocFucs_->Unmap == nullptr) || (grallocFucs_->Unmap(bufferHandle) != DISPLAY_SUCCESS)) {
        GRAPHIC_LOGE("Umap buffer failed.");
    }
    free(bufferHandle);
}

int32_t BufferManager::FlushCache(SurfaceBufferImpl& buffer) const
{
    RETURN_VAL_IF_FAIL((grallocFucs_ != nullptr), SURFACE_ERROR_NOT_READY);
    BufferHandle* bufferHandle = AllocateBufferHandle(buffer);
    if (bufferHandle == nullptr) {
        return -1;
    }
    if (buffer.GetUsage() == BUFFER_CONSUMER_USAGE_HARDWARE_CONSUMER_CACHE) {
        if ((grallocFucs_->FlushCache == nullptr) || (grallocFucs_->FlushCache(bufferHandle) != DISPLAY_SUCCESS)) {
            GRAPHIC_LOGE("Flush cache buffer failed.");
        }
    } else if (buffer.GetUsage() == BUFFER_CONSUMER_USAGE_HARDWARE_PRODUCER_CACHE) {
        if ((grallocFucs_->FlushMCache == nullptr) || (grallocFucs_->FlushMCache(bufferHandle) != DISPLAY_SUCCESS)) {
            GRAPHIC_LOGE("Flush M cache buffer failed.");
        }
    }
    free(bufferHandle);
    return SURFACE_ERROR_OK;
}
} // namespace OHOS

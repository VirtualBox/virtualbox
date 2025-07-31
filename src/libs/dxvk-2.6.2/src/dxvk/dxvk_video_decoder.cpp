/* $Id: dxvk_video_decoder.cpp 110305 2025-07-18 12:42:15Z vitali.pelenjow@oracle.com $ */
/** @file
 * VBoxDxVk - Video decoder.
 */

/*
 * Copyright (C) 2024-2025 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


#include "dxvk_video_decoder.h"
#include "dxvk_device.h"

namespace dxvk {
  /* VkVideoProfileListInfoKHR structure for VkImageCreateInfo::pNext.
   *
   * DxvkImage keeps a copy of DxvkImageCreateInfo parameter in order to
   * be able to re-create Vulkan image resource. Therefore the video decoder
   * specific additional structures for VkImageCreateInfo::pNext chain are
   * passed now as a memory buffer with a reference counter.
   */
  struct DxvkVideoDecodeProfileListInfo {
    VkVideoProfileListInfoKHR             profileListInfo;
    /* Vulkan profile info. */
    VkVideoProfileInfoKHR                 profileInfo;
    /* Vulkan profile specific information. profileInfo.pNext */
    union {
      VkVideoDecodeH264ProfileInfoKHR     h264ProfileInfo;
      VkVideoDecodeH265ProfileInfoKHR     h265ProfileInfo;
      VkVideoDecodeAV1ProfileInfoKHR      av1ProfileInfo;
    };

    static void init(DxvkVideoDecodeProfileListInfo *pThis, VkVideoProfileInfoKHR const &p) {
        pThis->profileInfo = p;

        if (pThis->profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
          pThis->h264ProfileInfo   = *(VkVideoDecodeH264ProfileInfoKHR *)p.pNext;
          pThis->profileInfo.pNext = &pThis->h264ProfileInfo;
        }
        else if (pThis->profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
          pThis->h265ProfileInfo   = *(VkVideoDecodeH265ProfileInfoKHR *)p.pNext;
          pThis->profileInfo.pNext = &pThis->h265ProfileInfo;
        }
        else if (pThis->profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
          pThis->av1ProfileInfo    = *(VkVideoDecodeAV1ProfileInfoKHR *)p.pNext;
          pThis->profileInfo.pNext = &pThis->av1ProfileInfo;
        }
        else {
          throw DxvkError(str::format("DxvkVideoDecoder: videoCodecOperation ", pThis->profileInfo.videoCodecOperation,
            " is not supported"));
        }

        pThis->profileListInfo = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR };
        pThis->profileListInfo.profileCount = 1;
        pThis->profileListInfo.pProfiles    = &pThis->profileInfo;
    };
  };


  DxvkVideoSessionHandle::DxvkVideoSessionHandle(
    const Rc<DxvkDevice>& device)
    : m_device(device) {
  }


  DxvkVideoSessionHandle::~DxvkVideoSessionHandle() {
    if (m_videoSession != VK_NULL_HANDLE) {
      m_device->vkd()->vkDestroyVideoSessionKHR(m_device->handle(), m_videoSession, nullptr);
    }
  }


  void DxvkVideoSessionHandle::create(
    const VkVideoSessionCreateInfoKHR& sessionCreateInfo) {
    VkResult vr = m_device->vkd()->vkCreateVideoSessionKHR(m_device->handle(),
      &sessionCreateInfo, nullptr, &m_videoSession);
    if (vr)
      throw DxvkError(str::format("DxvkVideoSessionHandle: vkCreateVideoSessionKHR failed: ", vr));
  }


  DxvkVideoSessionParametersHandle::DxvkVideoSessionParametersHandle(
    const Rc<DxvkDevice>& device)
    : m_device(device) {
  }


  DxvkVideoSessionParametersHandle::~DxvkVideoSessionParametersHandle() {
    if (m_videoSessionParameters != VK_NULL_HANDLE) {
      m_device->vkd()->vkDestroyVideoSessionParametersKHR(m_device->handle(), m_videoSessionParameters, nullptr);
    }
  }


  void DxvkVideoSessionParametersHandle::create(
    const Rc<DxvkVideoSessionHandle>& videoSession,
    const void *pDecoderSessionParametersCreateInfo) {
    m_videoSession = videoSession;

    VkVideoSessionParametersCreateInfoKHR sessionParametersCreateInfo =
      { VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR, pDecoderSessionParametersCreateInfo };
    sessionParametersCreateInfo.flags                          = 0;
    sessionParametersCreateInfo.videoSessionParametersTemplate = nullptr;
    sessionParametersCreateInfo.videoSession                   = m_videoSession->handle();

    VkResult vr = m_device->vkd()->vkCreateVideoSessionParametersKHR(m_device->handle(),
      &sessionParametersCreateInfo, nullptr, &m_videoSessionParameters);
    if (vr)
      throw DxvkError(str::format("DxvkVideoSessionParametersHandle: vkCreateVideoSessionParametersKHR failed: ", vr));
  }


  DxvkVideoBitstreamBuffer::DxvkVideoBitstreamBuffer(
    const Rc<DxvkDevice>& device,
    DxvkMemoryAllocator& memAlloc)
  : m_device(device),
    m_memAlloc(memAlloc) {
  }


  DxvkVideoBitstreamBuffer::~DxvkVideoBitstreamBuffer() {
    if (m_buffer != VK_NULL_HANDLE) {
      m_device->vkd()->vkDestroyBuffer(m_device->vkd()->device(), m_buffer, nullptr);
    }
  }


  void DxvkVideoBitstreamBuffer::create(
      const VkVideoProfileListInfoKHR &profileListInfo,
      VkDeviceSize size) {
    const VkMemoryPropertyFlags memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                         | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    /* Use a dedicated memory allocation for the buffer.
     * This is a workaround for Intel where the buffer memory requires 4096 bytes alignment,
     * otherwise H.264 video decoding produces garbled output.
     * The expectation is that the Intel decoder will always work fine with a dedicated allocation.
     */
    VkBufferCreateInfo bufferCreateInfo =
      { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, &profileListInfo };
    bufferCreateInfo.size        = size;
    bufferCreateInfo.usage       = VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (m_device->vkd()->vkCreateBuffer(m_device->vkd()->device(),
      &bufferCreateInfo, nullptr, &m_buffer))
      throw DxvkError(str::format(
        "DxvkBuffer: Failed to create video bitstream buffer:"
        "\n  flags: ", std::hex, bufferCreateInfo.flags,
        "\n  size:  ", std::dec, bufferCreateInfo.size,
        "\n  usage: ", std::hex, bufferCreateInfo.usage));

    // Query memory requirements
    VkMemoryDedicatedRequirements dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
    VkMemoryRequirements2 memoryRequirements = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicated };

    VkBufferMemoryRequirementsInfo2 memoryRequirementInfo = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
    memoryRequirementInfo.buffer = m_buffer;

    m_device->vkd()->vkGetBufferMemoryRequirements2(m_device->vkd()->device(),
      &memoryRequirementInfo, &memoryRequirements);

    if (m_device->adapter()->matchesDriver(VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, Version(), Version())
     || m_device->adapter()->matchesDriver(VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA, Version(), Version())) {
      memoryRequirements.memoryRequirements.alignment =
        align(memoryRequirements.memoryRequirements.alignment, 4096);
      memoryRequirements.memoryRequirements.size =
        align(memoryRequirements.memoryRequirements.size, 4096);
    }

    DxvkAllocationInfo allocationInfo;
    allocationInfo.resourceCookie = (uint64_t)0u;
    allocationInfo.properties = memFlags;
    allocationInfo.mode = 0u;

    VkMemoryDedicatedAllocateInfo dedicatedInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicatedInfo.buffer = m_buffer;

    m_allocation = m_memAlloc.allocateDedicatedMemory(memoryRequirements.memoryRequirements,
      allocationInfo, &dedicatedInfo);
    if (!m_allocation)
      throw DxvkError("DxvkBuffer: Failed to allocate device memory for video bitstream buffer");

    DxvkResourceMemoryInfo memoryInfo = m_allocation->getMemoryInfo();
    if (m_device->vkd()->vkBindBufferMemory(m_device->vkd()->device(),
          m_buffer, memoryInfo.memory, memoryInfo.offset) != VK_SUCCESS)
      throw DxvkError("DxvkBuffer: Failed to bind device memory for video bitstream buffer");

    /* Fetch data for quicker access. */
    m_mapPtr = (uint8_t *)m_allocation->mapPtr();
    m_length = bufferCreateInfo.size;
  }


  DxvkVideoDecoder::DxvkVideoDecoder(const Rc<DxvkDevice>& device, DxvkMemoryAllocator& memAlloc,
      const DxvkVideoDecodeProfileInfo& profile, uint32_t sampleWidth, uint32_t sampleHeight, VkFormat outputFormat,
            uint32_t bitstreamBufferSize)
  : m_device(device),
    m_memAlloc(memAlloc),
    m_profile(profile),
    m_sampleWidth(sampleWidth),
    m_sampleHeight(sampleHeight),
    m_outputFormat(outputFormat),
    m_videoSession(new DxvkVideoSessionHandle(device)),
    m_videoSessionParameters(new DxvkVideoSessionParametersHandle(device)),
    m_bitstreamBuffer(new DxvkVideoBitstreamBuffer(device, memAlloc)) {
    VkResult vr;

    DxvkFenceCreateInfo fenceInfo;
    fenceInfo.initialValue = m_queueOwnershipTransferValue;
    m_queueOwnershipTransferFence = m_device->createFence(fenceInfo);

    /*
     * Update internal pointers of m_profileInfo.
     */
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      m_profile.profileInfo.pNext        = &m_profile.h264ProfileInfo;
      m_profile.decodeCapabilities.pNext = &m_profile.decodeH264Capabilities;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      m_profile.profileInfo.pNext        = &m_profile.h265ProfileInfo;
      m_profile.decodeCapabilities.pNext = &m_profile.decodeH265Capabilities;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      m_profile.profileInfo.pNext        = &m_profile.av1ProfileInfo;
      m_profile.decodeCapabilities.pNext = &m_profile.decodeAV1Capabilities;
    }
    else {
      throw DxvkError(str::format("DxvkVideoDecoder: videoCodecOperation ", m_profile.profileInfo.videoCodecOperation,
        " is not supported"));
    }
    m_profile.videoCapabilities.pNext    = &m_profile.decodeCapabilities;

    /* Size of DPB and decode destination images. */
    m_DPB.decodedPictureExtent = { m_sampleWidth, m_sampleHeight, 1 };

    /* Align the decoder picture size to the macroblock/codingblock/superblock granularity. */
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      /* To the macroblock */
      m_DPB.decodedPictureExtent.width = align(m_DPB.decodedPictureExtent.width, 16);
      m_DPB.decodedPictureExtent.height = align(m_DPB.decodedPictureExtent.height, 16);
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      /* To the max coding block size */
      m_DPB.decodedPictureExtent.width = align(m_DPB.decodedPictureExtent.width, 64);
      m_DPB.decodedPictureExtent.height = align(m_DPB.decodedPictureExtent.height, 64);
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      /* To the largest superblock granularity */
      m_DPB.decodedPictureExtent.width = align(m_DPB.decodedPictureExtent.width, 128);
      m_DPB.decodedPictureExtent.height = align(m_DPB.decodedPictureExtent.height, 128);
    }

    /*
     * Assess capabilities.
     */
    /* Check that video resolution is supported. */
    if (m_DPB.decodedPictureExtent.width > m_profile.videoCapabilities.maxCodedExtent.width
     || m_DPB.decodedPictureExtent.height > m_profile.videoCapabilities.maxCodedExtent.height)
      throw DxvkError(str::format("DxvkVideoDecoder: requested resolution exceeds maximum: ",
        m_DPB.decodedPictureExtent.width, "x", m_DPB.decodedPictureExtent.height, " (",
        m_sampleWidth, "x", m_sampleHeight, ") > ",
        m_profile.videoCapabilities.maxCodedExtent.width, "x", m_profile.videoCapabilities.maxCodedExtent.height));

    if ((m_profile.videoCapabilities.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR) == 0) {
      /// @todo Allocate one image resource as array for DPB.
      throw DxvkError(str::format("DxvkVideoDecoder: VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR"
        " is not supported"));
    }

    /* Figure out if the decoder uses a DPB slot or a separate image for the output picture.*/
    if (m_profile.decodeCapabilities.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR)
      m_caps.distinctOutputImage = true; /* DPB and Output can be different images. */
    else
      m_caps.distinctOutputImage = false; /* DPB and Output can not be different images. */

    /* Prefer DPB_AND_OUTPUT_COINCIDE, because it does not require an additional output image. */
    if (m_profile.decodeCapabilities.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR)
      m_caps.distinctOutputImage = false; /* DPB and Output can coincide. */
    else
      m_caps.distinctOutputImage = true; /* DPB and Output can not coincide. */

    /*
     * Create resources.
     */
    /* Allocate and initialize a memory buffer with a reference counter. The buffer holds
     * VkVideoProfileListInfoKHR structure which is required for VkImageCreateInfo::pNext chain.
     * DxvkImage keeps a copy of DxvkImageCreateInfo parameter in order to be able to re-create
     * Vulkan image resource.
     */
    util::RcMemoryBuffer *pNext = new util::RcMemoryBuffer(sizeof(DxvkVideoDecodeProfileListInfo));
    DxvkVideoDecodeProfileListInfo *profileListInfo = (DxvkVideoDecodeProfileListInfo *)pNext->data();
    DxvkVideoDecodeProfileListInfo::init(profileListInfo, m_profile.profileInfo);

    m_bitstreamBuffer->create(profileListInfo->profileListInfo, bitstreamBufferSize);

    /* Decoded Picture Buffer (DPB), i.e. array of decoded frames.
     * AMD mesa/vulkan driver asserts if the number of slots is greater than the spec requirement.
     */
    uint32_t cMaxDPBSlots = m_profile.videoCapabilities.maxDpbSlots;
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      cMaxDPBSlots = std::min(cMaxDPBSlots, (uint32_t)16);
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      cMaxDPBSlots = std::min(cMaxDPBSlots, (uint32_t)STD_VIDEO_H265_MAX_DPB_SIZE);
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      cMaxDPBSlots = std::min(cMaxDPBSlots, (uint32_t)(STD_VIDEO_AV1_TOTAL_REFS_PER_FRAME + 1));
    }
    m_DPB.slots.resize(cMaxDPBSlots);

    for (auto &slot: m_DPB.slots) {
      DxvkImageCreateInfo imgInfo = {};
      /* Do not use VK_BUFFER_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR for DPB images, because
       * "images with only DPB usage remain tied to the video profiles the image was created with,
       * as the data layout of such DPB-only images may be implementation- and codec-dependent."
       * When m_caps.distinctOutputImage is true the DPB images have the "only DPB usage".
       */
      imgInfo.next        = pNext;
      imgInfo.type        = VK_IMAGE_TYPE_2D;
      imgInfo.format      = m_outputFormat;
      imgInfo.flags       = 0;
      imgInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      imgInfo.extent      = m_DPB.decodedPictureExtent;
      imgInfo.numLayers   = 1;
      imgInfo.mipLevels   = 1;
      if (m_caps.distinctOutputImage) {
        imgInfo.usage       = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
        imgInfo.stages      = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
      }
      else {
        imgInfo.usage       = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR
                            | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imgInfo.stages      = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR
                            | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
      }

      /* Hack: Access bits are not used as image creation parameters,
       * however they are used for checking if the memory must be GPU writable.
       * Provide flags that make it GPU writable.
       */
      imgInfo.access      = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                          | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      imgInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
      imgInfo.layout      = VK_IMAGE_LAYOUT_UNDEFINED;

      slot.image = m_device->createImage(imgInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

      DxvkImageViewKey viewInfo = {};
      viewInfo.viewType   = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format     = m_outputFormat;
      viewInfo.usage      = imgInfo.usage;
      viewInfo.aspects    = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.mipIndex   = 0;
      viewInfo.mipCount   = 1;
      viewInfo.layerIndex = 0;
      viewInfo.layerCount = 1;

      slot.imageView = slot.image->createView(viewInfo);

      slot.deactivate();
    }

    if (m_caps.distinctOutputImage
     || m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      /* Create an additional output image. Also for a possible usage of AV1 film grain feature. */
      DxvkImageCreateInfo imgInfo = {};
      imgInfo.next        = pNext;
      imgInfo.type        = VK_IMAGE_TYPE_2D;
      imgInfo.format      = m_outputFormat;
      imgInfo.flags       = 0;
      imgInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      imgInfo.extent      = m_DPB.decodedPictureExtent;
      imgInfo.numLayers   = 1;
      imgInfo.mipLevels   = 1;
      imgInfo.usage       = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                          | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
      imgInfo.stages      = VK_PIPELINE_STAGE_2_TRANSFER_BIT
                          | VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
      /* Hack: Access bits are not used as image creation parameters,
       * however they are used for checking if the memory must be GPU writable.
       * Provide flags that make it GPU writable.
       */
      imgInfo.access      = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                          | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      imgInfo.tiling      = VK_IMAGE_TILING_OPTIMAL;
      imgInfo.layout      = VK_IMAGE_LAYOUT_UNDEFINED;

      m_imageDecodeDst = m_device->createImage(imgInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

      DxvkImageViewKey viewInfo = {};
      viewInfo.viewType   = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format     = m_outputFormat;
      viewInfo.usage      = imgInfo.usage;
      viewInfo.aspects    = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.mipIndex   = 0;
      viewInfo.mipCount   = 1;
      viewInfo.layerIndex = 0;
      viewInfo.layerCount = 1;

      m_imageViewDecodeDst = m_imageDecodeDst->createView(viewInfo);
    }

    /*
     * Create video session object.
     */
    VkVideoSessionCreateInfoKHR sessionCreateInfo =
      { VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR };
    sessionCreateInfo.queueFamilyIndex           = m_device->queues().videoDecode.queueFamily;
    sessionCreateInfo.flags                      = 0;
    sessionCreateInfo.pVideoProfile              = &m_profile.profileInfo;
    sessionCreateInfo.pictureFormat              = m_outputFormat;
    sessionCreateInfo.maxCodedExtent             = m_profile.videoCapabilities.maxCodedExtent;
    sessionCreateInfo.referencePictureFormat     = m_outputFormat;
    sessionCreateInfo.maxDpbSlots                = m_DPB.slots.size();
    sessionCreateInfo.maxActiveReferencePictures = m_DPB.slots.size() - 1;
    sessionCreateInfo.pStdHeaderVersion          = &m_profile.videoCapabilities.stdHeaderVersion;

    m_videoSession->create(sessionCreateInfo);

    /* Vulkan need an explicit memory allocation for the video session. */
    uint32_t memoryRequirementsCount = 0;
    vr = m_device->vkd()->vkGetVideoSessionMemoryRequirementsKHR(m_device->handle(),
      m_videoSession->handle(), &memoryRequirementsCount, nullptr);
    if (vr)
      throw DxvkError(str::format("DxvkVideoDecoder: vkGetVideoSessionMemoryRequirementsKHR failed: ", vr));

    std::vector<VkVideoSessionMemoryRequirementsKHR> memoryRequirements(memoryRequirementsCount,
      { VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR });
    vr = m_device->vkd()->vkGetVideoSessionMemoryRequirementsKHR(m_device->handle(),
      m_videoSession->handle(), &memoryRequirementsCount, memoryRequirements.data());
    if (vr)
      throw DxvkError(str::format("DxvkVideoDecoder: vkGetVideoSessionMemoryRequirementsKHR failed: ", vr));

    if (memoryRequirementsCount > 0) { /* Intel graphics driver returns 0. */
      m_videoSessionMemory.resize(memoryRequirementsCount);

      std::vector<VkBindVideoSessionMemoryInfoKHR> bindMemoryInfos(memoryRequirementsCount);

      VkPhysicalDeviceMemoryProperties memoryProperties = m_device->adapter()->memoryProperties();

      for (uint32_t i = 0; i < memoryRequirementsCount; ++i) {
        VkVideoSessionMemoryRequirementsKHR requirement = memoryRequirements[i];

        /* AMD radv driver from Mesa returns alignment == 0.
         * DxvkMemoryAllocator::allocateMemory aligns the size:
         *   VkDeviceSize size = align(requirements.size, requirements.alignment);
         * The size becomes zero.
         */
        if (requirement.memoryRequirements.alignment == 0)
          requirement.memoryRequirements.alignment = 4096; /* Page align to be on safe side. */

        /* Find memory properties of the required memory type. */
        VkMemoryPropertyFlags memoryPropertyFlags = 0;
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
          if (requirement.memoryRequirements.memoryTypeBits & (UINT32_C(1) << i)) {
            memoryPropertyFlags = memoryProperties.memoryTypes[i].propertyFlags;
            break;
          }
        }

        if (memoryPropertyFlags == 0)
          throw DxvkError(str::format("DxvkVideoDecoder: no properties for memoryTypeBits: ",
            std::hex, requirement.memoryRequirements.memoryTypeBits, std::dec));

        DxvkAllocationInfo allocationInfo;
        allocationInfo.resourceCookie = (uint64_t)0u;
        allocationInfo.properties = memoryPropertyFlags;
        allocationInfo.mode = 0u;

        if (m_device->adapter()->matchesDriver(VK_DRIVER_ID_AMD_OPEN_SOURCE_KHR, Version(), Version())
         || m_device->adapter()->matchesDriver(VK_DRIVER_ID_MESA_RADV_KHR, Version(), Version())) {
          /* RADV allocates two memory buffers for a video session: 128KB in device local
           * and 36MB in host visible memory.
           * The latter allocation causes allocation failures of D3D11 dynamic resources later.
           * Allocating a dedicated memory for it works around this issue.
           */
          VkMemoryDedicatedAllocateInfo dedicatedInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };

          m_videoSessionMemory[i] = m_memAlloc.allocateDedicatedMemory(requirement.memoryRequirements,
            allocationInfo, &dedicatedInfo);
        }
        else
          m_videoSessionMemory[i] = m_memAlloc.allocateMemory(requirement.memoryRequirements, allocationInfo);

        if (!m_videoSessionMemory[i])
          throw DxvkError("DxvkVideoDecoder: Failed to allocate device memory for video session");

        DxvkResourceMemoryInfo memoryInfo = m_videoSessionMemory[i]->getMemoryInfo();

        VkBindVideoSessionMemoryInfoKHR &bindMemoryInfo = bindMemoryInfos[i];
        bindMemoryInfo.sType           = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
        bindMemoryInfo.memory          = memoryInfo.memory;
        bindMemoryInfo.memoryOffset    = memoryInfo.offset;
        /* Use original size instead of m_videoSessionMemory[i].length() because the latter
         * can be greater and then Vulkan validation complains.
         */
        bindMemoryInfo.memorySize      = requirement.memoryRequirements.size;
        bindMemoryInfo.memoryBindIndex = requirement.memoryBindIndex;
      }

      vr = m_device->vkd()->vkBindVideoSessionMemoryKHR(m_device->handle(),
        m_videoSession->handle(), memoryRequirementsCount, bindMemoryInfos.data());
      if (vr)
        throw DxvkError(str::format("DxvkVideoDecoder: vkBindVideoSessionMemoryKHR failed: ", vr));
    }

    /*
     * Create video session parameters object.
     */
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      VkVideoDecodeH264SessionParametersCreateInfoKHR h264SessionParametersCreateInfo =
        { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR };
      h264SessionParametersCreateInfo.maxStdSPSCount     = m_parameterSetCache.h264.sps.size();
      h264SessionParametersCreateInfo.maxStdPPSCount     = m_parameterSetCache.h264.pps.size();
      h264SessionParametersCreateInfo.pParametersAddInfo = nullptr; /* Added in 'Decode' as necessary. */

      m_videoSessionParameters->create(m_videoSession, &h264SessionParametersCreateInfo);
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      VkVideoDecodeH265SessionParametersCreateInfoKHR h265SessionParametersCreateInfo =
        { VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR };
      h265SessionParametersCreateInfo.maxStdVPSCount     = m_parameterSetCache.h265.vps.size();
      h265SessionParametersCreateInfo.maxStdSPSCount     = m_parameterSetCache.h265.sps.size();
      h265SessionParametersCreateInfo.maxStdPPSCount     = m_parameterSetCache.h265.pps.size();
      h265SessionParametersCreateInfo.pParametersAddInfo = nullptr; /* Added in 'Decode' as necessary. */

      m_videoSessionParameters->create(m_videoSession, &h265SessionParametersCreateInfo);
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      /* Session parameter will be (re-)created in Decode because "video session parameters objects cannot be
       * updated using the vkUpdateVideoSessionParametersKHR command. When a new AV1 sequence header is decoded
       * from the input video bitstream the application needs to create a new video session parameters object
       * to store it.".
       */
    }
    else {
      throw DxvkError(str::format("DxvkVideoDecoder: videoCodecOperation ", m_profile.profileInfo.videoCodecOperation,
        " is not supported"));
    }
  }


  DxvkVideoDecoder::~DxvkVideoDecoder() {
  }


  void DxvkVideoDecoder::TransferImageQueueOwnership(
    DxvkContext*          ctx,
    const Rc<DxvkImage>&  image,
    uint32_t              baseArrayLayer,
    DxvkCmdBuffer         srcCmdBuffer,
    uint32_t              srcQueueFamilyIndex,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2        srcAccessMask,
    VkImageLayout         oldLayout,
    DxvkCmdBuffer         dstCmdBuffer,
    uint32_t              dstQueueFamilyIndex,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2        dstAccessMask,
    VkImageLayout         newLayout) {
    std::array<VkImageMemoryBarrier2, 2> barriers{{
      { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 }}};

    /* Source queue release. dstAccessMask is ignored.
     * dstStageMask is ignored, because 'dependencyFlags does not include
     * VK_DEPENDENCY_QUEUE_FAMILY_OWNERSHIP_TRANSFER_USE_ALL_STAGES_BIT_KHR'.
     */
    barriers[0].srcStageMask        = srcStageMask;
    barriers[0].srcAccessMask       = srcAccessMask;
    barriers[0].dstStageMask        = 0;
    barriers[0].dstAccessMask       = 0;
    barriers[0].oldLayout           = oldLayout;
    barriers[0].newLayout           = newLayout;
    barriers[0].srcQueueFamilyIndex = srcQueueFamilyIndex;
    barriers[0].dstQueueFamilyIndex = dstQueueFamilyIndex;
    barriers[0].image               = image->handle();
    barriers[0].subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel   = 0;
    barriers[0].subresourceRange.levelCount     = 1;
    barriers[0].subresourceRange.baseArrayLayer = baseArrayLayer;
    barriers[0].subresourceRange.layerCount     = 1;

    /* Destination queue acquire. srcAccessMask is ignored.
     * srcStageMask is ignored, because 'dependencyFlags does not include
     * VK_DEPENDENCY_QUEUE_FAMILY_OWNERSHIP_TRANSFER_USE_ALL_STAGES_BIT_KHR'.
     */
    barriers[1] = barriers[0];
    barriers[1].srcStageMask        = 0;
    barriers[1].srcAccessMask       = 0;
    barriers[1].dstStageMask        = dstStageMask;
    barriers[1].dstAccessMask       = dstAccessMask;

    ctx->transferImageQueueOwnership(
      srcCmdBuffer,
      &barriers[0],
      dstCmdBuffer,
      &barriers[1],
      m_queueOwnershipTransferFence,
      ++m_queueOwnershipTransferValue);

    ctx->trackResource(DxvkAccess::Write, image);
  }


  void DxvkVideoDecoder::BeginFrame(
    DxvkContext* ctx,
    const Rc<DxvkImageView>& imageView) {

    m_outputImageView = imageView;

    if (m_profile.videoQueueHasTransfer) {
      /* Acquire ownership of the image to the video queue. */
      this->TransferImageQueueOwnership(ctx, m_outputImageView->image(), m_outputImageView->info().layerIndex,
        DxvkCmdBuffer::InitBuffer,
        m_device->queues().graphics.queueFamily,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        0,
        VK_IMAGE_LAYOUT_UNDEFINED, /* 'The contents ... may be discarded.' */
        DxvkCmdBuffer::VDecBuffer,
        m_device->queues().videoDecode.queueFamily,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL); /* A target of CopyImage */
    }
  }


  void DxvkVideoDecoder::EndFrame(
    DxvkContext* ctx) {

    if (m_profile.videoQueueHasTransfer) {
      /* Return ownership of the image back to the graphics queue. */
      this->TransferImageQueueOwnership(ctx, m_outputImageView->image(), m_outputImageView->info().layerIndex,
        DxvkCmdBuffer::VDecBuffer,
        m_device->queues().videoDecode.queueFamily,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        DxvkCmdBuffer::InitBuffer,
        m_device->queues().graphics.queueFamily,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, /// ALL_COMMANDS?
        VK_ACCESS_2_MEMORY_READ_BIT,
        m_outputImageView->image()->info().layout); /* VK_IMAGE_LAYOUT_GENERAL. */
    }

    m_outputImageView = nullptr;
  }


#define DXVK_VD_CMP_FIELD(_s, _f) \
  if ((_s##1)._f != (_s##2)._f) { \
    Logger::debug(str::format(#_s, ".", #_f ,": ", (int32_t)(_s##1)._f, " != ", (int32_t)(_s##2)._f)); \
    return false; \
  }

  static bool IsH265VPSEqual(
    const StdVideoH265VideoParameterSet &vps1,
    const StdVideoH265VideoParameterSet &vps2) {
    DXVK_VD_CMP_FIELD(vps, flags.vps_temporal_id_nesting_flag);
    DXVK_VD_CMP_FIELD(vps, flags.vps_sub_layer_ordering_info_present_flag);
    DXVK_VD_CMP_FIELD(vps, flags.vps_timing_info_present_flag);
    DXVK_VD_CMP_FIELD(vps, flags.vps_poc_proportional_to_timing_flag);
    DXVK_VD_CMP_FIELD(vps, vps_video_parameter_set_id);
    DXVK_VD_CMP_FIELD(vps, vps_max_sub_layers_minus1);
    DXVK_VD_CMP_FIELD(vps, vps_num_units_in_tick);
    DXVK_VD_CMP_FIELD(vps, vps_time_scale);
    DXVK_VD_CMP_FIELD(vps, vps_num_ticks_poc_diff_one_minus1);
    DXVK_VD_CMP_FIELD(vps, pProfileTierLevel->flags.general_tier_flag);
    DXVK_VD_CMP_FIELD(vps, pProfileTierLevel->flags.general_progressive_source_flag);
    DXVK_VD_CMP_FIELD(vps, pProfileTierLevel->flags.general_interlaced_source_flag);
    DXVK_VD_CMP_FIELD(vps, pProfileTierLevel->flags.general_non_packed_constraint_flag);
    DXVK_VD_CMP_FIELD(vps, pProfileTierLevel->flags.general_frame_only_constraint_flag);
    DXVK_VD_CMP_FIELD(vps, pProfileTierLevel->general_profile_idc);
    DXVK_VD_CMP_FIELD(vps, pProfileTierLevel->general_level_idc);
    return true;
  }


  static bool IsAV1SequenceHeaderEqual(
    const StdVideoAV1SequenceHeader &sh1,
    const StdVideoAV1SequenceHeader &sh2) {
    DXVK_VD_CMP_FIELD(sh, flags.still_picture);
    DXVK_VD_CMP_FIELD(sh, flags.reduced_still_picture_header);
    DXVK_VD_CMP_FIELD(sh, flags.use_128x128_superblock);
    DXVK_VD_CMP_FIELD(sh, flags.enable_filter_intra);
    DXVK_VD_CMP_FIELD(sh, flags.enable_intra_edge_filter);
    DXVK_VD_CMP_FIELD(sh, flags.enable_interintra_compound);
    DXVK_VD_CMP_FIELD(sh, flags.enable_masked_compound);
    DXVK_VD_CMP_FIELD(sh, flags.enable_warped_motion);
    DXVK_VD_CMP_FIELD(sh, flags.enable_dual_filter);
    DXVK_VD_CMP_FIELD(sh, flags.enable_order_hint);
    DXVK_VD_CMP_FIELD(sh, flags.enable_jnt_comp);
    DXVK_VD_CMP_FIELD(sh, flags.enable_ref_frame_mvs);
    DXVK_VD_CMP_FIELD(sh, flags.frame_id_numbers_present_flag);
    DXVK_VD_CMP_FIELD(sh, flags.enable_superres);
    DXVK_VD_CMP_FIELD(sh, flags.enable_cdef);
    DXVK_VD_CMP_FIELD(sh, flags.enable_restoration);
    DXVK_VD_CMP_FIELD(sh, flags.film_grain_params_present);
    DXVK_VD_CMP_FIELD(sh, flags.timing_info_present_flag);
    DXVK_VD_CMP_FIELD(sh, flags.initial_display_delay_present_flag);
    DXVK_VD_CMP_FIELD(sh, seq_profile);
    DXVK_VD_CMP_FIELD(sh, frame_width_bits_minus_1);
    DXVK_VD_CMP_FIELD(sh, frame_height_bits_minus_1);
    DXVK_VD_CMP_FIELD(sh, max_frame_width_minus_1);
    DXVK_VD_CMP_FIELD(sh, max_frame_height_minus_1);
    DXVK_VD_CMP_FIELD(sh, delta_frame_id_length_minus_2);
    DXVK_VD_CMP_FIELD(sh, additional_frame_id_length_minus_1);
    DXVK_VD_CMP_FIELD(sh, order_hint_bits_minus_1);
    DXVK_VD_CMP_FIELD(sh, seq_force_integer_mv);
    DXVK_VD_CMP_FIELD(sh, seq_force_screen_content_tools);
    return true;
  }


#define DXVK_VD_CMP_SPS_FIELDS(_f) DXVK_VD_CMP_FIELD(sps, _f)
  static bool IsH264SPSEqual(
    const StdVideoH264SequenceParameterSet &sps1,
    const StdVideoH264SequenceParameterSet &sps2) {
    DXVK_VD_CMP_SPS_FIELDS(flags.constraint_set0_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.constraint_set1_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.constraint_set2_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.constraint_set3_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.constraint_set4_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.constraint_set5_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.direct_8x8_inference_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.mb_adaptive_frame_field_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.frame_mbs_only_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.delta_pic_order_always_zero_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.separate_colour_plane_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.gaps_in_frame_num_value_allowed_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.qpprime_y_zero_transform_bypass_flag)
    DXVK_VD_CMP_SPS_FIELDS(flags.frame_cropping_flag)
    DXVK_VD_CMP_SPS_FIELDS(profile_idc)
    DXVK_VD_CMP_SPS_FIELDS(level_idc)
    DXVK_VD_CMP_SPS_FIELDS(chroma_format_idc)
    DXVK_VD_CMP_SPS_FIELDS(bit_depth_luma_minus8)
    DXVK_VD_CMP_SPS_FIELDS(bit_depth_chroma_minus8)
    DXVK_VD_CMP_SPS_FIELDS(log2_max_frame_num_minus4)
    DXVK_VD_CMP_SPS_FIELDS(pic_order_cnt_type)
    DXVK_VD_CMP_SPS_FIELDS(offset_for_non_ref_pic)
    DXVK_VD_CMP_SPS_FIELDS(offset_for_top_to_bottom_field)
    DXVK_VD_CMP_SPS_FIELDS(log2_max_pic_order_cnt_lsb_minus4)
    DXVK_VD_CMP_SPS_FIELDS(num_ref_frames_in_pic_order_cnt_cycle)
    DXVK_VD_CMP_SPS_FIELDS(max_num_ref_frames)
    DXVK_VD_CMP_SPS_FIELDS(pic_width_in_mbs_minus1)
    DXVK_VD_CMP_SPS_FIELDS(pic_height_in_map_units_minus1)
    DXVK_VD_CMP_SPS_FIELDS(frame_crop_left_offset)
    DXVK_VD_CMP_SPS_FIELDS(frame_crop_right_offset)
    DXVK_VD_CMP_SPS_FIELDS(frame_crop_top_offset)
    DXVK_VD_CMP_SPS_FIELDS(frame_crop_bottom_offset)
    return true;
  }
#undef DXVK_VD_CMP_SPS_FIELDS


  static bool IsH265SPSEqual(
    const StdVideoH265SequenceParameterSet &sps1,
    const StdVideoH265SequenceParameterSet &sps2) {
    DXVK_VD_CMP_FIELD(sps, flags.sps_temporal_id_nesting_flag);
    DXVK_VD_CMP_FIELD(sps, flags.separate_colour_plane_flag);
    DXVK_VD_CMP_FIELD(sps, flags.conformance_window_flag);
    DXVK_VD_CMP_FIELD(sps, flags.sps_sub_layer_ordering_info_present_flag);
    DXVK_VD_CMP_FIELD(sps, flags.scaling_list_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.sps_scaling_list_data_present_flag);
    DXVK_VD_CMP_FIELD(sps, flags.amp_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.sample_adaptive_offset_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.pcm_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.pcm_loop_filter_disabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.long_term_ref_pics_present_flag);
    DXVK_VD_CMP_FIELD(sps, flags.sps_temporal_mvp_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.strong_intra_smoothing_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.vui_parameters_present_flag);
    DXVK_VD_CMP_FIELD(sps, flags.sps_extension_present_flag);
    DXVK_VD_CMP_FIELD(sps, flags.sps_range_extension_flag);
    DXVK_VD_CMP_FIELD(sps, flags.transform_skip_rotation_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.transform_skip_context_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.implicit_rdpcm_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.explicit_rdpcm_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.extended_precision_processing_flag);
    DXVK_VD_CMP_FIELD(sps, flags.intra_smoothing_disabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.high_precision_offsets_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.persistent_rice_adaptation_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.cabac_bypass_alignment_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.sps_scc_extension_flag);
    DXVK_VD_CMP_FIELD(sps, flags.sps_curr_pic_ref_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.palette_mode_enabled_flag);
    DXVK_VD_CMP_FIELD(sps, flags.sps_palette_predictor_initializers_present_flag);
    DXVK_VD_CMP_FIELD(sps, flags.intra_boundary_filtering_disabled_flag);
    DXVK_VD_CMP_FIELD(sps, chroma_format_idc);
    DXVK_VD_CMP_FIELD(sps, pic_width_in_luma_samples);
    DXVK_VD_CMP_FIELD(sps, pic_height_in_luma_samples);
    DXVK_VD_CMP_FIELD(sps, sps_video_parameter_set_id);
    DXVK_VD_CMP_FIELD(sps, sps_max_sub_layers_minus1);
    DXVK_VD_CMP_FIELD(sps, sps_seq_parameter_set_id);
    DXVK_VD_CMP_FIELD(sps, bit_depth_luma_minus8);
    DXVK_VD_CMP_FIELD(sps, bit_depth_chroma_minus8);
    DXVK_VD_CMP_FIELD(sps, log2_max_pic_order_cnt_lsb_minus4);
    DXVK_VD_CMP_FIELD(sps, log2_min_luma_coding_block_size_minus3);
    DXVK_VD_CMP_FIELD(sps, log2_diff_max_min_luma_coding_block_size);
    DXVK_VD_CMP_FIELD(sps, log2_min_luma_transform_block_size_minus2);
    DXVK_VD_CMP_FIELD(sps, log2_diff_max_min_luma_transform_block_size);
    DXVK_VD_CMP_FIELD(sps, max_transform_hierarchy_depth_inter);
    DXVK_VD_CMP_FIELD(sps, max_transform_hierarchy_depth_intra);
    DXVK_VD_CMP_FIELD(sps, num_short_term_ref_pic_sets);
    DXVK_VD_CMP_FIELD(sps, num_long_term_ref_pics_sps);
    DXVK_VD_CMP_FIELD(sps, pcm_sample_bit_depth_luma_minus1);
    DXVK_VD_CMP_FIELD(sps, pcm_sample_bit_depth_chroma_minus1);
    DXVK_VD_CMP_FIELD(sps, log2_min_pcm_luma_coding_block_size_minus3);
    DXVK_VD_CMP_FIELD(sps, log2_diff_max_min_pcm_luma_coding_block_size);
    DXVK_VD_CMP_FIELD(sps, palette_max_size);
    DXVK_VD_CMP_FIELD(sps, delta_palette_max_predictor_size);
    DXVK_VD_CMP_FIELD(sps, motion_vector_resolution_control_idc);
    DXVK_VD_CMP_FIELD(sps, sps_num_palette_predictor_initializers_minus1);
    DXVK_VD_CMP_FIELD(sps, conf_win_left_offset);
    DXVK_VD_CMP_FIELD(sps, conf_win_right_offset);
    DXVK_VD_CMP_FIELD(sps, conf_win_top_offset);
    DXVK_VD_CMP_FIELD(sps, conf_win_bottom_offset);
    /* Unused DXVK_VD_CMP_FIELD(sps, pProfileTierLevel); */
    /* Unused DXVK_VD_CMP_FIELD(sps, pDecPicBufMgr); */
    /* Unused DXVK_VD_CMP_FIELD(sps, pScalingLists); */
    /* Unused DXVK_VD_CMP_FIELD(sps, pShortTermRefPicSet); */
    /* Unused DXVK_VD_CMP_FIELD(sps, pLongTermRefPicsSps); */
    /* Unused DXVK_VD_CMP_FIELD(sps, pSequenceParameterSetVui); */
    /* Unused DXVK_VD_CMP_FIELD(sps, pPredictorPaletteEntries); */
    return true;
  }


#define DXVK_VD_CMP_PPS_FIELDS(_f) DXVK_VD_CMP_FIELD(pps, _f)
  static bool IsH264PPSEqual(
    const StdVideoH264PictureParameterSet &pps1,
    const StdVideoH264PictureParameterSet &pps2) {
    DXVK_VD_CMP_PPS_FIELDS(flags.transform_8x8_mode_flag)
    DXVK_VD_CMP_PPS_FIELDS(flags.redundant_pic_cnt_present_flag)
    DXVK_VD_CMP_PPS_FIELDS(flags.constrained_intra_pred_flag)
    DXVK_VD_CMP_PPS_FIELDS(flags.deblocking_filter_control_present_flag)
    DXVK_VD_CMP_PPS_FIELDS(flags.weighted_pred_flag)
    DXVK_VD_CMP_PPS_FIELDS(flags.bottom_field_pic_order_in_frame_present_flag)
    DXVK_VD_CMP_PPS_FIELDS(flags.entropy_coding_mode_flag)
    DXVK_VD_CMP_PPS_FIELDS(flags.pic_scaling_matrix_present_flag)
    DXVK_VD_CMP_PPS_FIELDS(num_ref_idx_l0_default_active_minus1)
    DXVK_VD_CMP_PPS_FIELDS(num_ref_idx_l1_default_active_minus1)
    DXVK_VD_CMP_PPS_FIELDS(weighted_bipred_idc)
    DXVK_VD_CMP_PPS_FIELDS(pic_init_qp_minus26)
    DXVK_VD_CMP_PPS_FIELDS(pic_init_qs_minus26)
    DXVK_VD_CMP_PPS_FIELDS(chroma_qp_index_offset)
    DXVK_VD_CMP_PPS_FIELDS(second_chroma_qp_index_offset)
    if (pps1.flags.pic_scaling_matrix_present_flag) {
      /// @todo Compare scaling lists
    }
    return true;
  }
#undef DXVK_VD_CMP_PPS_FIELDS


  static bool IsH265PPSEqual(
    const StdVideoH265PictureParameterSet &pps1,
    const StdVideoH265PictureParameterSet &pps2) {
    DXVK_VD_CMP_FIELD(pps, flags.dependent_slice_segments_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.output_flag_present_flag);
    DXVK_VD_CMP_FIELD(pps, flags.sign_data_hiding_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.cabac_init_present_flag);
    DXVK_VD_CMP_FIELD(pps, flags.constrained_intra_pred_flag);
    DXVK_VD_CMP_FIELD(pps, flags.transform_skip_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.cu_qp_delta_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.pps_slice_chroma_qp_offsets_present_flag);
    DXVK_VD_CMP_FIELD(pps, flags.weighted_pred_flag);
    DXVK_VD_CMP_FIELD(pps, flags.weighted_bipred_flag);
    DXVK_VD_CMP_FIELD(pps, flags.transquant_bypass_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.tiles_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.entropy_coding_sync_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.uniform_spacing_flag);
    DXVK_VD_CMP_FIELD(pps, flags.loop_filter_across_tiles_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.pps_loop_filter_across_slices_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.deblocking_filter_control_present_flag);
    DXVK_VD_CMP_FIELD(pps, flags.deblocking_filter_override_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.pps_deblocking_filter_disabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.pps_scaling_list_data_present_flag);
    DXVK_VD_CMP_FIELD(pps, flags.lists_modification_present_flag);
    DXVK_VD_CMP_FIELD(pps, flags.slice_segment_header_extension_present_flag);
    DXVK_VD_CMP_FIELD(pps, flags.pps_extension_present_flag);
    DXVK_VD_CMP_FIELD(pps, flags.cross_component_prediction_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.chroma_qp_offset_list_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.pps_curr_pic_ref_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.residual_adaptive_colour_transform_enabled_flag);
    DXVK_VD_CMP_FIELD(pps, flags.pps_slice_act_qp_offsets_present_flag);
    DXVK_VD_CMP_FIELD(pps, flags.pps_palette_predictor_initializers_present_flag);
    DXVK_VD_CMP_FIELD(pps, flags.monochrome_palette_flag);
    DXVK_VD_CMP_FIELD(pps, flags.pps_range_extension_flag);
    DXVK_VD_CMP_FIELD(pps, pps_pic_parameter_set_id);
    DXVK_VD_CMP_FIELD(pps, pps_seq_parameter_set_id);
    DXVK_VD_CMP_FIELD(pps, sps_video_parameter_set_id);
    DXVK_VD_CMP_FIELD(pps, num_extra_slice_header_bits);
    DXVK_VD_CMP_FIELD(pps, num_ref_idx_l0_default_active_minus1);
    DXVK_VD_CMP_FIELD(pps, num_ref_idx_l1_default_active_minus1);
    DXVK_VD_CMP_FIELD(pps, init_qp_minus26);
    DXVK_VD_CMP_FIELD(pps, diff_cu_qp_delta_depth);
    DXVK_VD_CMP_FIELD(pps, pps_cb_qp_offset);
    DXVK_VD_CMP_FIELD(pps, pps_cr_qp_offset);
    DXVK_VD_CMP_FIELD(pps, pps_beta_offset_div2);
    DXVK_VD_CMP_FIELD(pps, pps_tc_offset_div2);
    DXVK_VD_CMP_FIELD(pps, log2_parallel_merge_level_minus2);
    DXVK_VD_CMP_FIELD(pps, log2_max_transform_skip_block_size_minus2);
    DXVK_VD_CMP_FIELD(pps, diff_cu_chroma_qp_offset_depth);
    DXVK_VD_CMP_FIELD(pps, chroma_qp_offset_list_len_minus1);
    for (uint32_t i = 0; i < STD_VIDEO_H265_CHROMA_QP_OFFSET_LIST_SIZE; ++i) {
      DXVK_VD_CMP_FIELD(pps, cb_qp_offset_list[i]);
    }
    for (uint32_t i = 0; i < STD_VIDEO_H265_CHROMA_QP_OFFSET_LIST_SIZE; ++i) {
      DXVK_VD_CMP_FIELD(pps, cr_qp_offset_list[i]);
    }
    DXVK_VD_CMP_FIELD(pps, log2_sao_offset_scale_luma);
    DXVK_VD_CMP_FIELD(pps, log2_sao_offset_scale_chroma);
    DXVK_VD_CMP_FIELD(pps, pps_act_y_qp_offset_plus5);
    DXVK_VD_CMP_FIELD(pps, pps_act_cb_qp_offset_plus5);
    DXVK_VD_CMP_FIELD(pps, pps_act_cr_qp_offset_plus3);
    DXVK_VD_CMP_FIELD(pps, pps_num_palette_predictor_initializers);
    DXVK_VD_CMP_FIELD(pps, luma_bit_depth_entry_minus8);
    DXVK_VD_CMP_FIELD(pps, chroma_bit_depth_entry_minus8);
    DXVK_VD_CMP_FIELD(pps, num_tile_columns_minus1);
    DXVK_VD_CMP_FIELD(pps, num_tile_rows_minus1);
    for (uint32_t i = 0; i < STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_COLS_LIST_SIZE; ++i) {
      DXVK_VD_CMP_FIELD(pps, column_width_minus1[i]);
    }
    for (uint32_t i = 0; i < STD_VIDEO_H265_CHROMA_QP_OFFSET_TILE_ROWS_LIST_SIZE; ++i) {
      DXVK_VD_CMP_FIELD(pps, row_height_minus1[i]);
    }
    if (pps1.flags.pps_scaling_list_data_present_flag) {
      /// @todo Compare scaling lists. DXVK_VD_CMP_FIELD(pps, pScalingLists);
    }
    /* Unused DXVK_VD_CMP_FIELD(pps, pPredictorPaletteEntries); */
    return true;
  }


  VkResult DxvkVideoDecoder::UpdateSessionParametersH264(
    DxvkVideoDecodeInputParameters *pParms) {
    /* Update internal pointer(s). */
    pParms->h264.sps.pOffsetForRefFrame = &pParms->h264.spsOffsetForRefFrame;
    pParms->h264.pps.pScalingLists      = &pParms->h264.ppsScalingLists;

    /* Information about a possible update of session parameters. */
    VkVideoDecodeH264SessionParametersAddInfoKHR h264AddInfo =
      { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR };

    /* Find out if the SPS is already in the cache. */
    auto itSPS = std::find_if(m_parameterSetCache.h264.sps.begin(),
      std::next(m_parameterSetCache.h264.sps.begin(), m_parameterSetCache.spsCount),
      [&sps = pParms->h264.sps](StdVideoH264SequenceParameterSet &v) -> bool { return IsH264SPSEqual(sps, v); });
    if (itSPS == std::next(m_parameterSetCache.h264.sps.begin(), m_parameterSetCache.spsCount)) {
      /* A new SPS. */
      if (itSPS == m_parameterSetCache.h264.sps.end()) {
        Logger::err(str::format("DxvkVideoDecoder: SPS count > ", m_parameterSetCache.h264.sps.size()));
        return VK_ERROR_TOO_MANY_OBJECTS;
      }

      *itSPS = pParms->h264.sps;
      ++m_parameterSetCache.spsCount;

      h264AddInfo.stdSPSCount = 1;
      h264AddInfo.pStdSPSs = &pParms->h264.sps;
    }

    /* Find out if the PPS is already in the cache. */
    auto itPPS = std::find_if(m_parameterSetCache.h264.pps.begin(),
      std::next(m_parameterSetCache.h264.pps.begin(), m_parameterSetCache.ppsCount),
      [&pps = pParms->h264.pps](StdVideoH264PictureParameterSet &v) -> bool { return IsH264PPSEqual(pps, v); });
    if (itPPS == std::next(m_parameterSetCache.h264.pps.begin(), m_parameterSetCache.ppsCount)) {
      /* A new PPS. */
      if (itPPS == m_parameterSetCache.h264.pps.end()) {
        Logger::err(str::format("DxvkVideoDecoder: PPS count > ", m_parameterSetCache.h264.pps.size()));
        return VK_ERROR_TOO_MANY_OBJECTS;
      }

      *itPPS = pParms->h264.pps;
      ++m_parameterSetCache.ppsCount;

      h264AddInfo.stdPPSCount = 1;
      h264AddInfo.pStdPPSs = &pParms->h264.pps;
    }

    const uint32_t spsId = std::distance(m_parameterSetCache.h264.sps.begin(), itSPS);
    const uint32_t ppsId = std::distance(m_parameterSetCache.h264.pps.begin(), itPPS);

    pParms->h264.sps.seq_parameter_set_id                = spsId;
    pParms->h264.pps.seq_parameter_set_id                = spsId;
    pParms->h264.pps.pic_parameter_set_id                = ppsId;
    pParms->h264.stdH264PictureInfo.seq_parameter_set_id = spsId;
    pParms->h264.stdH264PictureInfo.pic_parameter_set_id = ppsId;
    pParms->h264.sps.level_idc                           = m_profile.decodeH264Capabilities.maxLevelIdc;

    if (h264AddInfo.stdSPSCount == 0 && h264AddInfo.stdPPSCount == 0)
      return VK_SUCCESS;

    /* Update videoSessionParameters with the new picture info. */
    VkVideoSessionParametersUpdateInfoKHR updateInfo =
     { VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_UPDATE_INFO_KHR, &h264AddInfo };
    updateInfo.updateSequenceCount = ++m_parameterSetCache.updateSequenceCount; /* Must start from 1. */

    return m_device->vkd()->vkUpdateVideoSessionParametersKHR(m_device->handle(),
      m_videoSessionParameters->handle(), &updateInfo);
  }


  VkResult DxvkVideoDecoder::UpdateSessionParametersH265(
    DxvkVideoDecodeInputParameters *pParms) {
    /* Update internal pointer(s). */
    pParms->h265.vps.pProfileTierLevel = &pParms->h265.vpsProfileTierLevel;
    pParms->h265.sps.pProfileTierLevel = &pParms->h265.vpsProfileTierLevel;
    pParms->h265.sps.pDecPicBufMgr     = &pParms->h265.spsDecPicBufMgr;
    pParms->h265.pps.pScalingLists     = &pParms->h265.ppsScalingLists;

    /* Information about a possible update of session parameters. */
    VkVideoDecodeH265SessionParametersAddInfoKHR addInfo =
      { VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR };

    /* Find out if the VPS is already in the cache. */
    auto itVPS = std::find_if(m_parameterSetCache.h265.vps.begin(),
      std::next(m_parameterSetCache.h265.vps.begin(), m_parameterSetCache.vpsCount),
      [&vps = pParms->h265.vps](StdVideoH265VideoParameterSet &v) -> bool { return IsH265VPSEqual(vps, v); });
    if (itVPS == std::next(m_parameterSetCache.h265.vps.begin(), m_parameterSetCache.vpsCount)) {
      /* A new VPS. */
      if (itVPS == m_parameterSetCache.h265.vps.end()) {
        Logger::err(str::format("DxvkVideoDecoder: VPS count > ", m_parameterSetCache.h265.vps.size()));
        return VK_ERROR_TOO_MANY_OBJECTS;
      }

      const uint32_t idxVPS = std::distance(m_parameterSetCache.h265.vps.begin(), itVPS);
      m_parameterSetCache.h265.vps[idxVPS] = pParms->h265.vps;
      m_parameterSetCache.h265.vpsProfileTierLevel[idxVPS] = *pParms->h265.vps.pProfileTierLevel;
      m_parameterSetCache.h265.vps[idxVPS].pProfileTierLevel = &m_parameterSetCache.h265.vpsProfileTierLevel[idxVPS];
      ++m_parameterSetCache.vpsCount;

      addInfo.stdVPSCount = 1;
      addInfo.pStdVPSs = &pParms->h265.vps;
    }

    /* Find out if the SPS is already in the cache. */
    auto itSPS = std::find_if(m_parameterSetCache.h265.sps.begin(),
      std::next(m_parameterSetCache.h265.sps.begin(), m_parameterSetCache.spsCount),
      [&sps = pParms->h265.sps](StdVideoH265SequenceParameterSet &v) -> bool { return IsH265SPSEqual(sps, v); });
    if (itSPS == std::next(m_parameterSetCache.h265.sps.begin(), m_parameterSetCache.spsCount)) {
      /* A new SPS. */
      if (itSPS == m_parameterSetCache.h265.sps.end()) {
        Logger::err(str::format("DxvkVideoDecoder: SPS count > ", m_parameterSetCache.h265.sps.size()));
        return VK_ERROR_TOO_MANY_OBJECTS;
      }

      *itSPS = pParms->h265.sps;
      ++m_parameterSetCache.spsCount;

      addInfo.stdSPSCount = 1;
      addInfo.pStdSPSs = &pParms->h265.sps;
    }

    /* Find out if the PPS is already in the cache. */
    auto itPPS = std::find_if(m_parameterSetCache.h265.pps.begin(),
      std::next(m_parameterSetCache.h265.pps.begin(), m_parameterSetCache.ppsCount),
      [&pps = pParms->h265.pps](StdVideoH265PictureParameterSet &v) -> bool { return IsH265PPSEqual(pps, v); });
    if (itPPS == std::next(m_parameterSetCache.h265.pps.begin(), m_parameterSetCache.ppsCount)) {
      /* A new PPS. */
      if (itPPS == m_parameterSetCache.h265.pps.end()) {
        Logger::err(str::format("DxvkVideoDecoder: PPS count > ", m_parameterSetCache.h265.pps.size()));
        return VK_ERROR_TOO_MANY_OBJECTS;
      }

      *itPPS = pParms->h265.pps;
      ++m_parameterSetCache.ppsCount;

      addInfo.stdPPSCount = 1;
      addInfo.pStdPPSs = &pParms->h265.pps;
    }

    const uint32_t vpsId = std::distance(m_parameterSetCache.h265.vps.begin(), itVPS);
    const uint32_t spsId = std::distance(m_parameterSetCache.h265.sps.begin(), itSPS);
    const uint32_t ppsId = std::distance(m_parameterSetCache.h265.pps.begin(), itPPS);

    pParms->h265.vps.vps_video_parameter_set_id            = vpsId;
    pParms->h265.sps.sps_seq_parameter_set_id              = spsId;
    pParms->h265.sps.sps_video_parameter_set_id            = vpsId;
    pParms->h265.pps.pps_pic_parameter_set_id              = ppsId;
    pParms->h265.pps.pps_seq_parameter_set_id              = spsId;
    pParms->h265.pps.sps_video_parameter_set_id            = vpsId;
    pParms->h265.stdPictureInfo.sps_video_parameter_set_id = vpsId;
    pParms->h265.stdPictureInfo.pps_seq_parameter_set_id   = spsId;
    pParms->h265.stdPictureInfo.pps_pic_parameter_set_id   = ppsId;
    pParms->h265.vpsProfileTierLevel.general_level_idc     = m_profile.decodeH265Capabilities.maxLevelIdc;

    /* 42.13.6. H.265 Decoding Parameters: "RefPicSetStCurrBefore, RefPicSetStCurrAfter, and RefPicSetLtCurr"
     * ... "each element of these arrays" ... "identifies an active reference picture using its DPB slot index".
     * D3D11VideoDecoder passes surface ids in these arrays. Translate surface ids to the DPB slot indices.
     */
    for (uint32_t i = 0; i < 8; ++i) {
      const uint8_t idSurface = pParms->h265.stdPictureInfo.RefPicSetStCurrBefore[i];
      if (idSurface != 0xff) {
        if (m_DPB.refFrames.find(idSurface) != m_DPB.refFrames.end()
         && m_DPB.refFrames[idSurface].dpbSlotIndex >= 0) {
          const int32_t dpbSlotIndex = m_DPB.refFrames[idSurface].dpbSlotIndex;
          pParms->h265.stdPictureInfo.RefPicSetStCurrBefore[i] = (uint8_t)dpbSlotIndex;
        }
        else
          pParms->h265.stdPictureInfo.RefPicSetStCurrBefore[i] = 0xff;
      }
    }

    for (uint32_t i = 0; i < 8; ++i) {
      const uint8_t idSurface = pParms->h265.stdPictureInfo.RefPicSetStCurrAfter[i];
      if (idSurface != 0xff) {
        if (m_DPB.refFrames.find(idSurface) != m_DPB.refFrames.end()
         && m_DPB.refFrames[idSurface].dpbSlotIndex >= 0) {
          const int32_t dpbSlotIndex = m_DPB.refFrames[idSurface].dpbSlotIndex;
          pParms->h265.stdPictureInfo.RefPicSetStCurrAfter[i] = (uint8_t)dpbSlotIndex;
        }
        else
          pParms->h265.stdPictureInfo.RefPicSetStCurrAfter[i] = 0xff;
      }
    }

    for (uint32_t i = 0; i < 8; ++i) {
      const uint8_t idSurface = pParms->h265.stdPictureInfo.RefPicSetLtCurr[i];
      if (idSurface != 0xff) {
        if (m_DPB.refFrames.find(idSurface) != m_DPB.refFrames.end()
         && m_DPB.refFrames[idSurface].dpbSlotIndex >= 0) {
          const int32_t dpbSlotIndex = m_DPB.refFrames[idSurface].dpbSlotIndex;
          pParms->h265.stdPictureInfo.RefPicSetLtCurr[i] = (uint8_t)dpbSlotIndex;
        }
        else
          pParms->h265.stdPictureInfo.RefPicSetLtCurr[i] = 0xff;
      }
    }

    if (addInfo.stdVPSCount == 0 && addInfo.stdSPSCount == 0 && addInfo.stdPPSCount == 0)
      return VK_SUCCESS;

    /* Update videoSessionParameters with the new picture info. */
    VkVideoSessionParametersUpdateInfoKHR updateInfo =
     { VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_UPDATE_INFO_KHR, &addInfo };
    updateInfo.updateSequenceCount = ++m_parameterSetCache.updateSequenceCount; /* Must start from 1. */

    return m_device->vkd()->vkUpdateVideoSessionParametersKHR(m_device->handle(),
      m_videoSessionParameters->handle(), &updateInfo);
  }


  VkResult DxvkVideoDecoder::UpdateSessionParametersAV1(
    DxvkVideoDecodeInputParameters *pParms) {
    /* Update internal pointer(s). */
    pParms->av1.stdSequenceHeader.pColorConfig  = &pParms->av1.stdColorConfig;
    pParms->av1.stdPictureInfo.pTileInfo        = &pParms->av1.stdTileInfo;
    pParms->av1.stdPictureInfo.pQuantization    = &pParms->av1.stdQuantization;
    pParms->av1.stdPictureInfo.pSegmentation    = &pParms->av1.stdSegmentation;
    pParms->av1.stdPictureInfo.pLoopFilter      = &pParms->av1.stdLoopFilter;
    pParms->av1.stdPictureInfo.pCDEF            = &pParms->av1.stdCDEF;
    pParms->av1.stdPictureInfo.pLoopRestoration = &pParms->av1.stdLoopRestoration;
    pParms->av1.stdPictureInfo.pGlobalMotion    = &pParms->av1.stdGlobalMotion;
    pParms->av1.stdPictureInfo.pFilmGrain       = pParms->av1.stdPictureInfo.flags.apply_grain
                                                ? &pParms->av1.stdFilmGrain
                                                : nullptr;
    pParms->av1.stdTileInfo.pMiColStarts        = &pParms->av1.MiColStarts[0];
    pParms->av1.stdTileInfo.pWidthInSbsMinus1   = &pParms->av1.WidthInSbsMinus1[0];
    pParms->av1.stdTileInfo.pMiRowStarts        = &pParms->av1.MiRowStarts[0];
    pParms->av1.stdTileInfo.pHeightInSbsMinus1  = &pParms->av1.HeightInSbsMinus1[0];

    if (m_videoSessionParameters->handle() != VK_NULL_HANDLE
     && IsAV1SequenceHeaderEqual(pParms->av1.stdSequenceHeader, m_parameterSetCache.av1.stdSequenceHeader))
      return VK_SUCCESS;

    m_parameterSetCache.av1.stdSequenceHeader = pParms->av1.stdSequenceHeader;

    /* Create videoSessionParameters with the new info. */
    m_videoSessionParameters = new DxvkVideoSessionParametersHandle(m_device);

    VkVideoDecodeAV1SessionParametersCreateInfoKHR av1SessionParametersCreateInfo =
      { VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_SESSION_PARAMETERS_CREATE_INFO_KHR };
    av1SessionParametersCreateInfo.pStdSequenceHeader = &pParms->av1.stdSequenceHeader;

    m_videoSessionParameters->create(m_videoSession, &av1SessionParametersCreateInfo);

    return VK_SUCCESS;
  }


  static int8_t av1_get_relative_dist(const DxvkVideoDecodeInputParameters &parms,
    uint8_t a, uint8_t b) {
    /* AV1 spec 5.9.3. Get relative distance function */
    int8_t diff = a - b;
    const uint8_t m = 1 << parms.av1.stdSequenceHeader.order_hint_bits_minus_1;
    diff = (diff & (m - 1)) - (diff & m);
    return diff;
  }


  static void av1_RefFrameSignBias(DxvkVideoDecodeInputParameters &parms) {
    if (parms.av1.stdSequenceHeader.flags.enable_order_hint) {
      for (uint8_t i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; ++i) {
        const uint8_t refFrameName = STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME + i;
        const int8_t relativeDistance = av1_get_relative_dist(parms,
          parms.av1.stdPictureInfo.OrderHints[refFrameName], parms.av1.stdPictureInfo.OrderHint);

        /* Vulkan uses bit mask as array. */
        parms.av1.stdReferenceInfo.RefFrameSignBias |= (relativeDistance > 0) << refFrameName;
      }
    }
  }


  static void av1_skip_mode_params(DxvkVideoDecodeInputParameters &parms) {
    if (parms.av1.stdPictureInfo.frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY
     || parms.av1.stdPictureInfo.frame_type == STD_VIDEO_AV1_FRAME_TYPE_INTRA_ONLY
     || !parms.av1.stdPictureInfo.flags.reference_select
     || !parms.av1.stdSequenceHeader.flags.enable_order_hint) {
       return;
    }

    int8_t forwardIdx = -1;
    int8_t backwardIdx = -1;
    uint8_t forwardHint;
    uint8_t backwardHint;

    for (uint8_t i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; ++i) {
      const uint8_t refFrameName = STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME + i;
      const uint8_t refHint = parms.av1.stdPictureInfo.OrderHints[refFrameName];

      if (av1_get_relative_dist(parms, refHint, parms.av1.stdPictureInfo.OrderHint) < 0) {
        if (forwardIdx < 0
         || av1_get_relative_dist(parms, refHint, forwardHint) > 0) {
          forwardIdx = i;
          forwardHint = refHint;
        }
      } else if (av1_get_relative_dist(parms, refHint, parms.av1.stdPictureInfo.OrderHint) > 0) {
        if (backwardIdx < 0
         || av1_get_relative_dist(parms, refHint, backwardHint) < 0 ) {
          backwardIdx = i;
          backwardHint = refHint;
        }
      }
    }

    if (forwardIdx < 0) {
      return;
    }

    if (backwardIdx >= 0) {
      parms.av1.stdPictureInfo.SkipModeFrame[0] = STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME
        + std::min(forwardIdx, backwardIdx);
      parms.av1.stdPictureInfo.SkipModeFrame[1] = STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME
        + std::max(forwardIdx, backwardIdx);
    }
    else {
      int8_t secondForwardIdx = -1;
      uint8_t secondForwardHint;

      for (uint8_t i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; ++i) {
        const uint8_t refFrameName = STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME + i;
        const uint8_t refHint = parms.av1.stdPictureInfo.OrderHints[refFrameName];

        if (av1_get_relative_dist(parms, refHint, forwardHint) < 0) {
          if (secondForwardIdx < 0
           || av1_get_relative_dist(parms, refHint, secondForwardHint) > 0) {
            secondForwardIdx = i;
            secondForwardHint = refHint;
          }
        }
      }

      if (secondForwardIdx >= 0) {
        parms.av1.stdPictureInfo.SkipModeFrame[0] = STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME
          + std::min(forwardIdx, secondForwardIdx);
        parms.av1.stdPictureInfo.SkipModeFrame[1] = STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME
          + std::max(forwardIdx, secondForwardIdx);
      }
    }
  }


  static void av1ComputeParams(DxvkVideoDecodeInputParameters &parms) {
    /* Update various parameters as described in AV1 spec. */

    /* AV1 spec 5.9.2. Uncompressed header syntax */
    av1_RefFrameSignBias(parms);

    /* AV1 spec 5.9.22. Skip mode params syntax */
    av1_skip_mode_params(parms);
  }


  void DxvkVideoDecoder::Decode(
    DxvkContext* ctx,
    DxvkVideoDecodeInputParameters parms)
  {
    VkResult vr;

    /* Complete the provided parameters. */
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      vr = this->UpdateSessionParametersH264(&parms);
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      vr = this->UpdateSessionParametersH265(&parms);
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      vr = this->UpdateSessionParametersAV1(&parms);
    }
    if (vr != VK_SUCCESS)
      return;

    const bool fUseDistinctOutputImage = m_caps.distinctOutputImage
      || (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR
       && parms.av1.stdPictureInfo.flags.apply_grain);

    VkExtent2D codedExtent = { m_sampleWidth, m_sampleHeight };
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      codedExtent.width = parms.av1.stdSequenceHeader.max_frame_width_minus_1 + 1;
      codedExtent.height = parms.av1.stdSequenceHeader.max_frame_height_minus_1 + 1;
    }

    if (codedExtent.width > m_DPB.decodedPictureExtent.width
     || codedExtent.height > m_DPB.decodedPictureExtent.height) {
      Logger::err(str::format("DxvkVideoDecoder: frame size (", codedExtent.width, "x", codedExtent.height, ")",
        " exceeds DPB image size (", m_DPB.decodedPictureExtent.width, "x", m_DPB.decodedPictureExtent.height, ")"));
      return;
    }

    /*
     * Allocate space in the GPU buffer and copy encoded frame into it.
     */
    /* How many bytes in the ring buffer is required including alignment. */
    const uint32_t cbFrame = align(parms.bitstreamLength, m_profile.videoCapabilities.minBitstreamBufferSizeAlignment);
    /* How many bytes remains in the buffer. */
    const uint32_t cbRemaining = m_bitstreamBuffer->length() - m_offFree;

    if (cbFrame > cbRemaining) {
      m_offFree = 0; /* Start from begin of the ring buffer. */
      if (cbFrame > m_bitstreamBuffer->length())
        return; /* Frame data is apparently invalid. */
    }

    /* offFrame starts at 0, i.e. aligned. */
    const uint32_t offFrame = m_offFree;
    memcpy(m_bitstreamBuffer->mapPtr(offFrame),
      parms.bitstream.data(), parms.bitstreamLength);

    /* Advance the offset of the free space past the just copied frame. */
    m_offFree += cbFrame;
    m_offFree = align(m_offFree, m_profile.videoCapabilities.minBitstreamBufferOffsetAlignment);
    if (m_offFree >= m_bitstreamBuffer->length())
      m_offFree = 0; /* Start from begin of the ring buffer. */

    /* A BufferMemoryBarrier is not needed because the buffer update happens before vkSubmit and:
     * "Queue submission commands automatically perform a domain operation from host to device
     *  for all writes performed before the command executes"
     */

    /*
     * Reset Decoded Picture Buffer if requested.
     */
    bool doIDR = false;
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      doIDR = parms.h264.nal_unit_type == 5;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      doIDR = parms.h265.stdPictureInfo.flags.IdrPicFlag;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      doIDR = parms.av1.stdPictureInfo.frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY;
    }
    if (doIDR) /* IDR, immediate decoder reset. */
      m_DPB.reset();

    if (m_DPB.fOverflow)
      return;

    /*
     * Update information about decoded reference frames.
     */
    for (uint32_t i = 0; i < parms.refFramesCount; ++i) {
      const DxvkRefFrameInfo& r = parms.refFrames[i];

      /* Update ref frame info if the frame exists and is associated with a DPB slot.
       * This is always true for valid video streams.
       */
      if (m_DPB.refFrames.find(r.idSurface) != m_DPB.refFrames.end()
       && m_DPB.refFrames[r.idSurface].dpbSlotIndex != -1) {
        DxvkRefFrame& refFrame = m_DPB.refFrames[r.idSurface];
        refFrame.refFrameInfo = r;

        /* Update stdRefInfo with now known values from DxvkRefFrameInfo */
        if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
          StdVideoDecodeH264ReferenceInfo& stdRefInfo = m_DPB.slots[refFrame.dpbSlotIndex].h264.stdRefInfo;
          stdRefInfo.flags.used_for_long_term_reference = r.h264.longTermReference;
          stdRefInfo.flags.is_non_existing              = r.h264.nonExistingFrame;
          stdRefInfo.FrameNum                           = r.h264.frame_num;
          stdRefInfo.PicOrderCnt[0]                     = r.h264.PicOrderCnt[0];
          stdRefInfo.PicOrderCnt[1]                     = r.h264.PicOrderCnt[1];
        }
        else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
          StdVideoDecodeH265ReferenceInfo& stdRefInfo = m_DPB.slots[refFrame.dpbSlotIndex].h265.stdRefInfo;
          stdRefInfo.flags.used_for_long_term_reference = r.h265.longTermReference;
          stdRefInfo.flags.unused_for_reference         = 0; /* It is a ref frame. */
          stdRefInfo.PicOrderCntVal                     = r.h265.PicOrderCntVal;
        }
        else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
          StdVideoDecodeAV1ReferenceInfo& stdRefInfo = m_DPB.slots[refFrame.dpbSlotIndex].av1.stdRefInfo;
          parms.av1.stdPictureInfo.OrderHints[r.av1.frame_name] = stdRefInfo.OrderHint;
        }
      }
    }

    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      av1ComputeParams(parms);
    }

    /*
     * Begin video decoding.
     */
    auto itRefFrame = m_DPB.refFrames.find(parms.idSurface);
    if (itRefFrame != m_DPB.refFrames.end()) {
      /* The surface id is being reused for a new decoded picture. Remove old information. */
      DxvkRefFrame& refFrame = itRefFrame->second;

      if (refFrame.dpbSlotIndex != -1)
        m_DPB.slots[refFrame.dpbSlotIndex].deactivate();

      m_DPB.refFrames.erase(itRefFrame);
    }

    /* Find a destination DPB slot, i.e. the slot where the reconstructed picture will be placed. */
    int32_t dstSlotIndex = -1;

    /* Scan DPB slots for a free slot or a short term reference. */
    for (uint32_t i = 0; i < m_DPB.slots.size(); ++i) {
      DxvkDPBSlot& slot = m_DPB.slots[m_DPB.idxCurrentDPBSlot];
      bool keepSlot = false;
      if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
        keepSlot = slot.h264.stdRefInfo.flags.used_for_long_term_reference != 0;
        for (uint32_t i = 0; i < parms.refFramesCount && !keepSlot; ++i) {
          const DxvkRefFrameInfo& r = parms.refFrames[i];
          keepSlot = r.idSurface == slot.idSurface;
        }
      }
      else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
        keepSlot = slot.h265.stdRefInfo.flags.used_for_long_term_reference != 0;
        for (uint32_t i = 0; i < parms.refFramesCount && !keepSlot; ++i) {
          const DxvkRefFrameInfo& r = parms.refFrames[i];
          keepSlot = r.idSurface == slot.idSurface;
        }
      }
      else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
        /* Keep frames those are included in RefFrameMapTextureIndex */
        for (uint32_t i = 0; i < 8 && !keepSlot; ++i) {
          if (slot.idSurface == parms.av1.RefFrameMapTextureIndex[i]) {
            keepSlot = true;
          }
        }
      }
      if (slot.isActive
       && keepSlot) {
        m_DPB.idxCurrentDPBSlot = (m_DPB.idxCurrentDPBSlot + 1) % m_DPB.slots.size();
        continue;
      }

      /* This slot can be (re-)used. */
      dstSlotIndex = m_DPB.idxCurrentDPBSlot;

      if (slot.idSurface != DXVK_VIDEO_DECODER_SURFACE_INVALID) {
        /* If this slot contained a short-term reference, erase it. */
        itRefFrame = m_DPB.refFrames.find(slot.idSurface);
        if (itRefFrame != m_DPB.refFrames.end())
          m_DPB.refFrames.erase(itRefFrame);
        slot.idSurface = DXVK_VIDEO_DECODER_SURFACE_INVALID;
      }

      break;
    }

    if (dstSlotIndex == -1) {
      /* No free slots. This can happen only if entire DPB is occupied by long-term references,
       * which is probably due to an invalid video stream.
       * Skip frames until the next IDR frame.
       */
      m_DPB.fOverflow = true;
      return;
    }

    /* Init the target DPB slot, i.e. the slot where the reconstructed picture will be placed. */
    DxvkDPBSlot &dstDPBSlot = m_DPB.slots[dstSlotIndex];
    dstDPBSlot.isActive = false;
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      dstDPBSlot.h264.stdRefInfo  = parms.h264.stdH264ReferenceInfo;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      dstDPBSlot.h265.stdRefInfo  = parms.h265.stdReferenceInfo;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      dstDPBSlot.av1.stdRefInfo   = parms.av1.stdReferenceInfo;
      /* dstDPBSlot.av1.stdRefInfo.SavedOrderHints are updated after decoding of the frame. */
    }
    dstDPBSlot.idSurface          = DXVK_VIDEO_DECODER_SURFACE_INVALID; /* Reference picture will be associated later. */

    /*
     * Prepare destination DPB image layout.
     */
    VkImageMemoryBarrier2 barrier =
      { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };

    /* Change the destination DPB slot image layout to VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR */
    barrier.srcStageMask        = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask       = 0;
    barrier.dstStageMask        = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
    barrier.dstAccessMask       = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED; /* 'The contents ... may be discarded.' */
    barrier.newLayout           = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = dstDPBSlot.image->handle();
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = dstDPBSlot.baseArrayLayer;
    barrier.subresourceRange.layerCount     = 1;

    VkDependencyInfo dependencyInfo =
      { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers    = &barrier;

    ctx->emitPipelineBarrier(DxvkCmdBuffer::VDecBuffer, &dependencyInfo);

    if (fUseDistinctOutputImage) {
      /*
       * Prepare decode destination layout.
       */
      barrier.srcStageMask        = VK_PIPELINE_STAGE_2_NONE;
      barrier.srcAccessMask       = 0;
      barrier.dstStageMask        = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
      barrier.dstAccessMask       = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
      barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED; /* 'The contents ... may be discarded.' */
      barrier.newLayout           = VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image               = m_imageDecodeDst->handle();
      barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseMipLevel   = 0;
      barrier.subresourceRange.levelCount     = 1;
      barrier.subresourceRange.baseArrayLayer = 0;
      barrier.subresourceRange.layerCount     = 1;

      dependencyInfo.imageMemoryBarrierCount = 1;
      dependencyInfo.pImageMemoryBarriers    = &barrier;

      ctx->emitPipelineBarrier(DxvkCmdBuffer::VDecBuffer, &dependencyInfo);
    }

    uint32_t maxRefFrames = 0;
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      maxRefFrames = parms.h264.sps.max_num_ref_frames;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      maxRefFrames = parms.h265.spsDecPicBufMgr.max_dec_pic_buffering_minus1[0] + 1;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      maxRefFrames = STD_VIDEO_AV1_REFS_PER_FRAME;
    }
    const uint32_t maxRefSlotsCount = std::min(parms.refFramesCount, maxRefFrames);

    /* Reference pictures and the destination slot to be bound for video decoding. */
    std::vector<VkVideoDecodeH264DpbSlotInfoKHR> h264DpbSlotInfo(maxRefSlotsCount + 1);
    std::vector<VkVideoDecodeH265DpbSlotInfoKHR> h265DpbSlotInfo(maxRefSlotsCount + 1);
    std::vector<VkVideoDecodeAV1DpbSlotInfoKHR> av1DpbSlotInfo(maxRefSlotsCount + 1);
    std::vector<VkVideoPictureResourceInfoKHR> pictureResourceInfo(maxRefSlotsCount + 1);
    std::vector<VkVideoReferenceSlotInfoKHR> referenceSlotsInfo(maxRefSlotsCount + 1);

    const void *pNext = nullptr;

    uint32_t refSlotsCount = 0; /* How many reference frames are actually added to referenceSlotsInfo */
    for (uint32_t i = 0; i < maxRefSlotsCount; ++i) {
      const DxvkRefFrameInfo& r = parms.refFrames[i];

      itRefFrame = m_DPB.refFrames.find(r.idSurface);
      if (itRefFrame == m_DPB.refFrames.end()) {
        /* Skip invalid reference frame. */
        continue;
      }

      const int32_t dpbSlotIndex = itRefFrame->second.dpbSlotIndex;
      if (dpbSlotIndex == -1) {
        /* Skip invalid reference frame. */
        continue;
      }

      /* Skip if the frame id has been already added to referenceSlotsInfo. */
      uint32_t j = 0;
      for (; j < refSlotsCount; ++j) {
        if (referenceSlotsInfo[j].slotIndex == dpbSlotIndex) {
          Logger::debug(str::format("DPBM: DPB[", dpbSlotIndex, "]: reference picture [", i, "]",
            " (added at ", refSlotsCount, ") already exists at ", j));
          break;
        }
      }
      if (j < refSlotsCount)
        continue;

      if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
        h264DpbSlotInfo[refSlotsCount] =
          { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR };
        h264DpbSlotInfo[refSlotsCount].pStdReferenceInfo = &m_DPB.slots[dpbSlotIndex].h264.stdRefInfo;
        pNext = &h264DpbSlotInfo[refSlotsCount];
      }
      else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
        h265DpbSlotInfo[refSlotsCount] =
          { VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR };
        h265DpbSlotInfo[refSlotsCount].pStdReferenceInfo = &m_DPB.slots[dpbSlotIndex].h265.stdRefInfo;
        pNext = &h265DpbSlotInfo[refSlotsCount];
      }
      else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
        av1DpbSlotInfo[refSlotsCount] =
          { VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_DPB_SLOT_INFO_KHR };
        av1DpbSlotInfo[refSlotsCount].pStdReferenceInfo = &m_DPB.slots[dpbSlotIndex].av1.stdRefInfo;
        pNext = &av1DpbSlotInfo[refSlotsCount];
      }
      else {
        pNext = nullptr;
      }

      pictureResourceInfo[refSlotsCount] =
        { VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR };
      pictureResourceInfo[refSlotsCount].codedOffset      = { 0, 0 };
      pictureResourceInfo[refSlotsCount].codedExtent      = codedExtent;
      pictureResourceInfo[refSlotsCount].baseArrayLayer   = 0; /* "relative to the image subresource range" of the view */
      pictureResourceInfo[refSlotsCount].imageViewBinding = m_DPB.slots[dpbSlotIndex].imageView->handle();

      referenceSlotsInfo[refSlotsCount] =
        { VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, pNext };
      referenceSlotsInfo[refSlotsCount].slotIndex         = dpbSlotIndex;
      referenceSlotsInfo[refSlotsCount].pPictureResource  = &pictureResourceInfo[refSlotsCount];

      ++refSlotsCount;
    }

    /* Destination picture. */
    pNext = nullptr;
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      h264DpbSlotInfo[refSlotsCount] =
        { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR };
      h264DpbSlotInfo[refSlotsCount].pStdReferenceInfo = &dstDPBSlot.h264.stdRefInfo;
      pNext = &h264DpbSlotInfo[refSlotsCount];
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      h265DpbSlotInfo[refSlotsCount] =
        { VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR };
      h265DpbSlotInfo[refSlotsCount].pStdReferenceInfo = &dstDPBSlot.h265.stdRefInfo;
      pNext = &h265DpbSlotInfo[refSlotsCount];
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      av1DpbSlotInfo[refSlotsCount] =
        { VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_DPB_SLOT_INFO_KHR };
      av1DpbSlotInfo[refSlotsCount].pStdReferenceInfo = &dstDPBSlot.av1.stdRefInfo;
      pNext = &av1DpbSlotInfo[refSlotsCount];
    }

    pictureResourceInfo[refSlotsCount] =
      { VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR };
    pictureResourceInfo[refSlotsCount].codedOffset      = { 0, 0 };
    pictureResourceInfo[refSlotsCount].codedExtent      = codedExtent;
    pictureResourceInfo[refSlotsCount].baseArrayLayer   = 0; /* "relative to the image subresource range" of the view */
    pictureResourceInfo[refSlotsCount].imageViewBinding = dstDPBSlot.imageView->handle();

    referenceSlotsInfo[refSlotsCount] =
      { VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR, pNext };
    referenceSlotsInfo[refSlotsCount].slotIndex         = -1;
    referenceSlotsInfo[refSlotsCount].pPictureResource  = &pictureResourceInfo[refSlotsCount];

    /* Begin video coding scope. */
    VkVideoBeginCodingInfoKHR beginCodingInfo =
      { VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR };
    beginCodingInfo.flags                  = 0; /* reserved for future use */
    beginCodingInfo.videoSession           = m_videoSession->handle();
    beginCodingInfo.videoSessionParameters = m_videoSessionParameters->handle();
    beginCodingInfo.referenceSlotCount     = refSlotsCount + 1;
    beginCodingInfo.pReferenceSlots        = referenceSlotsInfo.data();

#ifdef DEBUG
    Logger::debug(str::format("VREF: beginVideoCoding: dstSlotIndex=", dstSlotIndex,
      " ", codedExtent.width, "x", codedExtent.height));
    for (uint32_t i = 0; i < beginCodingInfo.referenceSlotCount; ++i) {
      auto &s = beginCodingInfo.pReferenceSlots[i];
      const DxvkDPBSlot& dpbSlot = m_DPB.slots[s.slotIndex == -1 ? dstSlotIndex : s.slotIndex];

      int32_t FrameNum = -1;
      if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR)
        FrameNum = dpbSlot.h264.stdRefInfo.FrameNum;
      else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR)
        FrameNum = dpbSlot.h265.stdRefInfo.PicOrderCntVal;
      else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
        FrameNum = dpbSlot.av1.stdRefInfo.OrderHint;

      Logger::debug(str::format("VREF:  RefSlot[", i, "]: slotIndex=", s.slotIndex,
        ", FrameNum=", FrameNum,
        ", image=", dpbSlot.imageView->image()->handle(),
        ", view=", s.pPictureResource->imageViewBinding));
    }
#endif

    ctx->beginVideoCodingKHR(&beginCodingInfo);

    if (!m_fControlResetSubmitted) {
      VkVideoCodingControlInfoKHR controlInfo =
        { VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR };
      controlInfo.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;

      ctx->controlVideoCodingKHR(&controlInfo);

      m_fControlResetSubmitted = true;
    }

    /*
     * Setup video decoding parameters 'decodeInfo' (VkVideoDecodeInfoKHR).
     *
     * Reuse first refFramesCount elements in referenceSlotsInfo as pReferenceSlots for decodeVideo.
     * The last element is the destination picture for pSetupReferenceSlot.
     */

    /* Update the destination DPB slot index. It was set to -1 for beginVideoCoding above. */
    referenceSlotsInfo[refSlotsCount].slotIndex = dstSlotIndex;

    /* VkVideoDecodeInfoKHR decodeInfo.pNext */
    pNext = nullptr;

    VkVideoDecodeH264PictureInfoKHR h264PictureInfo =
      { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR };
    VkVideoDecodeH265PictureInfoKHR h265PictureInfo =
      { VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PICTURE_INFO_KHR };
    VkVideoDecodeAV1PictureInfoKHR av1PictureInfo =
      { VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_PICTURE_INFO_KHR };

    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      h264PictureInfo.pStdPictureInfo = &parms.h264.stdH264PictureInfo;
      h264PictureInfo.sliceCount      = parms.sliceOrTileOffsets.size();
      h264PictureInfo.pSliceOffsets   = parms.sliceOrTileOffsets.data();
      pNext = &h264PictureInfo;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      h265PictureInfo.pStdPictureInfo      = &parms.h265.stdPictureInfo;
      h265PictureInfo.sliceSegmentCount    = parms.sliceOrTileOffsets.size();
      h265PictureInfo.pSliceSegmentOffsets = parms.sliceOrTileOffsets.data();
      pNext = &h265PictureInfo;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      av1PictureInfo.pStdPictureInfo               = &parms.av1.stdPictureInfo;
      for (uint32_t i = 0; i < VK_MAX_VIDEO_AV1_REFERENCES_PER_FRAME_KHR; ++i) {
        av1PictureInfo.referenceNameSlotIndices[i] = -1;
        if (i >= parms.refFramesCount)
          continue;

        const DxvkRefFrameInfo& r = parms.refFrames[i];

        itRefFrame = m_DPB.refFrames.find(r.idSurface);
        if (itRefFrame == m_DPB.refFrames.end()) {
          /* Skip invalid reference frame. */
          continue;
        }

        const int32_t dpbSlotIndex = itRefFrame->second.dpbSlotIndex;
        if (dpbSlotIndex == -1) {
          /* Skip invalid reference frame. */
          continue;
        }

        av1PictureInfo.referenceNameSlotIndices[i] = dpbSlotIndex;
      }

      av1PictureInfo.frameHeaderOffset             = 0;
      av1PictureInfo.tileCount                     = parms.av1.tileCount;
      av1PictureInfo.pTileOffsets                  = parms.sliceOrTileOffsets.data();
      av1PictureInfo.pTileSizes                    = parms.sliceOrTileSizes.data();
      pNext = &av1PictureInfo;
    }

    VkVideoDecodeInfoKHR decodeInfo =
      { VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR, pNext };
    decodeInfo.flags               = 0;
    decodeInfo.srcBuffer           = m_bitstreamBuffer->buffer();
    decodeInfo.srcBufferOffset     = offFrame;
    decodeInfo.srcBufferRange      = cbFrame;
    decodeInfo.dstPictureResource  =
      { VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR };
    decodeInfo.dstPictureResource.codedOffset      = { 0, 0 };
    decodeInfo.dstPictureResource.codedExtent      = codedExtent;
    /* "baseArrayLayer relative to the image subresource range the image view specified in
     * imageViewBinding was created with."
     */
    decodeInfo.dstPictureResource.baseArrayLayer   = 0;
    if (fUseDistinctOutputImage) {
      decodeInfo.dstPictureResource.imageViewBinding = m_imageViewDecodeDst->handle();
    }
    else {
      decodeInfo.dstPictureResource.imageViewBinding = dstDPBSlot.imageView->handle();
    }
    decodeInfo.pSetupReferenceSlot = &referenceSlotsInfo[refSlotsCount];
    decodeInfo.referenceSlotCount  = refSlotsCount;
    decodeInfo.pReferenceSlots     = referenceSlotsInfo.data();

#ifdef DEBUG
    Logger::debug(str::format("VREF: decodeVideo: dstSlotIndex=", dstSlotIndex));
    for (uint32_t i = 0; i < decodeInfo.referenceSlotCount; ++i) {
      auto &s = decodeInfo.pReferenceSlots[i];
      const DxvkDPBSlot& dpbSlot = m_DPB.slots[s.slotIndex];

      int32_t FrameNum = -1;
      if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR)
        FrameNum = dpbSlot.h264.stdRefInfo.FrameNum;
      else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR)
        FrameNum = dpbSlot.h265.stdRefInfo.PicOrderCntVal;
      else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
        FrameNum = dpbSlot.av1.stdRefInfo.OrderHint;

      Logger::debug(str::format("VREF:  RefSlot[", i, "]: slotIndex=", s.slotIndex,
        ", FrameNum=", FrameNum,
        ", view=", s.pPictureResource->imageViewBinding));
    }
    Logger::debug(str::format("VREF:  dst: slotIndex=", dstSlotIndex,
      ", FrameNum=", (int32_t)(m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR
                   ? dstDPBSlot.h264.stdRefInfo.FrameNum
                   : (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR)
                   ? dstDPBSlot.h265.stdRefInfo.PicOrderCntVal
                   : (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
                   ? dstDPBSlot.av1.stdRefInfo.OrderHint
                   : -1),
      ", is_ref=", (uint32_t)(m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR
                   ? parms.h264.stdH264PictureInfo.flags.is_reference
                   : (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
                   ? parms.av1.reference_frame_update
                   : 0),
      ", view=", dstDPBSlot.imageView->handle()));
#endif

    ctx->decodeVideoKHR(&decodeInfo);

    /*
     * End video decoding.
     */
    VkVideoEndCodingInfoKHR endCodingInfo =
      { VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR};

    ctx->endVideoCodingKHR(&endCodingInfo);

    /*
     * Copy the decoded picture to the output view. Either from DPB slot or from m_imageDecodeDst.
     */
    Rc<DxvkImage> decodedPicture;
    uint32_t decodedArrayLayer;
    VkImageLayout decodedPictureLayout;
    if (fUseDistinctOutputImage) {
      decodedPicture       = m_imageDecodeDst;
      decodedArrayLayer    = 0;
      decodedPictureLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
    }
    else {
      decodedPicture       = dstDPBSlot.image;
      decodedArrayLayer    = dstDPBSlot.baseArrayLayer;
      decodedPictureLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR;
    }

    /* Prepare parameters for copying decoded image to the output view. */
    VkExtent3D outputExtent = m_outputImageView->mipLevelExtent(0u);
    VkExtent3D copyExtent = {
      std::min(outputExtent.width, m_DPB.decodedPictureExtent.width),
      std::min(outputExtent.height, m_DPB.decodedPictureExtent.height),
      1 };

    std::array<VkImageCopy2, 2> regions{{
      { VK_STRUCTURE_TYPE_IMAGE_COPY_2 },
      { VK_STRUCTURE_TYPE_IMAGE_COPY_2 }}};
    /* Y plane. */
    regions[0].srcSubresource.aspectMask       = VK_IMAGE_ASPECT_PLANE_0_BIT;
    //regions[0].srcSubresource.mipLevel       = 0;
    regions[0].srcSubresource.baseArrayLayer   = decodedArrayLayer;
    regions[0].srcSubresource.layerCount       = 1;
    //regions[0].srcOffset                     = { 0, 0, 0 };
    regions[0].dstSubresource.aspectMask       = VK_IMAGE_ASPECT_PLANE_0_BIT;
    //regions[0].dstSubresource.mipLevel       = 0;
    regions[0].dstSubresource.baseArrayLayer   = m_outputImageView->info().layerIndex;
    regions[0].dstSubresource.layerCount       = 1;
    //regions[0].dstOffset                     = { 0, 0, 0 };
    regions[0].extent                          = copyExtent;

    /* CbCr plane at half resolution. */
    regions[1].srcSubresource.aspectMask       = VK_IMAGE_ASPECT_PLANE_1_BIT;
    //regions[1].srcSubresource.mipLevel       = 0;
    regions[1].srcSubresource.baseArrayLayer   = decodedArrayLayer;
    regions[1].srcSubresource.layerCount       = 1;
    //regions[1].srcOffset                     = { 0, 0, 0 };
    regions[1].dstSubresource.aspectMask       = VK_IMAGE_ASPECT_PLANE_1_BIT;
    //regions[1].dstSubresource.mipLevel       = 0;
    regions[1].dstSubresource.baseArrayLayer   = m_outputImageView->info().layerIndex;
    regions[1].dstSubresource.layerCount       = 1;
    //regions[1].dstOffset                     = { 0, 0, 0 };
    regions[1].extent                          = { copyExtent.width / 2, copyExtent.height / 2, 1 };

    VkCopyImageInfo2 copyImageInfo =
      { VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2 };
    copyImageInfo.srcImage       = decodedPicture->handle();
    copyImageInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copyImageInfo.dstImage       = m_outputImageView->image()->handle();
    copyImageInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copyImageInfo.regionCount    = 2;
    copyImageInfo.pRegions       = regions.data();

    if (m_profile.videoQueueHasTransfer) {
      /* Wait for the decoded image to be available as a transfer source. */
      barrier.srcStageMask        = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
      barrier.srcAccessMask       = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
      barrier.dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
      barrier.dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT;
      barrier.oldLayout           = decodedPictureLayout;
      barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image               = decodedPicture->handle();
      barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseMipLevel   = 0;
      barrier.subresourceRange.levelCount     = 1;
      barrier.subresourceRange.baseArrayLayer = decodedArrayLayer;
      barrier.subresourceRange.layerCount     = 1;

      /* Output image is already in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL. */

      dependencyInfo =
        { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
      dependencyInfo.imageMemoryBarrierCount = 1;
      dependencyInfo.pImageMemoryBarriers    = &barrier;

      ctx->emitPipelineBarrier(DxvkCmdBuffer::VDecBuffer, &dependencyInfo);

      /* Copy decoded image -> output image. */
      ctx->emitCopyImage(DxvkCmdBuffer::VDecBuffer, &copyImageInfo);

      /* Restore layout of the decoded image. */
      barrier.srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
      barrier.srcAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT;
      barrier.dstStageMask        = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
      barrier.dstAccessMask       = VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR;
      barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.newLayout           = decodedPictureLayout;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image               = decodedPicture->handle();
      barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseMipLevel   = 0;
      barrier.subresourceRange.levelCount     = 1;
      barrier.subresourceRange.baseArrayLayer = decodedArrayLayer;
      barrier.subresourceRange.layerCount     = 1;

      /* Output image will be release back to the graphics queue in EndFrame. */

      dependencyInfo =
        { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
      dependencyInfo.imageMemoryBarrierCount = 1;
      dependencyInfo.pImageMemoryBarriers    = &barrier;

      ctx->emitPipelineBarrier(DxvkCmdBuffer::VDecBuffer, &dependencyInfo);
    }
    else {
      /*
       * Copy decoded picture to the output view.
       * Video queue can't do transfers.
       */
      /* Transfer the decoded image ownership to the graphics queue. */
      this->TransferImageQueueOwnership(ctx, decodedPicture, decodedArrayLayer,
        DxvkCmdBuffer::VDecBuffer,
        m_device->queues().videoDecode.queueFamily,
        VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
        VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,
        decodedPictureLayout,
        DxvkCmdBuffer::InitBuffer,
        m_device->queues().graphics.queueFamily,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

      /* Prepare output image for transfer destination. */
      barrier.srcStageMask        = VK_PIPELINE_STAGE_2_NONE;
      barrier.srcAccessMask       = 0;
      barrier.dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
      barrier.dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
      barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED; /* 'The contents ... may be discarded.' */
      barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image               = m_outputImageView->image()->handle();
      barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseMipLevel   = 0;
      barrier.subresourceRange.levelCount     = 1;
      barrier.subresourceRange.baseArrayLayer = m_outputImageView->info().layerIndex;
      barrier.subresourceRange.layerCount     = 1;

      dependencyInfo =
        { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
      dependencyInfo.imageMemoryBarrierCount = 1;
      dependencyInfo.pImageMemoryBarriers    = &barrier;

      ctx->emitPipelineBarrier(DxvkCmdBuffer::InitBuffer, &dependencyInfo);

      /* Copy decoded image -> output image. */
      ctx->emitCopyImage(DxvkCmdBuffer::InitBuffer, &copyImageInfo);

      /* Restore layout of the output image. */
      barrier.srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
      barrier.srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT;
      barrier.dstStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT_KHR;
      barrier.dstAccessMask       = 0;
      barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.newLayout           = m_outputImageView->image()->info().layout; /* VK_IMAGE_LAYOUT_GENERAL. */
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image               = m_outputImageView->image()->handle();
      barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.baseMipLevel   = 0;
      barrier.subresourceRange.levelCount     = 1;
      barrier.subresourceRange.baseArrayLayer = m_outputImageView->info().layerIndex;
      barrier.subresourceRange.layerCount     = 1;

      dependencyInfo =
        { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
      dependencyInfo.imageMemoryBarrierCount = 1;
      dependencyInfo.pImageMemoryBarriers    = &barrier;

      ctx->emitPipelineBarrier(DxvkCmdBuffer::InitBuffer, &dependencyInfo);

      /* Return the decoded image ownership back to the video queue. */
      this->TransferImageQueueOwnership(ctx, decodedPicture, decodedArrayLayer,
        DxvkCmdBuffer::InitBuffer,
        m_device->queues().graphics.queueFamily,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        DxvkCmdBuffer::VDecBuffer,
        m_device->queues().videoDecode.queueFamily,
        VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
        VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR | VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,
        decodedPictureLayout);
    }

    /*
     * Make sure that the involved objects are alive during command buffer execution.
     */
    ctx->trackObject(m_videoSession);
    ctx->trackObject(m_videoSessionParameters);
    ctx->trackResource(DxvkAccess::Write, m_outputImageView->image());
    if (m_profile.videoCapabilities.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR) {
      for (auto &slot: m_DPB.slots)
        ctx->trackResource(slot.isActive? DxvkAccess::Read : DxvkAccess::Write, slot.image);
    }
    else
      ctx->trackResource(DxvkAccess::Write, dstDPBSlot.image); /* Same image in every slot. */
    if (fUseDistinctOutputImage)
      ctx->trackResource(DxvkAccess::Write, m_imageDecodeDst);
    ctx->trackObject(m_bitstreamBuffer);

    /*
     * Keep reference picture and update its information if necessary.
     */
    bool activateSlot = false;
    if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
      activateSlot = parms.h264.stdH264PictureInfo.flags.is_reference != 0;
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
      activateSlot = true; /* It is not known yet if the picture is a reference. */
    }
    else if (m_profile.profileInfo.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
      activateSlot = parms.av1.reference_frame_update;
      if (activateSlot) {
        for (uint32_t i = 0; i < STD_VIDEO_AV1_NUM_REF_FRAMES; ++i)
          dstDPBSlot.av1.stdRefInfo.SavedOrderHints[i] = parms.av1.stdPictureInfo.OrderHints[i];
      }
    }
    if (activateSlot) {
      dstDPBSlot.isActive = true;

      /* Remember the surface id. */
      dstDPBSlot.idSurface             = parms.idSurface;
      m_DPB.refFrames[parms.idSurface] = { dstSlotIndex, { parms.idSurface } };

      m_DPB.idxCurrentDPBSlot = (m_DPB.idxCurrentDPBSlot + 1) % m_DPB.slots.size();
    }
  }

}

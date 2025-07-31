/* $Id: dxvk_video_decoder.h 110305 2025-07-18 12:42:15Z vitali.pelenjow@oracle.com $ */
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


#pragma once

#include "dxvk_context.h"
#include "dxvk_memory.h"
#include "dxvk_image.h"
#include "dxvk_fence.h"

namespace dxvk {

  class DxvkDevice;

#define DXVK_VIDEO_DECODER_SURFACE_INVALID 0xff

  struct DxvkVideoDecodeProfileInfo {
    const char *                          profileName;

    /* Vulkan profile specific information. profileInfo.pNext */
    union {
      VkVideoDecodeH264ProfileInfoKHR     h264ProfileInfo;
      VkVideoDecodeH265ProfileInfoKHR     h265ProfileInfo;
      VkVideoDecodeAV1ProfileInfoKHR      av1ProfileInfo;
    };
    /* Vulkan profile info. */
    VkVideoProfileInfoKHR                 profileInfo;

    union {
      VkVideoDecodeH264CapabilitiesKHR    decodeH264Capabilities;
      VkVideoDecodeH265CapabilitiesKHR    decodeH265Capabilities;
      VkVideoDecodeAV1CapabilitiesKHR     decodeAV1Capabilities;
    };
    VkVideoDecodeCapabilitiesKHR          decodeCapabilities;
    VkVideoCapabilitiesKHR                videoCapabilities;

    VkVideoFormatPropertiesKHR            dpbFormatProperties;

    bool                                  videoQueueHasTransfer;
  };


  struct DxvkRefFrameInfo {
    uint8_t      idSurface;
    union {
      struct {
        uint8_t  longTermReference : 1;
        uint8_t  usedForReference : 2;
        uint8_t  nonExistingFrame : 1;
        uint16_t frame_num;
        int32_t  PicOrderCnt[2];
      } h264;
      struct {
        uint8_t  longTermReference : 1;
        int32_t  PicOrderCntVal;
      } h265;
      struct {
        uint8_t  frame_name;
      } av1;
    };
  };


  struct DxvkVideoDecodeInputParameters {
    union {
      struct {
        StdVideoH264SequenceParameterSet sps;
        int32_t                          spsOffsetForRefFrame;

        StdVideoH264PictureParameterSet  pps;
        StdVideoH264ScalingLists         ppsScalingLists;

        StdVideoDecodeH264PictureInfo    stdH264PictureInfo;
        StdVideoDecodeH264ReferenceInfo  stdH264ReferenceInfo;

        uint8_t                          nal_unit_type;
      } h264;
      struct {
        StdVideoH265VideoParameterSet    vps;
        StdVideoH265ProfileTierLevel     vpsProfileTierLevel;
        StdVideoH265SequenceParameterSet sps;
        StdVideoH265DecPicBufMgr         spsDecPicBufMgr;
        StdVideoH265PictureParameterSet  pps;
        StdVideoH265ScalingLists         ppsScalingLists;

        StdVideoDecodeH265PictureInfo    stdPictureInfo;
        StdVideoDecodeH265ReferenceInfo  stdReferenceInfo;
      } h265;
      struct {
        StdVideoAV1SequenceHeader        stdSequenceHeader;
        StdVideoAV1ColorConfig           stdColorConfig;

        StdVideoDecodeAV1PictureInfo     stdPictureInfo;
        StdVideoAV1TileInfo              stdTileInfo;
        uint16_t                         MiColStarts[64];
        uint16_t                         MiRowStarts[64];
        uint16_t                         WidthInSbsMinus1[64];
        uint16_t                         HeightInSbsMinus1[64];
        StdVideoAV1Quantization          stdQuantization;
        StdVideoAV1Segmentation          stdSegmentation;
        StdVideoAV1LoopFilter            stdLoopFilter;
        StdVideoAV1CDEF                  stdCDEF;
        StdVideoAV1LoopRestoration       stdLoopRestoration;
        StdVideoAV1GlobalMotion          stdGlobalMotion;
        StdVideoAV1FilmGrain             stdFilmGrain;

        uint32_t                         tileCount;

        StdVideoDecodeAV1ReferenceInfo   stdReferenceInfo;

        uint8_t                          RefFrameMapTextureIndex[8];

        bool                             reference_frame_update;
      } av1;
    };

    std::vector<uint32_t>              sliceOrTileOffsets;
    std::vector<uint32_t>              sliceOrTileSizes;

    uint8_t                            idSurface;
    uint32_t                           refFramesCount;
    std::array<DxvkRefFrameInfo, 16>   refFrames;

    VkDeviceSize                       bitstreamLength;
    std::vector<uint8_t>               bitstream;
  };


  /**
   * \brief Video session handle
   * 
   * Manages a handle of video session object.
   */
  class DxvkVideoSessionHandle : public RcObject {

  public:
    DxvkVideoSessionHandle(
      const Rc<DxvkDevice>& device);
    ~DxvkVideoSessionHandle();

    void create(
      const VkVideoSessionCreateInfoKHR& sessionCreateInfo);

    VkVideoSessionKHR handle() const {
      return m_videoSession;
    }

  private:

    Rc<DxvkDevice>              m_device;
    VkVideoSessionKHR           m_videoSession = VK_NULL_HANDLE;

  };


  /**
   * \brief Video session parameters handle
   * 
   * Manages a handle of video session parameters object.
   */
  class DxvkVideoSessionParametersHandle : public RcObject {

  public:
    DxvkVideoSessionParametersHandle(
      const Rc<DxvkDevice>& device);
    ~DxvkVideoSessionParametersHandle();

    void create(
      const Rc<DxvkVideoSessionHandle>& videoSession,
      const void *pDecoderSessionParametersCreateInfo);

    VkVideoSessionParametersKHR handle() const {
      return m_videoSessionParameters;
    }

  private:

    Rc<DxvkDevice>              m_device;
    /* Hold a reference because session must be deleted after parameters. */
    Rc<DxvkVideoSessionHandle>  m_videoSession;
    VkVideoSessionParametersKHR m_videoSessionParameters = VK_NULL_HANDLE;

  };


  /**
   * \brief Video bitstream buffer
   * 
   * Manages a buffer for source bitstream for the decoder.
   */
  class DxvkVideoBitstreamBuffer : public RcObject {

  public:
    DxvkVideoBitstreamBuffer(
      const Rc<DxvkDevice>& device,
      DxvkMemoryAllocator& memAlloc);
    ~DxvkVideoBitstreamBuffer();

    void create(
      const VkVideoProfileListInfoKHR &profileListInfo,
      VkDeviceSize size);

    VkBuffer buffer() const {
      return m_buffer;
    }

    void* mapPtr(VkDeviceSize offset) const  {
      return m_mapPtr ? m_mapPtr + offset : nullptr;
    }

    VkDeviceSize length() const {
      return m_length;
    }

  private:

    Rc<DxvkDevice>              m_device;
    DxvkMemoryAllocator&        m_memAlloc;

    VkBuffer                    m_buffer = VK_NULL_HANDLE;
    Rc<DxvkResourceAllocation>  m_allocation;

    /* Shortcuts. */
    uint8_t*                    m_mapPtr = nullptr;
    VkDeviceSize                m_length = 0;
  };


  /**
   * \brief Video decoder
   *
   * Provides decoding.
   */
  class DxvkVideoDecoder : public RcObject {
    
  public:

    DxvkVideoDecoder(
      const Rc<DxvkDevice>& device,
            DxvkMemoryAllocator& memAlloc,
      const DxvkVideoDecodeProfileInfo& profile,
            uint32_t sampleWidth,
            uint32_t sampleHeight,
            VkFormat outputFormat,
            uint32_t bitstreamBufferSize);
    ~DxvkVideoDecoder();

    void BeginFrame(
      DxvkContext* ctx,
      const Rc<DxvkImageView>& imageView);

    void EndFrame(
      DxvkContext* ctx);

    void Decode(
      DxvkContext* ctx,
      DxvkVideoDecodeInputParameters parms);

    VkVideoCodecOperationFlagsKHR GetVideoCodecOperation() const {
      return m_profile.profileInfo.videoCodecOperation;
    }

  private:

    /* Creation parameters. */
    Rc<DxvkDevice>                      m_device;
    DxvkMemoryAllocator&                m_memAlloc;
    DxvkVideoDecodeProfileInfo          m_profile;
    uint32_t                            m_sampleWidth;
    uint32_t                            m_sampleHeight;
    VkFormat                            m_outputFormat;

    /* Caps derived from Vulkan caps. */
    struct DxvkVideoDecodeCapabilities {
      /* Whether vkCmdDecodeVideo output image can be not from DPB. */
      bool                              distinctOutputImage = false;
    };
    DxvkVideoDecodeCapabilities         m_caps;

    /* Vulkan video decoding objects. */
    Rc<DxvkVideoSessionHandle>          m_videoSession;
    std::vector< Rc<DxvkResourceAllocation> > m_videoSessionMemory;
    Rc<DxvkVideoSessionParametersHandle> m_videoSessionParameters;

    /* Whether Vulkan video decoder requires an initial reset. */
    bool                                m_fControlResetSubmitted = false;

    /* Provided in BeginFrame. */
    Rc<DxvkImageView>                   m_outputImageView = nullptr;

    /* Ring buffer for incoming encoded video data. */
    Rc<DxvkVideoBitstreamBuffer>        m_bitstreamBuffer;
    /* Ring buffer offset. */
    uint32_t                            m_offFree = 0;

    /* Decoded Picture Buffer.
     * Contains reconstructed pictures including the currently decoded frame.
     */
    struct DxvkDPBSlot {
      /* These fields will be assigned during video decoder initialization. */
      Rc<DxvkImage>                     image = nullptr;
      Rc<DxvkImageView>                 imageView = nullptr;
      uint32_t                          baseArrayLayer = 0;

      /* These fields are updated during decoding process. */
      bool                              isActive;
      union {
        struct {
          StdVideoDecodeH264ReferenceInfo stdRefInfo;
        } h264;
        struct {
          StdVideoDecodeH265ReferenceInfo stdRefInfo;
        } h265;
        struct {
          StdVideoDecodeAV1ReferenceInfo  stdRefInfo;
        } av1;
        uint8_t                           stdRefInfoData[
          std::max(sizeof(StdVideoDecodeH264ReferenceInfo),
          std::max(sizeof(StdVideoDecodeH265ReferenceInfo),
                   sizeof(StdVideoDecodeAV1ReferenceInfo)))];
      };
      uint8_t                           idSurface;

      void deactivate() {
        this->isActive = false;
        memset(this->stdRefInfoData, 0, sizeof(this->stdRefInfoData));
        this->idSurface = DXVK_VIDEO_DECODER_SURFACE_INVALID;
      }
    };

    /* Information about an uncompressed surface. */
    struct DxvkRefFrame {
      int32_t                           dpbSlotIndex = -1;
      DxvkRefFrameInfo                  refFrameInfo = {};
    };

    struct DxvkDPB {
      std::vector<DxvkDPBSlot>          slots;
      int                               idxCurrentDPBSlot = 0;
      bool                              fOverflow = false;

      /* Size of DPB images. */
      VkExtent3D                        decodedPictureExtent = { 0, 0, 1 };

      /* Uncompressed surface id (idSurface) -> frame information. */
      std::map<uint8_t, DxvkRefFrame>   refFrames;

      void reset() {
        for (auto &slot: this->slots)
          slot.deactivate();
        this->idxCurrentDPBSlot = 0;
        this->fOverflow = false;
        this->refFrames.clear();
      }
    };
    struct DxvkDPB                      m_DPB;

    /* Optional decode destination image if m_caps.distinctOutputImage is true. */
    Rc<DxvkImage>                       m_imageDecodeDst = nullptr;
    Rc<DxvkImageView>                   m_imageViewDecodeDst = nullptr;

    /* Store incoming SPS and PPS data and reuse its ids. */
    struct DxvkParameterSetCache
    {
      uint32_t                          vpsCount = 0;
      uint32_t                          spsCount = 0;
      uint32_t                          ppsCount = 0;
      uint32_t                          updateSequenceCount = 0;
      struct {
        std::array<StdVideoH264SequenceParameterSet, 32>  sps;
        std::array<StdVideoH264PictureParameterSet, 256>  pps;
      } h264;
      struct {
        std::array<StdVideoH265VideoParameterSet, 32>     vps;
        std::array<StdVideoH265ProfileTierLevel, 32>      vpsProfileTierLevel;
        std::array<StdVideoH265SequenceParameterSet, 32>  sps;
        std::array<StdVideoH265PictureParameterSet, 256>  pps;
      } h265;
      struct {
        StdVideoAV1SequenceHeader                         stdSequenceHeader;
      } av1;
    };
    DxvkParameterSetCache               m_parameterSetCache;

    Rc<DxvkFence>                       m_queueOwnershipTransferFence;
    std::atomic<uint64_t>               m_queueOwnershipTransferValue = 0;

    /*
     * Internal methods.
     */
    VkResult UpdateSessionParametersH264(
      DxvkVideoDecodeInputParameters *pParms);

    VkResult UpdateSessionParametersH265(
      DxvkVideoDecodeInputParameters *pParms);

    VkResult UpdateSessionParametersAV1(
      DxvkVideoDecodeInputParameters *pParms);

    void TransferImageQueueOwnership(
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
      VkImageLayout         newLayout);
  };
  
}

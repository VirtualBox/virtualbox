#include "config/config.hpp"
#include "d3d11_fence.hpp"
#include "d3d11_private.h"
#include "d3d11_query.hpp"
#include "dxmt_command_queue.hpp"
#include "d3d11_context_impl.cpp"
#include "dxmt_context.hpp"
#include "dxmt_staging.hpp"

namespace dxmt {
struct ContextInternalState {
  using device_mutex_t = d3d11_device_mutex;
  CommandQueue &cmd_queue;
  bool has_dirty_op_since_last_event = false;
};


template<typename Object> Rc<Object> forward_rc(Rc<Object>& obj) {
  return std::move(obj);
}

using ImmediateContextBase = MTLD3D11DeviceContextImplBase<ContextInternalState>;

template <>
template <CommandWithContext<ArgumentEncodingContext> cmd>
void
ImmediateContextBase::EmitST(cmd &&fn) {
  CommandChunk *chk = ctx_state.cmd_queue.CurrentChunk();
  chk->emitcc(std::forward<cmd>(fn));
}

template <>
template <CommandWithContext<ArgumentEncodingContext> cmd>
void
ImmediateContextBase::EmitOP(cmd &&fn) {
  CommandChunk *chk = ctx_state.cmd_queue.CurrentChunk();
  chk->emitcc(std::forward<cmd>(fn));
  ctx_state.has_dirty_op_since_last_event = true;
}

template <>
template <typename T>
moveonly_list<T>
ImmediateContextBase::AllocateCommandData(size_t n) {
  CommandChunk *chk = ctx_state.cmd_queue.CurrentChunk();
  return moveonly_list<T>((T *)chk->allocate_cpu_heap(sizeof(T) * n, alignof(T)), n);
}

template <>
std::tuple<void *, WMT::Buffer, uint64_t>
ImmediateContextBase::AllocateStagingBuffer(size_t size, size_t alignment) {
  return ctx_state.cmd_queue.AllocateStagingBuffer(size, alignment);
}

template <>
void
ImmediateContextBase::UseCopyDestination(Rc<StagingResource> &staging) {
  staging->useCopyDestination(ctx_state.cmd_queue.CurrentSeqId());
}

template <>
void
ImmediateContextBase::UseCopySource(Rc<StagingResource> &staging) {
  staging->useCopySource(ctx_state.cmd_queue.CurrentSeqId());
}

class MTLD3D11ImmediateContext : public ImmediateContextBase {
public:
  MTLD3D11ImmediateContext(MTLD3D11Device *pDevice, CommandQueue &cmd_queue) :
      ImmediateContextBase(pDevice, ctx_state, pDevice->mutex),
      cmd_queue(cmd_queue),
      ctx_state({cmd_queue}),
      d3dmt_(this, mutex) {
        ignore_map_flag_no_wait_ = Config::getInstance().getOption<bool>("d3d11.ignoreMapFlagNoWait", false);
      }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) override {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(ID3D11Multithread)
        && !(m_parent->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)) {
      *ppvObject = ref(&d3dmt_);
      return S_OK;
    }

    return ImmediateContextBase::QueryInterface(riid, ppvObject);
  }

  ULONG STDMETHODCALLTYPE
  AddRef() override {
    uint32_t refCount = this->refcount++;
    if (unlikely(!refCount))
      this->m_parent->AddRef();

    return refCount + 1;
  }

  ULONG STDMETHODCALLTYPE
  Release() override {
    uint32_t refCount = --this->refcount;
    D3D11_ASSERT(refCount != ~0u && "try to release a 0 reference object");
    if (unlikely(!refCount))
      this->m_parent->Release();

    return refCount;
  }

  HRESULT
  STDMETHODCALLTYPE
  Map(ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags,
      D3D11_MAPPED_SUBRESOURCE *pMappedResource) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    if (unlikely(!pResource || !pMappedResource))
      return E_INVALIDARG;
    UINT buffer_length = 0, &row_pitch = buffer_length;
    UINT bind_flag = 0, &depth_pitch = bind_flag;
    auto current_seq_id = cmd_queue.CurrentSeqId();
    auto coherent_seq_id = cmd_queue.CoherentSeqId();
    if (auto dynamic = GetDynamicBuffer(pResource, &buffer_length, &bind_flag)) {
      switch (MapType) {
      case D3D11_MAP_READ:
      case D3D11_MAP_WRITE:
      case D3D11_MAP_READ_WRITE:
        return E_INVALIDARG;
      case D3D11_MAP_WRITE_DISCARD: {
        if (bind_flag & D3D11_BIND_VERTEX_BUFFER) {
          state_.InputAssembler.VertexBuffers.set_dirty();
        }
        if (bind_flag & D3D11_BIND_CONSTANT_BUFFER) {
          for (auto &stage : state_.ShaderStages) {
            stage.ConstantBuffers.set_dirty();
          }
        }
        if (bind_flag & D3D11_BIND_SHADER_RESOURCE) {
          for (auto &stage : state_.ShaderStages) {
            stage.SRVs.set_dirty();
          }
        }

        if (auto next_sub = dynamic->nextSuballocation()) {
          EmitST([allocation = dynamic->immediateName(), next_sub](ArgumentEncodingContext &enc) mutable {
            allocation->useSuballocation(next_sub);
          });
        } else {
          dynamic->updateImmediateName(current_seq_id, dynamic->allocate(coherent_seq_id), 0, false);
          EmitST([allocation = dynamic->immediateName(), buffer = Rc(dynamic->buffer)](ArgumentEncodingContext &enc) mutable {
            allocation->useSuballocation(0);
            auto _ = buffer->rename(forward_rc(allocation));
          });
        }

        pMappedResource->pData = dynamic->immediateMappedMemory();
        pMappedResource->RowPitch = buffer_length;
        pMappedResource->DepthPitch = buffer_length;
        break;
      }
      case D3D11_MAP_WRITE_NO_OVERWRITE: {
        pMappedResource->pData = dynamic->immediateMappedMemory();
        pMappedResource->RowPitch = buffer_length;
        pMappedResource->DepthPitch = buffer_length;
        break;
      }
      }
      return S_OK;
    }
    if (auto dynamic = GetDynamicTexture(pResource, &row_pitch, &depth_pitch)) {
      switch (MapType) {
      case D3D11_MAP_READ:
      case D3D11_MAP_WRITE:
      case D3D11_MAP_READ_WRITE:
        return E_INVALIDARG;
      case D3D11_MAP_WRITE_DISCARD: {
        for (auto &stage : state_.ShaderStages) {
          stage.SRVs.set_dirty();
        }

        dynamic->updateImmediateName(current_seq_id, dynamic->allocate(coherent_seq_id), false);
        EmitST([allocation = dynamic->immediateName(),
              texture = Rc(dynamic->texture)](ArgumentEncodingContext &enc) mutable {
          auto _ = texture->rename(forward_rc(allocation));
        });

        pMappedResource->pData = dynamic->mappedMemory();
        pMappedResource->RowPitch = row_pitch;
        pMappedResource->DepthPitch = depth_pitch;
        break;
      }
      case D3D11_MAP_WRITE_NO_OVERWRITE: {
        pMappedResource->pData = dynamic->mappedMemory();
        pMappedResource->RowPitch = row_pitch;
        pMappedResource->DepthPitch = depth_pitch;
        break;
      }
      }
      return S_OK;
    }
    if (auto staging = GetStagingResource(pResource, Subresource)) {
      if (MapType > 3 || MapType == 0)
          return E_INVALIDARG;

      if (ignore_map_flag_no_wait_)
        MapFlags &= ~D3D11_MAP_FLAG_DO_NOT_WAIT;

      while (true) {
        auto result = staging->tryMap(coherent_seq_id, MapType & D3D11_MAP_READ, MapType & D3D11_MAP_WRITE);
        if (result == StagingMapResult::Mapped)
          return E_FAIL;
        if (result == StagingMapResult::Renamable) {
          // when write to a buffer that is gpu-readonly
          auto next_name = staging->allocate(coherent_seq_id);
          // can't guarantee a full overwrite
          std::memcpy(staging->mappedMemory(next_name), staging->mappedImmediateMemory(), staging->length);
          staging->updateImmediateName(current_seq_id, next_name);
          EmitST([staging, next_name](ArgumentEncodingContext &enc) mutable { staging->encoding_name = next_name; });
          result = StagingMapResult::Mappable;
        }
        if (result == StagingMapResult::Mappable) {
          TRACE("staging map ready");
          pMappedResource->pData = staging->mappedImmediateMemory();
          pMappedResource->RowPitch = staging->bytesPerRow;
          pMappedResource->DepthPitch = staging->bytesPerImage;
          return S_OK;
        }
        if (MapFlags & D3D11_MAP_FLAG_DO_NOT_WAIT) {
          return DXGI_ERROR_WAS_STILL_DRAWING;
        }
        // even it's in a while loop
        // only the first flush will have effect
        // and the following calls are essentially no-op
        Flush();
        TRACE("staging map block");
        auto& statistics = cmd_queue.CurrentFrameStatistics();
        auto t0 = clock::now();
        cmd_queue.WaitCPUFence(coherent_seq_id + uint64_t(result));
        auto t1 = clock::now();
        statistics.sync_count++;
        statistics.sync_interval += (t1 - t0);
        current_seq_id = cmd_queue.CurrentSeqId();
        coherent_seq_id = cmd_queue.CoherentSeqId();
      };
    };
    if (pMappedResource == nullptr) {
      UNIMPLEMENTED("unimplemented USAGE_DEFAULT resource mapping");
    }
    return E_INVALIDARG;
  }

  void
  STDMETHODCALLTYPE
  Unmap(ID3D11Resource *pResource, UINT Subresource) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    if (unlikely(!pResource))
      return;
    if (auto staging = GetStagingResource(pResource, Subresource)) {
      staging->unmap();
    };
  }

  void
  STDMETHODCALLTYPE
  Begin(ID3D11Asynchronous *pAsync) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    // in theory pAsync could be any of them: { Query, Predicate, Counter }.
    // However `Predicate` and `Counter` are not supported at all
    D3D11_QUERY_DESC desc;
    ((ID3D11Query *)pAsync)->GetDesc(&desc);
    switch (desc.Query) {
    case D3D11_QUERY_TIMESTAMP_DISJOINT:
    case D3D11_QUERY_TIMESTAMP:
    case D3D11_QUERY_EVENT:
      break;
    case D3D11_QUERY_OCCLUSION:
    case D3D11_QUERY_OCCLUSION_PREDICATE: {
      auto query = static_cast<IMTLD3DOcclusionQuery *>(pAsync);
      if (query->Begin())
        EmitST([query_ = query->__query()](ArgumentEncodingContext &enc) mutable {
          enc.beginVisibilityResultQuery(std::move(query_));
        });
      break;
    }
    case D3D11_QUERY_PIPELINE_STATISTICS: {
      // ignore
      break;
    }
    default:
      ERR("Unknown query type ", desc.Query);
      break;
    }
  }

  // See Begin()
  void
  STDMETHODCALLTYPE
  End(ID3D11Asynchronous *pAsync) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    D3D11_QUERY_DESC desc;
    ((ID3D11Query *)pAsync)->GetDesc(&desc);
    switch (desc.Query) {
    case D3D11_QUERY_TIMESTAMP:
    case D3D11_QUERY_TIMESTAMP_DISJOINT:
    case D3D11_QUERY_EVENT: {
      if (ctx_state.has_dirty_op_since_last_event) {
        auto event_id = cmd_queue.GetNextEventSeqId();
        static_cast<MTLD3D11EventQuery *>(pAsync)->Issue(event_id);
        InvalidateCurrentPass(true);
        EmitOP([event_id](ArgumentEncodingContext &enc) mutable {
          enc.signalEvent(event_id);
        });
        promote_flush = true;
        ctx_state.has_dirty_op_since_last_event = false;
      } else {
        static_cast<MTLD3D11EventQuery *>(pAsync)->Issue(cmd_queue.GetCurrentEventSeqId());
      }
      break;
    }
    case D3D11_QUERY_OCCLUSION:
    case D3D11_QUERY_OCCLUSION_PREDICATE: {
      auto query = static_cast<IMTLD3DOcclusionQuery *>(pAsync);
      if (query->End())
        EmitST([qeury_ = query->__query()](ArgumentEncodingContext &enc) mutable {
          enc.endVisibilityResultQuery(std::move(qeury_));
        });
      promote_flush = true;
      break;
    }
    case D3D11_QUERY_PIPELINE_STATISTICS: {
      // ignore
      break;
    }
    default:
      ERR("Unknown query type ", desc.Query);
      break;
    }
  }

  HRESULT
  STDMETHODCALLTYPE
  GetData(ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags) override {
    if (!pAsync || (DataSize && !pData))
      return E_INVALIDARG;

    // Allow dataSize to be zero
    if (DataSize && DataSize != pAsync->GetDataSize())
      return E_INVALIDARG;

    HRESULT hr = S_FALSE;

    D3D11_QUERY_DESC desc;
    ((ID3D11Query *)pAsync)->GetDesc(&desc);
    switch (desc.Query) {
    case D3D11_QUERY_EVENT:
    case D3D11_QUERY_TIMESTAMP:
    case D3D11_QUERY_TIMESTAMP_DISJOINT: {
      switch (static_cast<MTLD3D11EventQuery *>(pAsync)->CheckEventState(cmd_queue.SignaledEventSeqId())) {
      case EventState::Pending:
        break;
      case EventState::Stall:
        GetDataFlags &= ~D3D11_ASYNC_GETDATA_DONOTFLUSH;
        break;
      case EventState::Signaled:
        hr = S_OK;
        break;
      }
      break;
    }
    default:
      break;
    }
    switch (desc.Query) {
    case D3D11_QUERY_EVENT: {
      if (pData)
        *static_cast<BOOL *>(pData) = (hr == S_OK);
      break;
    }
    case D3D11_QUERY_OCCLUSION: {
      uint64_t null_data;
      uint64_t *data_ptr = pData ? (uint64_t *)pData : &null_data;
      hr = ((IMTLD3DOcclusionQuery *)pAsync)->GetData(data_ptr);
      break;
    }
    case D3D11_QUERY_OCCLUSION_PREDICATE: {
      BOOL null_data;
      BOOL *data_ptr = pData ? (BOOL *)pData : &null_data;
      hr = ((IMTLD3DOcclusionQuery *)pAsync)->GetData(data_ptr);
      break;
    }
    case D3D11_QUERY_TIMESTAMP: {
      if (pData)
        *static_cast<UINT64 *>(pData) = 0;
      break;
    }
    case D3D11_QUERY_TIMESTAMP_DISJOINT: {
      if (pData) {
        (*static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT *>(pData)) = {1'000'000'000, FALSE};
      }
      break;
    }
    case D3D11_QUERY_PIPELINE_STATISTICS: {
      if (pData) {
        (*static_cast<D3D11_QUERY_DATA_PIPELINE_STATISTICS *>(pData)) = {};
      }
      return S_OK;
    }
    default:
      ERR("Unknown query type ", desc.Query);
      return E_FAIL;
    }
    if (hr == S_FALSE && (GetDataFlags & D3D11_ASYNC_GETDATA_DONOTFLUSH) == 0) {
      cmd_queue.CurrentFrameStatistics().event_stall++;
      Flush();
    }
    return hr;
  }

  void
  STDMETHODCALLTYPE
  Flush() override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    if (!ctx_state.has_dirty_op_since_last_event && !promote_flush) {
      return;
    }
    promote_flush = true;
    InvalidateCurrentPass();
  }

  void PrepareFlush() override {
    InvalidateCurrentPass(true);
  }

  void
  STDMETHODCALLTYPE
  ExecuteCommandList(ID3D11CommandList *pCommandList, BOOL RestoreContextState) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    ResetEncodingContextState();

    Com<MTLD3D11CommandList> cmdlist = static_cast<MTLD3D11CommandList *>(pCommandList);
    auto seq_id = ctx_state.cmd_queue.CurrentSeqId();

    promote_flush = cmdlist->promote_flush;

    auto query_list = AllocateCommandData<Rc<VisibilityResultQuery>>(cmdlist->visibility_query_count);
    for (const auto &[query, index] : cmdlist->issued_visibility_query) {
      query_list[index] = new VisibilityResultQuery();
      query->DoDeferredQuery(query_list[index]);
    }

    for (const auto &query : cmdlist->issued_event_query) {
      End(query.ptr());
    }

    for (const auto &staging : cmdlist->read_staging_resources) {
      staging->useCopySource(seq_id);
    }

    for (const auto &staging : cmdlist->written_staging_resources) {
      staging->useCopyDestination(seq_id);
    }

    for (const auto &used_dynamic : cmdlist->used_dynamic_buffers) {
      if (!used_dynamic.latest)
        continue;
      used_dynamic.buffer->updateImmediateName(seq_id, Rc(used_dynamic.allocation), used_dynamic.suballocation, true);
    }

    for (const auto &used_dynamic : cmdlist->used_dynamic_textures) {
      if (!used_dynamic.latest)
        continue;
      used_dynamic.texture->updateImmediateName(seq_id, Rc(used_dynamic.allocation), true);
    }

    EmitOP([cmdlist = std::move(cmdlist), query_list = std::move(query_list)](ArgumentEncodingContext &enc) {
      // Finished command list should clean up the encoding context
      enc.pushDeferredVisibilityQuerys(query_list.data());
      cmdlist->Execute(enc);
      enc.popDeferredVisibilityQuerys();
    });

    if (RestoreContextState)
      RestoreEncodingContextState();
    else
      ResetD3D11ContextState();
  }

  HRESULT
  STDMETHODCALLTYPE
  FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList **ppCommandList) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    return DXGI_ERROR_INVALID_CALL;
  }

  D3D11_DEVICE_CONTEXT_TYPE
  STDMETHODCALLTYPE
  GetType() override {
    return D3D11_DEVICE_CONTEXT_IMMEDIATE;
  }

  virtual void
  WaitUntilGPUIdle() override {
    uint64_t seq = cmd_queue.CurrentSeqId();
    if(!InvalidateCurrentPass())
      Commit();
    cmd_queue.WaitCPUFence(seq);
  };

  void
  Commit() override {
    promote_flush = false;
    D3D11_ASSERT(cmdbuf_state == CommandBufferState::Idle);
    ctx_state.cmd_queue.CommitCurrentChunk();
    ctx_state.has_dirty_op_since_last_event = false;
  };

  HRESULT STDMETHODCALLTYPE Signal(ID3D11Fence *pFence, UINT64 Value) override {
    auto fence = static_cast<MTLD3D11Fence *>(pFence);

    InvalidateCurrentPass();
    EmitOP([event = fence->event, Value](ArgumentEncodingContext &enc) mutable {
      enc.signalEvent(std::move(event), Value);
    });
    Flush();

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Wait(ID3D11Fence *pFence, UINT64 Value) override {
    auto fence = static_cast<MTLD3D11Fence *>(pFence);

    Flush();
    EmitOP([event = fence->event, Value](ArgumentEncodingContext &enc) mutable {
      enc.waitEvent(std::move(event), Value);
    });

    return S_OK;
  }

private:
  std::vector<Com<IMTLD3DOcclusionQuery>> pending_occlusion_queries;
  CommandQueue &cmd_queue;
  ContextInternalState ctx_state;
  std::atomic<uint32_t> refcount = 0;
  D3D11Multithread d3dmt_;
  bool ignore_map_flag_no_wait_;
};

std::unique_ptr<MTLD3D11DeviceContextBase>
InitializeImmediateContext(MTLD3D11Device *pDevice, CommandQueue &cmd_queue) {
  return std::make_unique<MTLD3D11ImmediateContext>(pDevice, cmd_queue);
}

}; // namespace dxmt
#pragma once
#include "d3d11_4.h"
#include <atomic>

namespace dxmt {

class d3d11_device_mutex {
public:
  void lock();
  void unlock() noexcept;
  bool try_lock() { return true; }

  bool set_protected(bool Protected);
  bool get_protected();

private:
  bool protected_ = false;
  std::atomic_uint32_t owner_ = 0;
  uint32_t counter_ = 0;
};

class D3D11Multithread : public ID3D11Multithread {
public:
  D3D11Multithread(IUnknown *container, d3d11_device_mutex &mutex)
      : container_(container), mutex_(mutex) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    return container_->QueryInterface(riid, ppvObject);
  }

  ULONG STDMETHODCALLTYPE AddRef() final { return container_->AddRef(); }

  ULONG STDMETHODCALLTYPE Release() final { return container_->Release(); }

  void STDMETHODCALLTYPE Enter() final { mutex_.lock(); }

  void STDMETHODCALLTYPE Leave() final { mutex_.unlock(); }

  WINBOOL STDMETHODCALLTYPE SetMultithreadProtected(WINBOOL Enable) final {
    return mutex_.set_protected(Enable);
  }

  WINBOOL STDMETHODCALLTYPE GetMultithreadProtected() final {
    return mutex_.get_protected();
  }

private:
  IUnknown *container_;
  d3d11_device_mutex &mutex_;
};

} // namespace dxmt

#pragma once
#include <unknwn.h>

namespace dxmt {
/*
\see https://learn.microsoft.com/en-us/windows/win32/com/aggregation
*/
template <typename Outer, typename Inner>
class ComAggregatedObject : public Inner {

public:
  ComAggregatedObject(Outer *outer) noexcept : context(outer) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    return context->QueryInterface(riid, ppvObject);
  };

  ULONG STDMETHODCALLTYPE AddRef() final { return context->AddRef(); };

  ULONG STDMETHODCALLTYPE Release() final { return context->Release(); };

protected:
  Outer *context;
};

} // namespace dxmt
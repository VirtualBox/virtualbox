#pragma once

#include "com/com_private_data.hpp"
#include "com/com_object.hpp"

namespace dxmt {

template <typename Base> class MTLDXGIObject : public ComObjectWithInitialRef<Base> {

public:
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize,
                                           void *pData) final {
    return m_privateData.getData(Name, pDataSize, pData);
  }

  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize,
                                           const void *pData) final {
    return m_privateData.setData(Name, DataSize, pData);
  }

  HRESULT STDMETHODCALLTYPE
  SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) final {
    return m_privateData.setInterface(Name, pUnknown);
  }

private:
  ComPrivateData m_privateData;
};

template <typename Base, typename Device = IUnknown> class MTLDXGISubObject : public MTLDXGIObject<Base> {
public:
  MTLDXGISubObject(Device *device) { m_device = device; }

  HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **device) final {
    return m_device->QueryInterface(riid, device);
  }

protected:
  Device* m_device;
};

} // namespace dxmt

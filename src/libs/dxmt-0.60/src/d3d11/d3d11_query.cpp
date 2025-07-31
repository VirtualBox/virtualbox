#include "d3d11_query.hpp"
#include "dxmt_occlusion_query.hpp"

namespace dxmt {

class OcculusionQuery : public MTLD3DQueryBase<IMTLD3DOcclusionQuery> {
  using MTLD3DQueryBase<IMTLD3DOcclusionQuery>::MTLD3DQueryBase;

  QueryState state = QueryState::Signaled;

  Rc<VisibilityResultQuery> query = new VisibilityResultQuery();
  uint64_t accumulated_value = 0;

  virtual UINT STDMETHODCALLTYPE
  GetDataSize() override {
    return desc_.Query == D3D11_QUERY_OCCLUSION_PREDICATE ? sizeof(BOOL) : sizeof(UINT64);
  };

  virtual HRESULT
  GetData(void *data) override {
    if (state == QueryState::Building) {
      return DXGI_ERROR_INVALID_CALL;
    }
    if (state == QueryState::Signaled || query->getValue(&accumulated_value)) {
      if (desc_.Query == D3D11_QUERY_OCCLUSION_PREDICATE) {
        *((BOOL *)data) = accumulated_value != 0;
      } else {
        *((uint64_t *)data) = accumulated_value;
      }
      query = new VisibilityResultQuery();
      state = QueryState::Signaled;
      return S_OK;
    }
    return S_FALSE;
  }

  virtual bool
  Begin() override {
    if (state == QueryState::Signaled) {
      state = QueryState::Building;
      return true;
    }
    if (state == QueryState::Issued) {
      // discard previous issued query
      state = QueryState::Building;
      query = new VisibilityResultQuery();
      return true;
    }
    // FIXME: it's effectively ignoring  Begin() after Begin()
    return false;
  };

  virtual bool
  End() override {
    if (state == QueryState::Signaled) {
      // ignore  a single End()
      accumulated_value = 0;
      return false;
    }
    if (state == QueryState::Issued) {
      // FIXME: it's effectively ignoring End() after End()
      return false;
    }
    state = QueryState::Issued;
    return true;
  };

  virtual void DoDeferredQuery(dxmt::Rc<dxmt::VisibilityResultQuery> &deferred_query) override{
    accumulated_value = 0;
    state = QueryState::Issued;
    query = deferred_query;
  };

  virtual Rc<VisibilityResultQuery> __query() override {
    return query;
  }
};

HRESULT
CreateOcculusionQuery(MTLD3D11Device *pDevice, const D3D11_QUERY_DESC *pDesc, ID3D11Query **ppQuery) {
  if (ppQuery) {
    *ppQuery = ref(new OcculusionQuery(pDevice, pDesc));
    return S_OK;
  }
  return S_FALSE;
}

} // namespace dxmt
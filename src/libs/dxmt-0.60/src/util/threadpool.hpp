#pragma once

#include "log/log.hpp"
#include "util_error.hpp"

namespace dxmt {

#ifdef __WIN32__

#include "windef.h"
#include "winbase.h"
#include "threadpoolapiset.h"

template <typename threadpool_trait> class threadpool {
private:
  TP_CALLBACK_ENVIRON env_;
  PTP_POOL pool_ = nullptr;
  PTP_CLEANUP_GROUP cleanup_ = nullptr;
  static void CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE, PVOID Context,
                                    PTP_WORK) {
    threadpool_trait trait;
    trait.invoke_work(reinterpret_cast<threadpool_trait::work_type *>(Context));
  }

public:
  struct work_handle {
    PTP_WORK work = nullptr;
    bool done = false;
  };

  threadpool() {
    InitializeThreadpoolEnvironment(&env_);
    pool_ = CreateThreadpool(nullptr);
    if (!pool_) {
      throw MTLD3DError("Failed to create threadpool");
    }

    SetThreadpoolThreadMaximum(pool_, dxmt::thread::hardware_concurrency());

    cleanup_ = CreateThreadpoolCleanupGroup();
    if (!cleanup_) {
      CloseThreadpool(pool_);
      throw MTLD3DError("Failed to create threadpool cleanup group");
    }

    // // No more failures
    SetThreadpoolCallbackPool(&env_, pool_);
  }

  ~threadpool() {
    CloseThreadpoolCleanupGroupMembers(cleanup_, true, nullptr);
    CloseThreadpoolCleanupGroup(cleanup_);
    CloseThreadpool(pool_);
    DestroyThreadpoolEnvironment(&env_);
  }

  threadpool(threadpool const &) = delete;
  threadpool(threadpool &&) = delete;
  threadpool &operator=(threadpool const &) = delete;
  threadpool &operator=(threadpool &&) = delete;

  HRESULT enqueue(threadpool_trait::work_type *Work, work_handle *pHandle) {

    auto work_handle = CreateThreadpoolWork(&WorkCallback, Work, &env_);

    (*pHandle).work = work_handle;
    (*pHandle).done = false;
    SubmitThreadpoolWork(work_handle);
    return S_OK;
  }

  void wait(work_handle *handle) {
    if (handle->done)
      return;
    WaitForThreadpoolWorkCallbacks(handle->work, true);
    CloseThreadpoolWork(handle->work);
    handle->done = true;
    handle->work = nullptr;
  }
};

#endif

} // namespace dxmt
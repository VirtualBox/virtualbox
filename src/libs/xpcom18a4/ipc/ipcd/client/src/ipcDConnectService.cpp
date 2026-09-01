/* vim:set ts=2 sw=2 et cindent: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla IPC.
 *
 * The Initial Developer of the Original Code is IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Darin Fisher <darin@meer.net>
 *   Dmitry A. Kuminov <dmik@innotek.de>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
#undef  LOG_GROUP
#define LOG_GROUP LOG_GROUP_IPC
#include "ipcDConnectService.h"

#include "nsIServiceManagerUtils.h"
#include "nsIInterfaceInfo.h"
#include "nsIInterfaceInfoManager.h"
#include "nsIExceptionService.h"
#include "nsString.h"
#include "nsVoidArray.h"
#include "nsCRT.h"
#include "nsDeque.h"
#include "xptcall.h"

#ifdef VBOX
# include <map>
# include <list>
# include <iprt/asm.h>
# include <iprt/errcore.h>
# include <iprt/req.h>
# include <iprt/mem.h>
# include <iprt/time.h>
# include <iprt/thread.h>
# include <iprt/utf16.h>

# include <VBox/log.h>
#endif /* VBOX */

#if defined(DEBUG) && !defined(DCONNECT_STATS)
#define DCONNECT_STATS
#endif

#if defined(DCONNECT_STATS)
#include <stdio.h>
#endif

// XXX TODO:
//  1. add thread affinity field to SETUP messages

//-----------------------------------------------------------------------------

#define DCONNECT_IPC_TARGETID                      \
{ /* 43ca47ef-ebc8-47a2-9679-a4703218089f */       \
  0x43ca47ef,                                      \
  0xebc8,                                          \
  0x47a2,                                          \
  {0x96, 0x79, 0xa4, 0x70, 0x32, 0x18, 0x08, 0x9f} \
}
static const nsID kDConnectTargetID = DCONNECT_IPC_TARGETID;

//-----------------------------------------------------------------------------

#define DCON_WAIT_TIMEOUT RT_INDEFINITE_WAIT

//-----------------------------------------------------------------------------

//
// +--------------------------------------+
// | major opcode : 1 byte                |
// +--------------------------------------+
// | minor opcode : 1 byte                |
// +--------------------------------------+
// | flags        : 2 bytes               |
// +--------------------------------------+
// .                                      .
// . variable payload                     .
// .                                      .
// +--------------------------------------+
//

// dconnect major opcodes
#define DCON_OP_SETUP   1
#define DCON_OP_RELEASE 2
#define DCON_OP_INVOKE  3

#define DCON_OP_SETUP_REPLY  4
#define DCON_OP_INVOKE_REPLY 5

// dconnect minor opcodes for DCON_OP_SETUP
#define DCON_OP_SETUP_NEW_INST_CLASSID    1
#define DCON_OP_SETUP_NEW_INST_CONTRACTID 2
#define DCON_OP_SETUP_GET_SERV_CLASSID    3
#define DCON_OP_SETUP_GET_SERV_CONTRACTID 4
#define DCON_OP_SETUP_QUERY_INTERFACE     5

// dconnect minor opcodes for RELEASE
// dconnect minor opcodes for INVOKE

// DCON_OP_SETUP_REPLY and DCON_OP_INVOKE_REPLY flags
#define DCON_OP_FLAGS_REPLY_EXCEPTION   0x0001

// Within this time all the worker threads must be terminated.
#define VBOX_XPCOM_SHUTDOWN_TIMEOUT_MS  (5000)

#pragma pack(1)

struct DConnectOp
{
  PRUint8  opcode_major;
  PRUint8  opcode_minor;
  PRUint16 flags;
  PRUint32 request_index; // initialized with NewRequestIndex
};

// SETUP structs

struct DConnectSetup : DConnectOp
{
  nsID iid;
};

struct DConnectSetupClassID : DConnectSetup
{
  nsID classid;
};

struct DConnectSetupContractID : DConnectSetup
{
  char contractid[1]; // variable length
};

struct DConnectSetupQueryInterface : DConnectSetup
{
  DConAddr instance;
};

// SETUP_REPLY struct

struct DConnectSetupReply : DConnectOp
{
  DConAddr instance;
  nsresult status;
  // optionally followed by a specially serialized nsIException instance (see
  // ipcDConnectService::SerializeException) if DCON_OP_FLAGS_REPLY_EXCEPTION is
  // present in flags
};

// RELEASE struct

struct DConnectRelease : DConnectOp
{
  DConAddr instance;
};

// INVOKE struct

struct DConnectInvoke : DConnectOp
{
  DConAddr instance;
  PRUint16 method_index;
  // followed by an array of in-param blobs
};

// INVOKE_REPLY struct

struct DConnectInvokeReply : DConnectOp
{
  nsresult result;
  // followed by an array of out-param blobs if NS_SUCCEEDED(result), and
  // optionally by a specially serialized nsIException instance (see
  // ipcDConnectService::SerializeException) if DCON_OP_FLAGS_REPLY_EXCEPTION is
  // present in flags
};

#pragma pack()

//-----------------------------------------------------------------------------

struct DConAddrPlusPtr
{
  DConAddr addr;
  void *p;
};

//-----------------------------------------------------------------------------

ipcDConnectService *ipcDConnectService::mInstance = nsnull;

//-----------------------------------------------------------------------------

static nsresult
SetupPeerInstance(PRUint32 aPeerID, DConnectSetup *aMsg, PRUint32 aMsgLen,
                  void **aInstancePtr);

//-----------------------------------------------------------------------------

// A wrapper class holding an instance to an in-process XPCOM object.

class DConnectInstance
{
public:
  DConnectInstance(PRUint32 peer, nsIInterfaceInfo *iinfo, nsISupports *instance)
    : mPeer(peer)
    , mIInfo(iinfo)
    , mInstance(instance)
  {}

  nsISupports      *RealInstance()  { return mInstance; }
  nsIInterfaceInfo *InterfaceInfo() { return mIInfo; }
  PRUint32          Peer()          { return mPeer; }

  DConnectInstanceKey::Key GetKey() {
    const nsID *iid;
    mIInfo->GetIIDShared(&iid);
    return DConnectInstanceKey::Key(mPeer, mInstance, iid);
  }

  NS_IMETHODIMP_(nsrefcnt) AddRef(void)
  {
    NS_PRECONDITION(PRInt32(mRefCnt) >= 0, "illegal refcnt");
    nsrefcnt count;
    count = ASMAtomicIncU32((volatile uint32_t *)&mRefCnt);
    return count;
  }

  NS_IMETHODIMP_(nsrefcnt) Release(void)
  {
    nsrefcnt count;
    NS_PRECONDITION(0 != mRefCnt, "dup release");
    count = ASMAtomicDecU32((volatile uint32_t *)&mRefCnt);
    if (0 == count) {
      NS_PRECONDITION(PRInt32(mRefCntIPC) == 0, "non-zero IPC refcnt");
      mRefCnt = 1; /* stabilize */
      delete this;
      return 0;
    }
    return count;
  }

  // this gets called after calling AddRef() on an instance passed to the
  // client over IPC in order to have a count of IPC client-related references
  // separately from the overall reference count
  NS_IMETHODIMP_(nsrefcnt) AddRefIPC(void)
  {
    NS_PRECONDITION(PRInt32(mRefCntIPC) >= 0, "illegal refcnt");
    nsrefcnt count = ASMAtomicIncU32((volatile uint32_t *)&mRefCntIPC);
    return count;
  }

  // this gets called before calling Release() when DCON_OP_RELEASE is
  // received from the IPC client and in other cases to balance AddRefIPC()
  NS_IMETHODIMP_(nsrefcnt) ReleaseIPC(PRBool locked = PR_FALSE)
  {
    NS_PRECONDITION(0 != mRefCntIPC, "dup release");
    nsrefcnt count = ASMAtomicDecU32((volatile uint32_t *)&mRefCntIPC);
    if (0 == count) {
      // If the last IPC reference is released, remove this instance from the map.
      // ipcDConnectService is guaranteed to still exist here
      // (DConnectInstance lifetime is bound to ipcDConnectService)
      nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
      if (dConnect)
        dConnect->DeleteInstance(this, locked);
      else
        NS_NOTREACHED("ipcDConnectService has gone before DConnectInstance");
    }
    return count;
  }

private:
  nsAutoRefCnt               mRefCnt;
  nsAutoRefCnt               mRefCntIPC;
  PRUint32                   mPeer;  // peer process "owning" this instance
  nsCOMPtr<nsIInterfaceInfo> mIInfo;
  nsCOMPtr<nsISupports>      mInstance;
};

void
ipcDConnectService::ReleaseWrappers(nsVoidArray &wrappers, PRUint32 peer)
{
  nsAutoLock lock (mLock);

  for (PRInt32 i=0; i<wrappers.Count(); ++i)
  {
    DConnectInstance *wrapper = (DConnectInstance *)wrappers[i];
    if (mInstanceSet.Contains(wrapper) && wrapper->Peer() == peer)
    {
      wrapper->ReleaseIPC(PR_TRUE /* locked */);
      wrapper->Release();
    }
  }
}

//-----------------------------------------------------------------------------

static nsresult
SerializeParam(PIPCMSGWRITER pMsgWriter, const nsXPTType &t, const nsXPTCMiniVariant &v)
{
  switch (t.TagPart())
  {
    case nsXPTType::T_I8:
    case nsXPTType::T_U8:
      IPCMsgWriterPutU8(pMsgWriter, v.val.u8);
      break;

    case nsXPTType::T_I16:
    case nsXPTType::T_U16:
      IPCMsgWriterPutU16(pMsgWriter, v.val.u16);
      break;

    case nsXPTType::T_I32:
    case nsXPTType::T_U32:
      IPCMsgWriterPutU32(pMsgWriter, v.val.u32);
      break;

    case nsXPTType::T_I64:
    case nsXPTType::T_U64:
      IPCMsgWriterPutU64(pMsgWriter, v.val.u64);
      break;

    case nsXPTType::T_FLOAT:
      IPCMsgWriterPutBytes(pMsgWriter, &v.val.f, sizeof(float));
      break;

    case nsXPTType::T_DOUBLE:
      IPCMsgWriterPutBytes(pMsgWriter, &v.val.d, sizeof(double));
      break;

    case nsXPTType::T_BOOL:
      IPCMsgWriterPutBytes(pMsgWriter, &v.val.b, sizeof(PRBool));
      break;

    case nsXPTType::T_CHAR:
      IPCMsgWriterPutBytes(pMsgWriter, &v.val.c, sizeof(char));
      break;

    case nsXPTType::T_WCHAR:
      IPCMsgWriterPutBytes(pMsgWriter, &v.val.wc, sizeof(PRUnichar));
      break;

    case nsXPTType::T_IID:
      {
        AssertReturn(v.val.p, NS_ERROR_INVALID_POINTER);
        IPCMsgWriterPutBytes(pMsgWriter, v.val.p, sizeof(nsID));
      }
      break;

    case nsXPTType::T_CHAR_STR:
      {
        if (v.val.p)
        {
          int len = strlen((const char *) v.val.p);
          IPCMsgWriterPutU32(pMsgWriter, len);
          IPCMsgWriterPutBytes(pMsgWriter, v.val.p, len);
        }
        else
        {
          // put -1 to indicate null string
          IPCMsgWriterPutU32(pMsgWriter, (PRUint32) -1);
        }
      }
      break;

    case nsXPTType::T_WCHAR_STR:
      {
        if (v.val.p)
        {
          PRUint32 len = (PRUint32)(RTUtf16Len((PCRTUTF16)v.val.p) * sizeof(RTUTF16));
          IPCMsgWriterPutU32(pMsgWriter, len);
          IPCMsgWriterPutBytes(pMsgWriter, v.val.p, len);
        }
        else
        {
          // put -1 to indicate null string
          IPCMsgWriterPutU32(pMsgWriter, (PRUint32) -1);
        }
      }
      break;

    case nsXPTType::T_INTERFACE:
    case nsXPTType::T_INTERFACE_IS:
      NS_NOTREACHED("this should be handled elsewhere");
      return NS_ERROR_UNEXPECTED;

    case nsXPTType::T_ASTRING:
    case nsXPTType::T_DOMSTRING:
      {
        const nsAString *str = (const nsAString *) v.val.p;

        PRUint32 len = 2 * str->Length();
        nsAString::const_iterator begin;
        const PRUnichar *data = str->BeginReading(begin).get();

        IPCMsgWriterPutU32(pMsgWriter, len);
        IPCMsgWriterPutBytes(pMsgWriter, data, len);
      }
      break;

    case nsXPTType::T_UTF8STRING:
    case nsXPTType::T_CSTRING:
      {
        const nsACString *str = (const nsACString *) v.val.p;

        PRUint32 len = str->Length();
        nsACString::const_iterator begin;
        const char *data = str->BeginReading(begin).get();

        IPCMsgWriterPutU32(pMsgWriter, len);
        IPCMsgWriterPutBytes(pMsgWriter, data, len);
      }
      break;

    case nsXPTType::T_ARRAY:
      // arrays are serialized after all other params outside this routine
      break;

    case nsXPTType::T_VOID:
    case nsXPTType::T_PSTRING_SIZE_IS:
    case nsXPTType::T_PWSTRING_SIZE_IS:
    default:
      Log(("unexpected parameter type: %d\n", t.TagPart()));
      return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

static nsresult
DeserializeParam(PIPCMSGREADER pMsgReader, const nsXPTType &t, nsXPTCVariant &v)
{
  // defaults
  v.ptr = nsnull;
  v.type = t;
  v.flags = 0;

  switch (t.TagPart())
  {
    case nsXPTType::T_I8:
    case nsXPTType::T_U8:
      v.val.u8 = IPCMsgReaderGetU8(pMsgReader);
      break;

    case nsXPTType::T_I16:
    case nsXPTType::T_U16:
      v.val.u16 = IPCMsgReaderGetU16(pMsgReader);
      break;

    case nsXPTType::T_I32:
    case nsXPTType::T_U32:
      v.val.u32 = IPCMsgReaderGetU32(pMsgReader);
      break;

    case nsXPTType::T_I64:
    case nsXPTType::T_U64:
      v.val.u64 = IPCMsgReaderGetU64(pMsgReader);
      break;

    case nsXPTType::T_FLOAT:
      IPCMsgReaderReadBytes(pMsgReader, &v.val.f, sizeof(v.val.f));
      break;

    case nsXPTType::T_DOUBLE:
      IPCMsgReaderReadBytes(pMsgReader, &v.val.d, sizeof(v.val.d));
      break;

    case nsXPTType::T_BOOL:
      IPCMsgReaderReadBytes(pMsgReader, &v.val.b, sizeof(v.val.b));
      break;

    case nsXPTType::T_CHAR:
      IPCMsgReaderReadBytes(pMsgReader, &v.val.c, sizeof(v.val.c));
      break;

    case nsXPTType::T_WCHAR:
      IPCMsgReaderReadBytes(pMsgReader, &v.val.wc, sizeof(v.val.wc));
      break;

    case nsXPTType::T_IID:
      {
        nsID *buf = (nsID *)RTMemAlloc(sizeof(nsID));
        IPCMsgReaderReadBytes(pMsgReader, buf, sizeof(nsID));
        v.val.p = buf;
        v.SetValIsAllocated();
      }
      break;

    case nsXPTType::T_CHAR_STR:
      {
        PRUint32 len = IPCMsgReaderGetU32(pMsgReader);
        if (len == (PRUint32) -1)
        {
          // it's a null string
          v.val.p = nsnull;
        }
        else
        {
          char *buf = (char *)RTMemAlloc(len + 1);
          IPCMsgReaderReadBytes(pMsgReader, buf, len);
          buf[len] = char(0);

          v.val.p = buf;
          v.SetValIsAllocated();
        }
      }
      break;

    case nsXPTType::T_WCHAR_STR:
      {
        PRUint32 len = IPCMsgReaderGetU32(pMsgReader);
        if (len == (PRUint32) -1)
        {
          // it's a null string
          v.val.p = nsnull;
        }
        else
        {
          PRTUTF16 buf = (PRTUTF16)RTMemAlloc(len + sizeof(RTUTF16));
          IPCMsgReaderReadBytes(pMsgReader, buf, len);
          buf[len / sizeof(RTUTF16)] = RTUTF16(0);

          v.val.p = (PRUnichar *)buf;
          v.SetValIsAllocated();
        }
      }
      break;

    case nsXPTType::T_INTERFACE:
    case nsXPTType::T_INTERFACE_IS:
      {
        IPCMsgReaderReadBytes(pMsgReader, &v.val.u64, sizeof(DConAddr));
        // stub creation will be handled outside this routine.  we only
        // deserialize the DConAddr into v.val.u64 temporarily.
      }
      break;

    case nsXPTType::T_ASTRING:
    case nsXPTType::T_DOMSTRING:
      {
        PRUint32 len = IPCMsgReaderGetU32(pMsgReader);

        nsString *str = new nsString();
        str->SetLength(len / 2);
        PRUnichar *buf = str->BeginWriting();
        IPCMsgReaderReadBytes(pMsgReader, buf, len);

        v.val.p = str;
        v.SetValIsDOMString();
      }
      break;

    case nsXPTType::T_UTF8STRING:
    case nsXPTType::T_CSTRING:
      {
        PRUint32 len = IPCMsgReaderGetU32(pMsgReader);

        nsCString *str = new nsCString();
        str->SetLength(len);
        char *buf = str->BeginWriting();
        IPCMsgReaderReadBytes(pMsgReader, buf, len);

        v.val.p = str;

        // this distinction here is pretty pointless
        if (t.TagPart() == nsXPTType::T_CSTRING)
          v.SetValIsCString();
        else
          v.SetValIsUTF8String();
      }
      break;

    case nsXPTType::T_ARRAY:
      // arrays are deserialized after all other params outside this routine
      break;

    case nsXPTType::T_VOID:
    case nsXPTType::T_PSTRING_SIZE_IS:
    case nsXPTType::T_PWSTRING_SIZE_IS:
    default:
      Log(("unexpected parameter type\n"));
      return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

static nsresult
SetupParam(const nsXPTParamInfo &p, nsXPTCVariant &v)
{
  const nsXPTType &t = p.GetType();

  if (p.IsIn() && p.IsDipper())
  {
    v.ptr = nsnull;
    v.flags = 0;

    switch (t.TagPart())
    {
      case nsXPTType::T_ASTRING:
      case nsXPTType::T_DOMSTRING:
        v.val.p = new nsString();
        if (!v.val.p)
          return NS_ERROR_OUT_OF_MEMORY;
        v.type = t;
        v.SetValIsDOMString();
        break;

      case nsXPTType::T_UTF8STRING:
      case nsXPTType::T_CSTRING:
        v.val.p = new nsCString();
        if (!v.val.p)
          return NS_ERROR_OUT_OF_MEMORY;
        v.type = t;
        v.SetValIsCString();
        break;

      default:
        Log(("unhandled dipper: type=%d\n", t.TagPart()));
        return NS_ERROR_UNEXPECTED;
    }
  }
  else if (p.IsOut() || p.IsRetval())
  {
    memset(&v.val, 0, sizeof(v.val));
    v.ptr = &v.val;
    v.type = t;
    v.flags = 0;
    v.SetPtrIsData();

    // the ownership of output nsID, string, wstring, interface pointers and
    // arrays is transferred to the receiving party. Therefore, we need to
    // instruct FinishParam() to perform a cleanup after serializing them.
    switch (t.TagPart())
    {
      case nsXPTType::T_IID:
      case nsXPTType::T_CHAR_STR:
      case nsXPTType::T_WCHAR_STR:
      case nsXPTType::T_ARRAY:
        v.SetValIsAllocated();
        break;
      case nsXPTType::T_INTERFACE:
      case nsXPTType::T_INTERFACE_IS:
        v.SetValIsInterface();
        break;
      default:
        break;
    }
  }

  return NS_OK;
}

static void
FinishParam(nsXPTCVariant &v)
{
#ifdef VBOX
  /* make valgrind happy */
  if (!v.MustFreeVal())
    return;
#endif
  if (!v.val.p)
    return;

  if (v.IsValAllocated())
    RTMemFree(v.val.p);
  else if (v.IsValInterface())
    ((nsISupports *) v.val.p)->Release();
  else if (v.IsValDOMString())
    delete (nsAString *) v.val.p;
  else if (v.IsValUTF8String() || v.IsValCString())
    delete (nsACString *) v.val.p;
}

static nsresult
DeserializeResult(PIPCMSGREADER pMsgReader, const nsXPTType &t, nsXPTCMiniVariant &v)
{
  if (v.val.p == nsnull)
    return NS_OK;

  switch (t.TagPart())
  {
    case nsXPTType::T_I8:
    case nsXPTType::T_U8:
      *((PRUint8 *) v.val.p) = IPCMsgReaderGetU8(pMsgReader);
      break;

    case nsXPTType::T_I16:
    case nsXPTType::T_U16:
      *((PRUint16 *) v.val.p) = IPCMsgReaderGetU16(pMsgReader);
      break;

    case nsXPTType::T_I32:
    case nsXPTType::T_U32:
      *((PRUint32 *) v.val.p) = IPCMsgReaderGetU32(pMsgReader);
      break;

    case nsXPTType::T_I64:
    case nsXPTType::T_U64:
      *((PRUint64 *) v.val.p) = IPCMsgReaderGetU64(pMsgReader);
      break;

    case nsXPTType::T_FLOAT:
      IPCMsgReaderReadBytes(pMsgReader, v.val.p, sizeof(float));
      break;

    case nsXPTType::T_DOUBLE:
      IPCMsgReaderReadBytes(pMsgReader, v.val.p, sizeof(double));
      break;

    case nsXPTType::T_BOOL:
      IPCMsgReaderReadBytes(pMsgReader, v.val.p, sizeof(PRBool));
      break;

    case nsXPTType::T_CHAR:
      IPCMsgReaderReadBytes(pMsgReader, v.val.p, sizeof(char));
      break;

    case nsXPTType::T_WCHAR:
      IPCMsgReaderReadBytes(pMsgReader, v.val.p, sizeof(PRUnichar));
      break;

    case nsXPTType::T_IID:
      {
        nsID *buf = (nsID *)RTMemAlloc(sizeof(nsID));
        IPCMsgReaderReadBytes(pMsgReader, buf, sizeof(nsID));
        *((nsID **) v.val.p) = buf;
      }
      break;

    case nsXPTType::T_CHAR_STR:
      {
        PRUint32 len = IPCMsgReaderGetU32(pMsgReader);
        if (len == (PRUint32) -1)
        {
          // it's a null string
#ifdef VBOX
          *((char **) v.val.p) = NULL;
#else
          v.val.p = 0;
#endif
        }
        else
        {
          char *buf = (char *)RTMemAlloc(len + 1);
          IPCMsgReaderReadBytes(pMsgReader, buf, len);
          buf[len] = char(0);

          *((char **) v.val.p) = buf;
        }
      }
      break;

    case nsXPTType::T_WCHAR_STR:
      {
        PRUint32 len = IPCMsgReaderGetU32(pMsgReader);
        if (len == (PRUint32) -1)
        {
          // it's a null string
#ifdef VBOX
          *((PRUnichar **) v.val.p) = 0;
#else
          v.val.p = 0;
#endif
        }
        else
        {
          PRTUTF16 buf = (PRTUTF16)RTMemAlloc(len + sizeof(RTUTF16));
          IPCMsgReaderReadBytes(pMsgReader, buf, len);
          buf[len / sizeof(RTUTF16)] = RTUTF16(0);

          *((PRUnichar **) v.val.p) = (PRUnichar *)buf;
        }
      }
      break;

    case nsXPTType::T_INTERFACE:
    case nsXPTType::T_INTERFACE_IS:
      {
        // stub creation will be handled outside this routine.  we only
        // deserialize the DConAddr and the original value of v.val.p
        // into v.val.p temporarily.  needs temporary memory alloc.
        DConAddrPlusPtr *buf = (DConAddrPlusPtr *)RTMemAlloc(sizeof(DConAddrPlusPtr));
        IPCMsgReaderReadBytes(pMsgReader, &buf->addr, sizeof(DConAddr));
        buf->p = v.val.p;
        v.val.p = buf;
      }
      break;

    case nsXPTType::T_ASTRING:
    case nsXPTType::T_DOMSTRING:
      {
        PRUint32 len = IPCMsgReaderGetU32(pMsgReader);

        nsAString *str = (nsAString *) v.val.p;

        nsAString::iterator begin;
        str->SetLength(len / 2);
        str->BeginWriting(begin);

        IPCMsgReaderReadBytes(pMsgReader, begin.get(), len);
      }
      break;

    case nsXPTType::T_UTF8STRING:
    case nsXPTType::T_CSTRING:
      {
        PRUint32 len = IPCMsgReaderGetU32(pMsgReader);

        nsACString *str = (nsACString *) v.val.p;

        nsACString::iterator begin;
        str->SetLength(len);
        str->BeginWriting(begin);

        IPCMsgReaderReadBytes(pMsgReader, begin.get(), len);
      }
      break;

    case nsXPTType::T_ARRAY:
      // arrays are deserialized after all other params outside this routine
      break;

    case nsXPTType::T_VOID:
    case nsXPTType::T_PSTRING_SIZE_IS:
    case nsXPTType::T_PWSTRING_SIZE_IS:
    default:
      Log(("unexpected parameter type\n"));
      return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
//
// Returns an element from the nsXPTCMiniVariant array by properly casting it to
// nsXPTCVariant when requested
#define GET_PARAM(params, isXPTCVariantArray, idx) \
    (isXPTCVariantArray ? ((nsXPTCVariant *) params) [idx] : params [idx])

// isResult is PR_TRUE if the size_is and length_is params are out or retval
// so that nsXPTCMiniVariants contain pointers to their locations instead of the
// values themselves.
static nsresult
GetArrayParamInfo(nsIInterfaceInfo *iinfo, uint16 methodIndex,
                  const nsXPTMethodInfo &methodInfo, nsXPTCMiniVariant *params,
                  PRBool isXPTCVariantArray, const nsXPTParamInfo &paramInfo,
                  PRBool isResult, PRUint32 &size, PRUint32 &length,
                  nsXPTType &elemType)
{
  // XXX multidimensional arrays are not supported so dimension is always 0 for
  // getting the size_is argument number of the array itself and 1 for getting
  // the type of elements stored in the array.

  nsresult rv;

  // get the array size
  PRUint8 sizeArg;
  rv = iinfo->GetSizeIsArgNumberForParam(methodIndex, &paramInfo, 0, &sizeArg);
  if (NS_FAILED(rv))
    return rv;

  // get the number of valid elements
  PRUint8 lenArg;
  rv = iinfo->GetLengthIsArgNumberForParam(methodIndex, &paramInfo, 0, &lenArg);
  if (NS_FAILED(rv))
    return rv;

  // according to XPT specs
  // (http://www.mozilla.org/scriptable/typelib_file.html), size_is and
  // length_is for arrays is always uint32. Check this too.
  {
    nsXPTParamInfo pi = methodInfo.GetParam (sizeArg);
    if (pi.GetType().TagPart() != nsXPTType::T_U32)
    {
      Log(("unexpected size_is() parameter type: $d\n",
           pi.GetType().TagPart()));
      return NS_ERROR_UNEXPECTED;
    }

    pi = methodInfo.GetParam (lenArg);
    if (pi.GetType().TagPart() != nsXPTType::T_U32)
    {
      Log(("unexpected length_is() parameter type: $d\n",
           pi.GetType().TagPart()));
      return NS_ERROR_UNEXPECTED;
    }
  }

  if (isResult)
  {
    length = *((PRUint32 *) GET_PARAM(params,isXPTCVariantArray, lenArg).val.p);
    size = *((PRUint32 *) GET_PARAM(params, isXPTCVariantArray, sizeArg).val.p);
  }
  else
  {
    length = GET_PARAM(params, isXPTCVariantArray, lenArg).val.u32;
    size = GET_PARAM(params, isXPTCVariantArray, sizeArg).val.u32;
  }

  if (length > size)
  {
    NS_WARNING("length_is() value is greater than size_is() value");
    length = size;
  }

  // get type of array elements
  rv = iinfo->GetTypeForParam(methodIndex, &paramInfo, 1, &elemType);
  if (NS_FAILED(rv))
    return rv;

  if (elemType.IsArithmetic() &&
      (elemType.IsPointer() || elemType.IsUniquePointer() ||
       elemType.IsReference()))
  {
    Log(("arrays of pointers and references to arithmetic types are not yet supported\n"));
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  if (elemType.IsArray())
  {
    Log(("multidimensional arrays are not yet supported\n"));
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  return NS_OK;
}

static nsresult
GetTypeSize(const nsXPTType &type, PRUint32 &size, PRBool &isSimple)
{
  // get the type size in bytes
  size = 0;
  isSimple = PR_TRUE;
  switch (type.TagPart())
  {
    case nsXPTType::T_I8:             size = sizeof(PRInt8);      break;
    case nsXPTType::T_I16:            size = sizeof(PRInt16);     break;
    case nsXPTType::T_I32:            size = sizeof(PRInt32);     break;
    case nsXPTType::T_I64:            size = sizeof(PRInt64);     break;
    case nsXPTType::T_U8:             size = sizeof(PRUint8);     break;
    case nsXPTType::T_U16:            size = sizeof(PRUint16);    break;
    case nsXPTType::T_U32:            size = sizeof(PRUint32);    break;
    case nsXPTType::T_U64:            size = sizeof(PRUint64);    break;
    case nsXPTType::T_FLOAT:          size = sizeof(float);       break;
    case nsXPTType::T_DOUBLE:         size = sizeof(double);      break;
    case nsXPTType::T_BOOL:           size = sizeof(PRBool);      break;
    case nsXPTType::T_CHAR:           size = sizeof(char);        break;
    case nsXPTType::T_WCHAR:          size = sizeof(PRUnichar);   break;
    case nsXPTType::T_IID:            /* fall through */
    case nsXPTType::T_CHAR_STR:       /* fall through */
    case nsXPTType::T_WCHAR_STR:      /* fall through */
    case nsXPTType::T_ASTRING:        /* fall through */
    case nsXPTType::T_DOMSTRING:      /* fall through */
    case nsXPTType::T_UTF8STRING:     /* fall through */
    case nsXPTType::T_CSTRING:        /* fall through */
      size = sizeof(void *);
      isSimple = PR_FALSE;
      break;
    case nsXPTType::T_INTERFACE:      /* fall through */
    case nsXPTType::T_INTERFACE_IS:   /* fall through */
      size = sizeof(DConAddr);
      isSimple = PR_FALSE;
      break;
    default:
      Log(("unexpected parameter type: %d\n", type.TagPart()));
      return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

static nsresult
SerializeArrayParam(ipcDConnectService *dConnect,
                    PIPCMSGWRITER pMsgWriter, PRUint32 peerID,
                    nsIInterfaceInfo *iinfo, uint16 methodIndex,
                    const nsXPTMethodInfo &methodInfo,
                    nsXPTCMiniVariant *params, PRBool isXPTCVariantArray,
                    const nsXPTParamInfo &paramInfo,
                    void *array, nsVoidArray &wrappers)
{
  if (!array)
  {
    // put 0 to indicate null array
    IPCMsgWriterPutU8(pMsgWriter, 0);
    return NS_OK;
  }

  // put 1 to indicate non-null array
  IPCMsgWriterPutU8(pMsgWriter, 1);

  PRUint32 size = 0;
  PRUint32 length = 0;
  nsXPTType elemType;

  nsresult rv = GetArrayParamInfo(iinfo, methodIndex, methodInfo, params,
                                  isXPTCVariantArray, paramInfo, PR_FALSE,
                                  size, length, elemType);
  if (NS_FAILED (rv))
      return rv;

  PRUint32 elemSize = 0;
  PRBool isSimple = PR_TRUE;
  rv = GetTypeSize(elemType, elemSize, isSimple);
  if (NS_FAILED (rv))
      return rv;

  if (isSimple)
  {
    // this is a simple arithmetic type, write the whole array at once
    IPCMsgWriterPutBytes(pMsgWriter, array, length * elemSize);
    return NS_OK;
  }

  // iterate over valid (length_is) elements of the array
  // and serialize each of them
  nsXPTCMiniVariant v;
  for (PRUint32 i = 0; i < length; ++i)
  {
    v.val.p = ((void **) array) [i];

    if (elemType.IsInterfacePointer())
    {
      nsID iid;
      rv = dConnect->GetIIDForMethodParam(iinfo, &methodInfo, paramInfo, elemType,
                                          methodIndex, params, isXPTCVariantArray,
                                          iid);
      if (NS_SUCCEEDED(rv))
        rv = dConnect->SerializeInterfaceParam(pMsgWriter, peerID, iid,
                                               (nsISupports *) v.val.p,
                                               wrappers);
    }
    else
      rv = SerializeParam(pMsgWriter, elemType, v);

    if (NS_FAILED(rv))
        return rv;
  }

  return NS_OK;
}

// isResult is PR_TRUE if the array param is out or retval
static nsresult
DeserializeArrayParam(ipcDConnectService *dConnect,
                      PIPCMSGREADER pMsgReader, PRUint32 peerID,
                      nsIInterfaceInfo *iinfo, uint16 methodIndex,
                      const nsXPTMethodInfo &methodInfo,
                      nsXPTCMiniVariant *params, PRBool isXPTCVariantArray,
                      const nsXPTParamInfo &paramInfo,
                      PRBool isResult, void *&array)
{
  PRUint32 size = 0;
  PRUint32 length = 0;
  nsXPTType elemType;

  nsresult rv = GetArrayParamInfo(iinfo, methodIndex, methodInfo, params,
                                  isXPTCVariantArray, paramInfo, isResult,
                                  size, length, elemType);
  if (NS_FAILED(rv))
    return rv;

  PRUint8 prefix = IPCMsgReaderGetU8(pMsgReader);
  if (prefix == 0)
  {
    // it's a null array
    array = nsnull;
    return NS_OK;
  }
  // sanity
  if (prefix != 1)
  {
    Log(("unexpected array prefix: %u\n", prefix));
    return NS_ERROR_UNEXPECTED;
  }

  PRUint32 elemSize = 0;
  PRBool isSimple = PR_TRUE;
  rv = GetTypeSize(elemType, elemSize, isSimple);
  if (NS_FAILED (rv))
      return rv;

  // Note: for zero-sized arrays, we use the size of 1 because whether
  // malloc(0) returns a null pointer or not (which is used in isNull())
  // is implementation-dependent according to the C standard
  void *arr = RTMemAlloc((size ? size : 1) * elemSize);
  if (arr == nsnull)
    return NS_ERROR_OUT_OF_MEMORY;

  // initialize the unused space of the array with zeroes
  if (length < size)
    memset(((PRUint8 *) arr) + length * elemSize, 0,
           (size - length) * elemSize);

  if (isSimple)
  {
    // this is a simple arithmetic type, read the whole array at once
    IPCMsgReaderReadBytes(pMsgReader, arr, length * elemSize);

    array = arr;
    return NS_OK;
  }

  // iterate over valid (length_is) elements of the array
  // and deserialize each of them individually
  nsXPTCVariant v;
  for (PRUint32 i = 0; i < length; ++i)
  {
    rv = DeserializeParam(pMsgReader, elemType, v);

    if (NS_SUCCEEDED(rv) && elemType.IsInterfacePointer())
    {
      // grab the DConAddr value temporarily stored in the param
      PtrBits bits = v.val.u64;

      // DeserializeInterfaceParamBits needs IID only if it's a remote object
      nsID iid;
      if (bits & PTRBITS_REMOTE_BIT)
        rv = dConnect->GetIIDForMethodParam(iinfo, &methodInfo, paramInfo,
                                            elemType, methodIndex,
                                            params, isXPTCVariantArray, iid);
      if (NS_SUCCEEDED(rv))
      {
        nsISupports *obj = nsnull;
        rv = dConnect->DeserializeInterfaceParamBits(bits, peerID, iid, obj);
        if (NS_SUCCEEDED(rv))
          v.val.p = obj;
      }
    }

    if (NS_FAILED(rv))
      break;

    // note that we discard extended param informaton provided by nsXPTCVariant
    // and will have to "reconstruct" it from the type tag in FinishArrayParam()
    ((void **) arr) [i] = v.val.p;
  }

  if (NS_FAILED(rv))
    RTMemFree(arr);
  else
    array = arr;

  return rv;
}

static void
FinishArrayParam(nsIInterfaceInfo *iinfo, uint16 methodIndex,
                 const nsXPTMethodInfo &methodInfo, nsXPTCMiniVariant *params,
                 PRBool isXPTCVariantArray, const nsXPTParamInfo &paramInfo,
                 const nsXPTCMiniVariant &arrayVal)
{
  // nothing to do for a null array
  void *arr = arrayVal.val.p;
  if (!arr)
    return;

  PRUint32 size = 0;
  PRUint32 length = 0;
  nsXPTType elemType;

  // note that FinishArrayParam is called only from OnInvoke to free memory
  // after the call has been served. When OnInvoke sets up out and retval
  // parameters for the real method, it passes pointers to the nsXPTCMiniVariant
  // elements of the params array themselves so that they will eventually
  // receive the returned values. For this reason, both in 'in' param and
  // 'out/retaval' param cases, size_is and length_is may be read by
  // GetArrayParamInfo() by value. Therefore, isResult is always PR_FALSE.
  nsresult rv = GetArrayParamInfo(iinfo, methodIndex, methodInfo, params,
                                  isXPTCVariantArray, paramInfo, PR_FALSE,
                                  size, length, elemType);
  if (NS_FAILED (rv))
      return;

  nsXPTCVariant v;
  v.ptr = nsnull;
  v.flags = 0;

  // iterate over valid (length_is) elements of the array
  // and free each of them
  for (PRUint32 i = 0; i < length; ++i)
  {
    v.type = elemType.TagPart();

    switch (elemType.TagPart())
    {
      case nsXPTType::T_I8:             /* fall through */
      case nsXPTType::T_I16:            /* fall through */
      case nsXPTType::T_I32:            /* fall through */
      case nsXPTType::T_I64:            /* fall through */
      case nsXPTType::T_U8:             /* fall through */
      case nsXPTType::T_U16:            /* fall through */
      case nsXPTType::T_U32:            /* fall through */
      case nsXPTType::T_U64:            /* fall through */
      case nsXPTType::T_FLOAT:          /* fall through */
      case nsXPTType::T_DOUBLE:         /* fall through */
      case nsXPTType::T_BOOL:           /* fall through */
      case nsXPTType::T_CHAR:           /* fall through */
      case nsXPTType::T_WCHAR:          /* fall through */
        // nothing to free for arithmetic types
        continue;
      case nsXPTType::T_IID:            /* fall through */
      case nsXPTType::T_CHAR_STR:       /* fall through */
      case nsXPTType::T_WCHAR_STR:      /* fall through */
        v.val.p = ((void **) arr) [i];
        v.SetValIsAllocated();
        break;
      case nsXPTType::T_INTERFACE:      /* fall through */
      case nsXPTType::T_INTERFACE_IS:   /* fall through */
        v.val.p = ((void **) arr) [i];
        v.SetValIsInterface();
        break;
      case nsXPTType::T_ASTRING:        /* fall through */
      case nsXPTType::T_DOMSTRING:      /* fall through */
        v.val.p = ((void **) arr) [i];
        v.SetValIsDOMString();
        break;
      case nsXPTType::T_UTF8STRING:     /* fall through */
        v.val.p = ((void **) arr) [i];
        v.SetValIsUTF8String();
        break;
      case nsXPTType::T_CSTRING:        /* fall through */
        v.val.p = ((void **) arr) [i];
        v.SetValIsCString();
        break;
      default:
        Log(("unexpected parameter type: %d\n", elemType.TagPart()));
        return;
    }

    FinishParam(v);
  }
}

//-----------------------------------------------------------------------------

static PRUint32
NewRequestIndex()
{
  static volatile uint32_t sRequestIndex = 0;
  return ASMAtomicIncU32(&sRequestIndex);
}

//-----------------------------------------------------------------------------

#ifdef VBOX
typedef struct ClientDownInfo
{
    ClientDownInfo(PRUint32 aClient)
    {
        uClient = aClient;
        uTimestamp = RTTimeMilliTS();
    }

    PRUint32 uClient;
    uint64_t uTimestamp;
} ClientDownInfo;
typedef std::map<PRUint32, ClientDownInfo *> ClientDownMap;
typedef std::list<ClientDownInfo *> ClientDownList;

#define MAX_CLIENT_DOWN_SIZE 10000

/* Protected by the queue monitor. */
static ClientDownMap g_ClientDownMap;
static ClientDownList g_ClientDownList;

#endif /* VBOX */

class DConnectMsgSelector : public ipcIMessageObserver
{
public:
  DConnectMsgSelector(PRUint32 peer, PRUint8 opCodeMajor, PRUint32 requestIndex)
    : mPeer (peer)
    , mOpCodeMajor(opCodeMajor)
    , mRequestIndex(requestIndex)
  {}

  // stack based only
  NS_IMETHOD_(nsrefcnt) AddRef() { return 1; }
  NS_IMETHOD_(nsrefcnt) Release() { return 1; }

  NS_IMETHOD QueryInterface(const nsIID &aIID, void **aInstancePtr);

  NS_IMETHOD OnMessageAvailable(PRUint32 aSenderID, const nsID &aTarget,
                                const PRUint8 *aData, PRUint32 aDataLen)
  {
    // accept special "client dead" messages for a given peer
    // (empty target id, zero data and data length)
#ifndef VBOX
    if (aSenderID == mPeer && aTarget.Equals(nsID()) && !aData && !aDataLen)
        return NS_OK;
#else /* VBOX */
    if (aSenderID != IPC_SENDER_ANY && aTarget.Equals(nsID()) && !aData && !aDataLen)
    {
        // Insert new client down information. Start by expiring outdated
        // entries and free one element if there's still no space (if needed).
        uint64_t now = RTTimeMilliTS();
        while (!g_ClientDownList.empty())
        {
            ClientDownInfo *cInfo = g_ClientDownList.back();
            PRInt64 diff = (PRInt64)now - cInfo->uTimestamp;
            if (diff < 0)
                diff += (PRInt64)((uint64_t)-1) + 1;
            if (diff > 15 * 60 * RT_MS_1SEC)
            {
                g_ClientDownMap.erase(cInfo->uClient);
                g_ClientDownList.pop_back();
                NS_ASSERTION(g_ClientDownMap.size() == g_ClientDownList.size(),
                             "client down info inconsistency during expiry");
                delete cInfo;
            }
            else
                break;
        }

        ClientDownMap::iterator it = g_ClientDownMap.find(aSenderID);
        if (it == g_ClientDownMap.end())
        {
            /* Getting size of a map is O(1), size of a list can be O(n). */
            while (g_ClientDownMap.size() >= MAX_CLIENT_DOWN_SIZE)
            {
                ClientDownInfo *cInfo = g_ClientDownList.back();
                g_ClientDownMap.erase(cInfo->uClient);
                g_ClientDownList.pop_back();
                NS_ASSERTION(g_ClientDownMap.size() == g_ClientDownList.size(),
                             "client down info inconsistency during emergency evicting");
                delete cInfo;
            }

            ClientDownInfo *cInfo = new ClientDownInfo(aSenderID);
            RTMEM_MAY_LEAK(cInfo); /* tstVBoxAPIPerf leaks one allocated during ComPtr<IVirtualBoxClient>::createInprocObject(). */
            g_ClientDownMap[aSenderID] = cInfo;
            g_ClientDownList.push_front(cInfo);
            NS_ASSERTION(g_ClientDownMap.size() == g_ClientDownList.size(),
                         "client down info inconsistency after adding entry");
        }
        return (aSenderID == mPeer) ? NS_OK : IPC_WAIT_NEXT_MESSAGE;
    }
    // accept special "client up" messages for a given peer
    // (empty target id, zero data and data length=1)
    if (aTarget.Equals(nsID()) && !aData && aDataLen == 1)
    {
        ClientDownMap::iterator it = g_ClientDownMap.find(aSenderID);
        if (it != g_ClientDownMap.end())
        {
            ClientDownInfo *cInfo = it->second;
            g_ClientDownMap.erase(it);
            g_ClientDownList.remove(cInfo);
            NS_ASSERTION(g_ClientDownMap.size() == g_ClientDownList.size(),
                         "client down info inconsistency in client up case");
            delete cInfo;
        }
        return (aSenderID == mPeer) ? NS_OK : IPC_WAIT_NEXT_MESSAGE;
    }
    // accept special "client check" messages for an anonymous sender
    // (invalid sender id, empty target id, zero data and data length
    if (aSenderID == IPC_SENDER_ANY && aTarget.Equals(nsID()) && !aData && !aDataLen)
    {
        Log(("DConnectMsgSelector::OnMessageAvailable: poll liveness for mPeer=%d\n",
             mPeer));
        ClientDownMap::iterator it = g_ClientDownMap.find(mPeer);
        return (it == g_ClientDownMap.end()) ? IPC_WAIT_NEXT_MESSAGE : NS_OK;
    }
#endif /* VBOX */
    const DConnectOp *op = (const DConnectOp *) aData;
    // accept only reply messages with the given peer/opcode/index
    // (to prevent eating replies the other thread might be waiting for)
    // as well as any non-reply messages (to serve external requests that
    // might arrive while we're waiting for the given reply).
    if (aDataLen >= sizeof(DConnectOp) &&
        ((op->opcode_major != DCON_OP_SETUP_REPLY &&
	        op->opcode_major != DCON_OP_INVOKE_REPLY) ||
         (aSenderID == mPeer &&
          op->opcode_major == mOpCodeMajor &&
          op->request_index == mRequestIndex)))
      return NS_OK;
    else
      return IPC_WAIT_NEXT_MESSAGE;
  }

  const PRUint32 mPeer;
  const PRUint8 mOpCodeMajor;
  const PRUint32 mRequestIndex;
};
NS_IMPL_QUERY_INTERFACE1(DConnectMsgSelector, ipcIMessageObserver)

class DConnectCompletion : public ipcIMessageObserver
{
public:
  DConnectCompletion(PRUint32 peer, PRUint8 opCodeMajor, PRUint32 requestIndex)
    : mSelector(peer, opCodeMajor, requestIndex)
  {}

  // stack based only
  NS_IMETHOD_(nsrefcnt) AddRef() { return 1; }
  NS_IMETHOD_(nsrefcnt) Release() { return 1; }

  NS_IMETHOD QueryInterface(const nsIID &aIID, void **aInstancePtr);

  NS_IMETHOD OnMessageAvailable(PRUint32 aSenderID, const nsID &aTarget,
                                const PRUint8 *aData, PRUint32 aDataLen)
  {
    const DConnectOp *op = (const DConnectOp *) aData;
    Log(("DConnectCompletion::OnMessageAvailable: senderID=%d, opcode_major=%d, index=%d (waiting for %d)\n",
         aSenderID, op->opcode_major, op->request_index, mSelector.mRequestIndex));
    if (aSenderID == mSelector.mPeer &&
        op->opcode_major == mSelector.mOpCodeMajor &&
        op->request_index == mSelector.mRequestIndex)
    {
      OnResponseAvailable(aSenderID, op, aDataLen);
    }
    else
    {
      // ensure ipcDConnectService is not deleted before we finish
      nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
      if (dConnect)
        dConnect->OnMessageAvailable(aSenderID, aTarget, aData, aDataLen);
    }
    return NS_OK;
  }

  virtual void OnResponseAvailable(PRUint32 sender, const DConnectOp *op, PRUint32 opLen) = 0;

  DConnectMsgSelector &GetSelector()
  {
     return mSelector;
  }

protected:
  DConnectMsgSelector mSelector;
};
NS_IMPL_QUERY_INTERFACE1(DConnectCompletion, ipcIMessageObserver)

//-----------------------------------------------------------------------------

class DConnectInvokeCompletion : public DConnectCompletion
{
public:
  DConnectInvokeCompletion(PRUint32 peer, const DConnectInvoke *invoke)
    : DConnectCompletion(peer, DCON_OP_INVOKE_REPLY, invoke->request_index)
    , mReply(nsnull)
    , mParamsLen(0)
  {}

  ~DConnectInvokeCompletion() { if (mReply) free(mReply); }

  void OnResponseAvailable(PRUint32 sender, const DConnectOp *op, PRUint32 opLen)
  {
    mReply = (DConnectInvokeReply *) malloc(opLen);
    memcpy(mReply, op, opLen);

    // the length in bytes of the parameter blob
    mParamsLen = opLen - sizeof(*mReply);
  }

  PRBool IsPending() const { return mReply == nsnull; }
  nsresult GetResult() const { return mReply->result; }

  const PRUint8 *Params() const { return (const PRUint8 *) (mReply + 1); }
  PRUint32 ParamsLen() const { return mParamsLen; }

  const DConnectInvokeReply *Reply() const { return mReply; }

private:
  DConnectInvokeReply *mReply;
  PRUint32             mParamsLen;
};

//-----------------------------------------------------------------------------

#define DCONNECT_STUB_ID                           \
{ /* 132c1f14-5442-49cb-8fe6-e60214bbf1db */       \
  0x132c1f14,                                      \
  0x5442,                                          \
  0x49cb,                                          \
  {0x8f, 0xe6, 0xe6, 0x02, 0x14, 0xbb, 0xf1, 0xdb} \
}
static NS_DEFINE_IID(kDConnectStubID, DCONNECT_STUB_ID);

// this class represents the non-local object instance.

class DConnectStub : public nsXPTCStubBase
{
public:
  NS_DECL_ISUPPORTS

  DConnectStub(nsIInterfaceInfo *aIInfo,
               DConAddr aInstance,
               PRUint32 aPeerID)
    : mIInfo(aIInfo)
    , mInstance(aInstance)
    , mPeerID(aPeerID)
    , mCachedISupports(0)
    , mRefCntLevels()
    {}

  NS_HIDDEN ~DConnectStub();

  // return a refcounted pointer to the InterfaceInfo for this object
  // NOTE: on some platforms this MUST not fail or we crash!
  NS_IMETHOD GetInterfaceInfo(nsIInterfaceInfo **aInfo);

  // call this method and return result
  NS_IMETHOD CallMethod(PRUint16 aMethodIndex,
                        const nsXPTMethodInfo *aInfo,
                        nsXPTCMiniVariant *aParams);

  DConAddr Instance() { return mInstance; }
  PRUint32 PeerID()   { return mPeerID; }

  DConnectStubKey::Key GetKey() {
    return DConnectStubKey::Key(mPeerID, mInstance);
  }

  NS_IMETHOD_(nsrefcnt) AddRefIPC();

private:
  nsCOMPtr<nsIInterfaceInfo> mIInfo;

  // uniquely identifies this object instance between peers.
  DConAddr mInstance;

  // the "client id" of our IPC peer.  this guy owns the real object.
  PRUint32 mPeerID;

  // cached nsISupports stub for this object
  DConnectStub *mCachedISupports;

  // stack of reference counter values (protected by
  // ipcDConnectService::StubLock())
  nsDeque mRefCntLevels;
};

NS_IMETHODIMP_(nsrefcnt)
DConnectStub::AddRefIPC()
{
  // in this special version, we memorize the resulting reference count in the
  // associated stack array. This stack is then used by Release() to determine
  // when it is necessary to send a RELEASE request to the peer owning the
  // object in order to balance AddRef() the peer does on DConnectInstance every
  // time it passes an object over IPC.

  // NOTE: this function is to be called from DConnectInstance::CreateStub only!

  nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
  NS_ASSERTION(dConnect, "no ipcDConnectService (uninitialized?)");
  if (!dConnect)
    return 0;

  // dConnect->StubLock() must be already locked here by
  // DConnectInstance::CreateStub

  nsrefcnt count = AddRef();
  mRefCntLevels.Push((void *)(uintptr_t) count);
  return count;
}

nsresult
ipcDConnectService::CreateStub(const nsID &iid, PRUint32 peer, DConAddr instance,
                               DConnectStub **result)
{
  nsresult rv;

  nsCOMPtr<nsIInterfaceInfo> iinfo;
  rv = GetInterfaceInfo(iid, getter_AddRefs(iinfo));
  if (NS_FAILED(rv))
    return rv;

  nsAutoLock lock (mLock);

  if (mDisconnected)
    return NS_ERROR_NOT_INITIALIZED;

  // we also need the stub lock which protects DConnectStub::mRefCntLevels and
  // ipcDConnectService::mStubs
  nsAutoLock stubLock (mStubLock);

  DConnectStub *stub = nsnull;

  // first try to find an existing stub for a given peer and instance
  // (we do not care about IID because every DConAddr instance represents
  // exactly one interface of the real object on the peer's side)
  if (!mStubs.Get(DConnectStubKey::Key(peer, instance), &stub))
  {
    stub = new DConnectStub(iinfo, instance, peer);

    if (NS_UNLIKELY(!stub))
      rv = NS_ERROR_OUT_OF_MEMORY;
    else
    {
      rv = StoreStub(stub);
      if (NS_FAILED(rv))
        delete stub;
    }
  }

  if (NS_SUCCEEDED(rv))
  {
    stub->AddRefIPC();
    *result = stub;
  }

  return rv;
}

nsresult
ipcDConnectService::SerializeInterfaceParam(PIPCMSGWRITER pMsgWriter,
                                            PRUint32 peer, const nsID &iid,
                                            nsISupports *obj,
                                            nsVoidArray &wrappers)
{
  nsAutoLock lock (mLock);

  if (mDisconnected)
      return NS_ERROR_NOT_INITIALIZED;

  // we create an instance wrapper, and assume that the other side will send a
  // RELEASE message when it no longer needs the instance wrapper.  that will
  // usually happen after the call returns.
  //
  // XXX a lazy scheme might be better, but for now simplicity wins.

  // if the interface pointer references a DConnectStub corresponding
  // to an object in the address space of the peer, then no need to
  // create a new wrapper.

  // if the interface pointer references an object for which we already have
  // an existing wrapper, then we use it instead of creating a new one.  this
  // is based on the assumption that a valid COM object always returns exactly
  // the same pointer value in response to every
  // QueryInterface(NS_GET_IID(nsISupports), ...).

  if (!obj)
  {
    // write null address
    DConAddr nullobj = 0;
    IPCMsgWriterPutBytes(pMsgWriter, &nullobj, sizeof(nullobj));
  }
  else
  {
    DConnectStub *stub = nsnull;
    nsresult rv = obj->QueryInterface(kDConnectStubID, (void **) &stub);
    if (NS_SUCCEEDED(rv) && (stub->PeerID() == peer))
    {
      DConAddr p = stub->Instance();
      IPCMsgWriterPutBytes(pMsgWriter, &p, sizeof(p));
    }
    else
    {
      // create instance wrapper

      nsCOMPtr<nsIInterfaceInfo> iinfo;
      rv = GetInterfaceInfo(iid, getter_AddRefs(iinfo));
      if (NS_FAILED(rv))
        return rv;

      DConnectInstance *wrapper = nsnull;

      // first try to find an existing wrapper for the given object
      if (!FindInstanceAndAddRef(peer, obj, &iid, &wrapper))
      {
        wrapper = new DConnectInstance(peer, iinfo, obj);
        if (!wrapper)
          return NS_ERROR_OUT_OF_MEMORY;

        rv = StoreInstance(wrapper);
        if (NS_FAILED(rv))
        {
          delete wrapper;
          return rv;
        }

        // reference the newly created wrapper
        wrapper->AddRef();
      }

      // increase the second, IPC-only, reference counter (mandatory before
      // trying wrappers.AppendElement() to make sure ReleaseIPC() will remove
      // the wrapper from the instance map on failure)
      wrapper->AddRefIPC();

      if (!wrappers.AppendElement(wrapper))
      {
        wrapper->ReleaseIPC();
        wrapper->Release();
        return NS_ERROR_OUT_OF_MEMORY;
      }

      // wrapper remains referenced when passing it to the client
      // (will be released upon DCON_OP_RELEASE)

      // send address of the instance wrapper, and set the low bit to indicate
      // to the remote party that this is a remote instance wrapper.
      PtrBits bits = ((PtrBits)(uintptr_t) wrapper);
      NS_ASSERTION((bits & PTRBITS_REMOTE_BIT) == 0, "remote bit wrong)");
      bits |= PTRBITS_REMOTE_BIT;
      IPCMsgWriterPutBytes(pMsgWriter, &bits, sizeof(bits));
    }
    NS_IF_RELEASE(stub);
  }
  return NS_OK;
}

// NOTE: peer and iid are ignored if bits doesn't contain PTRBITS_REMOTE_BIT
nsresult
ipcDConnectService::DeserializeInterfaceParamBits(PtrBits bits, PRUint32 peer,
                                                  const nsID &iid,
                                                  nsISupports *&obj)
{
  nsresult rv;

  obj = nsnull;

  if (bits & PTRBITS_REMOTE_BIT)
  {
    // pointer is to a remote object.  we need to build a stub.

    bits &= ~PTRBITS_REMOTE_BIT;

    DConnectStub *stub;
    rv = CreateStub(iid, peer, (DConAddr) bits, &stub);
    if (NS_SUCCEEDED(rv))
      obj = stub;
  }
  else if (bits)
  {
    // pointer is to one of our instance wrappers. Replace it with the
    // real instance.

    DConnectInstance *wrapper = (DConnectInstance *) bits;
    // make sure we've been sent a valid wrapper
    if (!CheckInstanceAndAddRef(wrapper, peer))
    {
      NS_NOTREACHED("instance wrapper not found");
      return NS_ERROR_INVALID_ARG;
    }
    obj = wrapper->RealInstance();
    NS_ADDREF(obj);
    NS_RELEASE(wrapper);
  }
  else
  {
    // obj is alredy nsnull
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------

#define EXCEPTION_STUB_ID                          \
{ /* 70578d68-b25e-4370-a70c-89bbe56e6699 */       \
  0x70578d68,                                      \
  0xb25e,                                          \
  0x4370,                                          \
  {0xa7, 0x0c, 0x89, 0xbb, 0xe5, 0x6e, 0x66, 0x99} \
}
static NS_DEFINE_IID(kExceptionStubID, EXCEPTION_STUB_ID);

// ExceptionStub is used to cache all primitive-typed bits of a remote nsIException
// instance (such as the error message or line number) to:
//
// a) reduce the number of IPC calls;
// b) make sure exception information is available to the calling party even if
//    the called party terminates immediately after returning an exception.
//    To achieve this, all cacheable information is serialized together with
//    the instance wrapper itself.

class ExceptionStub : public nsIException
{
public:

  NS_DECL_ISUPPORTS
  NS_DECL_NSIEXCEPTION

  ExceptionStub(const nsACString &aMessage, nsresult aResult,
                const nsACString &aName, const nsACString &aFilename,
                PRUint32 aLineNumber, PRUint32 aColumnNumber,
                DConnectStub *aXcptStub)
    : mMessage(aMessage), mResult(aResult)
    , mName(aName), mFilename(aFilename)
    , mLineNumber (aLineNumber), mColumnNumber (aColumnNumber)
    , mXcptStub (aXcptStub) { NS_ASSERTION(aXcptStub, "NULL"); }

  ~ExceptionStub() {}

  nsIException *Exception() { return (nsIException *)(nsISupports *) mXcptStub; }
  DConnectStub *Stub() { return mXcptStub; }

private:

  nsCString mMessage;
  nsresult mResult;
  nsCString mName;
  nsCString mFilename;
  PRUint32 mLineNumber;
  PRUint32 mColumnNumber;
  nsRefPtr<DConnectStub> mXcptStub;
};

NS_IMPL_THREADSAFE_ADDREF(ExceptionStub)
NS_IMPL_THREADSAFE_RELEASE(ExceptionStub)

NS_IMETHODIMP
ExceptionStub::QueryInterface(const nsID &aIID, void **aInstancePtr)
{
  NS_ASSERTION(aInstancePtr,
               "QueryInterface requires a non-NULL destination!");

  // used to discover if this is an ExceptionStub instance.
  if (aIID.Equals(kExceptionStubID))
  {
    *aInstancePtr = this;
    NS_ADDREF_THIS();
    return NS_OK;
  }

  // regular NS_IMPL_QUERY_INTERFACE1 sequence

  nsISupports* foundInterface = 0;

  if (aIID.Equals(NS_GET_IID(nsIException)))
    foundInterface = NS_STATIC_CAST(nsIException*, this);
  else
  if (aIID.Equals(NS_GET_IID(nsISupports)))
    foundInterface = NS_STATIC_CAST(nsISupports*,
                                    NS_STATIC_CAST(nsIException *, this));
  else
  if (mXcptStub)
  {
    // ask the real nsIException object
    return mXcptStub->QueryInterface(aIID, aInstancePtr);
  }

  nsresult status;
  if (!foundInterface)
    status = NS_NOINTERFACE;
  else
  {
    NS_ADDREF(foundInterface);
    status = NS_OK;
  }
  *aInstancePtr = foundInterface;
  return status;
}

/* readonly attribute string message; */
NS_IMETHODIMP ExceptionStub::GetMessage(char **aMessage)
{
  AssertReturn(aMessage, NS_ERROR_INVALID_POINTER);
  *aMessage = ToNewCString(mMessage);
  return NS_OK;
}

/* readonly attribute nsresult result; */
NS_IMETHODIMP ExceptionStub::GetResult(nsresult *aResult)
{
  AssertReturn(aResult, NS_ERROR_INVALID_POINTER);
  *aResult = mResult;
  return NS_OK;
}

/* readonly attribute string name; */
NS_IMETHODIMP ExceptionStub::GetName(char **aName)
{
  AssertReturn(aName, NS_ERROR_INVALID_POINTER);
  *aName = ToNewCString(mName);
  return NS_OK;
}

/* readonly attribute string filename; */
NS_IMETHODIMP ExceptionStub::GetFilename(char **aFilename)
{
  AssertReturn(aFilename, NS_ERROR_INVALID_POINTER);
  *aFilename = ToNewCString(mFilename);
  return NS_OK;
}

/* readonly attribute PRUint32 lineNumber; */
NS_IMETHODIMP ExceptionStub::GetLineNumber(PRUint32 *aLineNumber)
{
  AssertReturn(aLineNumber, NS_ERROR_INVALID_POINTER);
  *aLineNumber = mLineNumber;
  return NS_OK;
}

/* readonly attribute PRUint32 columnNumber; */
NS_IMETHODIMP ExceptionStub::GetColumnNumber(PRUint32 *aColumnNumber)
{
  AssertReturn(aColumnNumber, NS_ERROR_INVALID_POINTER);
  *aColumnNumber = mColumnNumber;
  return NS_OK;
}

/* readonly attribute nsIStackFrame location; */
NS_IMETHODIMP ExceptionStub::GetLocation(nsIStackFrame **aLocation)
{
  if (Exception())
    return Exception()->GetLocation (aLocation);
  return NS_ERROR_UNEXPECTED;
}

/* readonly attribute nsIException inner; */
NS_IMETHODIMP ExceptionStub::GetInner(nsIException **aInner)
{
  if (Exception())
    return Exception()->GetInner (aInner);
  return NS_ERROR_UNEXPECTED;
}

/* readonly attribute nsISupports data; */
NS_IMETHODIMP ExceptionStub::GetData(nsISupports * *aData)
{
  if (Exception())
    return Exception()->GetData (aData);
  return NS_ERROR_UNEXPECTED;
}

/* string toString (); */
NS_IMETHODIMP ExceptionStub::ToString(char **_retval)
{
  if (Exception())
    return Exception()->ToString (_retval);
  return NS_ERROR_UNEXPECTED;
}

nsresult
ipcDConnectService::SerializeException(PIPCMSGWRITER pMsgWriter,
                                       PRUint32 peer, nsIException *xcpt,
                                       nsVoidArray &wrappers)
{
  PRBool cache_fields = PR_FALSE;

  // first, seralize the nsIException pointer.  The code is merely the same as
  // in SerializeInterfaceParam() except that when the exception to serialize
  // is an ExceptionStub instance and the real instance it stores as mXcpt
  // is a DConnectStub corresponding to an object in the address space of the
  // peer, we simply pass that object back instead of creating a new wrapper.

  {
    nsAutoLock lock (mLock);

    if (mDisconnected)
      return NS_ERROR_NOT_INITIALIZED;

    if (!xcpt)
    {
      // write null address
#ifdef VBOX
      // see ipcDConnectService::DeserializeException()!
      PtrBits bits = 0;
      IPCMsgWriterPutBytes(pMsgWriter, &bits, sizeof(bits));
#else
      IPCMsgWriterPutBytes(pMsgWriter, &xcpt, sizeof(xcpt));
#endif
    }
    else
    {
      ExceptionStub *stub = nsnull;
      nsresult rv = xcpt->QueryInterface(kExceptionStubID, (void **) &stub);
      if (NS_SUCCEEDED(rv) && (stub->Stub()->PeerID() == peer))
      {
        // send the wrapper instance back to the peer
        DConAddr p = stub->Stub()->Instance();
        IPCMsgWriterPutBytes(pMsgWriter, &p, sizeof(p));
      }
      else
      {
        // create instance wrapper

        const nsID &iid = nsIException::GetIID();
        nsCOMPtr<nsIInterfaceInfo> iinfo;
        rv = GetInterfaceInfo(iid, getter_AddRefs(iinfo));
        if (NS_FAILED(rv))
          return rv;

        DConnectInstance *wrapper = nsnull;

        // first try to find an existing wrapper for the given object
        if (!FindInstanceAndAddRef(peer, xcpt, &iid, &wrapper))
        {
          wrapper = new DConnectInstance(peer, iinfo, xcpt);
          if (!wrapper)
            return NS_ERROR_OUT_OF_MEMORY;

          rv = StoreInstance(wrapper);
          if (NS_FAILED(rv))
          {
            delete wrapper;
            return rv;
          }

          // reference the newly created wrapper
          wrapper->AddRef();
        }

        // increase the second, IPC-only, reference counter (mandatory before
        // trying wrappers.AppendElement() to make sure ReleaseIPC() will remove
        // the wrapper from the instance map on failure)
        wrapper->AddRefIPC();

        if (!wrappers.AppendElement(wrapper))
        {
          wrapper->ReleaseIPC();
          wrapper->Release();
          return NS_ERROR_OUT_OF_MEMORY;
        }

        // wrapper remains referenced when passing it to the client
        // (will be released upon DCON_OP_RELEASE)

        // send address of the instance wrapper, and set the low bit to indicate
        // to the remote party that this is a remote instance wrapper.
        PtrBits bits = ((PtrBits)(uintptr_t) wrapper) | PTRBITS_REMOTE_BIT;
        IPCMsgWriterPutBytes(pMsgWriter, &bits, sizeof(bits));

        // we want to cache fields to minimize the number of IPC calls when
        // accessing exception data on the peer side
        cache_fields = PR_TRUE;
      }
      NS_IF_RELEASE(stub);
    }
  }

  if (!cache_fields)
    return NS_OK;

  nsresult rv;
  nsXPIDLCString str;
  PRUint32 num;

  // message
  rv = xcpt->GetMessage(getter_Copies(str));
  if (NS_SUCCEEDED (rv))
  {
    PRUint32 len = str.Length();
    nsACString::const_iterator begin;
    const char *data = str.BeginReading(begin).get();
    IPCMsgWriterPutU32(pMsgWriter, len);
    IPCMsgWriterPutBytes(pMsgWriter, data, len);
  }
  else
    IPCMsgWriterPutU32(pMsgWriter, 0);

  // result
  nsresult res = 0;
  xcpt->GetResult(&res);
  IPCMsgWriterPutU32(pMsgWriter, res);

  // name
  rv = xcpt->GetName(getter_Copies(str));
  if (NS_SUCCEEDED (rv))
  {
    PRUint32 len = str.Length();
    nsACString::const_iterator begin;
    const char *data = str.BeginReading(begin).get();
    IPCMsgWriterPutU32(pMsgWriter, len);
    IPCMsgWriterPutBytes(pMsgWriter, data, len);
  }
  else
    IPCMsgWriterPutU32(pMsgWriter, 0);

  // filename
  rv = xcpt->GetFilename(getter_Copies(str));
  if (NS_SUCCEEDED (rv))
  {
    PRUint32 len = str.Length();
    nsACString::const_iterator begin;
    const char *data = str.BeginReading(begin).get();
    IPCMsgWriterPutU32(pMsgWriter, len);
    IPCMsgWriterPutBytes(pMsgWriter, data, len);
  }
  else
    IPCMsgWriterPutU32(pMsgWriter, 0);

  // lineNumber
  num = 0;
  xcpt->GetLineNumber(&num);
  IPCMsgWriterPutU32(pMsgWriter, num);

  // columnNumber
  num = 0;
  xcpt->GetColumnNumber(&num);
  IPCMsgWriterPutU32(pMsgWriter, num);

  return IPCMsgWriterHasError(pMsgWriter) ? NS_ERROR_OUT_OF_MEMORY : NS_OK;
}

nsresult
ipcDConnectService::DeserializeException(PIPCMSGREADER pMsgReader,
                                         PRUint32 peer,
                                         nsIException **xcpt)
{
  NS_ASSERTION (xcpt, "NULL");
  if (!xcpt)
    return NS_ERROR_INVALID_POINTER;

  nsresult rv;
  PRUint32 len;

  PtrBits bits = 0;
  IPCMsgReaderReadBytes(pMsgReader, &bits, sizeof(DConAddr));
  if (IPCMsgReaderHasError(pMsgReader))
    return NS_ERROR_INVALID_ARG;

  if (bits & PTRBITS_REMOTE_BIT)
  {
    // pointer is a peer-side exception instance wrapper,
    // read cahced exception data and create a stub for it.

    nsCAutoString message;
    len = IPCMsgReaderGetU32(pMsgReader);
    if (len)
    {
      message.SetLength(len);
      char *buf = message.BeginWriting();
      IPCMsgReaderReadBytes(pMsgReader, buf, len);
    }

    nsresult result = IPCMsgReaderGetU32(pMsgReader);

    nsCAutoString name;
    len = IPCMsgReaderGetU32(pMsgReader);
    if (len)
    {
      name.SetLength(len);
      char *buf = name.BeginWriting();
      IPCMsgReaderReadBytes(pMsgReader, buf, len);
    }

    nsCAutoString filename;
    len = IPCMsgReaderGetU32(pMsgReader);
    if (len)
    {
      filename.SetLength(len);
      char *buf = filename.BeginWriting();
      IPCMsgReaderReadBytes(pMsgReader, buf, len);
    }

    PRUint32 lineNumber = IPCMsgReaderGetU32(pMsgReader);
    PRUint32 columnNumber = IPCMsgReaderGetU32(pMsgReader);

    if (IPCMsgReaderHasError(pMsgReader))
      rv = NS_ERROR_INVALID_ARG;
    else
    {
      DConAddr addr = (DConAddr) (bits & ~PTRBITS_REMOTE_BIT);
      nsRefPtr<DConnectStub> stub;
      rv = CreateStub(nsIException::GetIID(), peer, addr,
                      getter_AddRefs(stub));
      if (NS_SUCCEEDED(rv))
      {
        // create a special exception "stub" with cached error info
        ExceptionStub *xcptStub =
          new ExceptionStub (message, result,
                             name, filename,
                             lineNumber, columnNumber,
                             stub);
        if (xcptStub)
        {
          *xcpt = xcptStub;
          NS_ADDREF(xcptStub);
        }
        else
          rv = NS_ERROR_OUT_OF_MEMORY;
      }
    }
  }
  else if (bits)
  {
    // pointer is to our instance wrapper for nsIException we've sent before
    // (the remote method we've called had called us back and got an exception
    // from us that it decided to return as its own result). Replace it with
    // the real instance.
    DConnectInstance *wrapper = (DConnectInstance *) bits;
    if (CheckInstanceAndAddRef(wrapper, peer))
    {
      *xcpt = (nsIException *) wrapper->RealInstance();
      NS_ADDREF(wrapper->RealInstance());
      wrapper->Release();
      rv = NS_OK;
    }
    else
    {
      NS_NOTREACHED("instance wrapper not found");
      rv = NS_ERROR_INVALID_ARG;
    }
  }
  else
  {
    // the peer explicitly passed us a NULL exception to indicate that the
    // exception on the current thread should be reset
    *xcpt = NULL;
    return NS_OK;
  }


  return rv;
}

//-----------------------------------------------------------------------------

DConnectStub::~DConnectStub()
{
#ifdef LOG_ENABLED
  const char *name = NULL;
  mIInfo->GetNameShared(&name);
  Log(("{%p} DConnectStub::<dtor>(): peer=%d instance=0x%Lx {%s}\n",
       this, mPeerID, mInstance, name));
#endif

  // release the cached nsISupports instance if it's not the same object
  if (mCachedISupports != 0 && mCachedISupports != this)
    NS_RELEASE(mCachedISupports);
}

NS_IMETHODIMP_(nsrefcnt)
DConnectStub::AddRef()
{
  nsrefcnt count;
  count = ASMAtomicIncU32((volatile uint32_t *)&mRefCnt);
  NS_LOG_ADDREF(this, count, "DConnectStub", sizeof(*this));
  return count;
}

NS_IMETHODIMP_(nsrefcnt)
DConnectStub::Release()
{
  nsrefcnt count;

  nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
  if (dConnect)
  {
    // lock the stub lock on every release to make sure that once the counter
    // drops to zero, we delete the stub from the set of stubs before a new
    // request to create a stub on other thread tries to find the existing
    // stub in the set (wchich could otherwise AddRef the object after it had
    // Released to zero and pass it to the client right before its
    // destruction).
    nsAutoLock stubLock (dConnect->StubLock());

    count = ASMAtomicDecU32((volatile uint32_t *)&mRefCnt);
    NS_LOG_RELEASE(this, count, "DConnectStub");


#ifdef LOG_ENABLED
    const char *name;
    mIInfo->GetNameShared(&name);
    Log(("{%p} DConnectStub::Release(): peer=%d instance=0x%Lx {%s}, new count=%d\n",
        this, mPeerID, mInstance, name, count));
#endif

    // mRefCntLevels may already be empty here (due to the "stabilize" trick below)
    if (mRefCntLevels.GetSize() > 0)
    {
      nsrefcnt top = (nsrefcnt) (long) mRefCntLevels.Peek();
      NS_ASSERTION(top <= count + 1, "refcount is beyond the top level");

      if (top == count + 1)
      {
        // refcount dropped to a value stored in ipcDConnectService::CreateStub.
        // Send a RELEASE request to the peer (see also AddRefIPC).

        // remove the top refcount value
        mRefCntLevels.Pop();

        if (0 == count)
        {
          // this is the last reference, remove from the set before we leave
          // the lock, to provide atomicity of these two operations
          dConnect->DeleteStub (this);

          NS_ASSERTION(mRefCntLevels.GetSize() == 0, "refcnt levels are still left");
        }

        // leave the lock before sending a message
        stubLock.unlock();

        nsresult rv;

        DConnectRelease msg;
        msg.opcode_major = DCON_OP_RELEASE;
        msg.opcode_minor = 0;
        msg.flags = 0;
        msg.request_index = 0; // not used, set to some unused value
        msg.instance = mInstance;

        // fire off asynchronously... we don't expect any response to this message.
        rv = IPC_SendMessage(mPeerID, kDConnectTargetID,
                           (const PRUint8 *) &msg, sizeof(msg));
        if (NS_FAILED(rv))
          NS_WARNING("failed to send RELEASE event");
      }
    }
  }
  else
  {
    count = ASMAtomicDecU32((volatile uint32_t *)&mRefCnt);
    NS_LOG_RELEASE(this, count, "DConnectStub");
  }

  if (0 == count)
  {
    mRefCnt = 1; /* stabilize */
    delete this;
    return 0;
  }

  return count;
}

NS_IMETHODIMP
DConnectStub::QueryInterface(const nsID &aIID, void **aInstancePtr)
{
  // used to discover if this is a DConnectStub instance.
  if (aIID.Equals(kDConnectStubID))
  {
    *aInstancePtr = this;
    NS_ADDREF_THIS();
    return NS_OK;
  }

  // In order to truely support the COM Identity Rule across processes,
  // we need to make the following code work:
  //
  //   IFoo *foo = ...
  //   nsISupports unk;
  //   foo->QueryInterface(NS_GET_IID(nsISupports), (void **) &unk);
  //   unk->Release();
  //   nsISupports unk2;
  //   foo->QueryInterface(NS_GET_IID(nsISupports), (void **) &unk2);
  //   Assert (unk == unk2);
  //
  // I.e. querying nsISupports on the same object must always return the same
  // pointer, even if the nsISupports object returned for the first time is
  // released before it is requested for the second time, as long as the
  // original object is kept alive (referenced by the client) between these
  // two queries.
  //
  // This is done by remembering the nsISupports stub returned by the peer
  // when nsISupports is queried for the first time. The remembered stub, when
  // it is not the same as this object, is strongly referenced in order to
  // keep it alive (and therefore have the same pointer value) as long as this
  // object is alive.
  //
  // Besides supporting the Identity Rule, this also reduces the number of IPC
  // calls, since an IPC call requesting nsISupports will be done only once
  // per every stub object.

  nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
  NS_ASSERTION(dConnect, "no ipcDConnectService (uninitialized?)");
  if (!dConnect)
    return NS_ERROR_NOT_INITIALIZED;

  nsresult rv;
  PRBool needISupports = aIID.Equals(NS_GET_IID(nsISupports));

  if (needISupports)
  {
    // XXX it would be sufficient to use cmpxchg here to protect access to
    // mCachedISupports, but NSPR doesn't provide cross-platform cmpxchg
    // functionality, so we have to use a shared lock instead...
    RTSemFastMutexRequest(dConnect->StubQILock());

    // check if we have already got a nsISupports stub for this object
    if (mCachedISupports != 0)
    {
      *aInstancePtr = mCachedISupports;
      NS_ADDREF(mCachedISupports);

      RTSemFastMutexRelease(dConnect->StubQILock());
      return NS_OK;
    }

    // check if this object is nsISupports itself
    {
      nsIID *iid = 0;
      rv = mIInfo->GetInterfaceIID(&iid);
      NS_ASSERTION(NS_SUCCEEDED(rv) && iid,
                   "nsIInterfaceInfo::GetInterfaceIID failed");
      if (NS_SUCCEEDED(rv) && iid &&
          iid->Equals(NS_GET_IID(nsISupports)))
      {
        RTMemFree((void*)iid);

        // nsISupports is queried on nsISupports, return ourselves
        *aInstancePtr = this;
        NS_ADDREF_THIS();
        // cache ourselves weakly
        mCachedISupports = this;

        RTSemFastMutexRelease(dConnect->StubQILock());
        return NS_OK;
      }
      if (iid)
        RTMemFree((void*)iid);
    }

    // stub lock remains held until we've queried the peer
  }

  // else, we need to query the peer object by making an IPC call

#ifdef LOG_ENABLED
  {
    const char *name;
    mIInfo->GetNameShared(&name);
    const char *nameQ;
    nsCOMPtr <nsIInterfaceInfo> iinfoQ;
    dConnect->GetInterfaceInfo(aIID, getter_AddRefs(iinfoQ));
    if (iinfoQ) {
        iinfoQ->GetNameShared(&nameQ);
        Log(("calling QueryInterface {%s} on peer object (stub=%p, instance=0x%Lx {%s})\n",
             nameQ, this, mInstance, name));
    }
  }
#endif

  DConnectSetupQueryInterface msg;
  msg.opcode_minor = DCON_OP_SETUP_QUERY_INTERFACE;
  msg.iid = aIID;
  msg.instance = mInstance;

  rv = SetupPeerInstance(mPeerID, &msg, sizeof(msg), aInstancePtr);

  if (needISupports)
  {
    if (NS_SUCCEEDED(rv))
    {
      // cache the nsISupports object (SetupPeerInstance returns DConnectStub)
      mCachedISupports = (DConnectStub *) *aInstancePtr;
      // use a weak reference if nsISupports is the same object as us
      if (this != mCachedISupports)
        NS_ADDREF(mCachedISupports);
    }

    RTSemFastMutexRelease(dConnect->StubQILock());
  }

  return rv;
}

NS_IMETHODIMP
DConnectStub::GetInterfaceInfo(nsIInterfaceInfo **aInfo)
{
  NS_ADDREF(*aInfo = mIInfo);
  return NS_OK;
}

NS_IMETHODIMP
DConnectStub::CallMethod(PRUint16 aMethodIndex,
                         const nsXPTMethodInfo *aInfo,
                         nsXPTCMiniVariant *aParams)
{
  Log(("DConnectStub::CallMethod [methodIndex=%hu]\n", aMethodIndex));

  nsresult rv;

  // reset the exception early.  this is necessary because we may return a
  // failure from here without setting an exception (which might be expected
  // by the caller to detect the error origin: the interface we are stubbing
  // may indicate in some way that it always sets the exception info on
  // failure, therefore an "infoless" failure means the origin is RPC).
  // besides that, resetting the excetion before every IPC call is exactly the
  // same thing as Win32 RPC does, so doing this is useful for getting
  // similarity in behaviors.

  nsCOMPtr <nsIExceptionService> es;
  es = do_GetService (NS_EXCEPTIONSERVICE_CONTRACTID, &rv);
  if (NS_FAILED (rv))
    return rv;
  nsCOMPtr <nsIExceptionManager> em;
  rv = es->GetCurrentExceptionManager (getter_AddRefs(em));
  if (NS_FAILED (rv))
    return rv;
  rv = em->SetCurrentException(NULL);
  if (NS_FAILED (rv))
    return rv;

  // ensure ipcDConnectService is not deleted before we finish
  nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
  if (!dConnect)
    return NS_ERROR_FAILURE;

  // dump arguments

  PRUint8 i, paramCount = aInfo->GetParamCount();

#ifdef LOG_ENABLED
  {
    const char *name;
    nsCOMPtr<nsIInterfaceInfo> iinfo;
    GetInterfaceInfo(getter_AddRefs(iinfo));
    iinfo->GetNameShared(&name);
    Log(("  instance=0x%Lx {%s}\n", mInstance, name));
    Log(("  name=%s\n", aInfo->GetName()));
    Log(("  param-count=%u\n", (PRUint32) paramCount));
  }
#endif

  IPCMSGWRITER MsgWriter;
  IPCMsgWriterInit(&MsgWriter, 64 + 16 * paramCount);

  // INVOKE message header
  DConnectInvoke invoke;
  invoke.opcode_major = DCON_OP_INVOKE;
  invoke.opcode_minor = 0;
  invoke.flags = 0;
  invoke.request_index = NewRequestIndex();
  invoke.instance = mInstance;
  invoke.method_index = aMethodIndex;

  Log(("  request-index=%d\n", (PRUint32) invoke.request_index));

  IPCMsgWriterPutBytes(&MsgWriter, &invoke, sizeof(invoke));

  // list of wrappers that get created during parameter serialization.  if we
  // are unable to send the INVOKE message, then we'll clean these up.
  nsVoidArray wrappers;

  for (i=0; i<paramCount; ++i)
  {
    const nsXPTParamInfo &paramInfo = aInfo->GetParam(i);

    if (paramInfo.IsIn() && !paramInfo.IsDipper())
    {
      const nsXPTType &type = paramInfo.GetType();

      if (type.IsInterfacePointer())
      {
        nsID iid;
        rv = dConnect->GetIIDForMethodParam(mIInfo, aInfo, paramInfo, type,
                                             aMethodIndex, aParams, PR_FALSE, iid);
        if (NS_SUCCEEDED(rv))
          rv = dConnect->SerializeInterfaceParam(&MsgWriter, mPeerID, iid,
                                                 (nsISupports *) aParams[i].val.p,
                                                 wrappers);
      }
      else
        rv = SerializeParam(&MsgWriter, type, aParams[i]);

      AssertMsgBreak(NS_SUCCEEDED(rv), ("i=%d rv=%#x\n", i, rv));
    }
    else if ((paramInfo.IsOut() || paramInfo.IsRetval()) && !aParams[i].val.p)
    {
      // report error early if NULL pointer is passed as an output parameter
      rv = NS_ERROR_NULL_POINTER;
      AssertMsgFailedBreak(("i=%d IsOut=%d IsRetval=%d NS_ERROR_NULL_POINTER\n", i, paramInfo.IsOut(), paramInfo.IsRetval()));
      break;
    }
  }

  if (NS_FAILED(rv))
  {
    // INVOKE message wasn't sent; clean up wrappers
    dConnect->ReleaseWrappers(wrappers, mPeerID);
    IPCMsgWriterCleanup(&MsgWriter);
    return rv;
  }

  // serialize input array parameters after everything else since the
  // deserialization procedure will need to get a size_is value which may be
  // stored in any preceeding or following param
  for (i=0; i<paramCount; ++i)
  {
    const nsXPTParamInfo &paramInfo = aInfo->GetParam(i);

    if (paramInfo.GetType().IsArray() &&
        paramInfo.IsIn() && !paramInfo.IsDipper())
    {
      rv = SerializeArrayParam(dConnect, &MsgWriter, mPeerID, mIInfo, aMethodIndex,
                               *aInfo, aParams, PR_FALSE, paramInfo,
                               aParams[i].val.p, wrappers);
      if (NS_FAILED(rv))
      {
        // INVOKE message wasn't sent; clean up wrappers
        dConnect->ReleaseWrappers(wrappers, mPeerID);
        IPCMsgWriterCleanup(&MsgWriter);
        return rv;
      }
    }
  }

  // temporarily disable the DConnect target observer to block normal processing
  // of pending messages through the event queue.
  IPC_DISABLE_MESSAGE_OBSERVER_FOR_SCOPE(kDConnectTargetID);

  rv = IPC_SendMessage(mPeerID, kDConnectTargetID,
                       IPCMsgWriterGetBuf(&MsgWriter),
                       IPCMsgWriterGetSize(&MsgWriter));
  IPCMsgWriterCleanup(&MsgWriter);

  Log(("DConnectStub::CallMethod: IPC_SendMessage()=%08X\n", rv));
  if (NS_FAILED(rv))
  {
    // INVOKE message wasn't delivered; clean up wrappers
    dConnect->ReleaseWrappers(wrappers, mPeerID);
    return rv;
  }

  // now, we wait for the method call to complete.  during that time, it's
  // possible that we'll receive other method call requests.  we'll process
  // those while waiting for out method call to complete.  it's critical that
  // we do so since those other method calls might need to complete before
  // out method call can complete!

  DConnectInvokeCompletion completion(mPeerID, &invoke);

  do
  {
    rv = IPC_WaitMessage(IPC_SENDER_ANY, kDConnectTargetID,
                         &completion.GetSelector(), &completion,
                         DCON_WAIT_TIMEOUT);
    Log(("DConnectStub::CallMethod: IPC_WaitMessage()=%08X\n", rv));
    if (NS_FAILED(rv))
    {
      // INVOKE message wasn't received; clean up wrappers
      dConnect->ReleaseWrappers(wrappers, mPeerID);
      return rv;
    }
  }
  while (completion.IsPending());

  IPCMSGREADER MsgReader;
  IPCMsgReaderInit(&MsgReader, completion.Params(), completion.ParamsLen());

  rv = completion.GetResult();
  if (NS_SUCCEEDED(rv))
  {
    PRUint8 i;

    // handle out-params and retvals: DCON_OP_INVOKE_REPLY has the data
    for (i=0; i<paramCount; ++i)
    {
      const nsXPTParamInfo &paramInfo = aInfo->GetParam(i);

      if (paramInfo.IsOut() || paramInfo.IsRetval())
        DeserializeResult(&MsgReader, paramInfo.GetType(), aParams[i]);
    }

    // fixup any interface pointers using a second pass so we can properly
    // handle INTERFACE_IS referencing an IID that is an out param! This pass is
    // also used to deserialize arrays (array data goes after all other params).
    for (i=0; i<paramCount && NS_SUCCEEDED(rv); ++i)
    {
      const nsXPTParamInfo &paramInfo = aInfo->GetParam(i);
      if ((paramInfo.IsOut() || paramInfo.IsRetval()) && aParams[i].val.p)
      {
        const nsXPTType &type = paramInfo.GetType();
        if (type.IsInterfacePointer())
        {
          // grab the DConAddr value temporarily stored in the param, restore
          // the pointer and free the temporarily allocated memory.
          DConAddrPlusPtr *dptr = (DConAddrPlusPtr *)aParams[i].val.p;
          PtrBits bits = dptr->addr;
          aParams[i].val.p = dptr->p;
          RTMemFree((void *)dptr);

          // DeserializeInterfaceParamBits needs IID only if it's a remote object
          nsID iid;
          if (bits & PTRBITS_REMOTE_BIT)
            rv = dConnect->GetIIDForMethodParam(mIInfo, aInfo, paramInfo, type,
                                                aMethodIndex, aParams, PR_FALSE,
                                                iid);
          if (NS_SUCCEEDED(rv))
          {
            nsISupports *obj = nsnull;
            rv = dConnect->DeserializeInterfaceParamBits(bits, mPeerID, iid, obj);
            if (NS_SUCCEEDED(rv))
              *(void **)aParams[i].val.p = obj;
          }
        }
        else if (type.IsArray())
        {
          void *array = nsnull;
          rv = DeserializeArrayParam(dConnect, &MsgReader, mPeerID, mIInfo,
                                     aMethodIndex, *aInfo, aParams, PR_FALSE,
                                     paramInfo, PR_TRUE, array);
          if (NS_SUCCEEDED(rv))
            *(void **)aParams[i].val.p = array;
        }
      }
    }
  }

  if (completion.Reply()->flags & DCON_OP_FLAGS_REPLY_EXCEPTION)
  {
    Log(("got nsIException instance, will create a stub\n"));

    nsIException *xcpt = nsnull;
    rv = dConnect->DeserializeException (&MsgReader, mPeerID, &xcpt);
    if (NS_SUCCEEDED(rv))
    {
      rv = em->SetCurrentException(xcpt);
      NS_IF_RELEASE(xcpt);
    }
    NS_ASSERTION(NS_SUCCEEDED(rv), "failed to deserialize/set exception");
  }

  return NS_SUCCEEDED(rv) ? completion.GetResult() : rv;
}

//-----------------------------------------------------------------------------

class DConnectSetupCompletion : public DConnectCompletion
{
public:
  DConnectSetupCompletion(PRUint32 peer, const DConnectSetup *setup)
    : DConnectCompletion(peer, DCON_OP_SETUP_REPLY, setup->request_index)
    , mSetup(setup)
    , mStatus(NS_OK)
  {}

  void OnResponseAvailable(PRUint32 sender, const DConnectOp *op, PRUint32 opLen)
  {
    if (op->opcode_major != DCON_OP_SETUP_REPLY)
    {
      NS_NOTREACHED("unexpected response");
      mStatus = NS_ERROR_UNEXPECTED;
      return;
    }

    if (opLen < sizeof(DConnectSetupReply))
    {
      NS_NOTREACHED("unexpected response size");
      mStatus = NS_ERROR_UNEXPECTED;
      return;
    }

    const DConnectSetupReply *reply = (const DConnectSetupReply *) op;

    Log(("got SETUP_REPLY: status=%x instance=0x%Lx\n", reply->status, reply->instance));

    mStatus = reply->status;

    if (NS_SUCCEEDED(reply->status))
    {
      // ensure ipcDConnectService is not deleted before we finish
      nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
      nsresult rv;
      if (dConnect)
        rv = dConnect->CreateStub(mSetup->iid, sender, reply->instance,
                                  getter_AddRefs(mStub));
      else
        rv = NS_ERROR_FAILURE;
      if (NS_FAILED(rv))
        mStatus = rv;
    }

    if (reply->flags & DCON_OP_FLAGS_REPLY_EXCEPTION)
    {
      const PRUint8 *params = ((const PRUint8 *) op) + sizeof (DConnectSetupReply);
      const PRUint32 paramsLen = opLen - sizeof (DConnectSetupReply);

      IPCMSGREADER MsgReader;
      IPCMsgReaderInit(&MsgReader, params, paramsLen);

      Log(("got nsIException instance, will create a stub\n"));

      nsresult rv;
      nsCOMPtr <nsIExceptionService> es;
      es = do_GetService (NS_EXCEPTIONSERVICE_CONTRACTID, &rv);
      if (NS_SUCCEEDED(rv))
      {
        nsCOMPtr <nsIExceptionManager> em;
        rv = es->GetCurrentExceptionManager (getter_AddRefs(em));
        if (NS_SUCCEEDED(rv))
        {
          // ensure ipcDConnectService is not deleted before we finish
          nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
          if (dConnect)
          {
            nsIException *xcpt = nsnull;
            rv = dConnect->DeserializeException (&MsgReader, sender, &xcpt);
            if (NS_SUCCEEDED(rv))
            {
              rv = em->SetCurrentException(xcpt);
              NS_IF_RELEASE(xcpt);
            }
          }
          else
            rv = NS_ERROR_UNEXPECTED;
        }
      }
      NS_ASSERTION(NS_SUCCEEDED(rv), "failed to deserialize/set exception");
      if (NS_FAILED(rv))
        mStatus = rv;
    }
  }

  nsresult GetStub(void **aInstancePtr)
  {
    if (NS_FAILED(mStatus))
      return mStatus;

    DConnectStub *stub = mStub;
    NS_IF_ADDREF(stub);
    *aInstancePtr = stub;
    return NS_OK;
  }

private:
  const DConnectSetup    *mSetup;
  nsresult                mStatus;
  nsRefPtr<DConnectStub>  mStub;
};

// static
nsresult
SetupPeerInstance(PRUint32 aPeerID, DConnectSetup *aMsg, PRUint32 aMsgLen,
                  void **aInstancePtr)
{
  *aInstancePtr = nsnull;

  aMsg->opcode_major = DCON_OP_SETUP;
  aMsg->flags = 0;
  aMsg->request_index = NewRequestIndex();

  // temporarily disable the DConnect target observer to block normal processing
  // of pending messages through the event queue.
  IPC_DISABLE_MESSAGE_OBSERVER_FOR_SCOPE(kDConnectTargetID);

  // send SETUP message, expect SETUP_REPLY

  nsresult rv = IPC_SendMessage(aPeerID, kDConnectTargetID,
                                (const PRUint8 *) aMsg, aMsgLen);
  if (NS_FAILED(rv))
    return rv;

  DConnectSetupCompletion completion(aPeerID, aMsg);

  // need to allow messages from other clients to be processed immediately
  // to avoid distributed dead locks.  the completion's OnMessageAvailable
  // will call our default OnMessageAvailable if it receives any message
  // other than the one for which it is waiting.

  do
  {
    rv = IPC_WaitMessage(IPC_SENDER_ANY, kDConnectTargetID,
                         &completion.GetSelector(), &completion,
                         DCON_WAIT_TIMEOUT);
    if (NS_FAILED(rv))
      break;

    rv = completion.GetStub(aInstancePtr);
  }
  while (NS_SUCCEEDED(rv) && *aInstancePtr == nsnull);

  return rv;
}

//-----------------------------------------------------------------------------

ipcDConnectService::ipcDConnectService()
 : mLock(NIL_RTSEMFASTMUTEX)
 , mStubLock(NIL_RTSEMFASTMUTEX)
 , mDisconnected(PR_TRUE)
 , mStubQILock(NIL_RTSEMFASTMUTEX)
 , mhReqPool(NIL_RTREQPOOL)
{
}

PR_STATIC_CALLBACK(PLDHashOperator)
EnumerateInstanceMapAndDelete (const DConnectInstanceKey::Key &aKey,
                               DConnectInstance *aData,
                               void *userArg)
{
  // this method is to be called on ipcDConnectService shutdown only
  // (after which no DConnectInstances may exist), so forcibly delete them
  // disregarding the reference counter

#ifdef LOG_ENABLED
  const char *name;
  aData->InterfaceInfo()->GetNameShared(&name);
  Log(("ipcDConnectService: WARNING: deleting unreleased instance=%p iface=%p {%s}\n", aData, aData->RealInstance(), name));
#endif

  delete aData;
  return PL_DHASH_NEXT;
}

ipcDConnectService::~ipcDConnectService()
{
  if (!mDisconnected)
    Shutdown();

  mInstance = nsnull;
  if (mStubQILock != NIL_RTSEMFASTMUTEX)
    RTSemFastMutexDestroy(mStubQILock);
  if (mStubLock != NIL_RTSEMFASTMUTEX)
    RTSemFastMutexDestroy(mStubLock);
  if (mLock != NIL_RTSEMFASTMUTEX)
    RTSemFastMutexDestroy(mLock);

  RTReqPoolRelease(mhReqPool);
  mhReqPool = NIL_RTREQPOOL;
}

//-----------------------------------------------------------------------------

nsresult
ipcDConnectService::Init()
{
  nsresult rv;

  Log(("ipcDConnectService::Init.\n"));

  rv = IPC_DefineTarget(kDConnectTargetID, this);
  if (NS_FAILED(rv))
    return rv;

  rv = IPC_AddClientObserver(this);
  if (NS_FAILED(rv))
    return rv;

  int vrc = RTSemFastMutexCreate(&mLock);
  if (RT_FAILURE(vrc))
    return NS_ERROR_OUT_OF_MEMORY;

  if (!mInstances.Init())
    return NS_ERROR_OUT_OF_MEMORY;
  if (!mInstanceSet.Init())
    return NS_ERROR_OUT_OF_MEMORY;

  vrc = RTSemFastMutexCreate(&mStubLock);
  if (RT_FAILURE(vrc))
    return NS_ERROR_OUT_OF_MEMORY;

  if (!mStubs.Init())
    return NS_ERROR_OUT_OF_MEMORY;

  mIIM = do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv))
    return rv;

  vrc = RTSemFastMutexCreate(&mStubQILock);
  if (RT_FAILURE(vrc))
    return NS_ERROR_OUT_OF_MEMORY;

  vrc = RTReqPoolCreate(1024 /*cMaxThreads*/, 10*RT_MS_1SEC /*cMsMinIdle*/,
                        64 /*cThreadsPushBackThreshold*/, RT_MS_1SEC /* cMsMaxPushBack */,
                        "DCon", &mhReqPool);
  if (RT_FAILURE(vrc))
  {
    mhReqPool = NIL_RTREQPOOL;
    return NS_ERROR_FAILURE;
  }

  mDisconnected = PR_FALSE;
  mInstance = this;

  Log(("ipcDConnectService::Init NS_OK.\n"));
  return NS_OK;
}

void
ipcDConnectService::Shutdown()
{
  {
    // set the disconnected flag to make sensitive public methods
    // unavailale from other (non worker) threads.
    nsAutoLock lock(mLock);
    mDisconnected = PR_TRUE;
  }

#if defined(DCONNECT_STATS)
  fprintf(stderr, "ipcDConnectService Stats\n");
  fprintf(stderr,
          " => number of worker threads:  %llu (created %llu)\n"
          " => requests processed:        %llu\n"
          " => avg requests process time: %llu ns\n"
          " => avg requests waiting time: %llu ns\n",
          (unsigned long long)RTReqPoolGetStat(mhReqPool, RTREQPOOLSTAT_THREADS),
          (unsigned long long)RTReqPoolGetStat(mhReqPool, RTREQPOOLSTAT_THREADS_CREATED),
          (unsigned long long)RTReqPoolGetStat(mhReqPool, RTREQPOOLSTAT_REQUESTS_PROCESSED),
          (unsigned long long)RTReqPoolGetStat(mhReqPool, RTREQPOOLSTAT_NS_AVERAGE_REQ_PROCESSING),
          (unsigned long long)RTReqPoolGetStat(mhReqPool, RTREQPOOLSTAT_NS_AVERAGE_REQ_QUEUED)
          );
#endif

  RTReqPoolRelease(mhReqPool);
  mhReqPool = NIL_RTREQPOOL;

  // make sure we have released all instances
  mInstances.EnumerateRead(EnumerateInstanceMapAndDelete, nsnull);

  mInstanceSet.Clear();
  mInstances.Clear();

  // clear the stub table
  // (this will not release stubs -- it's the client's responsibility)
  mStubs.Clear();
}

// this should be inlined
nsresult
ipcDConnectService::GetInterfaceInfo(const nsID &iid, nsIInterfaceInfo **result)
{
  return mIIM->GetInfoForIID(&iid, result);
}

// this is adapted from the version in xpcwrappednative.cpp
nsresult
ipcDConnectService::GetIIDForMethodParam(nsIInterfaceInfo *iinfo,
                                         const nsXPTMethodInfo *methodInfo,
                                         const nsXPTParamInfo &paramInfo,
                                         const nsXPTType &type,
                                         PRUint16 methodIndex,
                                         nsXPTCMiniVariant *dispatchParams,
                                         PRBool isXPTCVariantArray,
                                         nsID &result)
{
  PRUint8 argnum, tag = type.TagPart();
  nsresult rv;

  if (tag == nsXPTType::T_INTERFACE)
  {
    rv = iinfo->GetIIDForParamNoAlloc(methodIndex, &paramInfo, &result);
  }
  else if (tag == nsXPTType::T_INTERFACE_IS)
  {
    rv = iinfo->GetInterfaceIsArgNumberForParam(methodIndex, &paramInfo, &argnum);
    if (NS_FAILED(rv))
      return rv;

    const nsXPTParamInfo& arg_param = methodInfo->GetParam(argnum);
    const nsXPTType& arg_type = arg_param.GetType();

    // The xpidl compiler ensures this. We reaffirm it for safety.
    if (!arg_type.IsPointer() || arg_type.TagPart() != nsXPTType::T_IID)
      return NS_ERROR_UNEXPECTED;

    nsID *p = (nsID *) GET_PARAM(dispatchParams, isXPTCVariantArray, argnum).val.p;
    if (!p)
      return NS_ERROR_UNEXPECTED;

    result = *p;
  }
  else
    rv = NS_ERROR_UNEXPECTED;
  return rv;
}

nsresult
ipcDConnectService::StoreInstance(DConnectInstance *wrapper)
{
#ifdef LOG_ENABLED
  const char *name;
  wrapper->InterfaceInfo()->GetNameShared(&name);
  Log(("ipcDConnectService::StoreInstance(): instance=%p iface=%p {%s}\n",
       wrapper, wrapper->RealInstance(), name));
#endif

  nsresult rv = mInstanceSet.Put(wrapper);
  if (NS_SUCCEEDED(rv))
  {
    rv = mInstances.Put(wrapper->GetKey(), wrapper)
      ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
    if (NS_FAILED(rv))
      mInstanceSet.Remove(wrapper);
  }
  return rv;
}

void
ipcDConnectService::DeleteInstance(DConnectInstance *wrapper,
                                   PRBool locked /* = PR_FALSE */)
{
  if (!locked)
    RTSemFastMutexRequest(mLock);

#ifdef LOG_ENABLED
  const char *name;
  wrapper->InterfaceInfo()->GetNameShared(&name);
  Log(("ipcDConnectService::DeleteInstance(): instance=%p iface=%p {%s}\n",
       wrapper, wrapper->RealInstance(), name));
#endif

  mInstances.Remove(wrapper->GetKey());
  mInstanceSet.Remove(wrapper);

  if (!locked)
    RTSemFastMutexRelease(mLock);
}

PRBool
ipcDConnectService::FindInstanceAndAddRef(PRUint32 peer,
                                          const nsISupports *obj,
                                          const nsIID *iid,
                                          DConnectInstance **wrapper)
{
  PRBool result = mInstances.Get(DConnectInstanceKey::Key(peer, obj, iid), wrapper);
  if (result)
    (*wrapper)->AddRef();
  return result;
}

PRBool
ipcDConnectService::CheckInstanceAndAddRef(DConnectInstance *wrapper, PRUint32 peer)
{
  nsAutoLock lock (mLock);

  if (mInstanceSet.Contains(wrapper) && wrapper->Peer() == peer)
  {
    wrapper->AddRef();
    return PR_TRUE;
  }
  return PR_FALSE;
}

nsresult
ipcDConnectService::StoreStub(DConnectStub *stub)
{
#ifdef LOG_ENABLED
  const char *name;
  nsCOMPtr<nsIInterfaceInfo> iinfo;
  stub->GetInterfaceInfo(getter_AddRefs(iinfo));
  iinfo->GetNameShared(&name);
  Log(("ipcDConnectService::StoreStub(): stub=%p instance=0x%Lx {%s}\n",
       stub, stub->Instance(), name));
#endif

  return mStubs.Put(stub->GetKey(), stub)
      ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

void
ipcDConnectService::DeleteStub(DConnectStub *stub)
{
#ifdef LOG_ENABLED
  const char *name;
  nsCOMPtr<nsIInterfaceInfo> iinfo;
  stub->GetInterfaceInfo(getter_AddRefs(iinfo));
  iinfo->GetNameShared(&name);
  Log(("ipcDConnectService::DeleteStub(): stub=%p instance=0x%Lx {%s}\n",
       stub, stub->Instance(), name));
#endif

  // this method is intended to be called only from DConnectStub::Release().
  // the stub object is not deleted when removed from the table, because
  // DConnectStub pointers are not owned by mStubs.
  mStubs.Remove(stub->GetKey());
}

// not currently used
#if 0
PRBool
ipcDConnectService::FindStubAndAddRef(PRUint32 peer, const DConAddr instance,
                                      DConnectStub **stub)
{
  nsAutoLock stubLock (mStubLock);

  PRBool result = mStubs.Get(DConnectStubKey::Key(peer, instance), stub);
  if (result)
    NS_ADDREF(*stub);
  return result;
}
#endif

NS_IMPL_THREADSAFE_ISUPPORTS3(ipcDConnectService, ipcIDConnectService,
                                                  ipcIMessageObserver,
                                                  ipcIClientObserver)

NS_IMETHODIMP
ipcDConnectService::CreateInstance(PRUint32 aPeerID,
                                   const nsID &aCID,
                                   const nsID &aIID,
                                   void **aInstancePtr)
{
  DConnectSetupClassID msg;
  msg.opcode_minor = DCON_OP_SETUP_NEW_INST_CLASSID;
  msg.iid = aIID;
  msg.classid = aCID;

  return SetupPeerInstance(aPeerID, &msg, sizeof(msg), aInstancePtr);
}

NS_IMETHODIMP
ipcDConnectService::CreateInstanceByContractID(PRUint32 aPeerID,
                                               const char *aContractID,
                                               const nsID &aIID,
                                               void **aInstancePtr)
{
  size_t slen = strlen(aContractID);
  size_t size = sizeof(DConnectSetupContractID) + slen;

  DConnectSetupContractID *msg =
      (DConnectSetupContractID *) malloc(size);

  msg->opcode_minor = DCON_OP_SETUP_NEW_INST_CONTRACTID;
  msg->iid = aIID;
  memcpy(&msg->contractid, aContractID, slen + 1);

  nsresult rv = SetupPeerInstance(aPeerID, msg, size, aInstancePtr);

  free(msg);
  return rv;
}

NS_IMETHODIMP
ipcDConnectService::GetService(PRUint32 aPeerID,
                               const nsID &aCID,
                               const nsID &aIID,
                               void **aInstancePtr)
{
  DConnectSetupClassID msg;
  msg.opcode_minor = DCON_OP_SETUP_GET_SERV_CLASSID;
  msg.iid = aIID;
  msg.classid = aCID;

  return SetupPeerInstance(aPeerID, &msg, sizeof(msg), aInstancePtr);
}

NS_IMETHODIMP
ipcDConnectService::GetServiceByContractID(PRUint32 aPeerID,
                                           const char *aContractID,
                                           const nsID &aIID,
                                           void **aInstancePtr)
{
  size_t slen = strlen(aContractID);
  size_t size = sizeof(DConnectSetupContractID) + slen;

  DConnectSetupContractID *msg =
      (DConnectSetupContractID *) malloc(size);

  msg->opcode_minor = DCON_OP_SETUP_GET_SERV_CONTRACTID;
  msg->iid = aIID;
  memcpy(&msg->contractid, aContractID, slen + 1);

  nsresult rv = SetupPeerInstance(aPeerID, msg, size, aInstancePtr);

  free(msg);
  return rv;
}

//-----------------------------------------------------------------------------

NS_IMETHODIMP
ipcDConnectService::OnMessageAvailable(PRUint32 aSenderID,
                                       const nsID &aTarget,
                                       const PRUint8 *aData,
                                       PRUint32 aDataLen)
{
  if (mDisconnected)
    return NS_ERROR_NOT_INITIALIZED;

#ifdef LOG_ENABLED
  const DConnectOp *op = (const DConnectOp *) aData;
  Log(("ipcDConnectService::OnMessageAvailable: senderID=%d, opcode_major=%d, index=%d\n",
       aSenderID, op->opcode_major, op->request_index));
#endif

  void *pvDataDup = RTMemDup(aData, aDataLen);
  if (RT_UNLIKELY(!pvDataDup))
    return NS_ERROR_OUT_OF_MEMORY;
  int rc = RTReqPoolCallVoidNoWait(mhReqPool, (PFNRT)ProcessMessageOnWorkerThread, 4,
                                   this, aSenderID, pvDataDup, aDataLen);
  if (RT_FAILURE(rc))
    return NS_ERROR_FAILURE;

  return NS_OK;
}

struct PruneInstanceMapForPeerArgs
{
  ipcDConnectService *that;
  PRUint32 clientID;
  nsVoidArray &wrappers;
};

PR_STATIC_CALLBACK(PLDHashOperator)
PruneInstanceMapForPeer (const DConnectInstanceKey::Key &aKey,
                         DConnectInstance *aData,
                         void *userArg)
{
  PruneInstanceMapForPeerArgs *args = (PruneInstanceMapForPeerArgs *)userArg;
  NS_ASSERTION(args, "PruneInstanceMapForPeerArgs is NULL");

  if (args && args->clientID == aData->Peer())
  {
    nsrefcnt countIPC = aData->ReleaseIPC(PR_TRUE /* locked */);

    Log(("ipcDConnectService::PruneInstanceMapForPeer: instance=%p: %d IPC refs to release\n", aData, countIPC + 1));

    // release all IPC instances of the "officially dead" client (see
    // #OnRelease() to understand why it must be done under the lock). Note
    // that due to true multithreading, late OnRelease() requests may still
    // happen on other worker threads *after* OnClientStateChange() has been
    // called, but it's OK because the instance will be removed from the map
    // by the below code alreay and won't be deleted for the second time.
    while (countIPC)
    {
      countIPC = aData->ReleaseIPC(PR_TRUE /* locked */);
      aData->Release();
    }

    // collect the instance for the last release
    // (we'll do it later outside the lock)
    if (!args->wrappers.AppendElement(aData))
    {
      NS_NOTREACHED("Not enough memory");
      // bad but what to do
      aData->Release();
    }
  }
  return PL_DHASH_NEXT;
}

NS_IMETHODIMP
ipcDConnectService::OnClientStateChange(PRUint32 aClientID,
                                        PRUint32 aClientState)
{
  Log(("ipcDConnectService::OnClientStateChange: aClientID=%d, aClientState=%d\n",
       aClientID, aClientState));

  if (aClientState == ipcIClientObserver::CLIENT_DOWN)
  {
    if (aClientID == IPC_SENDER_ANY)
    {
      // a special case: our IPC system is being shutdown, try to safely
      // uninitialize everything...
      Shutdown();
    }
    else
    {
      Log(("ipcDConnectService::OnClientStateChange: pruning all instances created for peer %d...\n", aClientID));

      nsVoidArray wrappers;

      {
        nsAutoLock lock (mLock);

        // make sure we have removed all instances from instance maps
        PruneInstanceMapForPeerArgs args = { this, aClientID, wrappers };
        mInstances.EnumerateRead(PruneInstanceMapForPeer, (void *)&args);
      }

      Log(("ipcDConnectService::OnClientStateChange: %d lost instances\n", wrappers.Count()));

      // release all pending references left after PruneInstanceMapForPeer().
      // this may call wrapper destructors so it's important to do that
      // outside the lock because destructors will release the real objects
      // which may need to make asynchronous use our service
      for (PRInt32 i = 0; i < wrappers.Count(); ++i)
        ((DConnectInstance *) wrappers[i])->Release();
    }
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------

/**
 * Function called by the request thread pool to process a incoming request in
 * the context of a worker thread.
 */
/* static */ DECLCALLBACK(void)
ipcDConnectService::ProcessMessageOnWorkerThread(ipcDConnectService *aThis, PRUint32 aSenderID, void *aData, PRUint32 aDataLen)
{
  if (!aThis->mDisconnected)
    aThis->OnIncomingRequest(aSenderID, (const DConnectOp *)aData, aDataLen);
  RTMemFree(aData);
}

void
ipcDConnectService::OnIncomingRequest(PRUint32 peer, const DConnectOp *op, PRUint32 opLen)
{
  switch (op->opcode_major)
  {
    case DCON_OP_SETUP:
      OnSetup(peer, (const DConnectSetup *) op, opLen);
      break;
    case DCON_OP_RELEASE:
      OnRelease(peer, (const DConnectRelease *) op);
      break;
    case DCON_OP_INVOKE:
      OnInvoke(peer, (const DConnectInvoke *) op, opLen);
      break;
    default:
      NS_NOTREACHED("unknown opcode major");
  }
}

void
ipcDConnectService::OnSetup(PRUint32 peer, const DConnectSetup *setup, PRUint32 opLen)
{
  nsISupports *instance = nsnull;
  nsresult rv = NS_ERROR_FAILURE;

  switch (setup->opcode_minor)
  {
    // CreateInstance
    case DCON_OP_SETUP_NEW_INST_CLASSID:
    {
      const DConnectSetupClassID *setupCI = (const DConnectSetupClassID *) setup;

      nsCOMPtr<nsIComponentManager> compMgr;
      rv = NS_GetComponentManager(getter_AddRefs(compMgr));
      if (NS_SUCCEEDED(rv))
        rv = compMgr->CreateInstance(setupCI->classid, nsnull, setupCI->iid, (void **) &instance);

      break;
    }

    // CreateInstanceByContractID
    case DCON_OP_SETUP_NEW_INST_CONTRACTID:
    {
      const DConnectSetupContractID *setupCI = (const DConnectSetupContractID *) setup;

      nsCOMPtr<nsIComponentManager> compMgr;
      rv = NS_GetComponentManager(getter_AddRefs(compMgr));
      if (NS_SUCCEEDED(rv))
        rv = compMgr->CreateInstanceByContractID(setupCI->contractid, nsnull, setupCI->iid, (void **) &instance);

      break;
    }

    // GetService
    case DCON_OP_SETUP_GET_SERV_CLASSID:
    {
      const DConnectSetupClassID *setupCI = (const DConnectSetupClassID *) setup;

      nsCOMPtr<nsIServiceManager> svcMgr;
      rv = NS_GetServiceManager(getter_AddRefs(svcMgr));
      if (NS_SUCCEEDED(rv))
        rv = svcMgr->GetService(setupCI->classid, setupCI->iid, (void **) &instance);
      break;
    }

    // GetServiceByContractID
    case DCON_OP_SETUP_GET_SERV_CONTRACTID:
    {
      const DConnectSetupContractID *setupCI = (const DConnectSetupContractID *) setup;

      nsCOMPtr<nsIServiceManager> svcMgr;
      rv = NS_GetServiceManager(getter_AddRefs(svcMgr));
      if (NS_SUCCEEDED(rv))
        rv = svcMgr->GetServiceByContractID(setupCI->contractid, setupCI->iid, (void **) &instance);

      break;
    }

    // QueryInterface
    case DCON_OP_SETUP_QUERY_INTERFACE:
    {
      const DConnectSetupQueryInterface *setupQI = (const DConnectSetupQueryInterface *) setup;
      DConnectInstance *QIinstance = (DConnectInstance *)setupQI->instance;

      // make sure we've been sent a valid wrapper
      if (!CheckInstanceAndAddRef(QIinstance, peer))
      {
        NS_NOTREACHED("instance wrapper not found");
        rv = NS_ERROR_INVALID_ARG;
      }
      else
      {
        rv = QIinstance->RealInstance()->QueryInterface(setupQI->iid, (void **) &instance);
        QIinstance->Release();
      }
      break;
    }

    default:
      NS_NOTREACHED("unexpected minor opcode");
      rv = NS_ERROR_UNEXPECTED;
      break;
  }

  nsVoidArray wrappers;

  // now, create instance wrapper, and store it in our instances set.
  // this allows us to keep track of object references held on behalf of a
  // particular peer.  we can use this information to cleanup after a peer
  // that disconnects without sending RELEASE messages for its objects.
  DConnectInstance *wrapper = nsnull;
  if (NS_SUCCEEDED(rv))
  {
    nsCOMPtr<nsIInterfaceInfo> iinfo;
    rv = GetInterfaceInfo(setup->iid, getter_AddRefs(iinfo));
    if (NS_SUCCEEDED(rv))
    {
      nsAutoLock lock (mLock);

      // first try to find an existing wrapper for the given object
      if (!FindInstanceAndAddRef(peer, instance, &setup->iid, &wrapper))
      {
        wrapper = new DConnectInstance(peer, iinfo, instance);
        if (!wrapper)
          rv = NS_ERROR_OUT_OF_MEMORY;
        else
        {
          rv = StoreInstance(wrapper);
          if (NS_FAILED(rv))
          {
            delete wrapper;
            wrapper = nsnull;
          }
          else
          {
            // reference the newly created wrapper
            wrapper->AddRef();
          }
        }
      }

      if (wrapper)
      {
        // increase the second, IPC-only, reference counter (mandatory before
        // trying wrappers.AppendElement() to make sure ReleaseIPC() will remove
        // the wrapper from the instance map on failure)
        wrapper->AddRefIPC();

        if (!wrappers.AppendElement(wrapper))
        {
          wrapper->ReleaseIPC();
          wrapper->Release();
          rv = NS_ERROR_OUT_OF_MEMORY;
        }
      }

      // wrapper remains referenced when passing it to the client
      // (will be released upon DCON_OP_RELEASE)
    }
  }

  NS_IF_RELEASE(instance);

  nsCOMPtr <nsIException> exception;
  PRBool got_exception = PR_FALSE;

  if (rv != NS_OK)
  {
    // try to fetch an nsIException possibly set by one of the setup methods
    nsresult rv2;
    nsCOMPtr <nsIExceptionService> es;
    es = do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID, &rv2);
    if (NS_SUCCEEDED(rv2))
    {
      nsCOMPtr <nsIExceptionManager> em;
      rv2 = es->GetCurrentExceptionManager (getter_AddRefs (em));
      if (NS_SUCCEEDED(rv2))
      {
        rv2 = em->GetCurrentException (getter_AddRefs (exception));
        if (NS_SUCCEEDED(rv2))
        {
          Log(("got nsIException instance, will serialize\n"));
          got_exception = PR_TRUE;
        }
      }
    }
    NS_ASSERTION(NS_SUCCEEDED(rv2), "failed to get/serialize exception");
    if (NS_FAILED(rv2))
      rv = rv2;
  }

  IPCMSGWRITER MsgWriter;
  IPCMsgWriterInit(&MsgWriter, 64 /*cbInitial*/);

  DConnectSetupReply msg;
  msg.opcode_major = DCON_OP_SETUP_REPLY;
  msg.opcode_minor = 0;
  msg.flags = 0;
  msg.request_index = setup->request_index;
  msg.instance = (DConAddr)(uintptr_t)wrapper;
  msg.status = rv;

  if (got_exception)
    msg.flags |= DCON_OP_FLAGS_REPLY_EXCEPTION;

  IPCMsgWriterPutBytes(&MsgWriter, &msg, sizeof(msg));

  if (got_exception)
  {
    rv = SerializeException(&MsgWriter, peer, exception, wrappers);
    NS_ASSERTION(NS_SUCCEEDED(rv), "failed to get/serialize exception");
  }

  // fire off SETUP_REPLY, don't wait for a response
  if (NS_FAILED(rv))
    rv = IPC_SendMessage(peer, kDConnectTargetID,
                         (const PRUint8 *) &msg, sizeof(msg));
  else
    rv = IPC_SendMessage(peer, kDConnectTargetID,
                         IPCMsgWriterGetBuf(&MsgWriter), IPCMsgWriterGetSize(&MsgWriter));

  if (NS_FAILED(rv))
  {
    Log(("unable to send SETUP_REPLY: rv=%x\n", rv));
    ReleaseWrappers(wrappers, peer);
  }

  IPCMsgWriterCleanup(&MsgWriter);
}

void
ipcDConnectService::OnRelease(PRUint32 peer, const DConnectRelease *release)
{
  Log(("ipcDConnectService::OnRelease [peer=%u instance=0x%Lx]\n",
       peer, release->instance));

  DConnectInstance *wrapper = (DConnectInstance *)release->instance;

  nsAutoLock lock (mLock);

  // make sure we've been sent a valid wrapper from the same peer we created
  // this wrapper for
  if (mInstanceSet.Contains(wrapper) && wrapper->Peer() == peer)
  {
    // release the IPC reference from under the lock to ensure atomicity of
    // the "check + possible delete" sequence ("delete" is remove this wrapper
    // from the instance map when the IPC reference counter drops to zero)
    wrapper->ReleaseIPC(PR_TRUE /* locked */);
    // leave the lock before Release() because it may call the destructor
    // which will release the real object which may need to make asynchronous
    // use our service
    lock.unlock();
    wrapper->Release();
  }
  else
  {
    // it is possible that the client disconnection event handler has released
    // all client instances before the DCON_OP_RELEASE message sent by the
    // client gets processed here (because of true multithreading). Just log
    // a debug warning
    Log(("ipcDConnectService::OnRelease: WARNING: instance wrapper %p for peer %d not found\n", wrapper, peer));
  }
}

void
ipcDConnectService::OnInvoke(PRUint32 peer, const DConnectInvoke *invoke, PRUint32 opLen)
{
  Log(("ipcDConnectService::OnInvoke [peer=%u instance=0x%Lx method=%u]\n",
      peer, invoke->instance, invoke->method_index));

  DConnectInstance *wrapper = (DConnectInstance *)invoke->instance;

  IPCMSGREADER MsgReader;
  IPCMsgReaderInit(&MsgReader, (const PRUint8 *) (invoke + 1), opLen - sizeof(*invoke));

  const nsXPTMethodInfo *methodInfo;
  nsXPTCVariant *params = nsnull;
  nsCOMPtr<nsIInterfaceInfo> iinfo = nsnull;
  PRUint8 i, paramCount = 0, paramUsed = 0;
  nsresult rv;

  nsCOMPtr <nsIException> exception;
  PRBool got_exception = PR_FALSE;

  // make sure we've been sent a valid wrapper
  if (!CheckInstanceAndAddRef(wrapper, peer))
  {
    NS_NOTREACHED("instance wrapper not found");
    wrapper = nsnull;
    rv = NS_ERROR_INVALID_ARG;
    goto end;
  }

  iinfo = wrapper->InterfaceInfo();

  rv = iinfo->GetMethodInfo(invoke->method_index, &methodInfo);
  if (NS_FAILED(rv))
    goto end;

  paramCount = methodInfo->GetParamCount();

  Log(("  iface=%p\n", wrapper->RealInstance()));
  Log(("  name=%s\n", methodInfo->GetName()));
  Log(("  param-count=%u\n", (PRUint32) paramCount));
  Log(("  request-index=%d\n", (PRUint32) invoke->request_index));

  if (paramCount)
  {
    params = (nsXPTCVariant *)RTMemAllocZ(sizeof(nsXPTCVariant) * paramCount);
    if (!params)
    {
      rv = NS_ERROR_OUT_OF_MEMORY;
      goto end;
    }
  }

  // setup |params| for xptcall

  for (i=0; i<paramCount; ++i, ++paramUsed)
  {
    const nsXPTParamInfo &paramInfo = methodInfo->GetParam(i);

    // XXX are inout params an issue?
    // yes, we will need to do v.ptr = &v.val for them (DeserializeParam doesn't
    // currently do that) to let the callee correctly pick it up and change.

    if (paramInfo.IsIn() && !paramInfo.IsDipper())
      rv = DeserializeParam(&MsgReader, paramInfo.GetType(), params[i]);
    else
      rv = SetupParam(paramInfo, params[i]);

    if (NS_FAILED(rv))
      goto end;
  }

  // fixup any interface pointers.  we do this with a second pass so that
  // we can properly handle INTERFACE_IS. This pass is also used to deserialize
  // arrays (array data goes after all other params).
  for (i=0; i<paramCount; ++i)
  {
    const nsXPTParamInfo &paramInfo = methodInfo->GetParam(i);
    if (paramInfo.IsIn())
    {
      const nsXPTType &type = paramInfo.GetType();
      if (type.IsInterfacePointer())
      {
        // grab the DConAddr value temporarily stored in the param
#ifdef VBOX
        PtrBits bits = params[i].val.u64;
#else
        PtrBits bits = (PtrBits)(uintptr_t) params[i].val.p;
#endif

        // DeserializeInterfaceParamBits needs IID only if it's a remote object
        nsID iid;
        if (bits & PTRBITS_REMOTE_BIT)
        {
          rv = GetIIDForMethodParam(iinfo, methodInfo, paramInfo, type,
                                    invoke->method_index, params, PR_TRUE, iid);
          if (NS_FAILED(rv))
            goto end;
        }

        nsISupports *obj = nsnull;
        rv = DeserializeInterfaceParamBits(bits, peer, iid, obj);
        if (NS_FAILED(rv))
          goto end;

        params[i].val.p = obj;
        // mark as interface to let FinishParam() release this param
        params[i].SetValIsInterface();
      }
      else if (type.IsArray())
      {
        void *array = nsnull;
        rv = DeserializeArrayParam(this, &MsgReader, peer, iinfo,
                                   invoke->method_index, *methodInfo, params,
                                   PR_TRUE, paramInfo, PR_FALSE, array);
        if (NS_FAILED(rv))
          goto end;

        params[i].val.p = array;
        // mark to let FinishParam() free this param
        params[i].SetValIsAllocated();
      }
    }
  }

  rv = XPTC_InvokeByIndex(wrapper->RealInstance(),
                          invoke->method_index,
                          paramCount,
                          params);

  if (rv != NS_OK)
  {
    // try to fetch an nsIException possibly set by the method
    nsresult rv2;
    nsCOMPtr <nsIExceptionService> es;
    es = do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID, &rv2);
    if (NS_SUCCEEDED(rv2))
    {
      nsCOMPtr <nsIExceptionManager> em;
      rv2 = es->GetCurrentExceptionManager (getter_AddRefs (em));
      if (NS_SUCCEEDED(rv2))
      {
        rv2 = em->GetCurrentException (getter_AddRefs (exception));
        if (NS_SUCCEEDED(rv2))
        {
          Log(("got nsIException instance, will serialize\n"));
          got_exception = PR_TRUE;
        }
      }
    }
    NS_ASSERTION(NS_SUCCEEDED(rv2), "failed to get/serialize exception");
    if (NS_FAILED(rv2))
      rv = rv2;
  }

end:
  Log(("sending INVOKE_REPLY: rv=%x\n", rv));

  // balance CheckInstanceAndAddRef()
  if (wrapper)
    wrapper->Release();

  IPCMSGWRITER MsgWriter;
  IPCMsgWriterInit(&MsgWriter, 64 /*cbInitial*/);

  DConnectInvokeReply reply;
  reply.opcode_major = DCON_OP_INVOKE_REPLY;
  reply.opcode_minor = 0;
  reply.flags = 0;
  reply.request_index = invoke->request_index;
  reply.result = rv;

  if (got_exception)
    reply.flags |= DCON_OP_FLAGS_REPLY_EXCEPTION;

  IPCMsgWriterPutBytes(&MsgWriter, &reply, sizeof(reply));

  nsVoidArray wrappers;

  if (NS_SUCCEEDED(rv) && params)
  {
    // serialize out-params and retvals
    for (i=0; i<paramCount; ++i)
    {
      const nsXPTParamInfo paramInfo = methodInfo->GetParam(i);

      if (paramInfo.IsRetval() || paramInfo.IsOut())
      {
        const nsXPTType &type = paramInfo.GetType();

        if (type.IsInterfacePointer())
        {
          nsID iid;
          rv = GetIIDForMethodParam(iinfo, methodInfo, paramInfo, type,
                                    invoke->method_index, params, PR_TRUE, iid);
          if (NS_SUCCEEDED(rv))
            rv = SerializeInterfaceParam(&MsgWriter, peer, iid,
                                         (nsISupports *) params[i].val.p, wrappers);
        }
        else
          rv = SerializeParam(&MsgWriter, type, params[i]);

        if (NS_FAILED(rv))
        {
          reply.result = rv;
          break;
        }
      }
    }

    if (NS_SUCCEEDED(rv))
    {
      // serialize output array parameters after everything else since the
      // deserialization procedure will need to get a size_is value which may be
      // stored in any preceeding or following param
      for (i=0; i<paramCount; ++i)
      {
        const nsXPTParamInfo &paramInfo = methodInfo->GetParam(i);

        if (paramInfo.GetType().IsArray() &&
            (paramInfo.IsRetval() || paramInfo.IsOut()))
        {
          rv = SerializeArrayParam(this, &MsgWriter, peer, iinfo, invoke->method_index,
                                   *methodInfo, params, PR_TRUE, paramInfo,
                                   params[i].val.p, wrappers);
          if (NS_FAILED(rv))
          {
            reply.result = rv;
            break;
          }
        }
      }
    }
  }

  if (got_exception)
  {
    rv = SerializeException(&MsgWriter, peer, exception, wrappers);
    NS_ASSERTION(NS_SUCCEEDED(rv), "failed to get/serialize exception");
  }

  if (NS_FAILED(rv))
    rv = IPC_SendMessage(peer, kDConnectTargetID, (const PRUint8 *) &reply, sizeof(reply));
  else
    rv = IPC_SendMessage(peer, kDConnectTargetID, IPCMsgWriterGetBuf(&MsgWriter), IPCMsgWriterGetSize(&MsgWriter));
  if (NS_FAILED(rv))
  {
    Log(("unable to send INVOKE_REPLY: rv=%x\n", rv));
    ReleaseWrappers(wrappers, peer);
  }

  if (params)
  {
    // free individual elements of arrays (note: before freeing arrays
    // themselves in FinishParam())
    for (i=0; i<paramUsed; ++i)
    {
      const nsXPTParamInfo &paramInfo = methodInfo->GetParam(i);
      if (paramInfo.GetType().IsArray())
        FinishArrayParam(iinfo, invoke->method_index, *methodInfo,
                         params, PR_TRUE, paramInfo, params[i]);
    }

    for (i=0; i<paramUsed; ++i)
      FinishParam(params[i]);
    RTMemFree(params);
  }

  IPCMsgWriterCleanup(&MsgWriter);
}

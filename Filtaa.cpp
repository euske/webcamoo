//  Filtaa.cpp

#include <stdio.h>
#include <windows.h>
#include <dshow.h>
#include "Filtaa.h"

static HRESULT copyMediaType(AM_MEDIA_TYPE* dst, const AM_MEDIA_TYPE* src)
{
    if (src == NULL) return E_POINTER;
    if (dst == NULL) return E_POINTER;
    BYTE* fmt = (BYTE*)CoTaskMemAlloc(src->cbFormat);
    if (fmt == NULL) return E_OUTOFMEMORY;
    CopyMemory(dst, src, sizeof(*src));
    CopyMemory(fmt, src->pbFormat, src->cbFormat);
    dst->pbFormat = fmt;
    return S_OK;
}

static HRESULT freeMediaType(AM_MEDIA_TYPE* mt)
{
    if (mt == NULL) return E_POINTER;
    if (mt->pbFormat != NULL) {
        CoTaskMemFree(mt->pbFormat);
    }
    CoTaskMemFree(mt);
    return S_OK;
}

static LPCWSTR mt2str(const AM_MEDIA_TYPE* pmt)
{
    static WCHAR buf[256];
    if (pmt == NULL) return L"<null>";
    swprintf_s(buf, _countof(buf),
               L"<major=%08x, sub=%08x, size=%lu, format=%08x>",
               pmt->majortype.Data1, pmt->subtype.Data1,
               pmt->lSampleSize, pmt->formattype.Data1);
    return buf;
}


// FiltaaInputPinEnumMediaTypes
class FiltaaInputPinEnumMediaTypes : public IEnumMediaTypes
{
private:
    int _refCount;
    AM_MEDIA_TYPE _mts[1];
    int _nmts;
    int _index;

public:
    FiltaaInputPinEnumMediaTypes(int index=0) {
        _refCount = 0;
        ZeroMemory(_mts, sizeof(_mts));
        _mts[0].majortype = MEDIATYPE_Video;
        _mts[0].subtype = MEDIASUBTYPE_RGB24;
        _mts[0].bFixedSizeSamples = TRUE;
        _mts[0].bTemporalCompression = FALSE;
        _mts[0].formattype = FORMAT_VideoInfo;
        _nmts = 0;
        _index = index;
        AddRef();
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID iid, void** ppvObject) {
        if (ppvObject == NULL) return E_POINTER;
        if (iid == IID_IUnknown) {
            *ppvObject = this;
        } else if (iid == IID_IEnumMediaTypes) {
            *ppvObject = (IEnumMediaTypes*)this;
        } else {
            *ppvObject = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() {
        _refCount++; return _refCount;
    }
    STDMETHODIMP_(ULONG) Release() {
        _refCount--;
        if (_refCount) return _refCount;
        delete this;
        return 0;
    }

    // IEnumMediaTypes
    STDMETHODIMP Next(ULONG n, AM_MEDIA_TYPE** ppMediaTypes, ULONG* pFetched) {
        if (ppMediaTypes == NULL) return E_POINTER;
        if (n == 0) return E_INVALIDARG;
        ULONG i = 0;
        while (i < n && _index < _nmts) {
            ppMediaTypes[i++] = &(_mts[_index++]);
        }
        if (pFetched != NULL) {
            *pFetched = i;
        }
        return (i < n)? S_FALSE : S_OK;
    }
    STDMETHODIMP Skip(ULONG n) {
        while (0 < n--) {
            if (1 <= _index) return S_FALSE;
            _index++;
        }
        return S_OK;
    }
    STDMETHODIMP Reset(void) {
        _index = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumMediaTypes** pEnum) {
        if (pEnum == NULL) return E_POINTER;
        *pEnum = new FiltaaInputPinEnumMediaTypes(_index);
        return S_OK;
    }
    
};


//  FiltaaInputPin
//
class FiltaaInputPin : public IPin, public IMemInputPin
{
private:
    int _refCount;
    Filtaa* _filter;
    PIN_DIRECTION _direction;
    LPCWSTR _name;
    IPin* _connected;
    AM_MEDIA_TYPE _mediatype;
    IMemAllocator* _allocator;
    ~FiltaaInputPin();
    
public:
    FiltaaInputPin(Filtaa* filter, LPCWSTR name, PIN_DIRECTION direction);

    LPCWSTR Name()
        { return _name; }

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID iid, void** ppvObject);
    STDMETHODIMP_(ULONG) AddRef() {
        _refCount++; return _refCount;
    }
    STDMETHODIMP_(ULONG) Release() {
        _refCount--;
        if (_refCount) return _refCount;
        delete this;
        return 0;
    }

    // IPin methods
    STDMETHODIMP BeginFlush()
        { fprintf(stderr,"BeginFlush\n"); return S_OK; }
    STDMETHODIMP EndFlush()
        { fprintf(stderr,"EndFlush\n"); return S_OK; }
    STDMETHODIMP EndOfStream()
        { fprintf(stderr,"EndOfStream\n"); return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME , REFERENCE_TIME , double )
        { fprintf(stderr,"NewSegment\n"); return S_OK; }
    
    STDMETHODIMP Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt);
    STDMETHODIMP ConnectedTo(IPin** ppPin);
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* pmt);
    STDMETHODIMP Disconnect();
    STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt);
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** ppEnum);
    STDMETHODIMP QueryId(LPWSTR* Id);
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* pmt);
    STDMETHODIMP QueryDirection(PIN_DIRECTION* pPinDir);
    STDMETHODIMP QueryPinInfo(PIN_INFO* pInfo);
    
    STDMETHODIMP QueryInternalConnections(IPin**, ULONG*)
        { return E_NOTIMPL; }

    // IMemInputPin methods
    STDMETHODIMP GetAllocator(IMemAllocator** ppAllocator);
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES* )
        { return E_NOTIMPL; }
    STDMETHODIMP NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly);
    STDMETHODIMP Receive(IMediaSample* pSample);
    STDMETHODIMP ReceiveCanBlock()
        { return S_FALSE; }
    STDMETHODIMP ReceiveMultiple(IMediaSample** pSamples, long nSamples, long* nSamplesProcessed);
    
};

FiltaaInputPin::FiltaaInputPin(Filtaa* filter, LPCWSTR name, PIN_DIRECTION direction)
{
    fwprintf(stderr, L"InputPin(%p): name=%s, direction=%d\n", this, name, direction);
    _refCount = 0;
    _filter = filter;
    _name = name;
    _direction = direction;
    _connected = NULL;
    _allocator = NULL;
    AddRef();
}

FiltaaInputPin::~FiltaaInputPin()
{
    fwprintf(stderr, L"~InputPin\n");
    if (_connected != NULL) {
        _connected->Release();
        _connected = NULL;
    }
    if (_allocator != NULL) {
        _allocator->Release();
        _allocator = NULL;
    }
}

// IUnknown methods
STDMETHODIMP FiltaaInputPin::QueryInterface(REFIID iid, void** ppvObject)
{
    if (ppvObject == NULL) return E_POINTER;
    if (iid == IID_IUnknown) {
        *ppvObject = this;
    } else if (iid == IID_IPin) {
        *ppvObject = (IPin*)this;
    } else if (iid == IID_IMemInputPin) {
        *ppvObject = (IMemInputPin*)this;
    } else {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

// IPin methods
STDMETHODIMP FiltaaInputPin::EnumMediaTypes(IEnumMediaTypes** ppEnum)
{
    if (ppEnum == NULL) return E_POINTER;
    if (_connected == NULL) return VFW_E_NOT_CONNECTED;

    fwprintf(stderr, L"InputPin.EnumMediaTypes\n");
    *ppEnum = (IEnumMediaTypes*) new FiltaaInputPinEnumMediaTypes();
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt)
{
    // XXX
    HRESULT hr;
    if (pReceivePin == NULL || pmt == NULL) return E_POINTER;
    if (_connected != NULL) return VFW_E_ALREADY_CONNECTED;
    
    fwprintf(stderr, L"InputPin.Connect: pin=%p, mt=%s\n", pReceivePin, mt2str(pmt));
    
    hr = pReceivePin->ReceiveConnection((IPin*)this, pmt);
    if (FAILED(hr)) return hr;
    
    hr = copyMediaType(&_mediatype, pmt);
    // assert(_connected == NULL);
    if (SUCCEEDED(hr)) {
        _connected = pReceivePin;
        _connected->AddRef();
    }

    return hr;
}

STDMETHODIMP FiltaaInputPin::ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt)
{
    // XXX
    if (pConnector == NULL || pmt == NULL) return E_POINTER;
    if (_connected != NULL) return VFW_E_ALREADY_CONNECTED;
    fwprintf(stderr, L"InputPin.ReceiveConnection: pin=%p, mt=%s\n", pConnector, mt2str(pmt));
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::ConnectedTo(IPin** ppPin)
{
    if (ppPin == NULL) return E_POINTER;
    fwprintf(stderr, L"InputPin.ConnectedTo: %p\n", _connected);
    *ppPin = _connected;
    if (_connected == NULL) return VFW_E_NOT_CONNECTED;
    (*ppPin)->AddRef();
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::ConnectionMediaType(AM_MEDIA_TYPE* pmt)
{
    // XXX
    if (pmt == NULL) return E_POINTER;
    if (_connected == NULL) return VFW_E_NOT_CONNECTED;
    fwprintf(stderr, L"InputPin.ConnectionMediaType\n");
    return copyMediaType(pmt, &_mediatype);
}

STDMETHODIMP FiltaaInputPin::Disconnect()
{
    fwprintf(stderr, L"InputPin.Disconnect\n");
    if (_connected == NULL) return S_FALSE;
    _connected->Release();
    _connected = NULL;
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::QueryId(LPWSTR* Id)
{
    HRESULT hr;
    if (Id == NULL) return E_POINTER;
    fwprintf(stderr, L"InputPin.QueryId\n");
    LPWSTR dst = (LPWSTR)CoTaskMemAlloc(sizeof(WCHAR)*(lstrlen(_name)+1));
    if (dst == NULL) return E_OUTOFMEMORY;
    lstrcpy(dst, _name);
    *Id = dst;
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::QueryAccept(const AM_MEDIA_TYPE* pmt)
{
    fwprintf(stderr, L"InputPin.QueryAccept: %s\n", mt2str(pmt));
    // XXX
    return S_FALSE;
}

STDMETHODIMP FiltaaInputPin::QueryDirection(PIN_DIRECTION* pPinDir)
{
    if (pPinDir == NULL) return E_POINTER;
    fwprintf(stderr, L"InputPin.QueryDirection\n");
    *pPinDir = _direction;
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::QueryPinInfo(PIN_INFO* pInfo)
{
    if (pInfo == NULL) return E_POINTER;
    fwprintf(stderr, L"InputPin.QueryPinInfo: %p\n", this);
    ZeroMemory(pInfo, sizeof(*pInfo));
    pInfo->pFilter = (IBaseFilter*)_filter;
    if (pInfo->pFilter != NULL) {
        pInfo->pFilter->AddRef();
    }
    pInfo->dir = _direction;
    swprintf_s(pInfo->achName, _countof(pInfo->achName),
               L"FiltaaInputPin_%s_%p", _name, this);
    return S_OK;
}

// IMemInputPin methods
STDMETHODIMP FiltaaInputPin::GetAllocator(IMemAllocator** ppAllocator)
{
    if (ppAllocator == NULL) return E_POINTER;
    fwprintf(stderr, L"InputPin.GetAllocator\n");
    return CoCreateInstance(
        CLSID_MemoryAllocator, 0, CLSCTX_INPROC_SERVER,
        IID_IMemAllocator, (void**)ppAllocator);
}

STDMETHODIMP FiltaaInputPin::NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly)
{
    if (pAllocator == NULL) return E_POINTER;
    fwprintf(stderr, L"InputPin.NotifyAllocator: readonly=%d\n", bReadOnly);
    if (bReadOnly) return E_FAIL;
    pAllocator->AddRef();
    if (_allocator != NULL) {
        _allocator->Release();
        _allocator = NULL;
    }
    _allocator = pAllocator;
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::Receive(IMediaSample* pSample)
{
    // XXX
    if (pSample == NULL) return E_POINTER;
    fwprintf(stderr, L"InputPin.Receive: %p\n", pSample);
    return S_OK;
}
    
STDMETHODIMP FiltaaInputPin::ReceiveMultiple(
    IMediaSample** pSamples, long nSamples, long* nSamplesProcessed)
{
    HRESULT hr;
    if (pSamples == NULL) return E_POINTER;

    long n = 0;
    for (long i = 0; i < nSamples; i++) {
        hr = Receive(pSamples[i]);
        if (FAILED(hr)) return hr;
        n++;
    }
    if (nSamplesProcessed != NULL) {
        *nSamplesProcessed = n;
    }
    
    return hr;
}



// IEnumPins
class FiltaaEnumPins : public IEnumPins
{
private:
    int _refCount;
    IPin* _pins[2];
    int _npins;
    int _index;

public:
    FiltaaEnumPins(IPin* pIn, IPin* pOut, int index=0) {
        _refCount = 0;
        _pins[0] = pIn;
        _pins[1] = pOut;
        _npins = 2;
        _index = index;
        AddRef();
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID iid, void** ppvObject) {
        if (ppvObject == NULL) return E_POINTER;
        if (iid == IID_IUnknown) {
            *ppvObject = this;
        } else if (iid == IID_IEnumPins) {
            *ppvObject = (IEnumPins*)this;
        } else {
            *ppvObject = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() {
        _refCount++; return _refCount;
    }
    STDMETHODIMP_(ULONG) Release() {
        _refCount--;
        if (_refCount) return _refCount;
        delete this;
        return 0;
    }

    // IEnumPins
    STDMETHODIMP Next(ULONG n, IPin** ppPins, ULONG* pFetched) {
        if (ppPins == NULL) return E_POINTER;
        if (n == 0) return E_INVALIDARG;
        ULONG i = 0;
        while (i < n && _index < _npins) {
            fwprintf(stderr, L"EnumPins.Next: %p (%d)\n", _pins[_index], _index);
            IPin* pin = (ppPins[i++] = _pins[_index++]);
            pin->AddRef();
        }
        if (pFetched != NULL) {
            *pFetched = i;
        }
        return (i < n)? S_FALSE : S_OK;
    }
    STDMETHODIMP Skip(ULONG n) {
        fwprintf(stderr, L"EnumPins.Skip(%u)\n", n);
        while (0 < n--) {
            if (_npins <= _index) return S_FALSE;
            _index++;
        }
        return S_OK;
    }
    STDMETHODIMP Reset(void) {
        fwprintf(stderr, L"EnumPins.Reset\n");
        _index = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumPins** pEnum) {
        if (pEnum == NULL) return E_POINTER;
        *pEnum = new FiltaaEnumPins(_pins[0], _pins[1], _index);
        return S_OK;
    }
    
};


//  Filtaa
//
Filtaa::Filtaa()
{
    _refCount = 0;
    _state = State_Stopped;
    _clock = NULL;
    _graph = NULL;
    _pIn = new FiltaaInputPin(this, L"In", PINDIR_INPUT);
    _pOut = new FiltaaInputPin(this, L"Out", PINDIR_OUTPUT);
    AddRef();
}

Filtaa::~Filtaa()
{
    _pIn->Release();
    _pOut->Release();
}

// IUnknown methods
STDMETHODIMP Filtaa::QueryInterface(REFIID iid, void** ppvObject)
{
    if (ppvObject == NULL) return E_POINTER;
    if (iid == IID_IUnknown) {
        *ppvObject = this;
    } else if (iid == IID_IPersist) {
        *ppvObject = (IPersist*)this;
    } else if (iid == IID_IMediaFilter) {
        *ppvObject = (IMediaFilter*)this;
    } else if (iid == IID_IBaseFilter) {
        *ppvObject = (IBaseFilter*)this;
    } else {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

// IBaseFilter methods
STDMETHODIMP Filtaa::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName)
{
    fwprintf(stderr, L"Filtaa.JoinFilterGraph: name=%s\n", pName);
    if (_graph != NULL) {
        _graph->Release();
    }
    _graph = pGraph;
    if (_graph != NULL) {
        _graph->AddRef();
    }
    return S_OK;
}

STDMETHODIMP Filtaa::EnumPins(IEnumPins** ppEnum)
{
    fwprintf(stderr, L"Filtaa.EnumPins: %p\n", ppEnum);
    if (ppEnum == NULL) return E_POINTER;
    *ppEnum = (IEnumPins*) new FiltaaEnumPins((IPin*)_pIn, (IPin*)_pOut);
    return S_OK;
}

STDMETHODIMP Filtaa::FindPin(LPCWSTR Id, IPin** ppPin)
{
    fwprintf(stderr, L"Filtaa.FindPin: Id=%s\n", Id);
    if (Id == NULL) return E_POINTER;
    if (ppPin == NULL) return E_POINTER;
    if (lstrcmp(Id, _pIn->Name()) == 0) {
        *ppPin = (IPin*)_pIn;
        (*ppPin)->AddRef();
    } else if (lstrcmp(Id, _pOut->Name()) == 0) {
        *ppPin = (IPin*)_pOut;
        (*ppPin)->AddRef();
    } else {
        *ppPin = NULL;
        return VFW_E_NOT_FOUND;
    }
    return S_OK;
}

STDMETHODIMP Filtaa::QueryFilterInfo(FILTER_INFO* pInfo)
{
    fwprintf(stderr, L"Filtaa.QueryFilterInfo\n");
    if (pInfo == NULL) return E_POINTER;
    ZeroMemory(pInfo, sizeof(*pInfo));
    pInfo->pGraph = _graph;
    if (pInfo->pGraph != NULL) {
        pInfo->pGraph->AddRef();
    }
    StringCchCopy(pInfo->achName, _countof(pInfo->achName),
                  L"Filtaa");
    return S_OK;
}

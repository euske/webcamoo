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

static LPCWSTR mt2str(const AM_MEDIA_TYPE* pmt)
{
    static WCHAR buf[256];
    swprintf_s(buf, _countof(buf),
               L"<major=%08x, sub=%08x, size=%lu, format=%08x>",
               pmt->majortype.Data1, pmt->subtype.Data1,
               pmt->lSampleSize, pmt->formattype.Data1);
    return buf;
}


// FiltaaInputPinEnumMediaTypes
class FiltaaInputPinEnumMediaTypes : IEnumMediaTypes
{
private:
    int _refCount;
    int _index;

public:
    FiltaaInputPinEnumMediaTypes(int index=0) {
        _refCount = 0;
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
        ULONG i = 0;
        while (i < n && _index < 1) {
            AM_MEDIA_TYPE* pMediaType = ppMediaTypes[i++];
            VIDEOINFOHEADER* fmt = (VIDEOINFOHEADER*)CoTaskMemAlloc(sizeof(VIDEOINFOHEADER));
            if (fmt == NULL) return E_OUTOFMEMORY;
            pMediaType->majortype = MEDIATYPE_Video;
            pMediaType->subtype = MEDIASUBTYPE_RGB24;
            pMediaType->bFixedSizeSamples = TRUE;
            pMediaType->bTemporalCompression = FALSE;
            pMediaType->lSampleSize = 0;
            pMediaType->formattype = FORMAT_VideoInfo;            
            pMediaType->cbFormat = sizeof(*fmt);
            pMediaType->pbFormat = (BYTE*)fmt;
            ZeroMemory(fmt, sizeof(*fmt));
            fmt->bmiHeader.biSize = sizeof(*fmt);
            _index++;
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
class FiltaaInputPin : IPin, IMemInputPin
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
        { return S_OK; }
    STDMETHODIMP EndFlush()
        { return S_OK; }
    STDMETHODIMP EndOfStream()
        { return S_OK; }
    STDMETHODIMP NewSegment(REFERENCE_TIME , REFERENCE_TIME , double )
        { return S_OK; }
    
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
    fwprintf(stderr, L"InputPin.EnumMediaTypes\n");
    if (ppEnum == NULL) return E_POINTER;
    *ppEnum = (IEnumMediaTypes*) new FiltaaInputPinEnumMediaTypes();
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt)
{
    fwprintf(stderr, L"InputPin.Connect: pin=%p, mt=%s\n", pReceivePin, mt2str(pmt));
    // XXX
    HRESULT hr;
    if (pReceivePin == NULL) return E_POINTER;
    if (_connected != NULL) return VFW_E_ALREADY_CONNECTED;
    
    hr = pReceivePin->ReceiveConnection((IPin*)this, pmt);
    if (FAILED(hr)) return hr;
    
    hr = copyMediaType(&_mediatype, pmt);
    if (SUCCEEDED(hr)) {
        _connected = pReceivePin;
        _connected->AddRef();
    }

    return hr;
}

STDMETHODIMP FiltaaInputPin::ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt)
{
    fwprintf(stderr, L"InputPin.Connect: pin=%p, mt=%s\n", pConnector, mt2str(pmt));
    // XXX
    if (pConnector == NULL) return E_POINTER;
    if (_connected != NULL) return VFW_E_ALREADY_CONNECTED;
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::ConnectedTo(IPin** ppPin)
{
    if (ppPin == NULL) return E_POINTER;
    if (_connected == NULL) return VFW_E_NOT_CONNECTED;
    *ppPin = _connected;
    (*ppPin)->AddRef();
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::ConnectionMediaType(AM_MEDIA_TYPE* pmt)
{
    // XXX
    if (pmt == NULL) return E_POINTER;
    if (_connected == NULL) return VFW_E_NOT_CONNECTED;
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
    *pPinDir = _direction;
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::QueryPinInfo(PIN_INFO* pInfo)
{
    fwprintf(stderr, L"InputPin.QueryPinInfo\n");
    if (pInfo == NULL) return E_POINTER;
    pInfo->pFilter = (IBaseFilter*)_filter;
    pInfo->pFilter->AddRef();
    pInfo->dir = _direction;
    swprintf_s(pInfo->achName, _countof(pInfo->achName),
               L"FiltaaInputPin_%s_%p", _name, this);
    return S_OK;
}

// IMemInputPin methods
STDMETHODIMP FiltaaInputPin::GetAllocator(IMemAllocator** ppAllocator)
{
    fwprintf(stderr, L"InputPin.GetAllocator\n");
    if (ppAllocator == NULL) return E_POINTER;
    return CoCreateInstance(
        CLSID_MemoryAllocator, 0, CLSCTX_INPROC_SERVER,
        IID_IMemAllocator, (void**)ppAllocator);
}

STDMETHODIMP FiltaaInputPin::NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly)
{
    fwprintf(stderr, L"InputPin.NotifyAllocator: readonly=%d\n", bReadOnly);
    if (pAllocator == NULL) return E_POINTER;
    if (bReadOnly) return E_FAIL;
    pAllocator->AddRef();
    if (_allocator != NULL) {
        _allocator->Release();
    }
    _allocator = pAllocator;
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::Receive(IMediaSample* pSample)
{
    fwprintf(stderr, L"InputPin.Receive: %p\n", pSample);
    // XXX
    if (pSample == NULL) return E_POINTER;
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
class FiltaaEnumPins : IEnumPins
{
private:
    int _refCount;
    IPin* _pins[2];
    int _maxIndex;
    int _index;

public:
    FiltaaEnumPins(IPin* pIn, IPin* pOut, int index=0) {
        _refCount = 0;
        _pins[0] = pIn;
        _pins[1] = pOut;
        _maxIndex = 2;
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
        ULONG i = 0;
        while (i < n && _index < _maxIndex) {
            IPin* pin = _pins[_index++];
            fwprintf(stderr, L"EnumPins.Next: %p (%d)\n", pin, _index-1);
            pin->AddRef();
            ppPins[i++] = pin;
        }
        if (pFetched != NULL) {
            *pFetched = i;
        }
        return (i < n)? S_FALSE : S_OK;
    }
    STDMETHODIMP Skip(ULONG n) {
        while (0 < n--) {
            if (_maxIndex <= _index) return S_FALSE;
            _index++;
        }
        return S_OK;
    }
    STDMETHODIMP Reset(void) {
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

static const LPCWSTR FILTER_NAME = L"Filtaa";

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
    fwprintf(stderr, L"Filtaa.EnumPins\n");
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
    StringCbCopy(pInfo->achName, sizeof(pInfo->achName), FILTER_NAME);
    if (pInfo->pGraph != NULL) {
        pInfo->pGraph->AddRef();
    }
    return S_OK;
}

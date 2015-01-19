//  Filtaa.cpp

#include <stdio.h>
#include <windows.h>
#include <dshow.h>
#include "Filtaa.h"

static BOOL isMediaTypeAcceptable(const AM_MEDIA_TYPE* mt)
{
    if (mt->majortype != MEDIATYPE_Video) return FALSE;
    if (mt->subtype != MEDIASUBTYPE_RGB24) return FALSE;
    if (mt->formattype != FORMAT_VideoInfo) return FALSE;
    return TRUE;
}

static HRESULT copyMediaType(AM_MEDIA_TYPE* dst, const AM_MEDIA_TYPE* src)
{
    if (src == NULL) return E_POINTER;
    if (dst == NULL) return E_POINTER;
    CopyMemory(dst, src, sizeof(*src));
    if (src->cbFormat) {
        BYTE* fmt = (BYTE*)CoTaskMemAlloc(src->cbFormat);
        if (fmt == NULL) return E_OUTOFMEMORY;
        CopyMemory(fmt, src->pbFormat, src->cbFormat);
        dst->pbFormat = fmt;
    }
    return S_OK;
}

static HRESULT eraseMediaType(AM_MEDIA_TYPE* mt)
{
    if (mt == NULL) return E_POINTER;
    if (mt->pbFormat != NULL) {
        CoTaskMemFree(mt->pbFormat);
    }
    return S_OK;
}

static LPCWSTR mt2str(const AM_MEDIA_TYPE* mt)
{
    if (mt == NULL) return L"<null>";
    static WCHAR major[64];
    if (mt->majortype == MEDIATYPE_Video) {
        swprintf_s(major, _countof(major), L"Video");
    } else {
        swprintf_s(major, _countof(major), L"[%08x]", mt->majortype.Data1);
    }
    static WCHAR sub[64];
    if (mt->subtype == MEDIASUBTYPE_RGB24) {
        swprintf_s(sub, _countof(sub), L"RGB24");
    } else {
        swprintf_s(sub, _countof(sub), L"[%08x]", mt->subtype.Data1);
    }
    static WCHAR format[64];
    if (mt->formattype == FORMAT_VideoInfo && mt->cbFormat) {
        VIDEOINFOHEADER* vi = (VIDEOINFOHEADER*)mt->pbFormat;
        swprintf_s(format, _countof(format), L"VideoInfo(%dx%d)",
                   vi->bmiHeader.biWidth, vi->bmiHeader.biHeight);
    } else {
        swprintf_s(format, _countof(format), L"[%08x]", mt->formattype.Data1);
    }
    static WCHAR buf[256];
    swprintf_s(buf, _countof(buf),
               L"<major=%s, sub=%s, size=%lu, format=%s>",
               major, sub, mt->lSampleSize, format);
    return buf;
}


// IEnumPins
class FiltaaEnumPins : public IEnumPins
{
private:
    int _refCount;
    IPin* _pins[2];
    int _npins;
    int _index;
    
    ~FiltaaEnumPins() {
        for (int i = 0; i < _npins; i++) {
            _pins[i]->Release();
        }
    }

public:
    FiltaaEnumPins(IPin* pIn, IPin* pOut, int index=0) {
        _refCount = 0;
        _pins[0] = pIn;
        _pins[1] = pOut;
        _npins = 2;
        _index = index;
        for (int i = 0; i < _npins; i++) {
            _pins[i]->AddRef();
        }
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
        while (0 < n--) {
            if (_npins <= _index) return S_FALSE;
            _index++;
        }
        return S_OK;
    }
    STDMETHODIMP Reset() {
        _index = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumPins** pEnum) {
        if (pEnum == NULL) return E_POINTER;
        *pEnum = new FiltaaEnumPins(_pins[0], _pins[1], _index);
        return S_OK;
    }
};


// FiltaaEnumMediaTypes
class FiltaaEnumMediaTypes : public IEnumMediaTypes
{
private:
    int _refCount;
    AM_MEDIA_TYPE _mts[1];
    int _nmts;
    int _index;

    ~FiltaaEnumMediaTypes() {
        eraseMediaType(&(_mts[0]));
    }

public:
    FiltaaEnumMediaTypes(const AM_MEDIA_TYPE* mt, int index=0) {
        _refCount = 0;
        copyMediaType(&(_mts[0]), mt);
        _nmts = 1;
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
            fwprintf(stderr, L"EnumMediaTypes.Next: %p (%d)\n", &(_mts[_index]), _index);
            AM_MEDIA_TYPE* mt = (AM_MEDIA_TYPE*)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
            if (mt == NULL) return E_OUTOFMEMORY;
            if (FAILED(copyMediaType(mt, &(_mts[_index++])))) return E_OUTOFMEMORY;
            ppMediaTypes[i++] = mt;
        }
        if (pFetched != NULL) {
            *pFetched = i;
        }
        return (i < n)? S_FALSE : S_OK;
    }
    STDMETHODIMP Skip(ULONG n) {
        while (0 < n--) {
            if (_nmts <= _index) return S_FALSE;
            _index++;
        }
        return S_OK;
    }
    STDMETHODIMP Reset() {
        _index = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumMediaTypes** pEnum) {
        if (pEnum == NULL) return E_POINTER;
        *pEnum = new FiltaaEnumMediaTypes(_mts, _index);
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
    IMemInputPin* _transport;
    BOOL _flushing;
    
    ~FiltaaInputPin();
    
public:
    FiltaaInputPin(Filtaa* filter, LPCWSTR name, PIN_DIRECTION direction);

    LPCWSTR Name() { return _name; }
    IPin* Connected() { return _connected; }
    IMemInputPin* Transport() { return _transport; }

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
    STDMETHODIMP BeginFlush() {
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        _flushing = TRUE;
        return _filter->BeginFlush();
    }
    STDMETHODIMP EndFlush() {
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        _flushing = FALSE;
        return _filter->EndFlush();
    }
    STDMETHODIMP EndOfStream() {
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        return _filter->EndOfStream();
    }
    STDMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) {
        return _filter->NewSegment(tStart, tStop, dRate);
    }
    
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
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES* )
        { return E_NOTIMPL; }
    STDMETHODIMP GetAllocator(IMemAllocator** ppAllocator) {
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        return _filter->GetAllocator(ppAllocator);
    }
    STDMETHODIMP NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly) {
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        return _filter->NotifyAllocator(pAllocator, bReadOnly);
    }        
    STDMETHODIMP Receive(IMediaSample* pSample) {
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        return _filter->Receive(pSample);
    }
    STDMETHODIMP ReceiveCanBlock()
        { return S_FALSE; }
    STDMETHODIMP ReceiveMultiple(IMediaSample** pSamples, long nSamples, long* nSamplesProcessed);
    
};

FiltaaInputPin::FiltaaInputPin(Filtaa* filter, LPCWSTR name, PIN_DIRECTION direction)
{
    fwprintf(stderr, L"InputPin(%p,%s): direction=%d\n", this, name, direction);
    _refCount = 0;
    _filter = filter;
    _name = name;
    _direction = direction;
    _connected = NULL;
    _transport = NULL;
    _flushing = FALSE;
    AddRef();
}

FiltaaInputPin::~FiltaaInputPin()
{
    fwprintf(stderr, L"~InputPin(%s)\n", _name);
    if (_connected != NULL) {
        _connected->Release();
        _connected = NULL;
    }
    if (_transport != NULL) {
        _transport->Release();
        _transport = NULL;
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

    const AM_MEDIA_TYPE* mt = _filter->GetMediaType();
    fwprintf(stderr, L"InputPin(%s).EnumMediaTypes\n", _name);
    if (mt == NULL) return VFW_E_NOT_CONNECTED;
    *ppEnum = (IEnumMediaTypes*) new FiltaaEnumMediaTypes(mt);
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt)
{
    HRESULT hr;
    if (pReceivePin == NULL || pmt == NULL) return E_POINTER;
    if (_direction != PINDIR_OUTPUT) return E_UNEXPECTED;
    if (_connected != NULL) return VFW_E_ALREADY_CONNECTED;
    
    fwprintf(stderr, L"InputPin(%s).Connect: pin=%p, mt=%s\n", _name, pReceivePin, mt2str(pmt));
    hr = _filter->Connect(pmt);
    if (FAILED(hr)) return hr;
    
    hr = pReceivePin->ReceiveConnection((IPin*)this, pmt);
    if (FAILED(hr)) return hr;

    hr = pReceivePin->QueryInterface(IID_IMemInputPin, (void**)&_transport);
    if (FAILED(hr)) return hr;
    fwprintf(stderr, L"InputPin(%s).Connect: transport ok\n", _name);
    
    // assert(_connected == NULL);
    _connected = pReceivePin;
    _connected->AddRef();
    return hr;
}

STDMETHODIMP FiltaaInputPin::ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt)
{
    HRESULT hr;
    if (pConnector == NULL || pmt == NULL) return E_POINTER;
    if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
    if (_connected != NULL) return VFW_E_ALREADY_CONNECTED;
    fwprintf(stderr, L"InputPin(%s).ReceiveConnection: pin=%p, mt=%s\n", _name, pConnector, mt2str(pmt));
    
    hr = _filter->ReceiveConnection(pmt);
    if (FAILED(hr)) return hr;
    fwprintf(stderr, L"InputPin(%s).ReceiveConnection: mediatype ok\n", _name);
    
    _connected = pConnector;
    _connected->AddRef();
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::ConnectedTo(IPin** ppPin)
{
    if (ppPin == NULL) return E_POINTER;
    fwprintf(stderr, L"InputPin(%s).ConnectedTo: %p\n", _name, _connected);
    *ppPin = _connected;
    if (_connected == NULL) return VFW_E_NOT_CONNECTED;
    (*ppPin)->AddRef();
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::ConnectionMediaType(AM_MEDIA_TYPE* pmt)
{
    if (pmt == NULL) return E_POINTER;
    if (_connected == NULL) return VFW_E_NOT_CONNECTED;
    fwprintf(stderr, L"InputPin.ConnectionMediaType\n");
    return copyMediaType(pmt, _filter->GetMediaType());
}

STDMETHODIMP FiltaaInputPin::Disconnect()
{
    fwprintf(stderr, L"InputPin(%s).Disconnect\n", _name);
    if (_connected == NULL) return S_FALSE;
    _connected->Release();
    _connected = NULL;
    if (_transport != NULL) {
        _transport->Release();
        _transport = NULL;
    }
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
    return (isMediaTypeAcceptable(pmt))? S_OK : S_FALSE;
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
    StringCchCopy(pInfo->achName, _countof(pInfo->achName), _name);
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



//  Filtaa
//
Filtaa::Filtaa()
{
    _refCount = 0;
    _name = L"Filtaa";
    _state = State_Stopped;
    _clock = NULL;
    _graph = NULL;
    _pIn = new FiltaaInputPin(this, L"In", PINDIR_INPUT);
    _pOut = new FiltaaInputPin(this, L"Out", PINDIR_OUTPUT);
    ZeroMemory(&_mediatype, sizeof(_mediatype));
    _allocator = NULL;
    AddRef();
}

Filtaa::~Filtaa()
{
    eraseMediaType(&_mediatype);
    if (_allocator != NULL) {
        _allocator->Release();
        _allocator = NULL;
    }
    if (_clock != NULL) {
        _clock->Release();
        _clock = NULL;
    }
    if (_graph != NULL) {
        _graph->Release();
        _graph = NULL;
    }
    if (_pIn != NULL) {
        _pIn->Release();
        _pIn = NULL;
    }
    if (_pOut != NULL) {
        _pOut->Release();
        _pOut = NULL;
    }
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
    if (pGraph != NULL) {
        pGraph->AddRef();
    }
    if (_graph != NULL) {
        _graph->Release();
    }
    _graph = pGraph;
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
    StringCchCopy(pInfo->achName, _countof(pInfo->achName), _name);
    return S_OK;
}

STDMETHODIMP Filtaa::GetSyncSource(IReferenceClock** ppClock)
{
    fwprintf(stderr, L"Filtaa.GetSyncSource\n");
    if (ppClock == NULL) return E_POINTER;
    if (_clock != NULL) {
        _clock->AddRef();
    }
    (*ppClock) = _clock;
    return S_OK;
}

STDMETHODIMP Filtaa::SetSyncSource(IReferenceClock* pClock)
{
    fwprintf(stderr, L"Filtaa.SetSyncSource\n");
    if (pClock != NULL) {
        pClock->AddRef();
    }
    if (_clock != NULL) {
        _clock->Release();
    }
    _clock = pClock;
    return S_OK;
}

// others

const AM_MEDIA_TYPE* Filtaa::GetMediaType()
{
    if (_pIn->Connected() == NULL) {
        return NULL;
    } else {
        return &_mediatype;
    }
}

HRESULT Filtaa::Connect(const AM_MEDIA_TYPE* mt)
{
    if (_state != State_Stopped) return VFW_E_NOT_STOPPED;
    if (!isMediaTypeAcceptable(mt)) return VFW_E_TYPE_NOT_ACCEPTED;
    return S_OK;
}

HRESULT Filtaa::ReceiveConnection(const AM_MEDIA_TYPE* mt)
{
    if (_state != State_Stopped) return VFW_E_NOT_STOPPED;
    if (!isMediaTypeAcceptable(mt)) return VFW_E_TYPE_NOT_ACCEPTED;
    return copyMediaType(&_mediatype, mt);
}

HRESULT Filtaa::BeginFlush()
{
    fwprintf(stderr, L"Filtaa.BeginFlush\n");
    IPin* pin = _pOut->Connected();
    if (pin != NULL) {
        pin->BeginFlush();
    }
    return S_OK;
}

HRESULT Filtaa::EndFlush()
{
    fwprintf(stderr, L"Filtaa.EndFlush\n");
    IPin* pin = _pOut->Connected();
    if (pin != NULL) {
        pin->EndFlush();
    }
    return S_OK;
}

HRESULT Filtaa::EndOfStream()
{
    fwprintf(stderr, L"Filtaa.EndOfStream\n");
    IPin* pin = _pOut->Connected();
    if (pin != NULL) {
        pin->EndOfStream();
    }
    return S_OK;
}

HRESULT Filtaa::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
    fwprintf(stderr, L"Filtaa.NewSegment\n");
    IPin* pin = _pOut->Connected();
    if (pin != NULL) {
        pin->NewSegment(tStart, tStop, dRate);
    }
    return S_OK;
}


// IMemInputPin methods
HRESULT Filtaa::GetAllocator(IMemAllocator** ppAllocator)
{
    // XXX
    if (ppAllocator == NULL) return E_POINTER;
    fwprintf(stderr, L"Filtaa.GetAllocator\n");
    return CoCreateInstance(
        CLSID_MemoryAllocator, 0, CLSCTX_INPROC_SERVER,
        IID_IMemAllocator, (void**)ppAllocator);
}

HRESULT Filtaa::NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly)
{
    // XXX
    if (pAllocator == NULL) return E_POINTER;
    fwprintf(stderr, L"Filtaa.NotifyAllocator: readonly=%d\n", bReadOnly);
    //if (bReadOnly) return E_FAIL;
    pAllocator->AddRef();
    if (_allocator != NULL) {
        _allocator->Release();
    }
    _allocator = pAllocator;
    IMemInputPin* pin = _pOut->Transport();
    if (pin != NULL) {
        HRESULT hr = pin->NotifyAllocator(pAllocator, bReadOnly);
        fwprintf(stderr, L"Filtaa.NotifyAllocator: hr=%p\n", hr);
        return hr;
    }
    return S_OK;
}

HRESULT Filtaa::Receive(IMediaSample* pSample)
{
    // XXX
    if (pSample == NULL) return E_POINTER;
    fwprintf(stderr, L"Filtaa.Receive: %p\n", pSample);
    return S_OK;
}

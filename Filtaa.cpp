//  Filtaa.cpp

#include <stdio.h>
#include <windows.h>
#include <dshow.h>
#include "Filtaa.h"

const LPCWSTR FILTER_NAME = L"Filtaa";


//  FiltaaInputPin
//
FiltaaInputPin::FiltaaInputPin(Filtaa* filter)
{
    _refCount = 0;
    _filter = filter;
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
        _maxIndex = 0;
        _index = index;
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
    STDMETHODIMP Next(ULONG n, IPin** pPin, ULONG* pFetched) {
        if (pPin == NULL) return E_POINTER;
        ULONG fetched = 0;
        while (0 < n--) {
            if (_maxIndex <= _index) return S_FALSE;
            pPin[fetched++] = _pins[_index];
            _index++;
        }
        if (pFetched != NULL) {
            *pFetched = fetched;
        }
        return S_OK;
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
        (*pEnum)->AddRef();
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
    _pIn = NULL;
    _pOut = NULL;
    //_pIn = new FiltaaInputPin(this);
    //_pOut = new FiltaaInputPin(this);
}

// IUnknown methods
STDMETHODIMP Filtaa::QueryInterface(REFIID iid, void** ppvObject)
{
    if (ppvObject == NULL) return E_POINTER;
    if (iid == IID_IUnknown) {
        *ppvObject = this;
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
STDMETHODIMP Filtaa::EnumPins(IEnumPins** ppEnum)
{
    fwprintf(stderr, L"EnumPins!\n");
    if (ppEnum == NULL) return E_POINTER;
    *ppEnum = (IEnumPins*)new FiltaaEnumPins((IPin*)_pIn, (IPin*)_pOut);
    (*ppEnum)->AddRef();
    return S_OK;
}

STDMETHODIMP Filtaa::FindPin(LPCWSTR Id, IPin** ppPin)
{
    fwprintf(stderr, L"FindPin: Id=%s\n", Id);
    if (ppPin == NULL) return E_POINTER;
    if (lstrcmp(L"In", Id) == 0) {
        *ppPin = (IPin*)_pIn;
    } else if (lstrcmp(L"Out", Id) == 0) {
        *ppPin = (IPin*)_pOut;
    } else {
        *ppPin = NULL;
    }
    return S_OK;
}

STDMETHODIMP Filtaa::QueryFilterInfo(FILTER_INFO* pInfo)
{
    if (pInfo == NULL) return E_POINTER;
    fwprintf(stderr, L"QueryFilterInfo\n");
    ZeroMemory(pInfo, sizeof(*pInfo));
    pInfo->pGraph = _graph;
    StringCbCopy(pInfo->achName, sizeof(pInfo->achName), FILTER_NAME);
    if (pInfo->pGraph != NULL) {
        pInfo->pGraph->AddRef();
    }
    return S_OK;
}

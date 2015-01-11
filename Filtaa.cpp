//  Filtaa.cpp

#include <stdio.h>
#include <windows.h>
#include <dshow.h>
#include "Filtaa.h"

const LPCWSTR FILTER_NAME = L"Filtaa";


//  Filtaa
//
Filtaa::Filtaa()
{
    _state = State_Stopped;
}

// IUnknown methods
STDMETHODIMP Filtaa::QueryInterface(REFIID iid, void** ppvObject)
{
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
    *ppEnum = NULL;
    return E_FAIL;
}

STDMETHODIMP Filtaa::FindPin(LPCWSTR Id, IPin** ppPin)
{
    fwprintf(stderr, L"FindPin: Id=%s\n", Id);
    if (ppPin == NULL) return E_POINTER;
    *ppPin = NULL;
    return E_FAIL;
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

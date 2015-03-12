//  WebCamoo.cpp
//
//  This code is based on the "playcap" sample from Microsoft SDK,
//  but the significant portion was changed or rewritten.
//

// --- Start of Microsoft Credit ---
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// --- End of Microsoft Credit ---

#define _WIN32_WINNT 0x0500

#include <windows.h>
#include <dbt.h>
#include <dshow.h>
#include <stdio.h>
#include <strsafe.h>

#include "WebCamoo.h"
#include "Filtaa.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "strmiids.lib")


//  Constants
//
const LPCWSTR APPLICATION_NAME = L"WebCamoo";
const int THRESHOLD_DELTA = 5;

// Application-defined message to notify app of filtergraph events.
const UINT WM_GRAPHNOTIFY = WM_APP+1;
const UINT IDM_DEVICE_VIDEO_START = IDM_DEVICE_VIDEO_NONE+1;
const UINT IDM_DEVICE_VIDEO_END = IDM_DEVICE_VIDEO_NONE+9999;
const UINT IDM_DEVICE_AUDIO_START = IDM_DEVICE_AUDIO_NONE+1;
const UINT IDM_DEVICE_AUDIO_END = IDM_DEVICE_AUDIO_NONE+9999;


//  Misc. functions
//
static inline int min2(int x, int y)
{
    return (x < y)? x : y;
}
static inline int max2(int x, int y)
{
    return (x < y)? y : x;
}

static FILE* logfp = NULL;      // logging
static void log(LPCWSTR fmt, ...)
{
    if (logfp == NULL) return;
    WCHAR buf[1024];
    va_list args;
    va_start(args, fmt);
    StringCchVPrintf(buf, _countof(buf)-1, fmt, args);
    va_end(args);
    buf[1023] = L'\0';
    fwprintf(logfp, L"%s\n", buf);
    fflush(logfp);
}

static HMENU findSubMenu(HMENU hMenu, UINT id)
{
    MENUITEMINFO mii = {0};
    mii.cbSize = sizeof(mii);
    for (int i = 0; i < 10; i++) {
        HMENU hSubMenu = GetSubMenu(hMenu, i);
        if (GetMenuItemInfo(hSubMenu, id, FALSE, &mii)) {
            hMenu = hSubMenu;
            break;
        }
    }
    return hMenu;
}

static int findMenuItemPos(HMENU hMenu, UINT id)
{
    hMenu = findSubMenu(hMenu, id);
    MENUITEMINFO mii = {0};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_ID;
    int n = GetMenuItemCount(hMenu);
    for (int i = 0; i < n; i++) {
        if (GetMenuItemInfo(hMenu, i, TRUE, &mii)) {
            if (mii.wID == id) return i;
        }
    }
    return -1;
}

static BOOL isMenuItemChecked(HMENU hMenu, UINT id)
{
    hMenu = findSubMenu(hMenu, id);
    MENUITEMINFO mii = {0};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STATE;
    if (GetMenuItemInfo(hMenu, id, FALSE, &mii)) {
        return (mii.fState & MFS_CHECKED);
    }
    return FALSE;
}

static BOOL toggleMenuItemChecked(HMENU hMenu, UINT id)
{
    hMenu = findSubMenu(hMenu, id);
    MENUITEMINFO mii = {0};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STATE;
    if (GetMenuItemInfo(hMenu, id, FALSE, &mii)) {
        if ((mii.fState & MFS_CHECKED)) {
            mii.fState &= ~MFS_CHECKED;
        } else {
            mii.fState |= MFS_CHECKED;
        }
        SetMenuItemInfo(hMenu, id, FALSE, &mii);
        return TRUE;
    }
    return FALSE;
}

static void setMenuItemDisabled(HMENU hMenu, UINT id, BOOL disabled)
{
    hMenu = findSubMenu(hMenu, id);
    MENUITEMINFO mii = {0};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STATE;
    if (GetMenuItemInfo(hMenu, id, FALSE, &mii)) {
        if (disabled) {
            mii.fState |= MFS_DISABLED;
        } else {
            mii.fState &= ~MFS_DISABLED;
        }
        SetMenuItemInfo(hMenu, id, FALSE, &mii);
    }
}


// An application can advertise the existence of its filter graph
// by registering the graph with a global Running Object Table (ROT).
// The GraphEdit application can detect and remotely view the running
// filter graph, allowing you to 'spy' on the graph with GraphEdit.
//
// To enable registration in this sample, define REGISTER_FILTERGRAPH 1.
//
static const int REGISTER_FILTERGRAPH = 1;

// Adds a filter graph to the Running Object Table.
static HRESULT AddGraphToRot(IUnknown* pUnkGraph, DWORD* pdwRegister) 
{
    HRESULT hr;

    if (!pUnkGraph || !pdwRegister) return E_POINTER;

    IRunningObjectTable* pROT = NULL;
    hr = GetRunningObjectTable(0, &pROT);
    if (SUCCEEDED(hr)) {
        WCHAR wsz[256];
        IMoniker* pMoniker = NULL;
        hr = StringCchPrintfW(
            wsz, NUMELMS(wsz),
            L"FilterGraph %08x pid %08x\0",
            (DWORD_PTR)pUnkGraph, 
            GetCurrentProcessId());

        hr = CreateItemMoniker(L"!", wsz, &pMoniker);
        if (SUCCEEDED(hr)) {
            // Use the ROTFLAGS_REGISTRATIONKEEPSALIVE to ensure a strong reference
            // to the object.  Using this flag will cause the object to remain
            // registered until it is explicitly revoked with the Revoke() method.
            //
            // Not using this flag means that if GraphEdit remotely connects
            // to this graph and then GraphEdit exits, this object registration 
            // will be deleted, causing future attempts by GraphEdit to fail until
            // this application is restarted or until the graph is registered again.
            hr = pROT->Register(
                ROTFLAGS_REGISTRATIONKEEPSALIVE, pUnkGraph, 
                pMoniker, pdwRegister);
            pMoniker->Release();
        }
        pROT->Release();
    }
    
    return hr;
}

// Removes a filter graph from the Running Object Table.
static void RemoveGraphFromRot(DWORD pdwRegister)
{
    HRESULT hr;
    
    IRunningObjectTable* pROT = NULL;
    hr = GetRunningObjectTable(0, &pROT);
    if (SUCCEEDED(hr)) {
        pROT->Revoke(pdwRegister);
        pROT->Release();
    }
}

// getFriendlyName
static HRESULT getFriendlyName(
    VARIANT* pVar, IMoniker* pMoniker)
{
    HRESULT hr;
    if (pVar == NULL) return E_POINTER;
    if (pMoniker == NULL) return E_POINTER;

    IPropertyBag* pBag = NULL;
    hr = pMoniker->BindToStorage(NULL, NULL, IID_PPV_ARGS(&pBag));
    if (SUCCEEDED(hr)) {
        pVar->vt = VT_BSTR;
        hr = pBag->Read(L"FriendlyName", pVar, NULL);
        pBag->Release();
    }

    return hr;
}

// ResetCaptureDevices
static void ResetCaptureDevices(HMENU hMenu)
{
    int n = GetMenuItemCount(hMenu);
    for(int i = 0; i < n; i++) {
        MENUITEMINFO mii = {0};
        mii.cbSize = sizeof(mii);
        mii.fMask = (MIIM_ID | MIIM_DATA);
        GetMenuItemInfo(hMenu, i, TRUE, &mii);
        if ((IDM_DEVICE_VIDEO_START <= mii.wID &&
             mii.wID <= IDM_DEVICE_VIDEO_END) ||
            (IDM_DEVICE_AUDIO_START <= mii.wID &&
             mii.wID <= IDM_DEVICE_AUDIO_END)) {
            IMoniker* pMoniker = (IMoniker*)mii.dwItemData;
            if (pMoniker != NULL) {
                pMoniker->Release();
            }
            RemoveMenu(hMenu, mii.wID, MF_BYCOMMAND);
        }
    }
}

// AddCaptureDevices
static HRESULT AddCaptureDevices(HMENU hMenu, int pos, UINT wID, CLSID category)
{
    HRESULT hr;

    // Create the system device enumerator.
    ICreateDevEnum* pDevEnum = NULL;
    hr = CoCreateInstance(
        CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pDevEnum));
    
    if (SUCCEEDED(hr)) {
        // Create an enumerator for the video capture devices.
        IEnumMoniker* pClassEnum = NULL;
        hr = pDevEnum->CreateClassEnumerator(
            category, &pClassEnum, 0);
        
        // If there are no enumerators for the requested type, then 
        // CreateClassEnumerator will succeed, but pClassEnum will be NULL.
        if (SUCCEEDED(hr) && pClassEnum != NULL) {
            IMoniker* pMoniker = NULL;
            
            // Use the first video capture device on the device list.
            // Note that if the Next() call succeeds but there are no monikers,
            // it will return S_FALSE (which is not a failure).  Therefore, we
            // check that the return code is S_OK instead of using SUCCEEDED() macro.
            while (pClassEnum->Next(1, &pMoniker, NULL) == S_OK) {
                VARIANT var;
                hr = getFriendlyName(&var, pMoniker);
                if (SUCCEEDED(hr)) {
                    MENUITEMINFO mii = {0};
                    mii.cbSize = sizeof(mii);
                    mii.fMask = (MIIM_STRING | MIIM_ID | MIIM_DATA);
                    mii.dwTypeData = var.bstrVal;
                    mii.dwItemData = (ULONG_PTR)pMoniker;
                    mii.cch = lstrlen(var.bstrVal);
                    mii.wID = wID++;
                    InsertMenuItem(hMenu, pos, TRUE, &mii);
                    SysFreeString(var.bstrVal);
                    pMoniker->AddRef();
                    pos++;
                }
                pMoniker->Release();
            }
            pClassEnum->Release();
        }
        pDevEnum->Release();
    }

    return hr;
}

static HRESULT findPin(
    IBaseFilter* pFilter, PIN_DIRECTION direction,
    const GUID* category, IPin** ppPinFound)
{
    HRESULT hr;
    if (pFilter == NULL) return E_POINTER;
    if (ppPinFound == NULL) return E_POINTER;

    *ppPinFound = NULL;
    IEnumPins* pEnum = NULL;
    hr = pFilter->EnumPins(&pEnum);
    if (SUCCEEDED(hr)) {
        IPin* pPin = NULL;
        while ((hr = pEnum->Next(1, &pPin, NULL)) == S_OK) {
            PIN_DIRECTION dir;
            hr = pPin->QueryDirection(&dir);
            if (SUCCEEDED(hr) && dir == direction) {
                IKsPropertySet* pKS = NULL;
                hr = pPin->QueryInterface(IID_PPV_ARGS(&pKS));
                if (SUCCEEDED(hr)) {
                    GUID cat = {0};
                    DWORD cbReturned = 0;
                    hr = pKS->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY,
                                  NULL, 0, &cat, sizeof(cat), &cbReturned);
                    if (SUCCEEDED(hr) && IsEqualGUID(*category, cat)) {
                        *ppPinFound = pPin;
                        (*ppPinFound)->AddRef();
                    }
                    pKS->Release();
                }
            }
            pPin->Release();
            if (*ppPinFound != NULL) break;
        }
        pEnum->Release();
    }
    
    return hr;
}

static HRESULT disconnectFilters(
    IBaseFilter* pFilter)
{
    HRESULT hr;
    if (pFilter == NULL) return E_POINTER;

    FILTER_INFO finfo;
    hr = pFilter->QueryFilterInfo(&finfo);
    if (SUCCEEDED(hr) && finfo.pGraph != NULL) {
        IEnumPins* pEnum = NULL;
        hr = pFilter->EnumPins(&pEnum);
        if (SUCCEEDED(hr)) {
            IPin* pPinFrom = NULL;
            while ((hr = pEnum->Next(1, &pPinFrom, NULL)) == S_OK) {
                IPin* pPinTo = NULL;
                hr = pPinFrom->ConnectedTo(&pPinTo);
                if (SUCCEEDED(hr)) {
                    PIN_INFO pinfo;
                    hr = pPinTo->QueryPinInfo(&pinfo);
                    if (SUCCEEDED(hr)) {
                        if (pinfo.dir == PINDIR_INPUT) {
                            disconnectFilters(pinfo.pFilter);
                            finfo.pGraph->Disconnect(pPinFrom);
                            finfo.pGraph->Disconnect(pPinTo);
                            finfo.pGraph->RemoveFilter(pinfo.pFilter);
                        }
                        pinfo.pFilter->Release();
                        pinfo.pFilter = NULL;
                    }
                    pPinTo->Release();
                }
                pPinFrom->Release();
            }
            pEnum->Release();
        }
    }

    return hr;    
}   

class WebCamoo
{
    IGraphBuilder* _pGraph;
    ICaptureGraphBuilder2* _pCapture;
    DWORD _dwGraphRegister;

    IBaseFilter* _pVideoSrc;
    IBaseFilter* _pAudioSrc;
    IBaseFilter* _pVideoSink;
    IBaseFilter* _pAudioSink;
    Filtaa* _pFiltaa;
    IMediaEventEx* _pME;

    IVMRWindowlessControl* _pVW;
    FILTER_STATE _state;
    long _videoWidth;
    long _videoHeight;

    HWND _hWnd;
    HMENU _deviceMenu;
    HDEVNOTIFY _notify;
    IMoniker* _pVideoMoniker;
    IMoniker* _pAudioMoniker;

    void UpdateDeviceMenuItems();
    void UpdateDeviceMenuChecks();
    void UpdateOutputMenu();
    HRESULT UpdatePlayState(FILTER_STATE state);
    HRESULT ClearVideoFilterGraph();
    HRESULT ClearAudioFilterGraph();
    HRESULT BuildVideoFilterGraph();
    HRESULT BuildAudioFilterGraph();
    HRESULT SelectVideo(IMoniker* pVideoMoniker);
    HRESULT SelectAudio(IMoniker* pAudioMoniker);
    
    HRESULT ResizeVideoWindow(void);
    HRESULT HandleGraphEvent(void);

    HRESULT OpenVideoFilterProperties();
    HRESULT OpenVideoPinProperties();
    HRESULT OpenAudioFilterProperties();
    
public:
    WebCamoo();
    ~WebCamoo();

    HRESULT InitializeCOM();
    HRESULT InitializeWindow(HWND hWnd);
    void UninitializeWindow(void);
    void DoCommand(UINT cmd);
    void HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

WebCamoo::WebCamoo()
{
    _pGraph = NULL;
    _pCapture = NULL;
    _dwGraphRegister = 0;
    
    _pVideoSrc = NULL;
    _pAudioSrc = NULL;
    _pVideoSink = NULL;
    _pAudioSink = NULL;
    _pFiltaa = new Filtaa();
    _pME = NULL;
    
    _pVW = NULL;
    _state = State_Stopped;
    _videoWidth = 0;
    _videoHeight = 0;

    _hWnd = NULL;
    _deviceMenu = NULL;
    _notify = NULL;
    _pVideoMoniker = NULL;
    _pAudioMoniker = NULL;
}

HRESULT WebCamoo::InitializeCOM()
{
    HRESULT hr;

    // Create the filter graph.
    hr = CoCreateInstance(
        CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&_pGraph));
    if (FAILED(hr)) return hr;

    // Create the capture graph builder.
    hr = CoCreateInstance(
        CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&_pCapture));
    if (FAILED(hr)) return hr;

    // Attach the filter graph to the capture graph.
    hr = _pCapture->SetFiltergraph(_pGraph);
    if (FAILED(hr)) return hr;

    if (REGISTER_FILTERGRAPH) {
        // Add our graph to the running object table, which will allow
        // the GraphEdit application to "spy" on our graph.
        hr = AddGraphToRot(_pGraph, &_dwGraphRegister);
        if (FAILED(hr)) {
            _dwGraphRegister = 0;
        }
    }

    // Create the video window.
    hr = CoCreateInstance(
        CLSID_VideoMixingRenderer, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&_pVideoSink));
    if (FAILED(hr)) return hr;

    {
        // Make it windowless mode.
        IVMRFilterConfig* pConfig = NULL;
        hr = _pVideoSink->QueryInterface(IID_PPV_ARGS(&pConfig));
        if (FAILED(hr)) return hr;
        hr = pConfig->SetRenderingMode(VMRMode_Windowless);
        if (FAILED(hr)) return hr;
        pConfig->Release();
    }
    
    // Obtain interfaces for Media Events.
    hr = _pGraph->QueryInterface(
        IID_PPV_ARGS(&_pME));
    if (FAILED(hr)) return hr;

    // Create the audio output.
    hr = CoCreateInstance(
        CLSID_DSoundRender, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&_pAudioSink));
    if (FAILED(hr)) return hr;

    return hr;
}

WebCamoo::~WebCamoo()
{
    if (_pFiltaa != NULL) {
        _pFiltaa->Release();
        _pFiltaa = NULL;
    }
    
    if (_pME != NULL) {
        _pME->Release();
        _pME = NULL;
    }

    if (_pVideoSink != NULL) {
        _pVideoSink->Release();
        _pVideoSink = NULL;
    }
    
    if (_pAudioSink != NULL) {
        _pAudioSink->Release();
        _pAudioSink = NULL;
    }
    
    if (REGISTER_FILTERGRAPH) {
        // Remove filter graph from the running object table   
        if (_dwGraphRegister) {
            RemoveGraphFromRot(_dwGraphRegister);
            _dwGraphRegister = 0;
        }
    }

    // Release DirectShow interfaces.
    if (_pCapture != NULL) {
        _pCapture->Release();
        _pCapture = NULL;
    }
    if (_pGraph != NULL) {
        _pGraph->Release();
        _pGraph = NULL;
    }
}

// UpdateDeviceMenuItems
void WebCamoo::UpdateDeviceMenuItems()
{
    log(L"UpdateDeviceMenuItems");
    
    ResetCaptureDevices(_deviceMenu);
    AddCaptureDevices(
        _deviceMenu,
        findMenuItemPos(_deviceMenu, IDM_DEVICE_VIDEO_NONE),
        IDM_DEVICE_VIDEO_START,
        CLSID_VideoInputDeviceCategory);
    AddCaptureDevices(
        _deviceMenu, 
        findMenuItemPos(_deviceMenu, IDM_DEVICE_AUDIO_NONE),
        IDM_DEVICE_AUDIO_START,
        CLSID_AudioInputDeviceCategory);
}

// UpdateDeviceMenuChecks
void WebCamoo::UpdateDeviceMenuChecks()
{
    HRESULT hr;
    MENUITEMINFO mii = {0};
    mii.cbSize = sizeof(mii);
    
    BOOL videoFilterProp = FALSE;
    BOOL videoPinProp = FALSE;
    BOOL audioFilterProp = FALSE;
    
    if (_pVideoSrc != NULL) {
        ISpecifyPropertyPages* pSpec = NULL;
        hr = _pVideoSrc->QueryInterface(IID_PPV_ARGS(&pSpec));
        if (SUCCEEDED(hr)) {
            videoFilterProp = TRUE;
            pSpec->Release();
        }
        IPin* pPin = NULL;
        hr = findPin(_pVideoSrc, PINDIR_OUTPUT,
                     &PIN_CATEGORY_CAPTURE, &pPin);
        if (SUCCEEDED(hr) && pPin != NULL) {
            hr = pPin->QueryInterface(IID_PPV_ARGS(&pSpec));
            if (SUCCEEDED(hr)) {
                mii.fMask = MIIM_STATE;
                videoPinProp = TRUE;
                pSpec->Release();
            }
            pPin->Release();
        }
    }
    if (_pAudioSrc != NULL) {
        ISpecifyPropertyPages* pSpec = NULL;
        hr = _pAudioSrc->QueryInterface(IID_PPV_ARGS(&pSpec));
        if (SUCCEEDED(hr)) {
            audioFilterProp = TRUE;
            pSpec->Release();
        }
    }

    setMenuItemDisabled(_deviceMenu, IDM_OPEN_VIDEO_FILTER_PROPERTIES,
                        !videoFilterProp);
    setMenuItemDisabled(_deviceMenu, IDM_OPEN_VIDEO_PIN_PROPERTIES,
                        !videoPinProp);
    setMenuItemDisabled(_deviceMenu, IDM_OPEN_AUDIO_FILTER_PROPERTIES,
                        !audioFilterProp);
    
    int n = GetMenuItemCount(_deviceMenu);
    for(int i = 0; i < n; i++) {
        mii.fMask = (MIIM_ID | MIIM_DATA);
        GetMenuItemInfo(_deviceMenu, i, TRUE, &mii);
        BOOL checked;
        if (mii.wID == IDM_DEVICE_VIDEO_NONE) {
            checked = (_pVideoMoniker == NULL);
        } else if (mii.wID == IDM_DEVICE_AUDIO_NONE) {
            checked = (_pAudioMoniker == NULL);
        } else if (IDM_DEVICE_VIDEO_START <= mii.wID &&
                   mii.wID <= IDM_DEVICE_VIDEO_END) {
            checked = (_pVideoMoniker == (IMoniker*)mii.dwItemData);
        } else if (IDM_DEVICE_AUDIO_START <= mii.wID &&
                   mii.wID <= IDM_DEVICE_AUDIO_END) {
            checked = (_pAudioMoniker == (IMoniker*)mii.dwItemData);
        } else {
            continue;
        }
        mii.fMask = MIIM_STATE;
        mii.fState = (checked)? MFS_CHECKED : MFS_UNCHECKED;
        SetMenuItemInfo(_deviceMenu, i, TRUE, &mii);
    }
}

// UpdateOutputMenu
void WebCamoo::UpdateOutputMenu()
{
    HMENU hMenu = GetMenu(_hWnd);
    if (_state != State_Running) {
        setMenuItemDisabled(hMenu, IDM_KEEP_ASPECT_RATIO, TRUE);
        setMenuItemDisabled(hMenu, IDM_THRESHOLDING, TRUE);
        setMenuItemDisabled(hMenu, IDM_AUTO_THRESHOLD, TRUE);
        setMenuItemDisabled(hMenu, IDM_INC_THRESHOLD, TRUE);
        setMenuItemDisabled(hMenu, IDM_DEC_THRESHOLD, TRUE);
    } else {
        setMenuItemDisabled(hMenu, IDM_KEEP_ASPECT_RATIO, FALSE);
        setMenuItemDisabled(hMenu, IDM_THRESHOLDING, FALSE);
        BOOL thresholding = isMenuItemChecked(hMenu, IDM_THRESHOLDING);
        setMenuItemDisabled(hMenu, IDM_AUTO_THRESHOLD, !thresholding);
        setMenuItemDisabled(hMenu, IDM_INC_THRESHOLD, !thresholding);
        setMenuItemDisabled(hMenu, IDM_DEC_THRESHOLD, !thresholding);
    }
}

// UpdatePlayState
HRESULT WebCamoo::UpdatePlayState(FILTER_STATE state)
{
    HRESULT hr = S_OK;
    
    if (state != _state) {
        IMediaControl* pMC = NULL;
        hr = _pGraph->QueryInterface(IID_PPV_ARGS(&pMC));
        if (SUCCEEDED(hr)) {
            switch (state) {
            case State_Running:
                hr = pMC->Run();
                break;
            case State_Paused:
                hr = pMC->Pause();
                break;
            case State_Stopped:
                hr = pMC->Stop();
                break;
            }
            if (SUCCEEDED(hr)) {
                _state = state;
            }
            pMC->Release();
        }
        UpdateOutputMenu();
    }
    
    return hr;
}

// ClearVideoFilterGraph
HRESULT WebCamoo::ClearVideoFilterGraph()
{
    log(L"ClearVideoFilterGraph");

    if (_pVW == NULL) return S_OK;
    
    _pVW->Release();
    _pVW = NULL;
    _videoWidth = 0;
    _videoHeight = 0;
    if (_pVideoSrc != NULL) {
        disconnectFilters(_pVideoSrc);
    }

    return S_OK;
}

// ClearAudioFilterGraph
HRESULT WebCamoo::ClearAudioFilterGraph()
{
    log(L"ClearAudioFilterGraph");

    if (_pAudioSrc != NULL) {
        disconnectFilters(_pAudioSrc);
    }

    return S_OK;
}

// BuildVideoFilterGraph
HRESULT WebCamoo::BuildVideoFilterGraph()
{
    HRESULT hr;
    log(L"BuildVideoFilterGraph");

    if (_pVW != NULL) return S_OK;

    BOOL thresholding = isMenuItemChecked(GetMenu(_hWnd), IDM_THRESHOLDING);
    if (_pVideoSrc != NULL &&
        _pVideoSink != NULL) {
        // Add Capture filter to our graph.
        hr = _pGraph->AddFilter(_pVideoSink, L"VideoSink");
        if (FAILED(hr)) return hr;
        if (thresholding) {
            IBaseFilter* pFilter = NULL;
            hr = _pFiltaa->QueryInterface(IID_PPV_ARGS(&pFilter));
            if (SUCCEEDED(hr)) {
                hr = _pGraph->AddFilter(pFilter, L"Filtaa");
                if (SUCCEEDED(hr)) {
                    hr = _pCapture->RenderStream(
                        &PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video,
                        _pVideoSrc, pFilter, _pVideoSink);
                }
                pFilter->Release();
            }
            if (FAILED(hr)) return hr;
        } else {
            // Render the preview pin on the video capture filter.
            // Use this instead of _pGraph->RenderFile.
            hr = _pCapture->RenderStream(
                &PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video,
                _pVideoSrc, NULL, _pVideoSink);
            if (FAILED(hr)) return hr;
        }

        // Obtain interfaces for Video Window.
        hr = _pVideoSink->QueryInterface(
            IID_PPV_ARGS(&_pVW));
        if (FAILED(hr)) return hr;
        
        // Set the video window to be a child of the main window.
        hr = _pVW->SetVideoClippingWindow(_hWnd);
        if (FAILED(hr)) return hr;
    
        // Obtain the native video size.
        hr = _pVW->GetNativeVideoSize(&_videoWidth, &_videoHeight,
                                      NULL, NULL);
        if (FAILED(hr)) return hr;
        
        // Use helper function to position video window in client rect
        // of main application window.
        ResizeVideoWindow();
    }
    
    return hr;
}

// BuildAudioFilterGraph
HRESULT WebCamoo::BuildAudioFilterGraph()
{
    HRESULT hr;
    log(L"BuildVideoFilterGraph");

    if (_pAudioSrc != NULL &&
        _pAudioSink != NULL) {
        // Add Capture filter to our graph.
        hr = _pGraph->AddFilter(_pAudioSink, L"AudioSink");
        if (FAILED(hr)) return hr;
        // Render the preview pin on the audio capture filter.
        // Use this instead of _pGraph->RenderFile.
        hr = _pCapture->RenderStream(
            &PIN_CATEGORY_PREVIEW, &MEDIATYPE_Audio,
            _pAudioSrc, NULL, _pAudioSink);
        if (FAILED(hr)) return hr;
    }
    
    return hr;
}

HRESULT WebCamoo::SelectVideo(IMoniker* pMoniker)
{
    HRESULT hr;
    log(L"SelectVideo: %p", pMoniker);

    if (_pVideoSrc != NULL) {
        _pGraph->RemoveFilter(_pVideoSrc);
        _pVideoSrc->Release();
        _pVideoSrc = NULL;
    }

    if (pMoniker != NULL) {
        hr = pMoniker->BindToObject(
            NULL, NULL, IID_PPV_ARGS(&_pVideoSrc));
        if (SUCCEEDED(hr)) {
            _pGraph->AddFilter(_pVideoSrc, L"VideoSrc");
            _pVideoMoniker = pMoniker;
        }
    } else {
        hr = S_OK;
        _pVideoMoniker = NULL;
    }

    return hr;
}

HRESULT WebCamoo::SelectAudio(IMoniker* pMoniker)
{
    HRESULT hr;
    log(L"SelectAudio: %p", pMoniker);

    if (_pAudioSrc != NULL) {
        _pGraph->RemoveFilter(_pAudioSrc);
        _pAudioSrc->Release();
        _pAudioSrc = NULL;
    }
    
    if (pMoniker != NULL) {
        hr = pMoniker->BindToObject(
            NULL, NULL, IID_PPV_ARGS(&_pAudioSrc));
        if (SUCCEEDED(hr)) {
            _pGraph->AddFilter(_pAudioSrc, L"AudioSrc");
            _pAudioMoniker = pMoniker;
        }
    } else {
        hr = S_OK;
        _pAudioMoniker = NULL;
    }
    
    return S_OK;
}

HRESULT WebCamoo::InitializeWindow(HWND hWnd)
{
    HRESULT hr;

    _hWnd = hWnd;
    _deviceMenu = GetSubMenu(GetMenu(hWnd), 1);
    UpdateDeviceMenuItems();

    // Register for device add/remove notifications
    DEV_BROADCAST_DEVICEINTERFACE dev = {0};
    dev.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    dev.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    dev.dbcc_classguid = AM_KSCATEGORY_CAPTURE;
    _notify = RegisterDeviceNotification(hWnd, &dev, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Set the window handle used to process graph events.
    hr = _pME->SetNotifyWindow(
        (OAHWND)hWnd, WM_GRAPHNOTIFY, 0);
    if (FAILED(hr)) return hr;

    return hr;
}

void WebCamoo::UninitializeWindow(void)
{
    UpdatePlayState(State_Stopped);
    ClearVideoFilterGraph();
    SelectVideo(NULL);
    ClearAudioFilterGraph();
    SelectAudio(NULL);
        
    // Stop receiving events.
    _pME->SetNotifyWindow(0, WM_GRAPHNOTIFY, 0);

    if (_notify != NULL) {
        UnregisterDeviceNotification(_notify);
        _notify = NULL;
    }

    if (_deviceMenu != NULL) {
        ResetCaptureDevices(_deviceMenu);
        _deviceMenu = NULL;
    }
    
    _hWnd = NULL;
}

HRESULT WebCamoo::ResizeVideoWindow(void)
{
    HRESULT hr;
    if (_videoWidth == 0 || _videoHeight == 0 || _pVW == NULL) return S_OK;
        
    // Resize the video preview window to match owner window size
    RECT rc;
    GetClientRect(_hWnd, &rc);
    BOOL keepRatio = isMenuItemChecked(GetMenu(_hWnd), IDM_KEEP_ASPECT_RATIO);
    if (keepRatio) {
        int w0 = rc.right - rc.left;
        int h0 = rc.bottom - rc.top;
        int w1 = _videoWidth;
        int h1 = _videoHeight;
        if (w0*h1 < w1*h0) {
            // fit horizontally.
            h1 = h1*w0/w1;
            w1 = w0;
        } else {
            // fit vertically.
            w1 = w1*h0/h1;
            h1 = h0;
        }
        SetRect(&rc,
                (rc.left+rc.right-w1)/2,
                (rc.top+rc.bottom-h1)/2,
                (rc.left+rc.right+w1)/2,
                (rc.top+rc.bottom+h1)/2);
    } else {
        // Make the preview video fill our window.
        ;
    }

    hr = _pVW->SetVideoPosition(NULL, &rc);
    if (FAILED(hr)) return hr;

    InvalidateRect(_hWnd, NULL, TRUE);

    return S_OK;
}

HRESULT WebCamoo::HandleGraphEvent(void)
{
    LONG evCode;
    LONG_PTR evParam1, evParam2;
    HRESULT hr = S_OK;

    if (!_pME) return E_POINTER;

    while (SUCCEEDED(_pME->GetEvent(&evCode, &evParam1, &evParam2, 0))) {
        //log(L"MediaEvent: code=%ld, param1=%p, param2=%p", evCode, evParam1, evParam2);
        switch (evCode) {
        case EC_ERRORABORT:
            UpdatePlayState(State_Stopped);
            ClearVideoFilterGraph();
            SelectVideo(NULL);
            ClearAudioFilterGraph();
            SelectAudio(NULL);
            UpdateDeviceMenuChecks();
            break;
            
        case EC_DEVICE_LOST:
            if (evParam2 == 0) {
                IBaseFilter* pLost = NULL;
                hr = ((IUnknown*)evParam1)->QueryInterface(IID_PPV_ARGS(&pLost));
                if (pLost == _pVideoSrc) {
                    UpdatePlayState(State_Stopped);
                    ClearVideoFilterGraph();
                    SelectVideo(NULL);
                    UpdateDeviceMenuChecks();
                } else if (pLost == _pAudioSrc) {
                    UpdatePlayState(State_Stopped);
                    ClearAudioFilterGraph();
                    SelectAudio(NULL);
                    UpdateDeviceMenuChecks();
                }
            }
            break;
        }
        hr = _pME->FreeEventParams(evCode, evParam1, evParam2);
    }

    return hr;
}

HRESULT WebCamoo::OpenVideoFilterProperties()
{
    HRESULT hr;
    if (_pVideoSrc == NULL) return S_OK;
    
    ISpecifyPropertyPages* pSpec = NULL;
    hr = _pVideoSrc->QueryInterface(IID_PPV_ARGS(&pSpec));
    if (SUCCEEDED(hr)) {
        VARIANT var;
        hr = getFriendlyName(&var, _pVideoMoniker);
        if (SUCCEEDED(hr)) {
            CAUUID cauuid;
            hr = pSpec->GetPages(&cauuid);
            if (SUCCEEDED(hr) && 0 < cauuid.cElems) {
                hr = OleCreatePropertyFrame(
                    _hWnd, 0, 0, var.bstrVal, 1,
                    (IUnknown**)&_pVideoSrc, cauuid.cElems,
                    (GUID*)cauuid.pElems, 0, 0, NULL);
                CoTaskMemFree(cauuid.pElems);
            }
            pSpec->Release();
            SysFreeString(var.bstrVal);
        }
    }
    
    return hr;
}

HRESULT WebCamoo::OpenVideoPinProperties()
{
    HRESULT hr;
    if (_pVideoSrc == NULL) return S_OK;
    
    UpdatePlayState(State_Stopped);
    ClearVideoFilterGraph();

    IPin* pPin = NULL;
    hr = findPin(_pVideoSrc, PINDIR_OUTPUT,
                 &PIN_CATEGORY_CAPTURE, &pPin);
    if (SUCCEEDED(hr) && pPin != NULL) {
        ISpecifyPropertyPages* pSpec = NULL;
        hr = pPin->QueryInterface(IID_PPV_ARGS(&pSpec));
        if (SUCCEEDED(hr)) {
            VARIANT var;
            hr = getFriendlyName(&var, _pVideoMoniker);
            if (SUCCEEDED(hr)) {
                CAUUID cauuid;
                hr = pSpec->GetPages(&cauuid);
                if (SUCCEEDED(hr) && 0 < cauuid.cElems) {
                    hr = OleCreatePropertyFrame(
                        _hWnd, 0, 0, var.bstrVal, 1,
                        (IUnknown**)&pPin, cauuid.cElems,
                        (GUID*)cauuid.pElems, 0, 0, NULL);
                    CoTaskMemFree(cauuid.pElems);
                }
                pSpec->Release();
                SysFreeString(var.bstrVal);
            }
        }
        pPin->Release();
    }

    BuildVideoFilterGraph();
    UpdatePlayState(State_Running);
    
    return S_OK;
}

HRESULT WebCamoo::OpenAudioFilterProperties()
{
    HRESULT hr;
    if (_pAudioSrc == NULL) return S_OK;
    
    ISpecifyPropertyPages* pSpec = NULL;
    hr = _pAudioSrc->QueryInterface(IID_PPV_ARGS(&pSpec));
    if (SUCCEEDED(hr)) {
        VARIANT var;
        hr = getFriendlyName(&var, _pAudioMoniker);
        if (SUCCEEDED(hr)) {
            CAUUID cauuid;
            hr = pSpec->GetPages(&cauuid);
            if (SUCCEEDED(hr) && 0 < cauuid.cElems) {
                hr = OleCreatePropertyFrame(
                    _hWnd, 0, 0, var.bstrVal, 1,
                    (IUnknown**)&_pAudioSrc, cauuid.cElems,
                    (GUID*)cauuid.pElems, 0, 0, NULL);
                CoTaskMemFree(cauuid.pElems);
            }
            pSpec->Release();
            SysFreeString(var.bstrVal);
        }
    }
    
    return hr;
}

void WebCamoo::DoCommand(UINT cmd)
{
    HMENU hMenu = findSubMenu(GetMenu(_hWnd), cmd);
    WCHAR name[1024];
    MENUITEMINFO mii = {0};
    mii.cbSize = sizeof(mii);
    mii.dwTypeData = name;
    mii.cch = 1024;

    switch (cmd) {
    case IDM_EXIT:
        SendMessage(_hWnd, WM_CLOSE, 0, 0);
        break;

    case IDM_KEEP_ASPECT_RATIO:
        toggleMenuItemChecked(hMenu, cmd);
        ResizeVideoWindow();
        break;

    case IDM_THRESHOLDING:
        UpdatePlayState(State_Stopped);
        toggleMenuItemChecked(hMenu, cmd);
        ClearVideoFilterGraph();
        BuildVideoFilterGraph();
        UpdatePlayState(State_Running);
        break;

    case IDM_AUTO_THRESHOLD:
        toggleMenuItemChecked(hMenu, cmd);
        if (isMenuItemChecked(hMenu, cmd)) {
            _pFiltaa->SetThreshold(-1);
        } else {
            int threshold = _pFiltaa->GetAutoThreshold();
            _pFiltaa->SetThreshold(threshold);
        }
        UpdateOutputMenu();
        break;

    case IDM_INC_THRESHOLD:
        {
            int threshold;
            if (isMenuItemChecked(hMenu, IDM_AUTO_THRESHOLD)) {
                toggleMenuItemChecked(hMenu, IDM_AUTO_THRESHOLD);
                threshold = _pFiltaa->GetAutoThreshold();
            } else {
                threshold = _pFiltaa->GetThreshold();
            }
            threshold = min2(threshold+THRESHOLD_DELTA, 255);
            log(L"threshold=%d", threshold);
            _pFiltaa->SetThreshold(threshold);
        }
        UpdateOutputMenu();
        break;
    case IDM_DEC_THRESHOLD:
        {
            int threshold;
            if (isMenuItemChecked(hMenu, IDM_AUTO_THRESHOLD)) {
                toggleMenuItemChecked(hMenu, IDM_AUTO_THRESHOLD);
                threshold = _pFiltaa->GetAutoThreshold();
            } else {
                threshold = _pFiltaa->GetThreshold();
            }
            threshold = max2(0, threshold-THRESHOLD_DELTA);
            log(L"threshold=%d", threshold);
            _pFiltaa->SetThreshold(threshold);
        }
        UpdateOutputMenu();
        break;

    case IDM_OPEN_VIDEO_FILTER_PROPERTIES:
        OpenVideoFilterProperties();
        break;
        
    case IDM_OPEN_VIDEO_PIN_PROPERTIES:
        OpenVideoPinProperties();
        break;
        
    case IDM_OPEN_AUDIO_FILTER_PROPERTIES:
        OpenAudioFilterProperties();
        break;
        
    case IDM_DEVICE_VIDEO_NONE:
        UpdatePlayState(State_Stopped);
        ClearVideoFilterGraph();
        SelectVideo(NULL);
        UpdateDeviceMenuChecks();
        UpdatePlayState(State_Running);
        break;

    case IDM_DEVICE_AUDIO_NONE:
        UpdatePlayState(State_Stopped);
        ClearAudioFilterGraph();
        SelectAudio(NULL);
        UpdateDeviceMenuChecks();
        UpdatePlayState(State_Running);
        break;

    default:
        if (IDM_DEVICE_VIDEO_START <= cmd &&
            cmd <= IDM_DEVICE_VIDEO_END) {
            mii.fMask = (MIIM_STRING | MIIM_DATA);
            if (GetMenuItemInfo(hMenu, cmd, FALSE, &mii)) {
                if (mii.dwItemData != 0) {
                    IMoniker* pMoniker = (IMoniker*)mii.dwItemData;
                    UpdatePlayState(State_Stopped);
                    ClearVideoFilterGraph();
                    SelectVideo(pMoniker);
                    UpdateDeviceMenuChecks();
                    BuildVideoFilterGraph();
                    UpdatePlayState(State_Running);
                }
            }
        } else if (IDM_DEVICE_AUDIO_START <= cmd &&
                   cmd <= IDM_DEVICE_AUDIO_END) {
            mii.fMask = (MIIM_STRING | MIIM_DATA);
            if (GetMenuItemInfo(hMenu, cmd, FALSE, &mii)) {
                if (mii.dwItemData != 0) {
                    IMoniker* pMoniker = (IMoniker*)mii.dwItemData;
                    UpdatePlayState(State_Stopped);
                    ClearAudioFilterGraph();
                    SelectAudio(pMoniker);
                    UpdateDeviceMenuChecks();
                    BuildAudioFilterGraph();
                    UpdatePlayState(State_Running);
                }
            }
        }
    }
}

void WebCamoo::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (hWnd != _hWnd) return;
    
    switch (uMsg) {
    case WM_COMMAND:
        DoCommand(LOWORD(wParam));
        break;
        
    case WM_SIZE:
        ResizeVideoWindow();
        break;
        
    case WM_PAINT:
        if (_pVW != NULL) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            _pVW->RepaintVideo(hWnd, hdc);
            EndPaint(hWnd, &ps);
        }
        break;
        
    case WM_WINDOWPOSCHANGED:
        if (_pVW != NULL) {
            if (!IsWindowVisible(_hWnd) || IsIconic(_hWnd)) {
                UpdatePlayState(State_Stopped);
            } else {
                UpdatePlayState(State_Running);
            }
        }
        break;
        
    case WM_GRAPHNOTIFY:
        HandleGraphEvent();
        break;

    case WM_INITMENU:
        UpdateDeviceMenuChecks();
        break;
        
    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVICEARRIVAL ||
            wParam == DBT_DEVICEREMOVECOMPLETE) {
            PDEV_BROADCAST_HDR pdbh = (PDEV_BROADCAST_HDR)lParam;
            if (pdbh->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE pdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lParam;
                // Check for capture devices.
                if (pdbi->dbcc_classguid == AM_KSCATEGORY_CAPTURE) {
                    UpdateDeviceMenuItems();
                }
            }
        }
        break;
        
    case WM_CLOSE:
        UninitializeWindow();
        DestroyWindow(_hWnd);
        break;
    }
}

static LRESULT CALLBACK WndMainProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    HRESULT hr;
    //log(L"hWnd:%p, uMsg=%x, wParam=%lu, lParam=%p", hWnd, uMsg, wParam, lParam);

    switch (uMsg) {
    case WM_CREATE:
        {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            WebCamoo* self = (WebCamoo*)(cs->lpCreateParams);
            if (self != NULL) {
                SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
                hr = self->InitializeWindow(hWnd);
                if (FAILED(hr)) return -1;
            }
        }
        return FALSE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    LONG_PTR lp = GetWindowLongPtr(hWnd, GWLP_USERDATA);
    WebCamoo* self = (WebCamoo*)lp;
    if (self != NULL) {
        self->HandleMessage(hWnd, uMsg, wParam, lParam);
    }
    
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


int WebCamooMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    int nCmdShow,
    int argc, LPWSTR* argv)
{
    HRESULT hr;

    // Initialize COM.
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) exit(111);

    // Initialize App.
    WebCamoo* app = new WebCamoo();
    if (!app) exit(111);
    if (FAILED(app->InitializeCOM())) exit(111);

    // Register the window class.
    ATOM atom;
    {
        WNDCLASS klass = {0};
        klass.lpfnWndProc   = WndMainProc;
        klass.hInstance     = hInstance;
        klass.lpszClassName = L"WebCamooClass";
        klass.lpszMenuName  = MAKEINTRESOURCE(IDM_MENU);
	klass.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE+1);
        klass.hCursor       = LoadCursor(NULL, IDC_ARROW);
        klass.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WEBCAMOO));
        atom = RegisterClass(&klass);
        if (!atom) exit(111);
    }
    HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDM_ACCEL));
    if (!hAccel) exit(111);

    // Create the main window.
    // The WS_CLIPCHILDREN style is required.
    HWND hWnd = CreateWindow(
        (LPCTSTR)atom,
        APPLICATION_NAME,
        WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, app);
    if (!hWnd) exit(111);

    // Don't display the main window until the DirectShow
    // preview graph has been created.  Once video data is
    // being received and processed, the window will appear
    // and immediately have useful video data to display.
    // Otherwise, it will be black until video data arrives.
    ShowWindow(hWnd, nCmdShow);

    app->DoCommand(IDM_DEVICE_VIDEO_START);

    // Main message loop.
    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        if (TranslateAccelerator(hWnd, hAccel, &msg)) {
            ;
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Uninitialize App.
    delete app;

    // Release COM.
    CoUninitialize();

    return (int)msg.wParam;
}


// WinMain and wmain
#ifdef WINDOWS
int PASCAL WinMain(
    HINSTANCE hInstance, 
    HINSTANCE hPrevInstance, 
    LPSTR lpCmdLine,
    int nCmdShow)
{
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
#ifdef DEBUG
    _wfopen_s(&logfp, L"log.txt", L"a");
#endif
    return WebCamooMain(hInstance, hPrevInstance, nCmdShow, argc, argv);
}
#else
int wmain(int argc, wchar_t* argv[])
{
    logfp = stderr;
    return WebCamooMain(GetModuleHandle(NULL), NULL, SW_SHOWDEFAULT, argc, argv);
}
#endif

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

//------------------------------------------------------------------------------
// File: WebCamoo.cpp
//
// Desc: DirectShow sample code - a very basic application using video capture
//       devices.  It creates a window and uses the first available capture
//       device to render and preview video capture data.
//------------------------------------------------------------------------------

#define _WIN32_WINNT 0x0500

#include <windows.h>
#include <dbt.h>
#include <dshow.h>
#include <stdio.h>
#include <strsafe.h>
#include <stdexcept>

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
const int DEFAULT_VIDEO_WIDTH = 320;
const int DEFAULT_VIDEO_HEIGHT = 320;
const LPCWSTR APPLICATION_NAME = L"WebCamoo";

// Application-defined message to notify app of filtergraph events.
const UINT WM_GRAPHNOTIFY = WM_APP+1;
const UINT IDM_DEVICE_VIDEO_START = IDM_DEVICE_VIDEO_NONE+1;
const UINT IDM_DEVICE_VIDEO_END = IDM_DEVICE_VIDEO_NONE+9999;
const UINT IDM_DEVICE_AUDIO_START = IDM_DEVICE_AUDIO_NONE+1;
const UINT IDM_DEVICE_AUDIO_END = IDM_DEVICE_AUDIO_NONE+9999;

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
        CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC,
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
                IPropertyBag* pBag = NULL;
                hr = pMoniker->BindToStorage(NULL, NULL, IID_PPV_ARGS(&pBag));
                if (SUCCEEDED(hr)) {
                    VARIANT var;
                    var.vt = VT_BSTR;
                    hr = pBag->Read(L"FriendlyName", &var, NULL);
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
                    pBag->Release();
                }
                pMoniker->Release();
            }
            pClassEnum->Release();
        }
        pDevEnum->Release();
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

    IVideoWindow* _pVW;
    IMediaEventEx* _pME;
    FILTER_STATE _state;
    long _videoWidth;
    long _videoHeight;

    HWND _hWnd;
    HMENU _deviceMenu;
    HDEVNOTIFY _notify;
    IMoniker* _pVideoMoniker;
    IMoniker* _pAudioMoniker;

    BOOL GetMenuItemState(UINT id);
    void ToggleMenuItemState(UINT id);
    void UpdateDeviceMenuItems();
    void UpdateDeviceMenuChecks();
    HRESULT CleanupFilterGraph();
    HRESULT UpdateFilterGraph();
    HRESULT UpdatePlayState(FILTER_STATE state);
    
    void ResizeVideoWindow(void);
    HRESULT HandleGraphEvent(void);

public:
    WebCamoo();
    ~WebCamoo();

    HRESULT Initialize(HWND hWnd);
    void DoCommand(UINT cmd);
    void Uninitialize(void);
    void HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    HRESULT AttachVideo(IBaseFilter* pVideoSrc);
    HRESULT AttachAudio(IBaseFilter* pAudioSrc);
};

WebCamoo::WebCamoo()
{
    HRESULT hr;

    _pGraph = NULL;
    _pCapture = NULL;
    _dwGraphRegister = 0;
    
    _pVideoSrc = NULL;
    _pAudioSrc = NULL;
    _pVideoSink = NULL;
    _pAudioSink = NULL;
    _pFiltaa = new Filtaa();
    
    _pVW = NULL;
    _pME = NULL;
    _state = State_Stopped;
    _videoWidth = 0;
    _videoHeight = 0;

    _hWnd = NULL;
    _deviceMenu = NULL;
    _notify = NULL;
    _pVideoMoniker = NULL;
    _pAudioMoniker = NULL;

    // Create the filter graph.
    hr = CoCreateInstance(
        CLSID_FilterGraph, NULL, CLSCTX_INPROC,
        IID_PPV_ARGS(&_pGraph));
    if (FAILED(hr)) throw std::exception("Unable to initialize a FilterGraph.");

    // Create the capture graph builder.
    hr = CoCreateInstance(
        CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC,
        IID_PPV_ARGS(&_pCapture));
    if (FAILED(hr)) throw std::exception("Unable to initialize a GraphBuilder.");

    // Attach the filter graph to the capture graph.
    hr = _pCapture->SetFiltergraph(_pGraph);
    if (FAILED(hr)) throw std::exception("Unable to attach the Graph to the Builder.");

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
        CLSID_VideoRenderer, NULL, CLSCTX_INPROC,
        IID_PPV_ARGS(&_pVideoSink));
    if (FAILED(hr)) throw std::exception("Unable to create a Video Renderer.");

    // Obtain interfaces for Video Window.
    hr = _pVideoSink->QueryInterface(
        IID_PPV_ARGS(&_pVW));
    if (FAILED(hr)) throw std::exception("Unable to create a Video Window.");
    
    hr = _pGraph->QueryInterface(
        IID_PPV_ARGS(&_pME));
    if (FAILED(hr)) throw std::exception("Unable to create a Media Event.");

    // Create the audio output.
    hr = CoCreateInstance(
        CLSID_DSoundRender, NULL, CLSCTX_INPROC,
        IID_PPV_ARGS(&_pAudioSink));
    if (FAILED(hr)) throw std::exception("Unable to create a Audio Renderer.");
    
}

WebCamoo::~WebCamoo()
{
    if (_pFiltaa != NULL) {
        _pFiltaa->Release();
        _pFiltaa = NULL;
    }
    
    if (_pVW != NULL) {
        _pVW->Release();
        _pVW = NULL;
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

BOOL WebCamoo::GetMenuItemState(UINT id)
{
    HMENU hMenu = findSubMenu(GetMenu(_hWnd), id);
    MENUITEMINFO mii = {0};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STATE;
    if (GetMenuItemInfo(hMenu, id, FALSE, &mii)) {
        return (mii.fState & MFS_CHECKED);
    }
    return FALSE;
}

void WebCamoo::ToggleMenuItemState(UINT id)
{
    HMENU hMenu = findSubMenu(GetMenu(_hWnd), id);
    MENUITEMINFO mii = {0};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STATE;
    if (GetMenuItemInfo(hMenu, id, FALSE, &mii)) {
        BOOL checked = (mii.fState & MFS_CHECKED);
        mii.fState = (checked)? MFS_UNCHECKED : MFS_CHECKED;
        SetMenuItemInfo(hMenu, id, FALSE, &mii);
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
    int n = GetMenuItemCount(_deviceMenu);
    for(int i = 0; i < n; i++) {
        MENUITEMINFO mii = {0};
        mii.cbSize = sizeof(mii);
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

// CleanupFilterGraph
HRESULT WebCamoo::CleanupFilterGraph()
{
    HRESULT hr;
    log(L"CleanupFilterGraph");

    IEnumFilters* pEnum = NULL;
    hr = _pGraph->EnumFilters(&pEnum);
    if (SUCCEEDED(hr)) {
        // Remove every filter except Soruce/Sink.
        BOOL changed = TRUE;
        while (changed && SUCCEEDED(pEnum->Reset())) {
            changed = FALSE;
            IBaseFilter* pFilter = NULL;
            while (pEnum->Next(1, &pFilter, NULL) == S_OK) {
                hr = _pGraph->RemoveFilter(pFilter);
                pFilter->Release();
                if (SUCCEEDED(hr)) {
                    changed = TRUE;
                    break;
                }
            }
        }
        pEnum->Release();
    }

    return hr;
}

// UpdateFilterGraph
HRESULT WebCamoo::UpdateFilterGraph()
{
    HRESULT hr;
    log(L"UpdateFilterGraph");
    
    CleanupFilterGraph();

    if (_pVideoSrc != NULL &&
        _pVideoSink != NULL) {
        // Add Capture filter to our graph.
        hr = _pGraph->AddFilter(_pVideoSrc, L"VideoSrc");
        if (FAILED(hr)) return hr;
        hr = _pGraph->AddFilter(_pVideoSink, L"VideoSink");
        if (FAILED(hr)) return hr;
        if (GetMenuItemState(IDM_THRESHOLDING)) {
            IBaseFilter* pFilter = NULL;
            hr = _pFiltaa->QueryInterface(IID_PPV_ARGS(&pFilter));
            if (SUCCEEDED(hr)) {
                hr = _pGraph->AddFilter(pFilter, L"Filtaa");
                if (FAILED(hr)) return hr;
                hr = _pCapture->RenderStream(
                    &PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video,
                    _pVideoSrc, pFilter, _pVideoSink);
                pFilter->Release();
            }
        } else {
            // Render the preview pin on the video capture filter.
            // Use this instead of _pGraph->RenderFile.
            hr = _pCapture->RenderStream(
                &PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video,
                _pVideoSrc, NULL, _pVideoSink);
            if (FAILED(hr)) return hr;
        }
    }

    if (_pAudioSrc != NULL &&
        _pAudioSink != NULL) {
        // Add Capture filter to our graph.
        hr = _pGraph->AddFilter(_pAudioSrc, L"AudioSrc");
        if (FAILED(hr)) return hr;
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
                hr = pMC->StopWhenReady();
                break;
            }
            if (SUCCEEDED(hr)) {
                _state = state;
            }
            pMC->Release();
        }
    }
    
    return hr;
}

HRESULT WebCamoo::AttachVideo(IBaseFilter* pVideoSrc)
{
    HRESULT hr;
    log(L"AttachVideo: %p", pVideoSrc);

    _pVW->put_Visible(OAFALSE);

    if (pVideoSrc != NULL) {
        pVideoSrc->AddRef();
    }

    if (_pVideoSrc != NULL) {
        _pVideoSrc->Release();
        _pVideoSrc = NULL;
    }
    
    if (pVideoSrc != NULL) {
        _pVideoSrc = pVideoSrc;
        //_pVideoSrc->AddRef(); // already done.
    }
    _videoWidth = 0;
    _videoHeight = 0;
    
    hr = UpdateFilterGraph();
    if (FAILED(hr)) return hr;
        
    if (_pVideoSrc != NULL) {
        
        // Set the video window to be a child of the main window.
        hr = _pVW->put_Owner((OAHWND)_hWnd);
        if (FAILED(hr)) return hr;
    
        // Set video window style
        hr = _pVW->put_WindowStyle(WS_CHILD | WS_CLIPCHILDREN);
        if (FAILED(hr)) return hr;

        // Obtain the native video size.
        IBasicVideo* pVideo = NULL;
        hr = _pVideoSink->QueryInterface(IID_PPV_ARGS(&pVideo));
        if (FAILED(hr)) return hr;
        pVideo->GetVideoSize(&_videoWidth, &_videoHeight);
        pVideo->Release();
        
        // Use helper function to position video window in client rect
        // of main application window.
        ResizeVideoWindow();

        // Make the video window visible, now that it is properly positioned.
        hr = _pVW->put_Visible(OATRUE);
        if (FAILED(hr)) return hr;
    }

    return hr;
}

HRESULT WebCamoo::AttachAudio(IBaseFilter* pAudioSrc)
{
    HRESULT hr;
    log(L"AttachAudio: %p", pAudioSrc);

    if (pAudioSrc != NULL) {
        pAudioSrc->AddRef();
    }

    if (_pAudioSrc != NULL) {
        _pAudioSrc->Release();
        _pAudioSrc = NULL;
    }
    
    if (pAudioSrc != NULL) {
        _pAudioSrc = pAudioSrc;
        //_pAudioSrc->AddRef(); // already done.
    }
    
    hr = UpdateFilterGraph();
    if (FAILED(hr)) return hr;
    
    return hr;
}

HRESULT WebCamoo::Initialize(HWND hWnd)
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

void WebCamoo::Uninitialize(void)
{
    HRESULT hr;
    IMediaControl* pMC = NULL;
    hr = _pGraph->QueryInterface(IID_PPV_ARGS(&pMC));
    if (SUCCEEDED(hr)) {
        hr = pMC->Stop();
        pMC->Release();
    }
    AttachVideo(NULL);
    AttachAudio(NULL);
        
    // Relinquish ownership (IMPORTANT!) of the video window.
    // Failing to call put_Owner can lead to assert failures within
    // the video renderer, as it still assumes that it has a valid
    // parent window.
    _pVW->put_Visible(OAFALSE);
    _pVW->put_Owner(NULL);

    // Stop receiving events.
    _pME->SetNotifyWindow(NULL, WM_GRAPHNOTIFY, 0);

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

void WebCamoo::ResizeVideoWindow(void)
{
    HRESULT hr;
    if (_videoWidth == 0 || _videoHeight == 0) return;
        
    // Resize the video preview window to match owner window size
    RECT rc;
    GetClientRect(_hWnd, &rc);
    int w0 = rc.right - rc.left;
    int h0 = rc.bottom - rc.top;
    if (GetMenuItemState(IDM_KEEP_ASPECT_RATIO)) {
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
        _pVW->SetWindowPosition((rc.left+rc.right-w1)/2,
                                (rc.top+rc.bottom-h1)/2,
                                w1, h1);
    } else {
        // Make the preview video fill our window.
        _pVW->SetWindowPosition(rc.left, rc.top, w0, h0);
    }
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
            AttachVideo(NULL);
            AttachAudio(NULL);
            break;
        case EC_DEVICE_LOST:
            if (evParam2 == 0) {
                IBaseFilter* pLost = NULL;
                hr = ((IUnknown*)evParam1)->QueryInterface(IID_PPV_ARGS(&pLost));
                if (pLost == _pVideoSrc) {
                    UpdatePlayState(State_Stopped);
                    AttachVideo(NULL);
                } else if (pLost == _pAudioSrc) {
                    UpdatePlayState(State_Stopped);
                    AttachAudio(NULL);
                }
            }
            break;
        }
        hr = _pME->FreeEventParams(evCode, evParam1, evParam2);
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

    case IDM_THRESHOLDING:
        UpdatePlayState(State_Stopped);
        ToggleMenuItemState(cmd);
        UpdateFilterGraph();
        UpdatePlayState(State_Running);
        break;

    case IDM_KEEP_ASPECT_RATIO:
        ToggleMenuItemState(cmd);
        ResizeVideoWindow();
        break;

    case IDM_DEVICE_VIDEO_NONE:
        UpdatePlayState(State_Stopped);
        AttachVideo(NULL);
        _pVideoMoniker = NULL;
        UpdateDeviceMenuChecks();
        UpdatePlayState(State_Running);
        break;

    case IDM_DEVICE_AUDIO_NONE:
        UpdatePlayState(State_Stopped);
        AttachAudio(NULL);
        _pAudioMoniker = NULL;
        UpdateDeviceMenuChecks();
        UpdatePlayState(State_Running);
        break;

    default:
        if (IDM_DEVICE_VIDEO_START <= cmd &&
            cmd <= IDM_DEVICE_VIDEO_END) {
            mii.fMask = (MIIM_STRING | MIIM_DATA);
            if (GetMenuItemInfo(hMenu, cmd, FALSE, &mii)) {
                if (mii.dwItemData != NULL) {
                    IMoniker* pMoniker = (IMoniker*)mii.dwItemData;
                    IBaseFilter* pVideoSrc = NULL;
                    HRESULT hr = pMoniker->BindToObject(
                        NULL, NULL, IID_PPV_ARGS(&pVideoSrc));
                    if (SUCCEEDED(hr)) {
                        UpdatePlayState(State_Stopped);
                        hr = AttachVideo(pVideoSrc);
                        pVideoSrc->Release();
                        UpdatePlayState(State_Running);
                    }
                    _pVideoMoniker = pMoniker;
                    UpdateDeviceMenuChecks();
                }
            }
        } else if (IDM_DEVICE_AUDIO_START <= cmd &&
                   cmd <= IDM_DEVICE_AUDIO_END) {
            mii.fMask = (MIIM_STRING | MIIM_DATA);
            if (GetMenuItemInfo(hMenu, cmd, FALSE, &mii)) {
                if (mii.dwItemData != NULL) {
                    IMoniker* pMoniker = (IMoniker*)mii.dwItemData;
                    IBaseFilter* pAudioSrc = NULL;
                    HRESULT hr = pMoniker->BindToObject(
                        NULL, NULL, IID_PPV_ARGS(&pAudioSrc));
                    if (SUCCEEDED(hr)) {
                        UpdatePlayState(State_Stopped);
                        hr = AttachAudio(pAudioSrc);
                        pAudioSrc->Release();
                        UpdatePlayState(State_Running);
                    }
                    _pAudioMoniker = pMoniker;
                    UpdateDeviceMenuChecks();
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
        
    case WM_WINDOWPOSCHANGED:
        if (!IsWindowVisible(_hWnd) || IsIconic(_hWnd)) {
            UpdatePlayState(State_Stopped);
        } else {
            UpdatePlayState(State_Running);
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
        Uninitialize();
        DestroyWindow(_hWnd);
        break;
    }

    // Pass this message to the video window for notification of system changes.
    if (_pVW != NULL) {
        _pVW->NotifyOwnerMessage((LONG_PTR)_hWnd, uMsg, wParam, lParam);
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
                hr = self->Initialize(hWnd);
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
        klass.hIcon         = NULL;
        atom = RegisterClass(&klass);
        if (!atom) exit(111);
    }

    // Create the main window.
    // The WS_CLIPCHILDREN style is required.
    HWND hWnd = CreateWindow(
        (LPCTSTR)atom,
        APPLICATION_NAME,
        WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        DEFAULT_VIDEO_WIDTH, DEFAULT_VIDEO_HEIGHT,
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
        TranslateMessage(&msg);
        DispatchMessage(&msg);
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

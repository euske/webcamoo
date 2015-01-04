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
#include <dshow.h>
#include <stdio.h>
#include <strsafe.h>
#include <stdexcept>

#include "WebCamoo.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "strmiids.lib")


//  Constants
//
const int DEFAULT_VIDEO_WIDTH = 320;
const int DEFAULT_VIDEO_HEIGHT = 320;
const LPCTSTR APPLICATIONNAME = TEXT("Video Capture Previewer (WebCamoo)\0");
const LPCTSTR CLASSNAME = TEXT("VidCapPreviewer\0");

// Application-defined message to notify app of filtergraph events.
const UINT WM_GRAPHNOTIFY = WM_APP+1;


// Alert
static void Alert(TCHAR *szFormat, ...)
{
    TCHAR szBuffer[1024];  // Large buffer for long filenames or URLs.
    const size_t NUMCHARS = sizeof(szBuffer) / sizeof(szBuffer[0]);
    const int LASTCHAR = NUMCHARS - 1;

    // Format the input string
    va_list pArgs;
    va_start(pArgs, szFormat);

    // Use a bounded buffer size to prevent buffer overruns.  Limit count to
    // character size minus one to allow for a NULL terminating character.
    (void)StringCchVPrintf(szBuffer, NUMCHARS - 1, szFormat, pArgs);
    va_end(pArgs);

    // Ensure that the formatted string is NULL-terminated.
    szBuffer[LASTCHAR] = TEXT('\0');

    MessageBox(NULL, szBuffer, TEXT("WebCamoo Message"), MB_OK | MB_ICONERROR);
}


// An application can advertise the existence of its filter graph
// by registering the graph with a global Running Object Table (ROT).
// The GraphEdit application can detect and remotely view the running
// filter graph, allowing you to 'spy' on the graph with GraphEdit.
//
// To enable registration in this sample, define REGISTER_FILTERGRAPH 1.
//
#define REGISTER_FILTERGRAPH 1

#if REGISTER_FILTERGRAPH

// Adds a filter graph to the Running Object Table.
static HRESULT AddGraphToRot(IUnknown* pUnkGraph, DWORD* pdwRegister) 
{
    IMoniker* pMoniker;
    IRunningObjectTable* pROT;
    WCHAR wsz[128];
    HRESULT hr;

    if (!pUnkGraph || !pdwRegister) return E_POINTER;

    if (FAILED(GetRunningObjectTable(0, &pROT))) return E_FAIL;

    hr = StringCchPrintfW(
        wsz, NUMELMS(wsz),
        L"FilterGraph %08x pid %08x\0", (DWORD_PTR)pUnkGraph, 
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
    return hr;
}

// Removes a filter graph from the Running Object Table.
static void RemoveGraphFromRot(DWORD pdwRegister)
{
    IRunningObjectTable* pROT;

    if (SUCCEEDED(GetRunningObjectTable(0, &pROT))) {
        pROT->Revoke(pdwRegister);
        pROT->Release();
    }
}

#endif

// FindCaptureDevice
static HRESULT FindCaptureDevice(
    CLSID category, IBaseFilter** ppSrcFilter)
{
    HRESULT hr;
    IBaseFilter* pSrc = NULL;
    IMoniker* pMoniker = NULL;
    IEnumMoniker* pClassEnum = NULL;
    ICreateDevEnum* pDevEnum = NULL;

    if (!ppSrcFilter) return E_POINTER;
    
    // Create the system device enumerator.
    hr = CoCreateInstance(
        CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC,
        IID_ICreateDevEnum, (void**)&pDevEnum);
    if (FAILED(hr)) goto fail;

    // Create an enumerator for the video capture devices.
    hr = pDevEnum->CreateClassEnumerator(
        category, &pClassEnum, 0);
    if (FAILED(hr)) goto fail;
    if (pClassEnum == NULL) {
        // If there are no enumerators for the requested type, then 
        // CreateClassEnumerator will succeed, but pClassEnum will be NULL.
        hr = E_FAIL;
        goto fail;
    }

    // Use the first video capture device on the device list.
    // Note that if the Next() call succeeds but there are no monikers,
    // it will return S_FALSE (which is not a failure).  Therefore, we
    // check that the return code is S_OK instead of using SUCCEEDED() macro.
    hr = pClassEnum->Next(1, &pMoniker, NULL);
    if (hr == S_FALSE) {
        hr = E_FAIL;
        goto fail;
    }

    // Bind Moniker to a filter object.
    hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&pSrc);
    if (FAILED(hr)) goto fail;

    // Copy the found filter pointer to the output parameter.
    *ppSrcFilter = pSrc;
    (*ppSrcFilter)->AddRef();

fail:
    if (pSrc != NULL) {
        pSrc->Release();
        pSrc = NULL;
    }
    if (pMoniker != NULL) {
        pMoniker->Release();
        pMoniker = NULL;
    }
    if (pDevEnum != NULL) {
        pDevEnum->Release();
        pDevEnum = NULL;
    }
    if (pClassEnum != NULL) {
        pClassEnum->Release();
        pClassEnum = NULL;
    }

    return hr;
}


class WebCamoo
{
    IGraphBuilder* _pGraph;
    ICaptureGraphBuilder2* _pCapture;
    DWORD _dwGraphRegister;

    IVideoWindow* _pVW;
    IMediaControl* _pMC;
    IMediaEventEx* _pME;
    PLAYSTATE _psCurrent;

    void ResizeVideoWindow(HWND hWnd);
    HRESULT ChangePreviewState(int nShow);
    HRESULT HandleGraphEvent(void);

public:
    WebCamoo();
    ~WebCamoo();

    HRESULT Initialize(HWND hWnd);
    void Uninitialize(void);
    void HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    HRESULT AttachVideo();
    HRESULT AttachAudio();
};

WebCamoo::WebCamoo()
{
    HRESULT hr;

    _pGraph = NULL;
    _pCapture = NULL;
    _dwGraphRegister = 0;
    
    _pVW = NULL;
    _pMC = NULL;
    _pME = NULL;
    _psCurrent = Stopped;

    // Create the filter graph.
    hr = CoCreateInstance(
        CLSID_FilterGraph, NULL, CLSCTX_INPROC,
        IID_IGraphBuilder, (void**)&_pGraph);
    if (FAILED(hr)) throw std::exception("Unable to initialize a FilterGraph.");

    // Create the capture graph builder.
    hr = CoCreateInstance(
        CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC,
        IID_ICaptureGraphBuilder2, (void**)&_pCapture);
    if (FAILED(hr)) throw std::exception("Unable to initialize a GraphBuilder.");

    // Attach the filter graph to the capture graph.
    hr = _pCapture->SetFiltergraph(_pGraph);
    if (FAILED(hr)) throw std::exception("Unable to attach the Graph to the Builder.");

#if REGISTER_FILTERGRAPH
    // Add our graph to the running object table, which will allow
    // the GraphEdit application to "spy" on our graph.
    hr = AddGraphToRot(_pGraph, &_dwGraphRegister);
    if (FAILED(hr)) {
        _dwGraphRegister = 0;
    }
#endif    
}

WebCamoo::~WebCamoo()
{
#if REGISTER_FILTERGRAPH
    // Remove filter graph from the running object table   
    if (_dwGraphRegister) {
        RemoveGraphFromRot(_dwGraphRegister);
        _dwGraphRegister = 0;
    }
#endif

    // Release DirectShow interfaces.
    if (_pGraph != NULL) {
        _pGraph->Release();
        _pGraph = NULL;
    }
    if (_pCapture != NULL) {
        _pCapture->Release();
        _pCapture = NULL;
    }
}

HRESULT WebCamoo::AttachVideo()
{
    HRESULT hr;

    // Add a video filter.
    IBaseFilter* pSrcFilter = NULL;
        
    // Use the system device enumerator and class enumerator to find
    // a video capture/preview device, such as a desktop USB video camera.
    hr = FindCaptureDevice(CLSID_VideoInputDeviceCategory, &pSrcFilter);
    if (FAILED(hr)) return hr;
   
    // Add Capture filter to our graph.
    hr = _pGraph->AddFilter(pSrcFilter, L"Video Capture");
    if (FAILED(hr)) {
        pSrcFilter->Release();
        return hr;
    }

    // Render the preview pin on the video capture filter.
    // Use this instead of _pGraph->RenderFile.
    hr = _pCapture->RenderStream(
        &PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video,
        pSrcFilter, NULL, NULL);
    if (FAILED(hr)) {
        pSrcFilter->Release();
        return hr;
    }

    // Now that the filter has been added to the graph and we have
    // rendered its stream, we can release this reference to the filter.
    pSrcFilter->Release();
    
    return hr;
}

HRESULT WebCamoo::AttachAudio()
{
    HRESULT hr;

    // Add an audio filter.
    IBaseFilter* pSrcFilter = NULL;
        
    // Use the system device enumerator and class enumerator to find
    // a video capture/preview device, such as a desktop USB video camera.
    hr = FindCaptureDevice(CLSID_AudioInputDeviceCategory, &pSrcFilter);
    if (FAILED(hr)) return hr;
   
    // Add Capture filter to our graph.
    hr = _pGraph->AddFilter(pSrcFilter, L"Audio Capture");
    if (FAILED(hr)) {
        pSrcFilter->Release();
        return hr;
    }

    // Render the preview pin on the audio capture filter.
    // Use this instead of _pGraph->RenderFile.
    hr = _pCapture->RenderStream(
        &PIN_CATEGORY_PREVIEW, &MEDIATYPE_Audio,
        pSrcFilter, NULL, NULL);
    if (FAILED(hr)) {
        pSrcFilter->Release();
        return hr;
    }

    // Now that the filter has been added to the graph and we have
    // rendered its stream, we can release this reference to the filter.
    pSrcFilter->Release();
    
    return hr;
}

HRESULT WebCamoo::Initialize(HWND hWnd)
{
    HRESULT hr;

    // Obtain interfaces for media control and Video Window.
    hr = _pGraph->QueryInterface(
        IID_IVideoWindow, (void**)&_pVW);
    if (FAILED(hr)) return hr;

    hr = _pGraph->QueryInterface(
        IID_IMediaEventEx, (void**)&_pME);
    if (FAILED(hr)) return hr;

    hr = _pGraph->QueryInterface(
        IID_IMediaControl, (void**)&_pMC);
    if (FAILED(hr)) return hr;

    // Set the window handle used to process graph events.
    hr = _pME->SetNotifyWindow(
        (OAHWND)hWnd, WM_GRAPHNOTIFY, 0);
    if (FAILED(hr)) return hr;

    // Set the video window to be a child of the main window.
    hr = _pVW->put_Owner((OAHWND)hWnd);
    if (FAILED(hr)) return hr;
    
    // Set video window style
    hr = _pVW->put_WindowStyle(WS_CHILD | WS_CLIPCHILDREN);
    if (FAILED(hr)) return hr;

    // Use helper function to position video window in client rect
    // of main application window.
    ResizeVideoWindow(hWnd);

    // Make the video window visible, now that it is properly positioned.
    hr = _pVW->put_Visible(OATRUE);
    if (FAILED(hr)) return hr;

    // Start previewing video data.
    hr = _pMC->Run();
    if (FAILED(hr)) return hr;

    // Remember current state.
    _psCurrent = Running;
        
    return hr;
}

void WebCamoo::Uninitialize(void)
{
    // Stop previewing data.
    if (_pMC != NULL) {
        _pMC->StopWhenReady();
        _pMC->Release();
        _pMC = NULL;
    }

    _psCurrent = Stopped;

    // Relinquish ownership (IMPORTANT!) of the video window.
    // Failing to call put_Owner can lead to assert failures within
    // the video renderer, as it still assumes that it has a valid
    // parent window.
    if (_pVW != NULL) {
        _pVW->put_Visible(OAFALSE);
        _pVW->put_Owner(NULL);
        _pVW->Release();
        _pVW = NULL;
    }

    // Stop receiving events.
    if (_pME != NULL) {
        _pME->SetNotifyWindow(NULL, WM_GRAPHNOTIFY, 0);
        _pME->Release();
        _pME = NULL;
    }
}

void WebCamoo::ResizeVideoWindow(HWND hWnd)
{
    // Resize the video preview window to match owner window size
    if (_pVW != NULL) {
        RECT rc;
        // Make the preview video fill our window
        GetClientRect(hWnd, &rc);
        _pVW->SetWindowPosition(0, 0, rc.right, rc.bottom);
    }
}

HRESULT WebCamoo::ChangePreviewState(int nShow)
{
    HRESULT hr = S_OK;
    
    // If the media control interface isn't ready, don't call it.
    if (!_pMC) return S_OK;
    
    if (nShow) {
        if (_psCurrent != Running) {
            // Start previewing video data.
            hr = _pMC->Run();
            _psCurrent = Running;
        }
    } else {
        // Stop previewing video data.
        hr = _pMC->StopWhenReady();
        _psCurrent = Stopped;
    }
    
    return hr;
}

HRESULT WebCamoo::HandleGraphEvent(void)
{
    LONG evCode;
    LONG_PTR evParam1, evParam2;
    HRESULT hr = S_OK;

    if (!_pME) return E_POINTER;

    while (SUCCEEDED(_pME->GetEvent(&evCode, &evParam1, &evParam2, 0))) {
        // Free event parameters to prevent memory leaks associated with
        // event parameter data.  While this application is not interested
        // in the received events, applications should always process them.
        //
        hr = _pME->FreeEventParams(evCode, evParam1, evParam2);
        
        // Insert event processing code here, if desired
    }

    return hr;
}

void WebCamoo::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_GRAPHNOTIFY:
        HandleGraphEvent();
        break;
        
    case WM_SIZE:
        ResizeVideoWindow(hWnd);
        break;
        
    case WM_WINDOWPOSCHANGED:
        ChangePreviewState(!IsIconic(hWnd));
        break;
        
    case WM_CLOSE:
        Uninitialize();
        DestroyWindow(hWnd);
        break;
    }

    // Pass this message to the video window for notification of system changes.
    if (_pVW != NULL) {
        _pVW->NotifyOwnerMessage((LONG_PTR)hWnd, uMsg, wParam, lParam);
    }
}

static LRESULT CALLBACK WndMainProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    HRESULT hr;

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


int PASCAL WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
    HRESULT hr;

    // Initialize COM.
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) exit(111);

    // Initialize App.
    WebCamoo* app = new WebCamoo();
    if (!app) exit(111);
    if (FAILED(app->AttachVideo())) exit(111);
    //if (FAILED(app->AttachAudio())) exit(111);

    // Register the window class.
    ATOM atom;
    {
        WNDCLASS klass = {0};
        klass.lpfnWndProc   = WndMainProc;
        klass.hInstance     = hInstance;
        klass.lpszClassName = CLASSNAME;
        klass.lpszMenuName  = MAKEINTRESOURCE(IDM_MENU);
	klass.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        klass.hCursor       = LoadCursor(NULL, IDC_ARROW);
        klass.hIcon         = NULL;
        atom = RegisterClass(&klass);
        if (!atom) exit(111);
    }

    // Create the main window.
    // The WS_CLIPCHILDREN style is required.
    HWND hWnd = CreateWindow(
        (LPCTSTR)atom,
        APPLICATIONNAME,
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

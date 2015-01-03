// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

//------------------------------------------------------------------------------
// File: WebCamoo.h
//
// Desc: DirectShow sample code - header file for WebCamoo previewer
//       application.
//------------------------------------------------------------------------------


enum PLAYSTATE {Stopped, Paused, Running, Init};

//
// Constants
//
#define DEFAULT_VIDEO_WIDTH     320
#define DEFAULT_VIDEO_HEIGHT    320

#define APPLICATIONNAME TEXT("Video Capture Previewer (WebCamoo)\0")
#define CLASSNAME       TEXT("VidCapPreviewer\0")

// Application-defined message to notify app of filtergraph events
#define WM_GRAPHNOTIFY  WM_APP+1


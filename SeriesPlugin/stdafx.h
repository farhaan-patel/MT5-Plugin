//+------------------------------------------------------------------+
//|                                          MT5 Group Series Plugin  |
//|  stdafx.h - precompiled header / common includes                 |
//+------------------------------------------------------------------+
#pragma once

#define WINVER         _WIN32_WINNT_WIN7
#define _WIN32_WINNT   _WIN32_WINNT_WIN7
#define _WIN32_WINDOWS _WIN32_WINNT_WIN7
#define _WIN32_IE      _WIN32_IE_IE90
#define NTDDI_VERSION  NTDDI_WIN7
#define WIN32_LEAN_AND_MEAN
// _CRT_SECURE_NO_WARNINGS is defined project-wide (see vcxproj)

#include <windows.h>
#include <limits.h>
#include <new>

//--- MT5 Server API (path supplied via AdditionalIncludeDirectories = C:\MetaTrader5SDK\Include)
#include "MT5APIServer.h"
#include "MT5APITools.h"
//+------------------------------------------------------------------+

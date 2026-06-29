//+------------------------------------------------------------------+
//|                                          MT5 Group Series Plugin  |
//|  SeriesPlugin.cpp - DLL entry points & plugin description        |
//+------------------------------------------------------------------+
#include "stdafx.h"
#include "PluginInstance.h"

//--- module handle, used by the allocator to locate its config files
HMODULE g_hModule=nullptr;
//+------------------------------------------------------------------+
//| Plugin description (shown in the MT5 administrator)              |
//+------------------------------------------------------------------+
MTPluginInfo ExtPluginInfo=
  {
   100,                                   // plugin version (1.00)
   MTServerAPIVersion,                    // server API version the plugin was built against
   L"Group Series Login Allocator",       // name
   L"Copyright 2026",                     // copyright
   L"Assigns account logins from an independent per-group sequence."
  };
//+------------------------------------------------------------------+
//| DLL entry                                                        |
//+------------------------------------------------------------------+
BOOL APIENTRY DllMain(HMODULE hModule,DWORD reason,LPVOID /*reserved*/)
  {
   switch(reason)
     {
      case DLL_PROCESS_ATTACH:
         g_hModule=hModule;
         DisableThreadLibraryCalls(hModule);
         break;
      case DLL_PROCESS_DETACH:
         break;
     }
   return(TRUE);
  }
//+------------------------------------------------------------------+
//| Plugin About entry point                                         |
//+------------------------------------------------------------------+
MTAPIENTRY MTAPIRES MTServerAbout(MTPluginInfo& info)
  {
   info=ExtPluginInfo;
   return(MT_RET_OK);
  }
//+------------------------------------------------------------------+
//| Plugin instance creation entry point                             |
//+------------------------------------------------------------------+
MTAPIENTRY MTAPIRES MTServerCreate(UINT /*apiversion*/,IMTServerPlugin** plugin)
  {
   if(!plugin) return(MT_RET_ERR_PARAMS);
   if(((*plugin)=new(std::nothrow) CPluginInstance())==nullptr)
      return(MT_RET_ERR_MEM);
   return(MT_RET_OK);
  }
//+------------------------------------------------------------------+

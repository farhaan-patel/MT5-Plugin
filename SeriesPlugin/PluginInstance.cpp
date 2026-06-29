//+------------------------------------------------------------------+
//|                                          MT5 Group Series Plugin  |
//|  PluginInstance.cpp                                              |
//+------------------------------------------------------------------+
#include "stdafx.h"
#include "PluginInstance.h"
#include "Util.h"

//--- module handle captured in DllMain (SeriesPlugin.cpp), used to find config files
extern HMODULE g_hModule;
//+------------------------------------------------------------------+
//| Constructor / Destructor                                         |
//+------------------------------------------------------------------+
CPluginInstance::CPluginInstance(void) : m_api(nullptr),m_subscribed(false)
  {
   ZeroMemory(&m_info,sizeof(m_info));
  }
CPluginInstance::~CPluginInstance(void){ Stop(); }
//+------------------------------------------------------------------+
//| Release                                                          |
//+------------------------------------------------------------------+
void CPluginInstance::Release(void){ delete this; }
//+------------------------------------------------------------------+
//| Start: subscribe to user events, load the allocator              |
//+------------------------------------------------------------------+
MTAPIRES CPluginInstance::Start(IMTServerAPI* api)
  {
   if(!api) return(MT_RET_ERR_PARAMS);
   m_api=api;

   //--- server info (best-effort, for diagnostics)
   MTAPIRES retcode;
   if((retcode=m_api->About(m_info))!=MT_RET_OK)
      m_api->LoggerOut(MTLogErr,L"SeriesPlugin: About() failed [%d]",retcode);

   //--- load configuration + persisted counters, start SQL mirror
   m_allocator.Load(m_api,g_hModule);

   //--- subscribe to user events so HookUserAdd fires before each account is saved
   if((retcode=m_api->UserSubscribe(this))!=MT_RET_OK)
     {
      m_api->LoggerOut(MTLogErr,L"SeriesPlugin: UserSubscribe failed [%d]",retcode);
      return(retcode);
     }
   m_subscribed=true;

   m_api->LoggerOutString(MTLogOK,L"SeriesPlugin: started");
   return(MT_RET_OK);
  }
//+------------------------------------------------------------------+
//| Stop: unsubscribe, shut down allocator                           |
//+------------------------------------------------------------------+
MTAPIRES CPluginInstance::Stop(void)
  {
   if(m_api)
     {
      if(m_subscribed){ m_api->UserUnsubscribe(this); m_subscribed=false; }
      m_allocator.Shutdown();
      m_api->LoggerOutString(MTLogOK,L"SeriesPlugin: stopped");
      m_api=nullptr;
     }
   return(MT_RET_OK);
  }
//+------------------------------------------------------------------+
//| HookUserAdd: the interception point                              |
//|   - read the user's group                                        |
//|   - ask the allocator for the next login in that group's series  |
//|   - write it back onto the user, or abort creation on error      |
//+------------------------------------------------------------------+
MTAPIRES CPluginInstance::HookUserAdd(IMTUser* user)
  {
   if(!user || !m_api) return(MT_RET_OK);

   //--- nothing to do until configured
   if(!m_allocator.ConfigLoaded()) return(MT_RET_OK);

   std::wstring group     = user->Group();          // LPCWSTR -> wstring
   unsigned long long cur = (unsigned long long)user->Login();

   unsigned long long login=0;
   bool assigned=false;
   MTAPIRES res=(MTAPIRES)m_allocator.Allocate(group,cur,login,assigned);

   //--- allocator asked to abort (unknown group w/ reject, or range exhausted)
   if(res!=MT_RET_OK) return(res);

   //--- assign the computed login back onto the user record
   if(assigned)
     {
      MTAPIRES set=user->Login((UINT64)login);
      if(set!=MT_RET_OK)
        {
         m_api->LoggerOut(MTLogErr,L"SeriesPlugin: failed to set login %I64u on user (group '%s') [%d]",
                          login,group.c_str(),set);
         return(set);
        }
     }
   return(MT_RET_OK);
  }
//+------------------------------------------------------------------+

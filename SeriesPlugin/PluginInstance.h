//+------------------------------------------------------------------+
//|                                          MT5 Group Series Plugin  |
//|  PluginInstance.h - server plugin + user sink                    |
//+------------------------------------------------------------------+
#pragma once
#include "SeriesAllocator.h"
//+------------------------------------------------------------------+
//| Plugin instance: server plugin that hooks user creation          |
//+------------------------------------------------------------------+
class CPluginInstance : public IMTServerPlugin,
                        public IMTUserSink
  {
private:
   IMTServerAPI*     m_api;
   MTServerInfo      m_info;
   CSeriesAllocator  m_allocator;
   bool              m_subscribed;

public:
                     CPluginInstance(void);
   virtual          ~CPluginInstance(void);

   //--- IMTServerPlugin
   virtual void      Release(void) override;
   virtual MTAPIRES  Start(IMTServerAPI* server) override;
   virtual MTAPIRES  Stop(void) override;

   //--- IMTUserSink: the interception point. Fires before the user record
   //    is committed; we read the group and assign a series login.
   virtual MTAPIRES  HookUserAdd(IMTUser* user) override;
  };
//+------------------------------------------------------------------+

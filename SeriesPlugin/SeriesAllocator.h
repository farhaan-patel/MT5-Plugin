//+------------------------------------------------------------------+
//|                                          MT5 Group Series Plugin  |
//|  SeriesAllocator.h - per-group sequential login allocation        |
//|                                                                  |
//|  Owns the configuration (group -> start/max), the live counters,  |
//|  the lock that serialises allocation, state-file persistence and  |
//|  the SQL mirror. All allocation decisions go through Allocate().  |
//+------------------------------------------------------------------+
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include "SqlMirror.h"

class IMTServerAPI;
class IMTUser;
//+------------------------------------------------------------------+
//| One configured group series                                      |
//+------------------------------------------------------------------+
struct GroupRule
  {
   std::wstring      pattern;        // configured group, e.g. L"real\\group_a" or L"real\\group_a*"
   std::wstring      match;          // pattern without a trailing '*'
   bool              prefix;         // true when pattern ended with '*'
   unsigned long long start;         // first login in the series
   unsigned long long max;           // last allowed login, 0 = unlimited
   unsigned long long last;          // last assigned login (valid only if has_value)
   bool              has_value;      // false until first allocation
  };
//+------------------------------------------------------------------+
//| Allocator                                                        |
//+------------------------------------------------------------------+
class CSeriesAllocator
  {
private:
   IMTServerAPI*          m_api;          // not owned
   std::wstring           m_config_path;
   std::wstring           m_state_path;

   std::vector<GroupRule> m_rules;
   std::mutex             m_mutex;        // serialises allocation + state writes
   CSqlMirror             m_sql;

   //--- behaviour flags (from config)
   bool                   m_log_alloc;
   bool                   m_reject_unconfigured;
   bool                   m_override_explicit;
   bool                   m_check_existing;

   //--- helpers
   const GroupRule*       Match(const std::wstring &group) const; // most-specific match, nullptr if none
   GroupRule*             Match(const std::wstring &group);
   bool                   UserExists(unsigned long long login);   // via m_api->UserGet
   bool                   PersistStateLocked();                   // write JSON state (call under lock)
   void                   Log(LPCWSTR fmt,...);

public:
                          CSeriesAllocator();
                         ~CSeriesAllocator();

   //--- load config + state, start SQL mirror. Returns MT_RET_OK / error.
   long                   Load(IMTServerAPI* api,HMODULE module);
   void                   Shutdown();

   //--- core allocation.
   //    in : group, current_login (user->Login(), 0 if unset)
   //    out: out_login = login to assign, assigned = whether caller should set it
   //    ret: MT_RET_OK (assigned or deliberate pass-through) or an error to abort creation
   long                   Allocate(const std::wstring &group,unsigned long long current_login,
                                   unsigned long long &out_login,bool &assigned);

   bool                   ConfigLoaded() const { return !m_rules.empty(); }
   size_t                 RuleCount()   const { return m_rules.size(); }
  };
//+------------------------------------------------------------------+

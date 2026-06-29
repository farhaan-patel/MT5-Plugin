//+------------------------------------------------------------------+
//|                                          MT5 Group Series Plugin  |
//|  SeriesAllocator.cpp                                              |
//+------------------------------------------------------------------+
#include "stdafx.h"
#include "SeriesAllocator.h"
#include "Json.h"
#include "Util.h"
#include <cstdarg>
#include <ctime>
#include <algorithm>

//--- guard against an unbounded existence-skip scan
static const unsigned MAX_EXISTENCE_SKIPS = 1000000;
//+------------------------------------------------------------------+
//| Constructor / Destructor                                         |
//+------------------------------------------------------------------+
CSeriesAllocator::CSeriesAllocator()
   : m_api(nullptr),m_log_alloc(true),m_reject_unconfigured(false),
     m_override_explicit(false),m_check_existing(true) {}
CSeriesAllocator::~CSeriesAllocator(){ Shutdown(); }
//+------------------------------------------------------------------+
//| Logging passthrough                                              |
//+------------------------------------------------------------------+
void CSeriesAllocator::Log(LPCWSTR fmt,...)
  {
   if(!m_api) return;
   wchar_t buf[1024];
   va_list args; va_start(args,fmt);
   _vsnwprintf_s(buf,_countof(buf),_TRUNCATE,fmt,args);
   va_end(args);
   m_api->LoggerOut(MTLogOK,L"%s",buf);
  }
//+------------------------------------------------------------------+
//| Load config + state                                              |
//+------------------------------------------------------------------+
long CSeriesAllocator::Load(IMTServerAPI* api,HMODULE module)
  {
   m_api=api;
   std::wstring dir=util::ModuleDir(module);
   m_config_path=dir+L"\\SeriesPlugin.config.json";
   m_state_path =dir+L"\\SeriesPlugin.state.json";

   //--- read config file
   std::string text;
   if(!util::ReadFile(m_config_path,text))
     {
      Log(L"SeriesPlugin: config not found at %s - plugin will pass through all account creation",m_config_path.c_str());
      return(MT_RET_OK); // not fatal: behave as a no-op until configured
     }

   bool ok=false;
   js::Parser parser;
   js::Value root=parser.Parse(text,ok);
   if(!ok || !root.IsObject())
     {
      Log(L"SeriesPlugin: config parse error in %s - plugin disabled until fixed",m_config_path.c_str());
      return(MT_RET_OK);
     }

   //--- flags
   if(const js::Value* v=root.Find("log_allocations"))            m_log_alloc=v->AsBool(true);
   if(const js::Value* v=root.Find("reject_unconfigured_groups")) m_reject_unconfigured=v->AsBool(false);
   if(const js::Value* v=root.Find("override_explicit_login"))    m_override_explicit=v->AsBool(false);
   if(const js::Value* v=root.Find("check_existing_logins"))      m_check_existing=v->AsBool(true);

   //--- groups
   m_rules.clear();
   if(const js::Value* groups=root.Find("groups"))
      if(groups->IsArray())
         for(const js::Value &g : groups->arr)
           {
            if(!g.IsObject()) continue;
            const js::Value* pg=g.Find("group");
            const js::Value* ps=g.Find("start_login");
            if(!pg || !pg->IsString() || !ps || !ps->IsNumber()) continue;
            GroupRule r;
            r.pattern   = util::Utf8ToWide(pg->AsString());
            r.start     = ps->AsUInt64(0);
            r.max       = 0;
            if(const js::Value* pm=g.Find("max_login")) r.max=pm->AsUInt64(0);
            r.prefix    = (!r.pattern.empty() && r.pattern.back()==L'*');
            r.match     = r.prefix ? r.pattern.substr(0,r.pattern.size()-1) : r.pattern;
            r.last      = 0;
            r.has_value = false;
            if(r.pattern.empty() || r.start==0) continue;
            m_rules.push_back(r);
           }

   //--- SQL mirror config
   bool         sql_enabled=false;
   std::wstring sql_conn,sql_table;
   if(const js::Value* sm=root.Find("sql_mirror"))
      if(sm->IsObject())
        {
         if(const js::Value* v=sm->Find("enabled")) sql_enabled=v->AsBool(false);
         if(const js::Value* v=sm->Find("odbc_connection_string")) sql_conn=util::Utf8ToWide(v->AsString());
         if(const js::Value* v=sm->Find("table")) sql_table=util::Utf8ToWide(v->AsString());
        }

   //--- load persisted counters and apply to rules
   std::string state_text;
   if(util::ReadFile(m_state_path,state_text))
     {
      bool sok=false;
      js::Value sroot=parser.Parse(state_text,sok);
      if(sok && sroot.IsObject())
         if(const js::Value* counters=sroot.Find("counters"))
            if(counters->IsObject())
               for(const auto &kv : counters->obj)
                 {
                  std::wstring key=util::Utf8ToWide(kv.first);
                  unsigned long long last=kv.second.AsUInt64(0);
                  for(GroupRule &r : m_rules)
                     if(util::IEqualsW(r.pattern,key)){ r.last=last; r.has_value=true; break; }
                 }
     }

   //--- start SQL mirror
   m_sql.Configure(m_api,sql_enabled,sql_conn,sql_table);
   m_sql.Start();

   Log(L"SeriesPlugin: loaded %u group series, sql_mirror=%s, reject_unconfigured=%s",
       (unsigned)m_rules.size(), sql_enabled?L"on":L"off", m_reject_unconfigured?L"on":L"off");
   return(MT_RET_OK);
  }
//+------------------------------------------------------------------+
//| Shutdown                                                         |
//+------------------------------------------------------------------+
void CSeriesAllocator::Shutdown()
  {
   m_sql.Stop();
   m_api=nullptr;
  }
//+------------------------------------------------------------------+
//| Match a user group to the most specific configured rule          |
//+------------------------------------------------------------------+
const GroupRule* CSeriesAllocator::Match(const std::wstring &group) const
  {
   const GroupRule* best=nullptr;
   size_t best_len=0;
   for(const GroupRule &r : m_rules)
     {
      bool hit=false;
      if(r.prefix) hit=util::IStartsWithW(group,r.match);
      else         hit=util::IEqualsW(group,r.match);
      if(hit && r.match.size()>=best_len){ best=&r; best_len=r.match.size(); } // longest match wins
     }
   return best;
  }
GroupRule* CSeriesAllocator::Match(const std::wstring &group)
  { return const_cast<GroupRule*>(static_cast<const CSeriesAllocator*>(this)->Match(group)); }
//+------------------------------------------------------------------+
//| Does an account with this login already exist?                   |
//+------------------------------------------------------------------+
bool CSeriesAllocator::UserExists(unsigned long long login)
  {
   if(!m_api) return false;
   IMTUser* u=m_api->UserCreate();
   if(!u) return false;
   long res=m_api->UserGet((UINT64)login,u);
   u->Release();
   return(res==MT_RET_OK);
  }
//+------------------------------------------------------------------+
//| Write the state file (caller must hold m_mutex)                  |
//+------------------------------------------------------------------+
bool CSeriesAllocator::PersistStateLocked()
  {
   js::Value root=js::Value::Object();
   root.Set("version",js::Value::Num(1));
   js::Value counters=js::Value::Object();
   for(const GroupRule &r : m_rules)
      if(r.has_value)
         counters.Set(util::WideToUtf8(r.pattern.c_str()),js::Value::Num((double)r.last));
   root.Set("counters",counters);
   std::string out=js::Dump(root);
   bool ok=util::WriteFileAtomic(m_state_path,out);
   if(!ok) Log(L"SeriesPlugin: WARNING failed to persist state file %s",m_state_path.c_str());
   return ok;
  }
//+------------------------------------------------------------------+
//| Allocate a login for the given group                             |
//+------------------------------------------------------------------+
long CSeriesAllocator::Allocate(const std::wstring &group,unsigned long long current_login,
                                unsigned long long &out_login,bool &assigned)
  {
   assigned=false;
   out_login=current_login;

   //--- find the series for this group
   std::lock_guard<std::mutex> lock(m_mutex);
   GroupRule* rule=Match(group);
   if(!rule)
     {
      if(m_reject_unconfigured)
        {
         Log(L"SeriesPlugin: group '%s' is not configured - rejecting account creation",group.c_str());
         return(MT_RET_ERR_NOTFOUND);
        }
      //--- pass through: let MT5 assign its default login
      return(MT_RET_OK);
     }

   //--- respect an explicitly supplied login unless overriding is enabled
   if(current_login!=0 && !m_override_explicit)
     {
      Log(L"SeriesPlugin: group '%s' supplied explicit login %I64u - keeping it (override disabled)",
          group.c_str(),current_login);
      return(MT_RET_OK);
     }

   //--- compute the next candidate
   unsigned long long candidate = rule->has_value ? rule->last+1 : rule->start;

   //--- skip past any logins that already exist (handles pre-existing/manual accounts)
   unsigned skips=0;
   for(;;)
     {
      if(rule->max!=0 && candidate>rule->max)
        {
         Log(L"SeriesPlugin: series for '%s' exhausted (max %I64u) - rejecting account creation",
             rule->pattern.c_str(),rule->max);
         return(MT_RET_USR_LOGIN_EXHAUSTED);
        }
      if(m_check_existing && UserExists(candidate))
        {
         if(++skips>MAX_EXISTENCE_SKIPS)
           {
            Log(L"SeriesPlugin: too many existing logins scanned for '%s' - aborting",rule->pattern.c_str());
            return(MT_RET_USR_LOGIN_EXHAUSTED);
           }
         candidate++;
         continue;
        }
      break;
     }

   //--- reserve it: advance + persist BEFORE the account is committed so concurrent
   //    creations can never receive the same number (gaps on later failure are acceptable)
   rule->last      = candidate;
   rule->has_value = true;
   PersistStateLocked();

   out_login = candidate;
   assigned  = true;

   if(m_log_alloc)
      Log(L"SeriesPlugin: allocated login %I64u to group '%s' (series start %I64u)",
          candidate,group.c_str(),rule->start);

   //--- mirror to SQL (best-effort, async)
   SqlRecord rec; rec.group=group; rec.login=candidate; rec.start=rule->start; rec.when=(long long)time(nullptr);
   m_sql.Enqueue(rec);

   return(MT_RET_OK);
  }
//+------------------------------------------------------------------+

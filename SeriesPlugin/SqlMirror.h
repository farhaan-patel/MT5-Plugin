//+------------------------------------------------------------------+
//|                                          MT5 Group Series Plugin  |
//|  SqlMirror.h - best-effort ODBC mirror of allocations            |
//|                                                                  |
//|  The JSON state file is the source of truth. This class mirrors   |
//|  each allocation into a SQL table on a background thread so the    |
//|  trade hot-path is never blocked by, nor dependent on, the DB.    |
//+------------------------------------------------------------------+
#pragma once
#include <string>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class IMTServerAPI; // fwd - for logging

//--- one allocation event to mirror
struct SqlRecord
  {
   std::wstring group;        // full group name
   unsigned long long login;  // assigned login
   unsigned long long start;  // series start (for reporting)
   long long          when;   // unix seconds
  };

class CSqlMirror
  {
private:
   IMTServerAPI*           m_api;          // not owned, for logging only
   bool                    m_enabled;
   std::wstring            m_conn;         // ODBC connection string
   std::wstring            m_table;        // target table name

   std::thread             m_worker;
   std::mutex              m_mutex;
   std::condition_variable m_cv;
   std::deque<SqlRecord>   m_queue;
   std::atomic<bool>       m_stop;
   size_t                  m_max_queue;    // backpressure cap (drop oldest beyond this)

   void                    Run();          // worker loop
   bool                    WriteOne(void* hdbc,const SqlRecord &rec); // returns false on hard error

public:
                           CSqlMirror();
                          ~CSqlMirror();

   //--- configure from parsed values; safe to call before Start
   void                    Configure(IMTServerAPI* api,bool enabled,const std::wstring &conn,const std::wstring &table);
   void                    Start();
   void                    Stop();
   bool                    Enabled() const { return m_enabled; }

   //--- enqueue an allocation (non-blocking, thread-safe)
   void                    Enqueue(const SqlRecord &rec);
  };
//+------------------------------------------------------------------+

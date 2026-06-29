//+------------------------------------------------------------------+
//|                                          MT5 Group Series Plugin  |
//|  SqlMirror.cpp                                                    |
//+------------------------------------------------------------------+
#include "stdafx.h"
#include "SqlMirror.h"
#include <windows.h>
#include <sql.h>
#include <sqlext.h>

#pragma comment(lib,"odbc32.lib")
//+------------------------------------------------------------------+
//| local helper: log a diagnostic via the server logger             |
//+------------------------------------------------------------------+
static void MirrorLog(IMTServerAPI* api,LPCWSTR fmt,...);
//+------------------------------------------------------------------+
//| Constructor / Destructor                                         |
//+------------------------------------------------------------------+
CSqlMirror::CSqlMirror() : m_api(nullptr),m_enabled(false),m_stop(false),m_max_queue(100000) {}
CSqlMirror::~CSqlMirror(){ Stop(); }
//+------------------------------------------------------------------+
//| Configure                                                        |
//+------------------------------------------------------------------+
void CSqlMirror::Configure(IMTServerAPI* api,bool enabled,const std::wstring &conn,const std::wstring &table)
  {
   m_api     = api;
   m_enabled = enabled;
   m_conn    = conn;
   m_table   = table.empty() ? L"group_account_series" : table;
  }
//+------------------------------------------------------------------+
//| Start the worker thread                                          |
//+------------------------------------------------------------------+
void CSqlMirror::Start()
  {
   if(!m_enabled) return;
   m_stop=false;
   m_worker=std::thread(&CSqlMirror::Run,this);
  }
//+------------------------------------------------------------------+
//| Stop and flush                                                   |
//+------------------------------------------------------------------+
void CSqlMirror::Stop()
  {
   if(!m_worker.joinable()) return;
   {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stop=true;
   }
   m_cv.notify_all();
   m_worker.join();
  }
//+------------------------------------------------------------------+
//| Enqueue (non-blocking)                                           |
//+------------------------------------------------------------------+
void CSqlMirror::Enqueue(const SqlRecord &rec)
  {
   if(!m_enabled) return;
   {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back(rec);
    //--- crude backpressure: never let the queue grow unbounded if DB is dead
    while(m_queue.size()>m_max_queue) m_queue.pop_front();
   }
   m_cv.notify_one();
  }
//+------------------------------------------------------------------+
//| Worker loop: maintain a connection, drain the queue              |
//+------------------------------------------------------------------+
void CSqlMirror::Run()
  {
   SQLHENV henv=SQL_NULL_HENV;
   SQLHDBC hdbc=SQL_NULL_HDBC;
   bool    connected=false;
   long long next_retry=0; // GetTickCount64 threshold for reconnect backoff

   for(;;)
     {
      SqlRecord rec;
      bool have=false;
      {
       std::unique_lock<std::mutex> lock(m_mutex);
       m_cv.wait(lock,[&]{ return m_stop || !m_queue.empty(); });
       if(m_stop && m_queue.empty()) break;
       if(!m_queue.empty()){ rec=m_queue.front(); have=true; }
      }
      if(!have) continue;

      //--- (re)connect if needed, with backoff so a dead DB doesn't spin
      if(!connected)
        {
         long long now=(long long)GetTickCount64();
         if(now<next_retry){ Sleep(200); continue; }
         if(henv==SQL_NULL_HENV)
           {
            SQLAllocHandle(SQL_HANDLE_ENV,SQL_NULL_HANDLE,&henv);
            SQLSetEnvAttr(henv,SQL_ATTR_ODBC_VERSION,(SQLPOINTER)SQL_OV_ODBC3,0);
           }
         SQLAllocHandle(SQL_HANDLE_DBC,henv,&hdbc);
         SQLWCHAR outstr[1024]; SQLSMALLINT outlen=0;
         SQLRETURN r=SQLDriverConnectW(hdbc,nullptr,(SQLWCHAR*)m_conn.c_str(),SQL_NTS,
                                       outstr,1024,&outlen,SQL_DRIVER_NOPROMPT);
         if(SQL_SUCCEEDED(r)){ connected=true; MirrorLog(m_api,L"SQL mirror: connected"); }
         else
           {
            MirrorLog(m_api,L"SQL mirror: connect failed, backing off 30s");
            SQLFreeHandle(SQL_HANDLE_DBC,hdbc); hdbc=SQL_NULL_HDBC;
            next_retry=now+30000; // 30s backoff
            continue;
           }
        }

      //--- write the record; pop only on success so nothing is lost on transient errors
      if(WriteOne(hdbc,rec))
        {
         std::lock_guard<std::mutex> lock(m_mutex);
         if(!m_queue.empty()) m_queue.pop_front();
        }
      else
        {
         //--- treat as connection loss: drop the connection and retry later
         MirrorLog(m_api,L"SQL mirror: write failed, will reconnect");
         SQLDisconnect(hdbc);
         SQLFreeHandle(SQL_HANDLE_DBC,hdbc); hdbc=SQL_NULL_HDBC;
         connected=false;
         next_retry=(long long)GetTickCount64()+5000; // 5s before reconnect
        }
     }

   //--- cleanup
   if(hdbc!=SQL_NULL_HDBC){ if(connected) SQLDisconnect(hdbc); SQLFreeHandle(SQL_HANDLE_DBC,hdbc); }
   if(henv!=SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV,henv);
  }
//+------------------------------------------------------------------+
//| Write one record: UPSERT (UPDATE, else INSERT) - portable        |
//+------------------------------------------------------------------+
bool CSqlMirror::WriteOne(void* hdbc_void,const SqlRecord &rec)
  {
   SQLHDBC hdbc=(SQLHDBC)hdbc_void;
   SQLHSTMT hstmt=SQL_NULL_HSTMT;
   if(!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT,hdbc,&hstmt))) return false;

   bool result=false;
   SQLLEN cbGroup=SQL_NTS;
   SQLBIGINT bLogin=(SQLBIGINT)rec.login;
   SQLBIGINT bStart=(SQLBIGINT)rec.start;

   //--- UPDATE existing row first
   std::wstring upd=L"UPDATE "+m_table+L" SET last_login=?, updated_at=GETDATE() WHERE group_name=?";
   //--- some engines lack GETDATE(); fall back handled by caller config/DDL notes.
   if(SQL_SUCCEEDED(SQLPrepareW(hstmt,(SQLWCHAR*)upd.c_str(),SQL_NTS)))
     {
      SQLBindParameter(hstmt,1,SQL_PARAM_INPUT,SQL_C_SBIGINT,SQL_BIGINT,0,0,&bLogin,0,nullptr);
      SQLBindParameter(hstmt,2,SQL_PARAM_INPUT,SQL_C_WCHAR,SQL_WVARCHAR,256,0,
                       (SQLPOINTER)rec.group.c_str(),0,&cbGroup);
      SQLRETURN r=SQLExecute(hstmt);
      if(SQL_SUCCEEDED(r))
        {
         SQLLEN rows=0; SQLRowCount(hstmt,&rows);
         if(rows>0) result=true;          // updated existing
         else
           {
            //--- no row updated -> INSERT a new one
            SQLFreeStmt(hstmt,SQL_CLOSE);
            std::wstring ins=L"INSERT INTO "+m_table+
               L" (group_name,start_login,last_login,updated_at) VALUES (?,?,?,GETDATE())";
            if(SQL_SUCCEEDED(SQLPrepareW(hstmt,(SQLWCHAR*)ins.c_str(),SQL_NTS)))
              {
               SQLBindParameter(hstmt,1,SQL_PARAM_INPUT,SQL_C_WCHAR,SQL_WVARCHAR,256,0,
                                (SQLPOINTER)rec.group.c_str(),0,&cbGroup);
               SQLBindParameter(hstmt,2,SQL_PARAM_INPUT,SQL_C_SBIGINT,SQL_BIGINT,0,0,&bStart,0,nullptr);
               SQLBindParameter(hstmt,3,SQL_PARAM_INPUT,SQL_C_SBIGINT,SQL_BIGINT,0,0,&bLogin,0,nullptr);
               if(SQL_SUCCEEDED(SQLExecute(hstmt))) result=true;
              }
           }
        }
     }

   SQLFreeHandle(SQL_HANDLE_STMT,hstmt);
   return result;
  }
//+------------------------------------------------------------------+
//| Logging helper (IMTServerAPI comes from stdafx.h -> MT5APIServer) |
//+------------------------------------------------------------------+
static void MirrorLog(IMTServerAPI* api,LPCWSTR fmt,...)
  {
   if(!api) return;
   wchar_t buf[512];
   va_list args; va_start(args,fmt);
   _vsnwprintf_s(buf,_countof(buf),_TRUNCATE,fmt,args);
   va_end(args);
   api->LoggerOut(MTLogOK,L"%s",buf);
  }
//+------------------------------------------------------------------+

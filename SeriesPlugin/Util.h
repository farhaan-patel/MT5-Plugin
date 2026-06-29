//+------------------------------------------------------------------+
//|                                          MT5 Group Series Plugin  |
//|  Util.h - string conversion + atomic file helpers                |
//+------------------------------------------------------------------+
#pragma once
#include <windows.h>
#include <string>

namespace util
  {
//--- UTF-16 (MT5 native LPCWSTR) -> UTF-8
   inline std::string WideToUtf8(const wchar_t *w)
     {
      if(!w || !*w) return std::string();
      int len=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);
      if(len<=0) return std::string();
      std::string s((size_t)(len-1),'\0');
      WideCharToMultiByte(CP_UTF8,0,w,-1,&s[0],len,nullptr,nullptr);
      return s;
     }
//--- UTF-8 -> UTF-16
   inline std::wstring Utf8ToWide(const std::string &s)
     {
      if(s.empty()) return std::wstring();
      int len=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0);
      if(len<=0) return std::wstring();
      std::wstring w((size_t)len,L'\0');
      MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),&w[0],len);
      return w;
     }
//--- case-insensitive wide compare (group names are case-insensitive in MT5)
   inline bool IEqualsW(const std::wstring &a,const std::wstring &b)
     {
      if(a.size()!=b.size()) return false;
      return _wcsnicmp(a.c_str(),b.c_str(),a.size())==0;
     }
//--- does 'text' start with 'prefix' (case-insensitive)?
   inline bool IStartsWithW(const std::wstring &text,const std::wstring &prefix)
     {
      if(prefix.size()>text.size()) return false;
      return _wcsnicmp(text.c_str(),prefix.c_str(),prefix.size())==0;
     }
//--- directory containing the given module (plugin DLL)
   inline std::wstring ModuleDir(HMODULE hModule)
     {
      wchar_t path[MAX_PATH]={0};
      DWORD n=GetModuleFileNameW(hModule,path,MAX_PATH);
      std::wstring p(path,n);
      size_t slash=p.find_last_of(L"\\/");
      if(slash!=std::wstring::npos) p.erase(slash);
      return p;
     }
//--- read a whole file into a UTF-8 string (strips a UTF-8 BOM if present)
   inline bool ReadFile(const std::wstring &path,std::string &out)
     {
      HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
      if(h==INVALID_HANDLE_VALUE) return false;
      LARGE_INTEGER size; size.QuadPart=0;
      GetFileSizeEx(h,&size);
      out.clear();
      if(size.QuadPart>0)
        {
         out.resize((size_t)size.QuadPart);
         DWORD read=0;
         BOOL ok=::ReadFile(h,&out[0],(DWORD)size.QuadPart,&read,nullptr);
         CloseHandle(h);
         if(!ok) return false;
         out.resize(read);
        }
      else CloseHandle(h);
      //--- strip UTF-8 BOM
      if(out.size()>=3 && (unsigned char)out[0]==0xEF && (unsigned char)out[1]==0xBB && (unsigned char)out[2]==0xBF)
         out.erase(0,3);
      return true;
     }
//--- atomic write: write to <path>.tmp then ReplaceFile/MoveFileEx over target
   inline bool WriteFileAtomic(const std::wstring &path,const std::string &data)
     {
      std::wstring tmp=path+L".tmp";
      HANDLE h=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
      if(h==INVALID_HANDLE_VALUE) return false;
      DWORD written=0;
      BOOL ok=TRUE;
      if(!data.empty()) ok=::WriteFile(h,data.data(),(DWORD)data.size(),&written,nullptr);
      FlushFileBuffers(h);
      CloseHandle(h);
      if(!ok || written!=(DWORD)data.size()){ DeleteFileW(tmp.c_str()); return false; }
      //--- MoveFileEx with REPLACE_EXISTING is atomic on NTFS
      if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        { DeleteFileW(tmp.c_str()); return false; }
      return true;
     }
  } // namespace util
//+------------------------------------------------------------------+

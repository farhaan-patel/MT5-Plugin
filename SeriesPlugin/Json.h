//+------------------------------------------------------------------+
//|                                          MT5 Group Series Plugin  |
//|  Json.h - minimal, dependency-free JSON parser/writer (header)    |
//|                                                                  |
//|  Supports objects, arrays, strings (with \uXXXX + escapes),       |
//|  numbers, true/false/null. UTF-8 in/out. Tolerates // and /* */   |
//|  comments and trailing commas so the config file is human-edit    |
//|  friendly. Never throws on malformed input - Parse() returns a    |
//|  null value and reports ok=false instead.                         |
//+------------------------------------------------------------------+
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstdlib>

namespace js
  {
//--- JSON value kinds
   enum class Type { Null, Bool, Number, String, Array, Object };
//+------------------------------------------------------------------+
//| A single JSON value (recursive)                                  |
//+------------------------------------------------------------------+
   class Value
     {
   public:
      Type                          type = Type::Null;
      bool                          b    = false;
      double                        num  = 0.0;
      std::string                   str;                 // UTF-8
      std::vector<Value>            arr;
      std::vector<std::pair<std::string,Value>> obj;     // preserves order

      Value() = default;

      bool   IsObject() const { return type==Type::Object; }
      bool   IsArray()  const { return type==Type::Array;  }
      bool   IsString() const { return type==Type::String; }
      bool   IsNumber() const { return type==Type::Number; }
      bool   IsBool()   const { return type==Type::Bool;   }
      bool   IsNull()   const { return type==Type::Null;   }

      //--- object lookup (returns nullptr when absent / not an object)
      const Value* Find(const std::string &key) const
        {
         if(type!=Type::Object) return nullptr;
         for(const auto &kv : obj) if(kv.first==key) return &kv.second;
         return nullptr;
        }
      //--- typed getters with defaults
      bool        AsBool(bool def=false) const { return type==Type::Bool ? b : def; }
      std::string AsString(const std::string &def=std::string()) const { return type==Type::String ? str : def; }
      double      AsDouble(double def=0.0) const { return type==Type::Number ? num : def; }
      int64_t     AsInt64(int64_t def=0) const { return type==Type::Number ? (int64_t)num : def; }
      uint64_t    AsUInt64(uint64_t def=0) const { return type==Type::Number ? (uint64_t)num : def; }

      //--- convenience for building output
      static Value Object(){ Value v; v.type=Type::Object; return v; }
      static Value Array() { Value v; v.type=Type::Array;  return v; }
      static Value Str(const std::string &s){ Value v; v.type=Type::String; v.str=s; return v; }
      static Value Num(double d){ Value v; v.type=Type::Number; v.num=d; return v; }
      static Value Boolean(bool x){ Value v; v.type=Type::Bool; v.b=x; return v; }
      void Set(const std::string &key,const Value &val){ obj.emplace_back(key,val); }
     };
//+------------------------------------------------------------------+
//| Parser                                                           |
//+------------------------------------------------------------------+
   class Parser
     {
   private:
      const char* p;
      const char* end;
      bool        m_ok;

      void SkipWs()
        {
         for(;;)
           {
            while(p<end && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n')) p++;
            //--- line comment
            if(p+1<end && p[0]=='/' && p[1]=='/')
              { p+=2; while(p<end && *p!='\n') p++; continue; }
            //--- block comment
            if(p+1<end && p[0]=='/' && p[1]=='*')
              { p+=2; while(p+1<end && !(p[0]=='*'&&p[1]=='/')) p++; if(p+1<end) p+=2; continue; }
            break;
           }
        }

      static void Utf8Append(std::string &out,unsigned cp)
        {
         if(cp<=0x7F) out.push_back((char)cp);
         else if(cp<=0x7FF){ out.push_back((char)(0xC0|(cp>>6))); out.push_back((char)(0x80|(cp&0x3F))); }
         else if(cp<=0xFFFF){ out.push_back((char)(0xE0|(cp>>12))); out.push_back((char)(0x80|((cp>>6)&0x3F))); out.push_back((char)(0x80|(cp&0x3F))); }
         else { out.push_back((char)(0xF0|(cp>>18))); out.push_back((char)(0x80|((cp>>12)&0x3F))); out.push_back((char)(0x80|((cp>>6)&0x3F))); out.push_back((char)(0x80|(cp&0x3F))); }
        }

      unsigned ParseHex4()
        {
         unsigned v=0;
         for(int i=0;i<4 && p<end;i++)
           {
            char c=*p++; v<<=4;
            if(c>='0'&&c<='9') v|=(c-'0');
            else if(c>='a'&&c<='f') v|=(c-'a'+10);
            else if(c>='A'&&c<='F') v|=(c-'A'+10);
            else { m_ok=false; }
           }
         return v;
        }

      std::string ParseString()
        {
         std::string out;
         if(p>=end || *p!='"'){ m_ok=false; return out; }
         p++; // opening quote
         while(p<end)
           {
            char c=*p++;
            if(c=='"') return out;
            if(c=='\\')
              {
               if(p>=end){ m_ok=false; return out; }
               char e=*p++;
               switch(e)
                 {
                  case '"':  out.push_back('"');  break;
                  case '\\': out.push_back('\\'); break;
                  case '/':  out.push_back('/');  break;
                  case 'b':  out.push_back('\b'); break;
                  case 'f':  out.push_back('\f'); break;
                  case 'n':  out.push_back('\n'); break;
                  case 'r':  out.push_back('\r'); break;
                  case 't':  out.push_back('\t'); break;
                  case 'u':
                    {
                     unsigned cp=ParseHex4();
                     //--- surrogate pair
                     if(cp>=0xD800 && cp<=0xDBFF && p+1<end && p[0]=='\\' && p[1]=='u')
                       { p+=2; unsigned lo=ParseHex4(); cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00); }
                     Utf8Append(out,cp);
                     break;
                    }
                  default: m_ok=false; out.push_back(e); break;
                 }
              }
            else out.push_back(c);
           }
         m_ok=false; // unterminated
         return out;
        }

      Value ParseNumber()
        {
         const char* start=p;
         if(p<end && (*p=='-'||*p=='+')) p++;
         while(p<end && ((*p>='0'&&*p<='9')||*p=='.'||*p=='e'||*p=='E'||*p=='+'||*p=='-')) p++;
         std::string tok(start,(size_t)(p-start));
         Value v; v.type=Type::Number; v.num=strtod(tok.c_str(),nullptr);
         return v;
        }

      Value ParseValue()
        {
         SkipWs();
         if(p>=end){ m_ok=false; return Value(); }
         char c=*p;
         if(c=='"'){ Value v; v.type=Type::String; v.str=ParseString(); return v; }
         if(c=='{') return ParseObject();
         if(c=='[') return ParseArray();
         if(c=='t'){ if(end-p>=4 && strncmp(p,"true",4)==0){ p+=4; return Value::Boolean(true); } m_ok=false; return Value(); }
         if(c=='f'){ if(end-p>=5 && strncmp(p,"false",5)==0){ p+=5; return Value::Boolean(false); } m_ok=false; return Value(); }
         if(c=='n'){ if(end-p>=4 && strncmp(p,"null",4)==0){ p+=4; return Value(); } m_ok=false; return Value(); }
         if(c=='-'||c=='+'||(c>='0'&&c<='9')) return ParseNumber();
         m_ok=false; return Value();
        }

      Value ParseArray()
        {
         Value v; v.type=Type::Array; p++; // '['
         SkipWs();
         if(p<end && *p==']'){ p++; return v; }
         for(;;)
           {
            v.arr.push_back(ParseValue());
            SkipWs();
            if(p<end && *p==','){ p++; SkipWs(); if(p<end && *p==']'){ p++; break; } continue; } // trailing comma ok
            if(p<end && *p==']'){ p++; break; }
            m_ok=false; break;
           }
         return v;
        }

      Value ParseObject()
        {
         Value v; v.type=Type::Object; p++; // '{'
         SkipWs();
         if(p<end && *p=='}'){ p++; return v; }
         for(;;)
           {
            SkipWs();
            if(p>=end || *p!='"'){ m_ok=false; break; }
            std::string key=ParseString();
            SkipWs();
            if(p>=end || *p!=':'){ m_ok=false; break; }
            p++; // ':'
            Value val=ParseValue();
            v.obj.emplace_back(key,val);
            SkipWs();
            if(p<end && *p==','){ p++; SkipWs(); if(p<end && *p=='}'){ p++; return v; } continue; } // trailing comma ok
            if(p<end && *p=='}'){ p++; return v; }
            m_ok=false; break;
           }
         return v;
        }

   public:
      //--- parse a UTF-8 buffer. ok=false on any malformed input.
      Value Parse(const std::string &text,bool &ok)
        {
         p=text.c_str(); end=p+text.size(); m_ok=true;
         Value v=ParseValue();
         SkipWs();
         ok=m_ok;
         return v;
        }
     };
//+------------------------------------------------------------------+
//| Writer (compact-ish, 2-space indented)                           |
//+------------------------------------------------------------------+
   inline void WriteEscaped(std::string &out,const std::string &s)
     {
      out.push_back('"');
      for(unsigned char c : s)
        {
         switch(c)
           {
            case '"':  out+="\\\""; break;
            case '\\': out+="\\\\"; break;
            case '\b': out+="\\b";  break;
            case '\f': out+="\\f";  break;
            case '\n': out+="\\n";  break;
            case '\r': out+="\\r";  break;
            case '\t': out+="\\t";  break;
            default:
               if(c<0x20){ char buf[8]; sprintf_s(buf,sizeof(buf),"\\u%04x",c); out+=buf; }
               else out.push_back((char)c);
           }
        }
      out.push_back('"');
     }

   inline void WriteNumber(std::string &out,double d)
     {
      //--- integers printed without trailing ".0"
      if(d==(double)(int64_t)d && d<9.2e18 && d>-9.2e18)
        { char buf[32]; sprintf_s(buf,sizeof(buf),"%lld",(long long)(int64_t)d); out+=buf; }
      else
        { char buf[32]; sprintf_s(buf,sizeof(buf),"%.10g",d); out+=buf; }
     }

   inline void Write(std::string &out,const Value &v,int indent=0)
     {
      auto pad=[&](int n){ for(int i=0;i<n;i++) out+="  "; };
      switch(v.type)
        {
         case Type::Null:   out+="null"; break;
         case Type::Bool:   out+= v.b?"true":"false"; break;
         case Type::Number: WriteNumber(out,v.num); break;
         case Type::String: WriteEscaped(out,v.str); break;
         case Type::Array:
            if(v.arr.empty()){ out+="[]"; break; }
            out+="[\n";
            for(size_t i=0;i<v.arr.size();i++)
              { pad(indent+1); Write(out,v.arr[i],indent+1); if(i+1<v.arr.size()) out+=','; out+='\n'; }
            pad(indent); out+="]";
            break;
         case Type::Object:
            if(v.obj.empty()){ out+="{}"; break; }
            out+="{\n";
            for(size_t i=0;i<v.obj.size();i++)
              { pad(indent+1); WriteEscaped(out,v.obj[i].first); out+=": "; Write(out,v.obj[i].second,indent+1); if(i+1<v.obj.size()) out+=','; out+='\n'; }
            pad(indent); out+="}";
            break;
        }
     }

   inline std::string Dump(const Value &v){ std::string s; Write(s,v,0); s.push_back('\n'); return s; }
  } // namespace js
//+------------------------------------------------------------------+

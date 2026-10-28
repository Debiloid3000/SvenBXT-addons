#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>

// Отладка загрузки: журнал в %TEMP%\\SvenBXT_addons_boot.log.
// Только после вызова Init/LoadEngine; НИКАКОГО дискового I/O в DllMain.
namespace addon_log {
inline void write(const char* plugin, const char* stage) {
    if (!plugin || !stage) return;
    char dir[MAX_PATH]={}, path[MAX_PATH]={}, line[512]={};
    DWORD len=GetTempPathA(MAX_PATH,dir);
    if(!len || len>=MAX_PATH) return;
    const char* file="SvenBXT_addons_boot.log";
    if(strlen(dir)+strlen(file)+1>=MAX_PATH) return;
    std::snprintf(path,sizeof(path),"%s%s",dir,file);
    std::snprintf(line,sizeof(line),"[%lu] [%s] %s\r\n",
        static_cast<unsigned long>(GetTickCount()),plugin,stage);
    OutputDebugStringA(line);
    HANDLE h=CreateFileA(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
                         nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return;
    DWORD ignored=0;
    WriteFile(h,line,static_cast<DWORD>(strlen(line)),&ignored,nullptr);
    CloseHandle(h);
}
}

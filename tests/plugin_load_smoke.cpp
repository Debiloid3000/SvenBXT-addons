#include <metahook.h>
#include <IPlugins.h>
#include <cvardef.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

// x86 loader smoke test: checks imports, CreateInterface(V4),
// and registration without a real game engine or patching callbacks.
namespace {
std::map<std::string,cvar_t> vars;
std::map<std::string,void(*)()> commands;
cvar_t* reg(const char* name,const char* def,int flags) {
    auto& c=vars[name?name:""];
    c.flags=flags;
    c.value=def?static_cast<float>(std::atof(def)):0.0f;
    return &c;
}
int cmd(const char* name,void(*fn)()) {
    if(name&&fn) commands[name]=fn;
    return 1;
}
}
using Factory=void* (__cdecl*)(const char*,int*);
int main(int argc,char** argv) {
    if(argc<2) return 10;
    for(int i=1;i<argc;++i) {
        HMODULE module=LoadLibraryA(argv[i]);
        if(!module) {
            std::fprintf(stderr,"LoadLibrary failed for %s: Win32 %lu\n",
                    argv[i],static_cast<unsigned long>(GetLastError()));
            return 11;
        }
        auto factory=reinterpret_cast<Factory>(GetProcAddress(module,"CreateInterface"));
        if(!factory) {std::fprintf(stderr,"CreateInterface missing: %s\n",argv[i]);return 12;}
        int result=-1;
        auto* p=static_cast<IPluginsV4*>(factory(METAHOOK_PLUGIN_API_VERSION_V4,&result));
        if(!p) {std::fprintf(stderr,"V4 interface missing: %s\n",argv[i]);return 13;}
        vars.clear();
        commands.clear();
        cl_enginefunc_t engine={};
        engine.pfnRegisterVariable=&reg;
        engine.pfnAddCommand=&cmd;
        p->Init(nullptr,nullptr,nullptr);
        p->LoadEngine(&engine);
        // MetaHook does not provide one universally guarded export-table
        // definition. Use opaque aligned backing memory and verify that a
        // PASSIVE LoadClient leaves it byte-for-byte unchanged.
        alignas(void*) unsigned char fakeExports[2048]={};
        unsigned char snapshot[sizeof(fakeExports)]={};
        std::memcpy(snapshot,fakeExports,sizeof(fakeExports));
        p->LoadClient(reinterpret_cast<cl_exportfuncs_t*>(fakeExports));
        if(std::memcmp(snapshot,fakeExports,sizeof(fakeExports))!=0) {
            std::fprintf(stderr,"Plugin modified client callbacks during startup: %s\n",argv[i]);
            return 15;
        }
        const bool isHud=std::strstr(argv[i],"HudEdit")!=nullptr;
        const char* command=isHud?"hudedit_start":"sbxtma_start";
        const char* cvar=isHud?"hudedit_inventory_x":"sbxtma_safe_mode";
        if(!commands.count(command)||!vars.count(cvar)) {
            std::fprintf(stderr,"Missing CVar or command after LoadEngine: %s\n",argv[i]);
            return 14;
        }
        p->Shutdown();
        std::printf("LoadLibrary/CreateInterface/LoadEngine pass: %s (%u cvars, %u commands)\n",
             argv[i],static_cast<unsigned>(vars.size()),static_cast<unsigned>(commands.size()));
        FreeLibrary(module);
    }
    return 0;
}

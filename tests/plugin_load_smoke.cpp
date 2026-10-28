#include <metahook.h>
#include <IPlugins.h>
#include <cvardef.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// x86 loader smoke test: checks imports, CreateInterface(V4),
// and registration without a real game engine or patching callbacks.
namespace {
std::map<std::string,cvar_t> vars;
std::map<std::string,void(*)()> commands;
std::vector<const char*> namePointers;
cvar_t* reg(const char* name,const char* def,int flags) {
    namePointers.push_back(name);
    auto& c=vars[name?name:""];
    c.flags=flags;
    c.value=def?static_cast<float>(std::atof(def)):0.0f;
    return &c;
}
int cmd(const char* name,void(*fn)()) {
    if(name&&fn) commands[name]=fn;
    return 1;
}
cvar_t* lookup(const char* name) {
    auto it=vars.find(name?name:"");
    return it==vars.end()?nullptr:&it->second;
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
        namePointers.clear();
        cl_enginefunc_t engine={};
        engine.pfnRegisterVariable=&reg;
        engine.pfnAddCommand=&cmd;
        engine.pfnGetCvarPointer=&lookup;
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
        if(isHud) {
            if(!commands.count("hudedit_init") || !commands.count("hudedit_status") ||
               !commands.count("hudedit_help") || !vars.empty()) {
                std::fprintf(stderr,"HUD Edit must expose diagnostics without registering CVars during startup: %s\n",argv[i]);
                return 14;
            }
            commands.at("hudedit_status")();
            commands.at("hudedit_init")();
            commands.at("hudedit_status")();
            if(!commands.count("hudedit_start") || !vars.count("hudedit_inventory_x") ||
               !vars.count("hudedit_inventory_visible") ||
               !vars.count("hudedit_flashlight_y") ||
               !lookup("hudedit_crosshair_size")) {
                std::fprintf(stderr,"HUD Edit lazy init did not expose settings to engine CVar lookup: %s\n",argv[i]);
                return 16;
            }
            // Ensure the engine can retain names in the original pointer form.
            bool sawInventoryX=false,sawFlashlightY=false;
            for(const char* ptr:namePointers) {
                if(!ptr) continue;
                if(std::strcmp(ptr,"hudedit_inventory_x")==0) sawInventoryX=true;
                if(std::strcmp(ptr,"hudedit_flashlight_y")==0) sawFlashlightY=true;
            }
            if(!sawInventoryX||!sawFlashlightY) {
                std::fprintf(stderr,"CVar names became dangling pointers after registration\n");
                return 17;
            }
        } else {
            if (!commands.count("sbxtma_init") || !commands.count("sbxtma_status") ||
                !vars.empty() || commands.count("sbxtma_start")) {
                std::fprintf(stderr,"Movement performed unsafe early registration or lacked init: %s\n",argv[i]);
                return 18;
            }
            commands.at("sbxtma_status")();
            commands.at("sbxtma_init")();
            if (!commands.count("sbxtma_start") || !lookup("sbxtma_safe_mode")) {
                std::fprintf(stderr,"Movement deferred initialization failed: %s\n",argv[i]);
                return 20;
            }
        }
        p->Shutdown();
        std::printf("LoadLibrary/CreateInterface/LoadEngine pass: %s (%u cvars, %u commands)\n",
             argv[i],static_cast<unsigned>(vars.size()),static_cast<unsigned>(commands.size()));
        FreeLibrary(module);
    }
    return 0;
}

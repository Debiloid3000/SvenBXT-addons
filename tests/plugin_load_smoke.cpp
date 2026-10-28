#include <windows.h>
#include <cstdio>

// Real x86 executable smoke test: loads each DLL outside the game.
// Verifies that the dynamic loader can resolve all imports and that the
// MetaHook V4 interface factory exists. Does NOT claim in-game compatibility.
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
        if(!factory) {
            std::fprintf(stderr,"CreateInterface missing in %s\n",argv[i]);
            return 12;
        }
        int result=-1;
        void* iface=factory("METAHOOK_PLUGIN_API_VERSION004",&result);
        if(!iface) {
            std::fprintf(stderr,"IPluginsV4 interface missing in %s\n",argv[i]);
            return 13;
        }
        std::printf("LoadLibrary/CreateInterface OK: %s\n",argv[i]);
        FreeLibrary(module);
    }
    return 0;
}

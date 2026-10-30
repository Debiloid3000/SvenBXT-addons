#include <metahook.h>
#include <IPlugins.h>
#include <cvardef.h>
#include <pm_defs.h>
#include <pm_movevars.h>
#include <const.h>
#include <usercmd.h>
#include <ref_params.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

// A simulated CLIENT lifecycle, not just LoadLibrary. Exercise both DLLs in
// both chain orders and pass a different engine table through Initialize.
namespace {
void require(bool ok,const char* message) {
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
struct Variable { cvar_t c={};std::string text; };
std::map<std::string,Variable> vars;
std::map<std::string,void(*)()> commands;
bool inHudInit=false, initialized=false;
int initCalls=0,hudCalls=0,moveCalls=0,cmdCalls=0,redrawCalls=0,frameCalls=0,shutdownCalls=0;
int drawCalls=0,traceCalls=0;
float clientTime=2;
std::vector<std::string> drawText;
cvar_t* lookup(const char* name) {
    auto it=vars.find(name?name:"");return it==vars.end()?nullptr:&it->second.c;
}
cvar_t* addVariable(const char* name,const char* def,int flags) {
    require(!lookup(name),"duplicate CVar registration");
    auto& v=vars[name];v.text=def;
    v.c.name=const_cast<char*>(name);v.c.string=v.text.data();
    v.c.value=static_cast<float>(std::atof(def));v.c.flags=flags;
    return &v.c;
}
cvar_t* reg(const char* name,const char* def,int flags) {
    require(initialized && inHudInit,"registration happened before original HUD_Init");
    return addVariable(name,def,flags);
}
int addCommand(const char* name,void(*fn)()) {
    require(initialized && inHudInit,"command registration happened before HUD_Init");
    require(!commands.count(name),"duplicate command registration");
    commands[name]=fn;return 1;
}
void setValue(const char* name,float value) {
    auto* c=lookup(name);require(c!=nullptr,"setting missing CVar");
    auto& v=vars.at(name);char text[64];std::snprintf(text,sizeof(text),"%.6g",value);
    v.text=text;c->string=v.text.data();c->value=value;
}
float getTime() { return clientTime; }
int engineType() { return ENGINE_SVENGINE; }
void textColor(float,float,float) {}
void fill(int,int,int,int,int,int,int,int) { ++drawCalls; }
int console(int x,int,char* text) { ++drawCalls;drawText.emplace_back(text);return x; }
int screen(SCREENINFO* info) { info->iWidth=1280;info->iHeight=720;return 1; }
int clientInitialize(cl_enginefunc_t*,int) { initialized=true;++initCalls;return 1; }
void hudInit() { require(initialized,"HUD_Init before Initialize");++hudCalls; }
int vidInit() { return 1; }
void playerMove(playermove_t*,int) { ++moveCalls; }
void createMove(float,usercmd_t*,int) { ++cmdCalls; }
int redraw(float,int) { ++redrawCalls;return 7; }
void frame(double) { ++frameCalls; }
void view(ref_params_t*) {}
void shutdown() { ++shutdownCalls; }
pmtrace_t trace(float*,float* end,int,int) {
    ++traceCalls;pmtrace_t result={};result.fraction=1;
    std::memcpy(result.endpos,end,sizeof(result.endpos));return result;
}
cl_exportfuncs_t clientExports() {
    cl_exportfuncs_t e={};e.Initialize=&clientInitialize;e.HUD_Init=&hudInit;
    e.HUD_VidInit=&vidInit;e.HUD_PlayerMove=&playerMove;e.CL_CreateMove=&createMove;
    e.HUD_Redraw=&redraw;e.HUD_Frame=&frame;e.V_CalcRefdef=&view;e.HUD_Shutdown=&shutdown;
    return e;
}
cl_enginefunc_t liveEngine() {
    cl_enginefunc_t e={};e.pfnRegisterVariable=&reg;e.pfnAddCommand=&addCommand;
    e.pfnGetCvarPointer=&lookup;e.Cvar_SetValue=&setValue;e.GetClientTime=&getTime;
    e.pfnFillRGBA=&fill;e.pfnDrawConsoleString=&console;
    e.pfnGetScreenInfo=&screen;e.pfnDrawSetTextColor=&textColor;
    return e;
}
struct Plugin { HMODULE module;IPluginsV4* api; };
Plugin load(const char* path) {
    HMODULE module=LoadLibraryA(path);
    if(!module) std::fprintf(stderr,"Win32 loader error %lu: %s\n",GetLastError(),path);
    require(module!=nullptr,"LoadLibrary");
    auto factory=reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface"));
    require(factory!=nullptr,"CreateInterface export");
    auto* api=static_cast<IPluginsV4*>(factory(METAHOOK_PLUGIN_API_VERSION_V4,nullptr));
    require(api!=nullptr,"MetaHook V4 interface");
    require(!std::strcmp(api->GetVersion(),"2.3.0"),"wrong addon version");
    return {module,api};
}
void command(const char* name) { require(commands.count(name)!=0,name);commands.at(name)(); }
void run(const char* movementPath,const char* hudPath,bool reverse) {
    vars.clear();commands.clear();drawText.clear();drawCalls=traceCalls=0;
    initialized=false;inHudInit=false;
    initCalls=hudCalls=moveCalls=cmdCalls=redrawCalls=frameCalls=shutdownCalls=0;
    Plugin movement=load(movementPath),hud=load(hudPath);
    std::vector<Plugin> plugins=reverse?std::vector<Plugin>{hud,movement}:std::vector<Plugin>{movement,hud};
    metahook_api_t host={};host.GetEngineType=&engineType;
    cl_enginefunc_t bootstrap={}; // Intentionally unusable until Initialize.
    auto e=clientExports();
    for(auto p:plugins) { p.api->Init(&host,nullptr,nullptr);p.api->LoadEngine(&bootstrap);p.api->LoadClient(&e); }
    require(vars.empty() && commands.empty(),"engine facilities used during LoadEngine/LoadClient");
    auto engine=liveEngine();
    require(e.Initialize(&engine,7)==1,"Initialize result not chained");
    require(initCalls==1 && vars.empty(),"registration before HUD_Init");
    addVariable("fps_max","165",0);addVariable("crosshair","1",0);
    addVariable("sbxt_hud_speedometer","1",0);addVariable("hud_draw","1",0);
    inHudInit=true;e.HUD_Init();inHudInit=false;
    require(hudCalls==1,"original HUD_Init not called exactly once");
    for(const char* name:{"sbxtma_mode","sbxtma_allow_online","sbxtma_prediction","hudedit_inventory_x",
        "hudedit_inventory_region_x","hudedit_flashlight_r","hudedit_crosshair_size"})
        require(lookup(name)!=nullptr,name);
    require(lookup("sbxtma_mode")->value==2,"default movement mode is not Assist");
    require(lookup("sbxtma_allow_online")->value==1,"Sven multiplayer disabled by default");
    require(lookup("hudedit_speed")->value==0,"HUD Edit duplicated existing speedometer");
    const size_t registrations=vars.size(),commandCount=commands.size();
    command("hudedit_init");command("sbxtma_init");command("hudedit_status");command("sbxtma_status");
    require(vars.size()==registrations && commands.size()==commandCount,"init aliases not idempotent");
    for(auto& [name,v]:vars) require(!std::strcmp(name.c_str(),v.c.name),"CVar name pointer expired");
    require(e.HUD_Redraw(2,0)==7 && redrawCalls==1,"redraw chain broken");
    require(drawCalls==0,"movement rendered unsolicited speedometer/header");
    auto pm=std::make_unique<playermove_10152_t>();std::memset(pm.get(),0,sizeof(*pm));
    movevars_t mv={};mv.gravity=800;mv.airaccelerate=10;
    pm->movevars=&mv;pm->PM_PlayerTrace=&trace;pm->runfuncs=1;pm->multiplayer=1;
    pm->movetype=MOVETYPE_WALK;pm->onground=-1;pm->frametime=.01f;pm->maxspeed=320;
    pm->velocity[0]=320;pm->velocity[2]=-10;
    pm->cmd.forwardmove=123;pm->cmd.sidemove=45;
    const usercmd_t savedPredictionCmd=pm->cmd;
    e.HUD_PlayerMove(reinterpret_cast<playermove_t*>(pm.get()),0);
    require(moveCalls==1 && traceCalls>=2,"prediction inactive after automatic startup");
    require(!std::memcmp(&savedPredictionCmd,&pm->cmd,sizeof(usercmd_t)),"addon mutated prediction-only command");
    usercmd_t cmd={};cmd.msec=10;cmd.forwardmove=320;cmd.sidemove=100;
    e.CL_CreateMove(.01f,&cmd,1);
    require(cmdCalls==1 && std::fabs(cmd.forwardmove)<.01f && cmd.sidemove>319,
        "assist did not edit the outgoing command, or GoldSrc side sign is wrong");
    require(cmd.viewangles[0]==0 && cmd.viewangles[1]==0 && cmd.viewangles[2]==0,"assist turned the camera");
    setValue("sbxtma_mode",1);cmd.forwardmove=120;cmd.sidemove=80;
    e.CL_CreateMove(.01f,&cmd,1);require(cmd.forwardmove==120 && cmd.sidemove==80,"info mode edited input");
    setValue("sbxtma_mode",2);setValue("sbxtma_allow_online",0);
    e.CL_CreateMove(.01f,&cmd,1);require(cmd.forwardmove==120 && cmd.sidemove==80,"online opt-out ignored");
    setValue("sbxtma_allow_online",1);
    pm->spectator=1;e.HUD_PlayerMove(reinterpret_cast<playermove_t*>(pm.get()),0);
    e.CL_CreateMove(.01f,&cmd,1);require(cmd.forwardmove==120 && cmd.sidemove==80,"spectator input changed");
    pm->spectator=0;setValue("sbxtma_objectboost",1);pm->numphysent=2;pm->numtouch=1;
    pm->touchindex[0].ent=1;pm->physents[1].movetype=MOVETYPE_PUSHSTEP;
    e.HUD_PlayerMove(reinterpret_cast<playermove_t*>(pm.get()),0);e.CL_CreateMove(.01f,&cmd,1);
    require(lookup("fps_max")->value==4,"pushable detection did not enable boost");
    setValue("sbxtma_mode",0);e.HUD_Frame(2);
    require(lookup("fps_max")->value==165,"mode off did not restore exact FPS limit");
    setValue("sbxtma_mode",2);command("sbxtma_stop");
    cmd.forwardmove=100;cmd.sidemove=80;e.CL_CreateMove(.01f,&cmd,1);
    require(cmd.forwardmove==100 && cmd.sidemove==80,"stop did not disable assist");
    command("sbxtma_start");
    setValue("hudedit_crosshair",1);e.HUD_Redraw(2,0);
    require(lookup("crosshair")->value==0 && drawCalls>0,"HUD Edit did not auto-start");
    command("hudedit_stop");require(lookup("crosshair")->value==1,"stop did not restore stock crosshair");
    const int previousRedraws=redrawCalls;e.HUD_Redraw(2,0);
    require(redrawCalls==previousRedraws+1,"stop broke another plugin's callback chain");
    command("hudedit_start");e.HUD_Redraw(2,0);
    require(lookup("crosshair")->value==0,"HUD Edit restart failed");
    e.HUD_Shutdown();require(shutdownCalls==1,"shutdown chain broken");
    require(lookup("crosshair")->value==1 && lookup("fps_max")->value==165,"shutdown leaked global settings");
    // Client reload in one engine session must chain fresh exports without
    // registering the same CVar/command twice or recursing into stale hooks.
    e=clientExports();for(auto p:plugins) p.api->LoadClient(&e);
    e.Initialize(&engine,7);inHudInit=true;e.HUD_Init();inHudInit=false;
    require(vars.size()==registrations && commands.size()==commandCount,"client reload duplicated settings");
    require(hudCalls==2 && initCalls==2,"client reload callback chain failed");
    e.HUD_Shutdown();for(auto p:plugins) {p.api->ExitGame(0);p.api->Shutdown();}
    for(auto p:plugins) FreeLibrary(p.module);
    vars.clear();commands.clear();
    std::printf("PASS: automatic startup, outgoing commands, stop/restart, reload (%s order)\n",reverse?"HUD/Movement":"Movement/HUD");
}
} // namespace
int main(int argc,char** argv) {
    if(argc!=3) {std::fprintf(stderr,"Usage: plugin_load_smoke Movement.dll HudEdit.dll\n");return 2;}
    run(argv[1],argv[2],false);run(argv[1],argv[2],true);return 0;
}

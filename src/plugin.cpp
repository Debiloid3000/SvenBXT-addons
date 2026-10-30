#include <metahook.h>
#include <pm_defs.h>
#include <pm_movevars.h>
#include <cvardef.h>
#include <in_buttons.h>
#include <const.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "movement_assist.hpp"
#include "startup_diagnostics.hpp"

metahook_api_t* g_pMetaHookAPI = nullptr;
mh_interface_t* g_pInterface = nullptr;
mh_enginesave_t* g_pMetaSave = nullptr;
cl_enginefunc_t gEngfuncs = {};

namespace {
using namespace sbxtma;
cl_exportfuncs_t original = {};
bool settingsReady = false, running = true, pixelHeld = false, boostHeld = false;
cvar_t *mode=nullptr, *online=nullptr, *notify=nullptr, *enabledCvar=nullptr;
cvar_t *edgeEnabled=nullptr, *jumpEnabled=nullptr, *strafeEnabled=nullptr;
cvar_t *pixelEnabled=nullptr, *boostEnabled=nullptr, *triggerWidth=nullptr;
cvar_t *predictionEnabled=nullptr;
cvar_t *configVersion=nullptr, *legacySafeMode=nullptr;
MovementState state;
AssistController controller;
float sampledAt = -1, lastFrameTime = -1;
bool fpsLimited = false;
float fpsPrevious = 100;
bool enabled(cvar_t* c) { return c && c->value >= .5f; }
float now() { return gEngfuncs.GetClientTime ? gEngfuncs.GetClientTime() : 0; }
bool active() { return running && settingsReady && enabled(enabledCvar) && mode && mode->value >= .5f; }

class Notifier {
    struct Entry { std::string title, subtitle; float expire=0; };
    std::array<Entry, 4> entries{};
    size_t head=0;
public:
    void clear() { entries={}; head=0; }
    void add(const char* title,const char* subtitle) {
        if (!enabled(notify) || !active()) return;
        for (const auto& e:entries) if(e.title==title && e.expire>now()) return;
        entries[head]={title,subtitle,now()+1.8f};
        head=(head+1)%entries.size();
    }
    void draw() const {
        if (!active() || !enabled(notify) || !gEngfuncs.pfnFillRGBA ||
            !gEngfuncs.pfnDrawConsoleString) return;
        int y=200;
        for (const auto& e:entries) {
            if(e.expire<=now()) continue;
            gEngfuncs.pfnFillRGBA(16,y,269,43,13,18,29,192);
            gEngfuncs.pfnFillRGBA(16,y,3,43,82,198,237,255);
            if(gEngfuncs.pfnDrawSetTextColor) gEngfuncs.pfnDrawSetTextColor(.96f,.97f,1);
            gEngfuncs.pfnDrawConsoleString(28,y+5,const_cast<char*>(e.title.c_str()));
            if(gEngfuncs.pfnDrawSetTextColor) gEngfuncs.pfnDrawSetTextColor(.7f,.82f,.9f);
            gEngfuncs.pfnDrawConsoleString(28,y+23,const_cast<char*>(e.subtitle.c_str()));
            y+=48;
        }
    }
};
Notifier notifications;

void restoreFps() {
    if(fpsLimited && gEngfuncs.Cvar_SetValue) gEngfuncs.Cvar_SetValue("fps_max",fpsPrevious);
    fpsLimited=false;
}
void resetState() {
    restoreFps(); state={}; controller.reset(); sampledAt=-1;
    notifications.clear(); pixelHeld=boostHeld=false;
}
void start() { running=true; }
void stop() { running=false; resetState(); }
void pixelDown() { pixelHeld=true; }
void pixelUp() { pixelHeld=false; }
void boostDown() { boostHeld=true; }
void boostUp() { boostHeld=false; restoreFps(); }
void status() {
    if(gEngfuncs.Con_Printf)
        gEngfuncs.Con_Printf("Movement 2.3.0: %s; mode %.0f; online %s; prediction %s; sample %s\n",
            active()?"running":"off", mode?mode->value:0, enabled(online)?"on":"off",
            enabled(predictionEnabled)?"on":"off",state.valid?"ready":"waiting for player");
}
cvar_t* reg(const char* name,const char* value,int flags=FCVAR_ARCHIVE) {
    if(gEngfuncs.pfnGetCvarPointer)
        if(auto* existing=gEngfuncs.pfnGetCvarPointer(name)) return existing;
    return gEngfuncs.pfnRegisterVariable?gEngfuncs.pfnRegisterVariable(name,value,flags):nullptr;
}
void initializeSettings() {
    if(settingsReady) return;
    enabledCvar=reg("sbxtma_enabled","1");
    mode=reg("sbxtma_mode","2");
    online=reg("sbxtma_allow_online","1");
    notify=reg("sbxtma_notifications","1");
    edgeEnabled=reg("sbxtma_edgebug","1");
    jumpEnabled=reg("sbxtma_jumpbug","1");
    strafeEnabled=reg("sbxtma_strafe","1");
    pixelEnabled=reg("sbxtma_pixelsurf","1");
    boostEnabled=reg("sbxtma_objectboost","0");
    triggerWidth=reg("sbxtma_trigger_width","0");
    predictionEnabled=reg("sbxtma_prediction","1");
    configVersion=reg("sbxtma_config_version","0");
    // Read old archived diagnostic settings for a one-time upgrade. This
    // compatibility CVar is no longer an execution gate for movement.
    legacySafeMode=reg("sbxtma_safe_mode","-1");
    reg("sbxtma_version","2.3.0",0);
    settingsReady=enabledCvar && mode && online && notify && edgeEnabled && jumpEnabled &&
        strafeEnabled && pixelEnabled && boostEnabled && triggerWidth && predictionEnabled &&
        configVersion && legacySafeMode;
    if(gEngfuncs.pfnAddCommand) {
        // Backward-compatible commands. Normal startup never needs these.
        gEngfuncs.pfnAddCommand("sbxtma_init",&initializeSettings);
        gEngfuncs.pfnAddCommand("sbxtma_start",&start);
        gEngfuncs.pfnAddCommand("sbxtma_stop",&stop);
        gEngfuncs.pfnAddCommand("sbxtma_status",&status);
        gEngfuncs.pfnAddCommand("+sbxtma_pixelsurf",&pixelDown);
        gEngfuncs.pfnAddCommand("-sbxtma_pixelsurf",&pixelUp);
        gEngfuncs.pfnAddCommand("+sbxtma_objectboost",&boostDown);
        gEngfuncs.pfnAddCommand("-sbxtma_objectboost",&boostUp);
    }
    addon_log::write("movement",settingsReady?"automatic HUD_Init complete":"HUD_Init registration failed");
}

// Called only after the game's PM_Move, while its trace context is valid.
// Never retain a playermove pointer, call PM_Move twice, or change pm->cmd.
template<class PM> Forecast forecast(const PM& pm) {
    Forecast out;
    if(!enabled(predictionEnabled) || !pm.PM_PlayerTrace || !pm.movevars ||
       pm.frametime<=0 || pm.frametime>.05f) return out;
    out.valid=true;
    Vec3 pos(pm.origin[0],pm.origin[1],pm.origin[2]);
    Vec3 vel(pm.velocity[0],pm.velocity[1],pm.velocity[2]);
    const float dt=pm.frametime;
    const float gravity=pm.movevars->gravity*(pm.gravity>0?pm.gravity:1);
    bool touchedGround=false;
    for(int tick=1;tick<=2;++tick) {
        vel.z-=gravity*dt*.5f;
        const auto dest=pos+vel*dt;
        float start[3]={pos.x,pos.y,pos.z},end[3]={dest.x,dest.y,dest.z};
        const auto hit=pm.PM_PlayerTrace(start,end,PM_NORMAL,-1);
        if(!std::isfinite(hit.fraction) || hit.allsolid || hit.startsolid) return {};
        pos={hit.endpos[0],hit.endpos[1],hit.endpos[2]};
        if(hit.fraction<1) {
            Vec3 normal(hit.plane.normal[0],hit.plane.normal[1],hit.plane.normal[2]);
            if(normal.z>.7f && vel.z<0) {
                if(!out.groundTick) out.groundTick=tick;
                touchedGround=true;
            }
            out.wall |= std::fabs(normal.z)<.2f;
            vel=slide_against_plane(vel,normal);
        } else if(touchedGround) {
            // A predicted landing followed by loss of support at the ledge.
            float below[3]={pos.x,pos.y,pos.z-2};
            float probe[3]={pos.x,pos.y,pos.z};
            const auto support=pm.PM_PlayerTrace(probe,below,PM_NORMAL,-1);
            out.edge=support.fraction==1 && !support.allsolid && !support.startsolid;
        }
        vel.z-=gravity*dt*.5f;
    }
    return out;
}
template<class PM> void sample(const PM& pm) {
    if(pm.server || !pm.runfuncs) return; // Ignore prediction replays.
    state={}; sampledAt=now();
    if(pm.dead || pm.spectator || pm.movetype!=MOVETYPE_WALK || pm.waterlevel>1 ||
       !std::isfinite(pm.velocity[0]) || !std::isfinite(pm.velocity[1]) ||
       !std::isfinite(pm.velocity[2]) || !pm.movevars) { restoreFps(); return; }
    state.valid=state.walking=true;
    state.grounded=pm.onground!=-1;
    state.multiplayer=pm.multiplayer!=0;
    state.velocity={pm.velocity[0],pm.velocity[1],pm.velocity[2]};
    state.maxspeed=pm.clientmaxspeed>0?std::min(pm.maxspeed,pm.clientmaxspeed):pm.maxspeed;
    state.airaccelerate=pm.movevars->airaccelerate;
    if(!state.grounded) state.forecast=forecast(pm);
    if(enabled(boostEnabled) || boostHeld) {
        constexpr int capacity=sizeof(pm.physents)/sizeof(pm.physents[0]);
        const int count=std::max(0,std::min(pm.numtouch,capacity));
        for(int i=0;i<count;++i) {
            const int index=pm.touchindex[i].ent;
            if(index<0 || index>=pm.numphysent || index>=capacity) continue;
            const auto& ent=pm.physents[index];
            if(ent.movetype==MOVETYPE_PUSHSTEP ||
               std::strncmp(ent.name,"func_pushable",sizeof(ent.name))==0) {
                state.pushable=true; break;
            }
        }
    }
}
int onInitialize(cl_enginefunc_t* engine,int version) {
    // LoadEngine's bootstrap table is not the live client engine table.
    if(engine) gEngfuncs=*engine;
    return original.Initialize?original.Initialize(engine,version):1;
}
void onHudInit() {
    if(original.HUD_Init) original.HUD_Init();
    initializeSettings();
}
int onVidInit() { resetState(); return original.HUD_VidInit?original.HUD_VidInit():1; }
void onMove(playermove_t* pm,int server) {
    if(original.HUD_PlayerMove) original.HUD_PlayerMove(pm,server);
    if(!pm || server || !active() || !g_pMetaHookAPI || !g_pMetaHookAPI->GetEngineType) return;
    const int engine=g_pMetaHookAPI->GetEngineType();
    if(engine==ENGINE_SVENGINE) sample(*reinterpret_cast<playermove_10152_t*>(pm));
    else if(engine==ENGINE_GOLDSRC || engine==ENGINE_GOLDSRC_HL25) sample(*pm);
}
void onCreateMove(float dt,usercmd_t* cmd,int inGame) {
    if(original.CL_CreateMove) original.CL_CreateMove(dt,cmd,inGame);
    if(!active() || !inGame || !cmd || !state.valid || sampledAt<0 ||
       now()<sampledAt || now()-sampledAt>.5f) { restoreFps(); controller.reset(); return; }
    Controls input{cmd->buttons,cmd->forwardmove,cmd->sidemove,cmd->viewangles[1]};
    AssistSettings cfg;
    cfg.mode=static_cast<int>(mode->value);cfg.online=enabled(online);
    cfg.edge=enabled(edgeEnabled);cfg.jump=enabled(jumpEnabled);
    cfg.strafe=enabled(strafeEnabled);cfg.pixel=enabled(pixelEnabled)||pixelHeld;
    const float step=cmd->msec?cmd->msec*.001f:dt;
    const int events=controller.apply(state,input,cfg,clampf(step,.001f,.1f));
    // This is the outgoing usercmd. Both prediction AND the server see it.
    cmd->buttons=input.buttons;cmd->forwardmove=input.forward;cmd->sidemove=input.side;
    if(events&Edge) notifications.add("EDGEBUG","Predicted ledge contact");
    if(events&Jumpbug) notifications.add("JUMPBUG","Predicted landing window");
    if(events&Pixelsurf) notifications.add("PIXELSURF","Predicted wall micro-contact");
    if(triggerWidth && triggerWidth->value>0) {
        const float fps=trigger_skip_fps(state.velocity.speed2d(),triggerWidth->value);
        if(fps>=4) {
            char message[64];std::snprintf(message,sizeof(message),"~%.0f FPS (estimate)",fps);
            notifications.add("TRIGGER SKIP",message);
        }
    }
    const bool boost=cfg.mode>=2 && (!state.multiplayer || cfg.online) &&
        state.pushable && (enabled(boostEnabled)||boostHeld);
    if(boost && !fpsLimited && gEngfuncs.pfnGetCvarPointer && gEngfuncs.Cvar_SetValue) {
        if(auto* fps=gEngfuncs.pfnGetCvarPointer("fps_max")) {
            fpsPrevious=fps->value;gEngfuncs.Cvar_SetValue("fps_max",4);fpsLimited=true;
            notifications.add("OBJECT BOOST","Pushable contact / 4 FPS");
        }
    } else if(!boost) restoreFps();
}
int onRedraw(float t,int intermission) {
    const int result=original.HUD_Redraw?original.HUD_Redraw(t,intermission):1;
    if(!intermission) notifications.draw();
    return result;
}
void onFrame(double t) {
    if(original.HUD_Frame) original.HUD_Frame(t);
    // The first frame runs after startup configs. Upgrade only the old
    // diagnostic defaults; later user changes remain authoritative.
    if(settingsReady && configVersion->value<1 && gEngfuncs.Cvar_SetValue) {
        if(enabled(legacySafeMode)) {
            if(mode->value==1) gEngfuncs.Cvar_SetValue("sbxtma_mode",2);
            if(online->value==0) gEngfuncs.Cvar_SetValue("sbxtma_allow_online",1);
            if(predictionEnabled->value==0) gEngfuncs.Cvar_SetValue("sbxtma_prediction",1);
        }
        gEngfuncs.Cvar_SetValue("sbxtma_safe_mode",0);
        gEngfuncs.Cvar_SetValue("sbxtma_config_version",1);
    }
    const float time=now();
    if(!active() || (lastFrameTime>=0 && time<lastFrameTime) ||
       sampledAt<0 || time-sampledAt>.5f) {
        restoreFps();state.valid=false;controller.reset();notifications.clear();
    }
    lastFrameTime=time;
}
void onShutdown() {
    resetState();
    if(original.HUD_Shutdown) original.HUD_Shutdown();
}
} // namespace

void IPluginsV4::Init(metahook_api_t* api,mh_interface_t* interfaces,mh_enginesave_t* save) {
    g_pMetaHookAPI=api;g_pInterface=interfaces;g_pMetaSave=save;
    addon_log::write("movement","V4 Init 2.3.0");
}
void IPluginsV4::LoadEngine(cl_enginefunc_t*) {
    // Engine facilities may not be initialized at this stage. Do not call them.
    addon_log::write("movement","LoadEngine: waiting for client Initialize");
}
void IPluginsV4::LoadClient(cl_exportfuncs_t* exports) {
    if(!exports) return;
    original=*exports;
    exports->Initialize=&onInitialize;
    exports->HUD_Init=&onHudInit;
    exports->HUD_VidInit=&onVidInit;
    exports->HUD_PlayerMove=&onMove;
    exports->CL_CreateMove=&onCreateMove;
    exports->HUD_Redraw=&onRedraw;
    exports->HUD_Frame=&onFrame;
    exports->HUD_Shutdown=&onShutdown;
    addon_log::write("movement","LoadClient: callbacks chained, no engine registrations");
}
void IPluginsV4::Shutdown() { resetState(); }
void IPluginsV4::ExitGame(int) { resetState(); }
const char* IPluginsV4::GetVersion() { return "2.3.0"; }
EXPOSE_SINGLE_INTERFACE(IPluginsV4,IPluginsV4,METAHOOK_PLUGIN_API_VERSION_V4);

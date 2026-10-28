#include <metahook.h>
#include <pm_defs.h>
#include <pm_movevars.h>
#include <cvardef.h>
#include <in_buttons.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "movement_math.hpp"
#include "startup_diagnostics.hpp"

// Самостоятельный MetaHookSv V4 плагин. Никаких невалидированных адресов PM_PlayerMove.
// HUD_PlayerMove — безопасный клиентский вход с playermove_t*, который вызывает PM_Move.
// Предсказание на два тика приблизительно: трассировка + гравитация, а не копия PM_Move.
// SvEngine использует playermove_10152_t с массивами MAX_PHYSENTS=1024.

metahook_api_t* g_pMetaHookAPI = nullptr;
mh_interface_t* g_pInterface = nullptr;
mh_enginesave_t* g_pMetaSave = nullptr;
cl_enginefunc_t gEngfuncs = {};

namespace {
using namespace sbxtma;
using MoveFn = void (*)(playermove_t*, int);
using RedrawFn = int (*)(float, int);

// Крашоустойчивый режим: заменяем функции в экспортной таблице клиента,
// вместо изменения кода по адресу с InlineHook. MetaHook V4 разрешает это.
cl_exportfuncs_t* hookedExports=nullptr;
using ShutdownFn = void (*)();
ShutdownFn originalShutdown=nullptr;
MoveFn originalMovement = nullptr;
RedrawFn originalRedraw = nullptr;
cvar_t *mode = nullptr, *online = nullptr, *notify = nullptr;
cvar_t *edgeEnabled = nullptr, *jumpEnabled = nullptr, *strafeEnabled = nullptr;
cvar_t *pixelEnabled = nullptr, *boostEnabled = nullptr, *triggerWidth = nullptr;
cvar_t *predictionEnabled=nullptr,*safeMode=nullptr,*addonVersion=nullptr;
cl_exportfuncs_t* pendingExports=nullptr;
bool activationRequested=false;
bool movementCommandRegistered=false;
bool sessionActive=false;
bool enabled(cvar_t* c) { return c && c->value > 0.5f; }
float now() { return gEngfuncs.GetClientTime ? gEngfuncs.GetClientTime() : 0.f; }

class Notifier {
    struct Entry {
        std::string title, subtitle;
        float expire = 0;
    };
    std::array<Entry, 5> entries{};
    size_t head = 0;
public:
    void add(const char* title, const char* subtitle) {
        if (!enabled(notify) || !mode || mode->value < 0.5f) return;
        for (const Entry& e : entries)
            if (e.title == title && e.expire > now()) return;
        entries[head] = {title, subtitle, now() + 1.8f};
        head = (head + 1) % entries.size();
    }
    void draw(float speed, float triggerFps) const {
        if (!enabled(notify) || !mode || mode->value < 0.5f ||
            !gEngfuncs.pfnGetScreenInfo || !gEngfuncs.pfnFillRGBA ||
            !gEngfuncs.pfnDrawConsoleString) return;
        SCREENINFO info = {};
        info.iSize = sizeof(info);
        if (!gEngfuncs.pfnGetScreenInfo(&info) || info.iWidth < 300) return;
        const int x = info.iWidth - 284;
        int y = static_cast<int>(info.iHeight * 0.56f);
        char header[64] = {};
        std::snprintf(header, sizeof(header), "MOVEMENT  |  %.0f u/s", speed);
        panel(x, y - 48, header, 120, 218, 255);
        if (triggerFps >= 4.f) {
            char fps[64] = {};
            std::snprintf(fps, sizeof(fps), "TRIGGER  ~%.0f FPS (estimate)", triggerFps);
            panel(x, y - 5, fps, 245, 184, 90);
            y += 43;
        }
        for (const Entry& e : entries) {
            if (e.expire <= now()) continue;
            panel(x, y, e.title.c_str(), 82, 198, 237);
            if (gEngfuncs.pfnDrawSetTextColor)
                gEngfuncs.pfnDrawSetTextColor(.70f, .82f, .90f);
            gEngfuncs.pfnDrawConsoleString(x + 12, y + 20,
                const_cast<char*>(e.subtitle.c_str()));
            y += 54;
        }
    }
private:
    void panel(int x, int y, const char* title, int r, int g, int b) const {
        // Оригинальные notification cards: тёмный фон, тонкая цветная полоска.
        gEngfuncs.pfnFillRGBA(x, y, 269, 43, 13, 18, 29, 192);
        gEngfuncs.pfnFillRGBA(x, y, 4, 43, r, g, b, 255);
        if (gEngfuncs.pfnDrawSetTextColor)
            gEngfuncs.pfnDrawSetTextColor(.96f, .97f, 1.f);
        gEngfuncs.pfnDrawConsoleString(x + 12, y + 6,
                                        const_cast<char*>(title));
    }
};
Notifier notifications;

struct Forecast {
    Vec3 pos, vel;
    bool ground = false;
    bool wall = false;
    float normalZ = 0;
    float fraction = 1;
};
class Predictor {
public:
    template <typename PM>
    Forecast twoTicks(const PM& pm) const {
        Forecast out{};
        out.pos = {pm.origin[0], pm.origin[1], pm.origin[2]};
        out.vel = {pm.velocity[0], pm.velocity[1], pm.velocity[2]};
        if (!pm.PM_PlayerTrace || !pm.movevars || pm.frametime <= 0) return out;
        float dt = clampf(pm.frametime, .001f, .05f);
        float gravity = pm.movevars->gravity * (pm.gravity > 0 ? pm.gravity : 1);
        for (int tick = 0; tick != 2; ++tick) {
            out.vel.z -= gravity * dt;
            Vec3 dest = out.pos + out.vel * dt;
            float start[3] = {out.pos.x, out.pos.y, out.pos.z};
            float end[3] = {dest.x, dest.y, dest.z};
            const pmtrace_t hit = pm.PM_PlayerTrace(start, end, PM_NORMAL, -1);
            if (!std::isfinite(hit.fraction)) break;
            out.fraction = clampf(hit.fraction, 0, 1);
            out.pos = {hit.endpos[0], hit.endpos[1], hit.endpos[2]};
            if (out.fraction < 1 && !hit.allsolid) {
                Vec3 n(hit.plane.normal[0], hit.plane.normal[1], hit.plane.normal[2]);
                out.normalZ = n.z;
                out.ground |= n.z > .7f && out.vel.z < 0;
                out.wall |= std::fabs(n.z) < .2f;
                out.vel = slide_against_plane(out.vel, n);
            }
        }
        return out;
    }
};

class MovementAddon {
    Predictor predictor;
    Forecast previousForecast{};
    bool haveForecast = false;
    bool edgeLatch = false;
    bool jumpLatch = false;
    bool fpsLimited = false;
    float fpsPrevious = 100;
    float speed = 0;
    float fpsHint = 0;
public:
    template <typename PM>
    void before(PM* pm, int server) {
        if (!pm || server || pm->server || pm->dead || pm->spectator ||
            !pm->movevars || pm->frametime <= 0 || pm->frametime > .2f) return;
        Vec3 vel(pm->velocity[0], pm->velocity[1], pm->velocity[2]);
        speed = vel.speed2d();
        fpsHint = enabled(triggerWidth) ?
            trigger_skip_fps(speed, triggerWidth->value) : 0;
        if (!mode || mode->value < .5f) { resetFrame(); return; }
        // Используем прогноз предыдущего кадра: нельзя вызывать PM_PlayerTrace
        // до того, как игровая PM_Move инициализирует внутренний pmove.
        const Forecast f = haveForecast ? previousForecast : Forecast{};
        const bool air = pm->onground == -1 && pm->waterlevel <= 1;
        const bool assist = mode->value >= 1.5f &&
                             (!pm->multiplayer || enabled(online));
        if (air) {
            edge(*pm, f, assist);
            jumpbug(*pm, f, assist);
            vectorStrafe(*pm, vel, assist);
        } else resetFrame();
        if (enabled(pixelEnabled) && air && f.wall && std::fabs(vel.z) < 35) {
            if (assist) { pm->cmd.forwardmove = 0; pm->cmd.sidemove = 0; }
            notifications.add("PIXELSURF", assist ? "Movement lock 1 tick" : "Wall micro-contact");
        }
        if (air && f.ground && pm->runfuncs)
            notifications.add("LANDING", "Ground within ~2 ticks");
    }
    template <typename PM>
    void after(const PM* pm, int server) {
        if (!pm || server || pm->server) return;
        // Только после вызова оригинальной HUD_PlayerMove глобальный контекст
        // pmove принадлежит корректному кадру, и можно безопаснее трассировать.
        // Не трогаем client trace API по умолчанию: на некоторых билдах
        // Sven Co-op старые сигнатуры и таблицы PM_PlayerTrace не совпадают.
        if (enabled(predictionEnabled) && pm->movevars && pm->PM_PlayerTrace &&
            pm->frametime > 0 && pm->frametime <= .05f && !pm->dead) {
            previousForecast = predictor.twoTicks(*pm);
            haveForecast = true;
        } else {
            haveForecast = false;
        }
        objectBoost(*pm);
    }
    void draw() const { notifications.draw(speed, fpsHint); }
    void shutdown() { restoreFps(); resetFrame(); haveForecast = false; speed = fpsHint = 0; }

private:
    void resetFrame() { edgeLatch = jumpLatch = false; }
    template <typename PM>
    void edge(PM& pm, const Forecast& f, bool assist) {
        // Это НЕ точное предсказание edgebug, а детектор нужного окна.
        bool candidate = enabled(edgeEnabled) && pm.velocity[2] < -180.f &&
                         f.wall && !f.ground && f.fraction < .99f;
        if (candidate && !edgeLatch) {
            if (assist) pm.cmd.buttons |= IN_DUCK;
            notifications.add("EDGEBUG", assist ? "Duck pulse / 1 tick" : "Edge candidate");
        }
        edgeLatch = candidate;
    }
    template <typename PM>
    void jumpbug(PM& pm, const Forecast& f, bool assist) {
        bool candidate = enabled(jumpEnabled) && pm.velocity[2] < -180.f &&
                         f.ground && f.normalZ > .7f;
        if (candidate && !jumpLatch) {
            if (assist) {
                pm.cmd.buttons &= static_cast<unsigned short>(~IN_DUCK);
                pm.cmd.buttons |= IN_JUMP;
            }
            notifications.add("JUMPBUG", assist ? "Release duck + jump" : "Landing candidate");
        }
        jumpLatch = candidate;
    }
    template <typename PM>
    void vectorStrafe(PM& pm, Vec3 v, bool assist) {
        if (!enabled(strafeEnabled) || !assist || v.speed2d() < 10.f) return;
        float wish = clampf(pm.maxspeed, 1.f, 400.f);
        float ideal = ideal_strafe_angle(wish, pm.movevars->airaccelerate,
                                         pm.frametime, v.speed2d());
        if (ideal <= .01f) return;
        float side = pm.cmd.sidemove < 0 ? -1.f : 1.f;
        float target = std::atan2(v.y, v.x) + radians(side * ideal);
        float yaw = radians(pm.cmd.viewangles[1]);
        pm.cmd.forwardmove = wish * std::cos(target - yaw);
        pm.cmd.sidemove = wish * std::sin(target - yaw);
        // Взгляд/углы камеры не меняем.
    }
    template <typename PM>
    void objectBoost(const PM& pm) {
        bool touched = false;
        if (enabled(boostEnabled)) {
            const int maxPhys = static_cast<int>(sizeof(pm.physents) / sizeof(pm.physents[0]));
            int count = std::max(0, std::min(pm.numtouch, maxPhys));
            for (int i = 0; i < count; ++i) {
                int index = pm.touchindex[i].ent;
                if (index < 0 || index >= pm.numphysent || index >= maxPhys)
                    continue;
                const physent_t& entity = pm.physents[index];
                if (std::strncmp(entity.name, "func_pushable", sizeof(entity.name)) == 0) {
                    touched = true;
                    break;
                }
            }
        }
        bool permitted = mode && mode->value >= 1.5f &&
                         (!pm.multiplayer || enabled(online));
        if (touched && permitted && !fpsLimited &&
            gEngfuncs.pfnGetCvarFloat && gEngfuncs.Cvar_SetValue) {
            fpsPrevious = clampf(gEngfuncs.pfnGetCvarFloat("fps_max"), 4, 1000);
            gEngfuncs.Cvar_SetValue("fps_max", 4.f);
            fpsLimited = true;
            notifications.add("OBJECT BOOST", "Pushable contact / 4 FPS");
        }
        if (!touched || !permitted) restoreFps();
    }
    void restoreFps() {
        if (fpsLimited && gEngfuncs.Cvar_SetValue) {
            gEngfuncs.Cvar_SetValue("fps_max", fpsPrevious);
            notifications.add("FPS RESTORED", "Previous FPS limit restored");
        }
        fpsLimited = false;
    }
};
MovementAddon addon;

void onMove(playermove_t* pm, int server) {
    // Никаких обращений к структуре до оригинальной функции, пока игра
    // инициализирует кадр. В режиме Assist используется прошлый прогноз.
    const bool safe = enabled(safeMode);
    const bool sven = g_pMetaHookAPI && g_pMetaHookAPI->GetEngineType &&
                      g_pMetaHookAPI->GetEngineType() == ENGINE_SVENGINE;
    if(pm && !safe && mode && mode->value >= 1.5f) {
        if (sven) addon.before(reinterpret_cast<playermove_10152_t*>(pm), server);
        else addon.before(pm, server);
    }
    if(originalMovement) originalMovement(pm, server);
    if(!pm) return;
    if(sven) {
        if(safe) addon.before(reinterpret_cast<playermove_10152_t*>(pm),server);
        addon.after(reinterpret_cast<playermove_10152_t*>(pm),server);
    } else {
        if(safe) addon.before(pm,server);
        addon.after(pm,server);
    }
}
int onRedraw(float t, int intermission) {
    int result = originalRedraw ? originalRedraw(t, intermission) : 1;
    if (!intermission) addon.draw();
    return result;
}
void onClientShutdown() {
    addon_log::write("movement","HUD_Shutdown");
    // Восстанавливаем экспортированные функции, пока таблица клиента жива.
    if(hookedExports) {
        if(hookedExports->HUD_PlayerMove==&onMove) hookedExports->HUD_PlayerMove=originalMovement;
        if(hookedExports->HUD_Redraw==&onRedraw) hookedExports->HUD_Redraw=originalRedraw;
        if(hookedExports->HUD_Shutdown==&onClientShutdown)
            hookedExports->HUD_Shutdown=originalShutdown;
    }
    hookedExports=nullptr;pendingExports=nullptr;
    sessionActive=false;
    if(originalShutdown) originalShutdown();
    addon.shutdown();
}
void removeHooks() {
    addon_log::write("movement","removeHooks");
    addon.shutdown();
    // Нельзя разыменовывать экспортную таблицу после выгрузки client.dll.
    // onClientShutdown восстанавливает указатели до выгрузки.
    hookedExports=nullptr;
    pendingExports=nullptr;
    sessionActive=false;
    originalMovement=nullptr;
    originalRedraw=nullptr;
    originalShutdown=nullptr;
}
void registerVariables() {
    if (!gEngfuncs.pfnRegisterVariable) return;
    mode = gEngfuncs.pfnRegisterVariable("sbxtma_mode", "1", FCVAR_ARCHIVE);
    online = gEngfuncs.pfnRegisterVariable("sbxtma_allow_online", "0", FCVAR_ARCHIVE);
    notify = gEngfuncs.pfnRegisterVariable("sbxtma_notifications", "1", FCVAR_ARCHIVE);
    edgeEnabled = gEngfuncs.pfnRegisterVariable("sbxtma_edgebug", "1", FCVAR_ARCHIVE);
    jumpEnabled = gEngfuncs.pfnRegisterVariable("sbxtma_jumpbug", "1", FCVAR_ARCHIVE);
    strafeEnabled = gEngfuncs.pfnRegisterVariable("sbxtma_strafe", "1", FCVAR_ARCHIVE);
    pixelEnabled = gEngfuncs.pfnRegisterVariable("sbxtma_pixelsurf", "1", FCVAR_ARCHIVE);
    boostEnabled = gEngfuncs.pfnRegisterVariable("sbxtma_objectboost", "0", FCVAR_ARCHIVE);
    triggerWidth = gEngfuncs.pfnRegisterVariable("sbxtma_trigger_width", "0", FCVAR_ARCHIVE);
    predictionEnabled=gEngfuncs.pfnRegisterVariable("sbxtma_prediction", "0", FCVAR_ARCHIVE);
    safeMode=gEngfuncs.pfnRegisterVariable("sbxtma_safe_mode", "1", FCVAR_ARCHIVE);
    addonVersion=gEngfuncs.pfnRegisterVariable("sbxtma_version", "2.1.0", 0);
}
// Start the hooks only when explicitly requested in a RUNNING game.
// A successful LoadLibrary must never install a client detour.
void activate() {
    addon_log::write("movement","sbxtma_start requested");
    activationRequested=true;
    if(sessionActive || !pendingExports) {
        addon_log::write("movement","sbxtma_start unavailable: client callbacks not ready");
        return;
    }
    auto* exp=pendingExports;
    if(!exp->HUD_PlayerMove || !exp->HUD_Redraw || !exp->HUD_Shutdown) {
        addon_log::write("movement","sbxtma_start missing client callbacks");
        return;
    }
    hookedExports=exp;
    originalMovement=exp->HUD_PlayerMove;
    originalRedraw=exp->HUD_Redraw;
    originalShutdown=exp->HUD_Shutdown;
    if(originalMovement==&onMove || originalRedraw==&onRedraw ||
       originalShutdown==&onClientShutdown) {
        addon_log::write("movement","sbxtma_start rejected recursive callback");
        hookedExports=nullptr;return;
    }
    exp->HUD_PlayerMove=&onMove;
    exp->HUD_Redraw=&onRedraw;
    exp->HUD_Shutdown=&onClientShutdown;
    sessionActive=true;
    addon_log::write("movement","sbxtma_start callbacks attached");
}
void deactivate() {
    addon_log::write("movement","sbxtma_stop");
    activationRequested=false;
    if(sessionActive && hookedExports) {
        if(hookedExports->HUD_PlayerMove==&onMove)
            hookedExports->HUD_PlayerMove=originalMovement;
        if(hookedExports->HUD_Redraw==&onRedraw)
            hookedExports->HUD_Redraw=originalRedraw;
        if(hookedExports->HUD_Shutdown==&onClientShutdown)
            hookedExports->HUD_Shutdown=originalShutdown;
    }
    hookedExports=nullptr;
    sessionActive=false;
    addon.shutdown();
}
} // namespace

void IPluginsV4::Init(metahook_api_t* api, mh_interface_t* interfaces, mh_enginesave_t* save) {
    g_pMetaHookAPI = api; g_pInterface = interfaces; g_pMetaSave = save;
    addon_log::write("movement","V4 Init");
}
void IPluginsV4::LoadEngine(cl_enginefunc_t* funcs) {
    if (!funcs) return;
    gEngfuncs = *funcs;
    addon_log::write("movement","LoadEngine begin");
    registerVariables();
    if(!movementCommandRegistered && gEngfuncs.pfnAddCommand) {
        gEngfuncs.pfnAddCommand("sbxtma_start",&activate);
        gEngfuncs.pfnAddCommand("sbxtma_stop",&deactivate);
        movementCommandRegistered=true;
    }
    addon_log::write("movement","LoadEngine completed (passive)");
}
void IPluginsV4::LoadClient(cl_exportfuncs_t* exports) {
    addon_log::write("movement","LoadClient (no automatic hooks)");
    pendingExports=exports;
    // Startup safety is mandatory. Even an archived mode=2 must not
    // overwrite client callbacks before user explicitly enables them.
}
void IPluginsV4::Shutdown() { addon_log::write("movement","V4 Shutdown"); removeHooks(); }
void IPluginsV4::ExitGame(int) { addon_log::write("movement","V4 ExitGame"); removeHooks(); }
const char* IPluginsV4::GetVersion() { return "2.1.0-safe"; }
EXPOSE_SINGLE_INTERFACE(IPluginsV4, IPluginsV4, METAHOOK_PLUGIN_API_VERSION_V4);

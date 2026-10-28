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

// Самостоятельный MetaHookSv V4 плагин. Никаких невалидированных адресов PM_PlayerMove.
// HUD_PlayerMove — безопасный клиентский вход с playermove_t*, который вызывает PM_Move.
// Предсказание на два тика приблизительно: трассировка + гравитация, а не копия PM_Move.

metahook_api_t* g_pMetaHookAPI = nullptr;
mh_interface_t* g_pInterface = nullptr;
mh_enginesave_t* g_pMetaSave = nullptr;
cl_enginefunc_t gEngfuncs = {};

namespace {
using namespace sbxtma;
using MoveFn = void (*)(playermove_t*, int);
using RedrawFn = int (*)(float, int);

hook_t* movementHook = nullptr;
hook_t* redrawHook = nullptr;
MoveFn originalMovement = nullptr;
RedrawFn originalRedraw = nullptr;
cvar_t *mode = nullptr, *online = nullptr, *notify = nullptr;
cvar_t *edgeEnabled = nullptr, *jumpEnabled = nullptr, *strafeEnabled = nullptr;
cvar_t *pixelEnabled = nullptr, *boostEnabled = nullptr, *triggerWidth = nullptr;
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
    Forecast twoTicks(const playermove_t& pm) const {
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
    bool edgeLatch = false;
    bool jumpLatch = false;
    bool fpsLimited = false;
    float fpsPrevious = 100;
    float speed = 0;
    float fpsHint = 0;
public:
    void before(playermove_t* pm, int server) {
        if (!pm || server || pm->server || pm->dead || pm->spectator ||
            !pm->movevars || pm->frametime <= 0 || pm->frametime > .2f) return;
        Vec3 vel(pm->velocity[0], pm->velocity[1], pm->velocity[2]);
        speed = vel.speed2d();
        fpsHint = enabled(triggerWidth) ?
            trigger_skip_fps(speed, triggerWidth->value) : 0;
        if (!mode || mode->value < .5f) { resetFrame(); return; }
        const Forecast f = predictor.twoTicks(*pm);
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
    void after(const playermove_t* pm, int server) {
        if (!pm || server || pm->server) return;
        objectBoost(*pm);
    }
    void draw() const { notifications.draw(speed, fpsHint); }
    void shutdown() { restoreFps(); resetFrame(); speed = fpsHint = 0; }

private:
    void resetFrame() { edgeLatch = jumpLatch = false; }
    void edge(playermove_t& pm, const Forecast& f, bool assist) {
        // Это НЕ точное предсказание edgebug, а детектор нужного окна.
        bool candidate = enabled(edgeEnabled) && pm.velocity[2] < -180.f &&
                         f.wall && !f.ground && f.fraction < .99f;
        if (candidate && !edgeLatch) {
            if (assist) pm.cmd.buttons |= IN_DUCK;
            notifications.add("EDGEBUG", assist ? "Duck pulse / 1 tick" : "Edge candidate");
        }
        edgeLatch = candidate;
    }
    void jumpbug(playermove_t& pm, const Forecast& f, bool assist) {
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
    void vectorStrafe(playermove_t& pm, Vec3 v, bool assist) {
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
    void objectBoost(const playermove_t& pm) {
        bool touched = false;
        if (enabled(boostEnabled)) {
            int count = std::max(0, std::min(pm.numtouch, MAX_PHYSENTS));
            for (int i = 0; i < count; ++i) {
                int index = pm.touchindex[i].ent;
                if (index < 0 || index >= pm.numphysent || index >= MAX_PHYSENTS)
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
    addon.before(pm, server);
    if (originalMovement) originalMovement(pm, server);
    addon.after(pm, server);
}
int onRedraw(float t, int intermission) {
    int result = originalRedraw ? originalRedraw(t, intermission) : 1;
    if (!intermission) addon.draw();
    return result;
}
void removeHooks() {
    addon.shutdown();
    if (g_pMetaHookAPI && movementHook) g_pMetaHookAPI->UnHook(movementHook);
    if (g_pMetaHookAPI && redrawHook) g_pMetaHookAPI->UnHook(redrawHook);
    movementHook = redrawHook = nullptr;
    originalMovement = nullptr;
    originalRedraw = nullptr;
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
}
} // namespace

void IPluginsV4::Init(metahook_api_t* api, mh_interface_t* interfaces, mh_enginesave_t* save) {
    g_pMetaHookAPI = api; g_pInterface = interfaces; g_pMetaSave = save;
}
void IPluginsV4::LoadEngine(cl_enginefunc_t* funcs) {
    if (!funcs) return;
    gEngfuncs = *funcs;
    registerVariables();
}
void IPluginsV4::LoadClient(cl_exportfuncs_t* exports) {
    if (!g_pMetaHookAPI || !exports || movementHook || redrawHook) return;
    if (!exports->HUD_PlayerMove || !exports->HUD_Redraw) return;
    // MetaHook откладывает хуки до загрузки всех плагинов.
    movementHook = g_pMetaHookAPI->InlineHook(
        reinterpret_cast<void*>(exports->HUD_PlayerMove),
        reinterpret_cast<void*>(&onMove), reinterpret_cast<void**>(&originalMovement));
    redrawHook = g_pMetaHookAPI->InlineHook(
        reinterpret_cast<void*>(exports->HUD_Redraw),
        reinterpret_cast<void*>(&onRedraw), reinterpret_cast<void**>(&originalRedraw));
    if (!movementHook || !redrawHook) removeHooks();
}
void IPluginsV4::Shutdown() { removeHooks(); }
void IPluginsV4::ExitGame(int) { removeHooks(); }
const char* IPluginsV4::GetVersion() { return "0.1.0"; }
EXPOSE_SINGLE_INTERFACE(IPluginsV4, IPluginsV4, METAHOOK_PLUGIN_API_VERSION_V4);

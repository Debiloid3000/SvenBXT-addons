#include <metahook.h>
#include <cvardef.h>
#include <ref_params.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <array>
#include "hudedit_layout.hpp"
#include "hudedit_regions.hpp"
#include "startup_diagnostics.hpp"
// VGUI Options frontend remains in the repository as experimental source,
// but is not linked into the safe startup DLL until its runtime is validated.

// SvenBXTHudEdit — отдельный MetaHookSv V4-плагин.
// Настройки палитры, замены элементов HUD, координат и спидометра.
// Исходники — новая реализация по мотивам возможностей HUDColor (DrAbcOfficial).
// Непроверенные адреса движка никогда не вызываются.

metahook_api_t* g_pMetaHookAPI = nullptr;
mh_interface_t* g_pInterface = nullptr;
mh_enginesave_t* g_pMetaSave = nullptr;
cl_enginefunc_t gEngfuncs = {};

namespace {
using RedrawFn = int (*)(float, int);
using InitFn = void (*)();
using ViewFn = void (*)(ref_params_t*);
using SprFn = decltype(gEngfuncs.pfnSPR_Set);
using SprDrawFn = decltype(gEngfuncs.pfnSPR_Draw);
using SprDrawAdditiveFn = decltype(gEngfuncs.pfnSPR_DrawAdditive);
using SprDrawHolesFn = decltype(gEngfuncs.pfnSPR_DrawHoles);
using TextColorFn = decltype(gEngfuncs.pfnDrawSetTextColor);
using ScaleFn = void (*)(int*, int*, int*, int);
using ConsoleFn = decltype(gEngfuncs.pfnDrawConsoleString);
using FillFn = decltype(gEngfuncs.pfnFillRGBA);

hook_t* redrawHook = nullptr;
hook_t* initHook = nullptr;
hook_t* viewHook = nullptr;
hook_t* spriteHook = nullptr;
hook_t* scaleHook = nullptr;
hook_t* drawHook = nullptr;
hook_t* drawAdditiveHook = nullptr;
hook_t* drawHolesHook = nullptr;
hook_t* textColorHook = nullptr;
hook_t* consoleHook=nullptr;
hook_t* fillHook=nullptr;
RedrawFn oldRedraw = nullptr;
InitFn oldInit = nullptr;
ViewFn oldView = nullptr;
SprFn oldSprite = nullptr;
ScaleFn oldScale = nullptr;
SprDrawFn oldDraw = nullptr;
SprDrawAdditiveFn oldDrawAdditive = nullptr;
SprDrawHolesFn oldDrawHoles = nullptr;
TextColorFn oldTextColor = nullptr;
ConsoleFn oldConsole=nullptr;
FillFn oldFill=nullptr;
pfnUserMsgHook oldHealthMessage = nullptr;
pfnUserMsgHook oldArmorMessage = nullptr;
bool msgHooksInstalled = false;
bool settingsRegistered = false;
bool colorCommandRegistered = false;
bool safeCommandsRegistered = false;
struct RegisteredCvar { const char* name; cvar_t* ptr; };
std::array<RegisteredCvar,256> registeredCVars{};
size_t registeredCount=0, failedRegistrationCount=0;
bool hudSessionActive=false;
bool nativeHooksEnabled=false;
cl_exportfuncs_t* pendingExports=nullptr;
using ShutdownFn=void (*)();
ShutdownFn originalShutdown=nullptr;
bool wasEverActivated=false;
bool insideHudRedraw = false;
int cachedScreenHeight = 0;
int cachedScreenWidth = 0;
int currentSprite = 0;
int spriteR=255,spriteG=255,spriteB=255;
bool drawingCustomOverlay=false;
float lastTextR=1.f,lastTextG=1.f,lastTextB=1.f;

cvar_t *active=nullptr, *r=nullptr, *g=nullptr, *b=nullptr;
cvar_t *lowR=nullptr, *lowG=nullptr, *lowB=nullptr, *critical=nullptr;
cvar_t *rainbow=nullptr, *rainbowSpeed=nullptr, *flashTime=nullptr;
cvar_t *hideStock=nullptr, *healthCustom=nullptr, *armorCustom=nullptr;
cvar_t *healthNumber=nullptr, *healthIcon=nullptr, *armorNumber=nullptr, *armorIcon=nullptr;
cvar_t *healthNX=nullptr, *healthNY=nullptr, *healthIX=nullptr, *healthIY=nullptr;
cvar_t *armorNX=nullptr, *armorNY=nullptr, *armorIX=nullptr, *armorIY=nullptr;
cvar_t *speedEnable=nullptr, *speedX=nullptr, *speedY=nullptr;
cvar_t *speedStyle=nullptr, *speedScale=nullptr, *speedTheme=nullptr;
cvar_t *speedR=nullptr, *speedG=nullptr, *speedB=nullptr;
cvar_t *speedLabel=nullptr;
cvar_t *opacity=nullptr;
cvar_t *forceAllColors=nullptr;
cvar_t *inventoryBottom=nullptr, *inventoryRegionX=nullptr, *inventoryRegionY=nullptr;
cvar_t *inventoryRegionW=nullptr, *inventoryRegionH=nullptr, *inventoryMargin=nullptr;
struct HudGroup {
    cvar_t *enabled=nullptr,*visible=nullptr,*x=nullptr,*y=nullptr,*regionX=nullptr,*regionY=nullptr,*regionW=nullptr,*regionH=nullptr;
    cvar_t *useTheme=nullptr,*red=nullptr,*green=nullptr,*blue=nullptr;
    // Names must remain valid for the entire game session. The old local
    // 'char name[96]' went out of scope immediately after registration.
    std::array<std::array<char,96>,12> persistentNames{};
};
HudGroup inventory, flashlight, chapter;
cvar_t *healthNR=nullptr,*healthNG=nullptr,*healthNB=nullptr,*healthNTheme=nullptr;
cvar_t *healthIR=nullptr,*healthIG=nullptr,*healthIB=nullptr,*healthITheme=nullptr;
cvar_t *armorNR=nullptr,*armorNG=nullptr,*armorNB=nullptr,*armorNTheme=nullptr;
cvar_t *armorIR=nullptr,*armorIG=nullptr,*armorIB=nullptr,*armorITheme=nullptr;
cvar_t *rainbowAll=nullptr,*criticalPriority=nullptr;
cvar_t *chEnabled=nullptr,*chPreview=nullptr,*chSize=nullptr,*chThickness=nullptr;
cvar_t *chGap=nullptr,*chOutline=nullptr,*chDot=nullptr,*chAlpha=nullptr,*chDynamic=nullptr;
cvar_t *chTStyle=nullptr,*chUseTheme=nullptr,*chRed=nullptr,*chGreen=nullptr,*chBlue=nullptr;
cvar_t *chOutlineRed=nullptr,*chOutlineGreen=nullptr,*chOutlineBlue=nullptr;
bool suppressingNativeCrosshair=false;
float savedNativeCrosshair=1;

int health = -1, armor = -1;
float speed = 0, damagedUntil = 0;
bool suppressingOriginalSpeed = false, suppressingOriginalHud = false;
float savedSpeedState = 1, savedHudState = 1;

float now() { return gEngfuncs.GetClientTime ? gEngfuncs.GetClientTime() : 0.f; }
float val(cvar_t* c, float fallback=0) { return c ? c->value : fallback; }
bool yes(cvar_t* c) { return val(c) >= .5f; }
bool globalRainbow() { return yes(rainbow) && yes(rainbowAll); }
int byte(float x) { return static_cast<int>(std::lround(std::max(0.f, std::min(255.f, x)))); }
int coord(cvar_t* c, int automatic) { return c && c->value >= 0 ? static_cast<int>(c->value) : automatic; }

struct RGB { int r,g,b; };
RGB hsv(float h) {
    const float f = (h - std::floor(h)) * 6.f;
    const float x = 1.f - std::fabs(std::fmod(f, 2.f) - 1.f);
    const int section = static_cast<int>(f);
    if (section==0) return {255,byte(255*x),0};
    if (section==1) return {byte(255*x),255,0};
    if (section==2) return {0,255,byte(255*x)};
    if (section==3) return {0,byte(255*x),255};
    if (section==4) return {byte(255*x),0,255};
    return {255,0,byte(255*x)};
}
RGB mainColor() {
    if (yes(rainbow))
        return hsv(now() * std::max(.01f, val(rainbowSpeed,.1f)));
    return {byte(val(r,100)), byte(val(g,130)), byte(val(b,200))};
}
RGB lowColor() { return {byte(val(lowR,250)),byte(val(lowG,25)),byte(val(lowB,45))}; }
RGB currentHealthColor() {
    return (health >= 0 && health <= val(critical,25) || now() < damagedUntil) &&
           yes(criticalPriority) && !yes(forceAllColors) && !globalRainbow()
        ? lowColor() : mainColor();
}
void transform(int& red,int& green,int& blue) {
    if (!yes(active)) return;
    RGB c;
    if (yes(forceAllColors) || globalRainbow()) c=mainColor();
    else if (red==100 && green==130 && blue==200) c=mainColor();
    else if (red==250 && green==0 && blue==0) c=lowColor();
    else return;
    red=c.r;green=c.g;blue=c.b;
}
RGB styled(bool theme,cvar_t* red,cvar_t* green,cvar_t* blue) {
    return theme ? mainColor() : RGB{byte(val(red,255)),byte(val(green,255)),byte(val(blue,255))};
}
RGB groupColor(const HudGroup& group) {
    return styled(yes(forceAllColors)||globalRainbow()||yes(group.useTheme),group.red,group.green,group.blue);
}
hudedit::Region bounds(const HudGroup& group) {
    return {static_cast<int>(val(group.regionX,0)),static_cast<int>(val(group.regionY,0)),
            static_cast<int>(val(group.regionW,0)),static_cast<int>(val(group.regionH,0)),
            static_cast<int>(val(group.x,-1)),static_cast<int>(val(group.y,-1)),
            yes(group.visible),yes(group.enabled)};
}
const HudGroup* groupAt(int x,int y) {
    if (!yes(active) || !insideHudRedraw || drawingCustomOverlay) return nullptr;
    // Фонарик и текст главы имеют приоритет над зоной инвентаря.
    for(const auto* group : {&flashlight,&chapter,&inventory}) {
        if(hudedit::contains(bounds(*group),x,y,cachedScreenWidth,cachedScreenHeight))
            return group;
    }
    return nullptr;
}
hudedit::Point location(const HudGroup& group,int x,int y) {
    return hudedit::move(bounds(group),x,y,cachedScreenWidth,cachedScreenHeight,
                    &group==&inventory && yes(inventoryBottom),
                    static_cast<int>(val(inventoryMargin,12)));
}
void onSprite(int sprite, int red, int green, int blue) {
    currentSprite=sprite;spriteR=red;spriteG=green;spriteB=blue;
    transform(red,green,blue);
    if (oldSprite) oldSprite(sprite,red,green,blue);
}
void onScale(int* red,int* green,int* blue,int alpha) {
    if (red && green && blue) transform(*red,*green,*blue);
    if (oldScale) oldScale(red,green,blue,alpha);
}
// Text elements using the legacy engine draw API, e.g. inventory text and chapter text.
// VGUI elements which bypass this interface cannot be recolored by this hook.
void onTextColor(float red, float green, float blue) {
    lastTextR=red;lastTextG=green;lastTextB=blue;
    if (insideHudRedraw && yes(active) && yes(forceAllColors)) {
        const RGB tint=mainColor();
        red=tint.r/255.f; green=tint.g/255.f; blue=tint.b/255.f;
    }
    if (oldTextColor) oldTextColor(red,green,blue);
}
int onConsole(int x,int y,char* message) {
    if(!oldConsole) return x;
    const HudGroup* group=groupAt(x,y);
    if(!group) return oldConsole(x,y,message);
    if(!yes(group->visible)) return x;
    const auto p=location(*group,x,y);
    const RGB color=groupColor(*group);
    if(oldTextColor) oldTextColor(color.r/255.f,color.g/255.f,color.b/255.f);
    const int result=oldConsole(p.x,p.y,message);
    if(oldTextColor) oldTextColor(lastTextR,lastTextG,lastTextB);
    return result;
}
void onFill(int x,int y,int w,int h,int red,int green,int blue,int alpha) {
    if(!oldFill) return;
    const HudGroup* group=groupAt(x,y);
    if(group) {
        if(!yes(group->visible)) return;
        const auto p=location(*group,x,y);x=p.x;y=p.y;
        const RGB color=groupColor(*group);
        red=color.r;green=color.g;blue=color.b;
    } else {
        transform(red,green,blue);
    }
    oldFill(x,y,w,h,red,green,blue,alpha);
}
// Treat every sprite of one HUD region as part of a single group.
// Coordinates and color change together, not per weapon icon.
template<typename T> void renderSprite(T draw,int frame,int x,int y,const wrect_t* rect) {
    if(!draw) return;
    const HudGroup* group=groupAt(x,y);
    if(!group) {draw(frame,x,y,rect);return;}
    if(!yes(group->visible)) return;
    const auto p=location(*group,x,y);
    const RGB c=groupColor(*group);
    if(oldSprite && currentSprite) oldSprite(currentSprite,c.r,c.g,c.b);
    draw(frame,p.x,p.y,rect);
    int r=spriteR,g=spriteG,b=spriteB;transform(r,g,b);
    if(oldSprite && currentSprite)oldSprite(currentSprite,r,g,b);
}
void onDraw(int frame,int x,int y,const wrect_t* rect) {
    renderSprite(oldDraw,frame,x,y,rect);
}
void onDrawAdditive(int frame,int x,int y,const wrect_t* rect) {
    renderSprite(oldDrawAdditive,frame,x,y,rect);
}
void onDrawHoles(int frame,int x,int y,const wrect_t* rect) {
    renderSprite(oldDrawHoles,frame,x,y,rect);
}
void changeColorCommand() {
    if (!gEngfuncs.Cmd_Argc || !gEngfuncs.Cmd_Argv ||
        !gEngfuncs.Cvar_SetValue || gEngfuncs.Cmd_Argc()!=4) {
        if (gEngfuncs.Con_Printf)
            gEngfuncs.Con_Printf("Usage: hudedit_color R G B (0..255)\n");
        return;
    }
    int colors[3]={};
    for(int i=0;i<3;++i) {
        const char* text=gEngfuncs.Cmd_Argv(i+1);
        if(!text || !*text) return;
        char* end=nullptr;
        const long n=std::strtol(text,&end,10);
        if(end==text || *end || n<0 || n>255) {
            if (gEngfuncs.Con_Printf)
                gEngfuncs.Con_Printf("HUD Edit: values must be 0..255\n");
            return;
        }
        colors[i]=static_cast<int>(n);
    }
    gEngfuncs.Cvar_SetValue("hudedit_r",static_cast<float>(colors[0]));
    gEngfuncs.Cvar_SetValue("hudedit_g",static_cast<float>(colors[1]));
    gEngfuncs.Cvar_SetValue("hudedit_b",static_cast<float>(colors[2]));
    gEngfuncs.Cvar_SetValue("hudedit_speed_use_theme",1);
    gEngfuncs.Cvar_SetValue("hudedit_all_colors",1);
    gEngfuncs.Cvar_SetValue("hudedit_rainbow",0);
    if(gEngfuncs.Con_Printf)
        gEngfuncs.Con_Printf("HUD Edit: global RGB %d %d %d\n",colors[0],colors[1],colors[2]);
}

int onHealth(const char* name, int length, void* bytes) {
    if (bytes && length >= 1) {
        const auto* p=static_cast<const unsigned char*>(bytes);
        const int value=length >= 2 ? static_cast<int>(p[0] | (p[1]<<8)) : static_cast<int>(p[0]);
        if (health >= 0 && value < health) damagedUntil=now()+std::max(0.f,val(flashTime,.5f));
        health=value;
    }
    return oldHealthMessage ? oldHealthMessage(name,length,bytes) : 1;
}
int onArmor(const char* name, int length, void* bytes) {
    if (bytes && length >= 1) {
        const auto* p=static_cast<const unsigned char*>(bytes);
        armor=length >= 2 ? static_cast<int>(p[0] | (p[1]<<8)) : static_cast<int>(p[0]);
    }
    return oldArmorMessage ? oldArmorMessage(name,length,bytes) : 1;
}

void reg(cvar_t*& dest,const char* name,const char* value) {
    dest=gEngfuncs.pfnRegisterVariable ?
        gEngfuncs.pfnRegisterVariable(name,value,FCVAR_ARCHIVE) : nullptr;
    if(!dest) {
        ++failedRegistrationCount;
        addon_log::write("hudedit","CVar registration returned NULL");
    }
    if(registeredCount<registeredCVars.size())
        registeredCVars[registeredCount++]={name,dest};
}
void registerGroup(HudGroup& group,const char* key,bool defaultEnabled,
                   const char* left,const char* top,const char* width,const char* height) {
    size_t slot=0;
    auto make=[&](cvar_t*& ptr,const char* suffix,const char* value) {
        auto& name=group.persistentNames.at(slot++);
        std::snprintf(name.data(),name.size(),"hudedit_%s_%s",key,suffix);
        reg(ptr,name.data(),value);
    };
    make(group.enabled,"enabled",defaultEnabled?"1":"0");
    make(group.visible,"visible","1");
    make(group.x,"x","-1");make(group.y,"y","-1");
    make(group.regionX,"region_x",left);make(group.regionY,"region_y",top);
    make(group.regionW,"region_w",width);make(group.regionH,"region_h",height);
    make(group.useTheme,"use_theme","1");
    make(group.red,"r","255");make(group.green,"g","255");make(group.blue,"b","255");
}
void settings() {
    addon_log::write("hudedit","settings begin (user-triggered)");
    reg(active,"hudedit_enabled","1");
    addon_log::write("hudedit","settings: base CVars entered");
    cvar_t* version=nullptr;
    version=gEngfuncs.pfnRegisterVariable?gEngfuncs.pfnRegisterVariable("hudedit_version","2.2.0",0):nullptr;
    if(registeredCount<registeredCVars.size())
        registeredCVars[registeredCount++]={"hudedit_version",version};
    if(!version) ++failedRegistrationCount;
    reg(r,"hudedit_r","100"); reg(g,"hudedit_g","130"); reg(b,"hudedit_b","200");
    reg(lowR,"hudedit_critical_r","250");reg(lowG,"hudedit_critical_g","25");
    reg(lowB,"hudedit_critical_b","45");reg(critical,"hudedit_critical_hp","25");
    reg(rainbow,"hudedit_rainbow","0");reg(rainbowSpeed,"hudedit_rainbow_speed","0.10");
    reg(flashTime,"hudedit_damage_flash","0.5");
    reg(rainbowAll,"hudedit_rainbow_all","1");
    reg(criticalPriority,"hudedit_critical_priority","0");
    reg(hideStock,"hudedit_hide_stock","0");
    reg(healthCustom,"hudedit_health","0");reg(armorCustom,"hudedit_armor","0");
    reg(healthNumber,"hudedit_health_number","1");reg(healthIcon,"hudedit_health_icon","1");
    reg(armorNumber,"hudedit_armor_number","1");reg(armorIcon,"hudedit_armor_icon","1");
    reg(healthNX,"hudedit_health_num_x","-1");reg(healthNY,"hudedit_health_num_y","-1");
    reg(healthIX,"hudedit_health_icon_x","-1");reg(healthIY,"hudedit_health_icon_y","-1");
    reg(armorNX,"hudedit_armor_num_x","-1");reg(armorNY,"hudedit_armor_num_y","-1");
    reg(armorIX,"hudedit_armor_icon_x","-1");reg(armorIY,"hudedit_armor_icon_y","-1");
    reg(speedEnable,"hudedit_speed","1");reg(speedX,"hudedit_speed_x","-1");
    reg(speedY,"hudedit_speed_y","-1");reg(speedStyle,"hudedit_speed_style","1");
    reg(speedScale,"hudedit_speed_scale","2");
    reg(speedTheme,"hudedit_speed_use_theme","1");
    reg(speedR,"hudedit_speed_r","255");reg(speedG,"hudedit_speed_g","255");
    reg(speedB,"hudedit_speed_b","255");reg(speedLabel,"hudedit_speed_label","0");
    reg(opacity,"hudedit_opacity","255");
    reg(forceAllColors,"hudedit_all_colors","0");
    // Только меню выбора предметов сверху слева, остальные элементы не двигаем.
    reg(inventoryBottom,"hudedit_inventory_bottom","0");
    reg(inventoryRegionX,"hudedit_inventory_region_x","0");
    reg(inventoryRegionY,"hudedit_inventory_region_y","0");
    reg(inventoryRegionW,"hudedit_inventory_region_w","210");
    reg(inventoryRegionH,"hudedit_inventory_region_h","170");
    reg(inventoryMargin,"hudedit_inventory_margin","12");
    addon_log::write("hudedit","settings: before inventory group");
    registerGroup(inventory,"inventory",true,"0","0","210","170");
    addon_log::write("hudedit","settings: inventory group done");
    // Flashlight and chapter text are not always rendered through GoldSrc draw APIs.
    // Opt-in regions avoid accidentally catching HP/ammo/other map titles.
    addon_log::write("hudedit","settings: before flashlight group");
    registerGroup(flashlight,"flashlight",false,"0","-105","85","65");
    addon_log::write("hudedit","settings: flashlight group done");
    addon_log::write("hudedit","settings: before chapter group");
    registerGroup(chapter,"chapter",false,"180","0","650","110");
    addon_log::write("hudedit","settings: chapter group done");
    reg(healthNTheme,"hudedit_health_number_use_theme","1");
    reg(healthNR,"hudedit_health_number_r","255");
    reg(healthNG,"hudedit_health_number_g","255");
    reg(healthNB,"hudedit_health_number_b","255");
    reg(healthITheme,"hudedit_health_icon_use_theme","1");
    reg(healthIR,"hudedit_health_icon_r","255");
    reg(healthIG,"hudedit_health_icon_g","255");
    reg(healthIB,"hudedit_health_icon_b","255");
    reg(armorNTheme,"hudedit_armor_number_use_theme","1");
    reg(armorNR,"hudedit_armor_number_r","255");
    reg(armorNG,"hudedit_armor_number_g","255");
    reg(armorNB,"hudedit_armor_number_b","255");
    reg(armorITheme,"hudedit_armor_icon_use_theme","1");
    reg(armorIR,"hudedit_armor_icon_r","255");
    reg(armorIG,"hudedit_armor_icon_g","255");
    reg(armorIB,"hudedit_armor_icon_b","255");
    reg(chEnabled,"hudedit_crosshair","0");
    reg(chPreview,"hudedit_crosshair_preview","0");
    reg(chSize,"hudedit_crosshair_size","12");
    reg(chThickness,"hudedit_crosshair_thickness","2");
    reg(chGap,"hudedit_crosshair_gap","5");
    reg(chOutline,"hudedit_crosshair_outline","1");
    reg(chDot,"hudedit_crosshair_dot","0");
    reg(chAlpha,"hudedit_crosshair_alpha","255");
    reg(chDynamic,"hudedit_crosshair_dynamic","0");
    reg(chTStyle,"hudedit_crosshair_t","0");
    reg(chUseTheme,"hudedit_crosshair_use_theme","1");
    reg(chRed,"hudedit_crosshair_r","40");
    reg(chGreen,"hudedit_crosshair_g","230");
    reg(chBlue,"hudedit_crosshair_b","100");
    reg(chOutlineRed,"hudedit_crosshair_outline_r","0");
    reg(chOutlineGreen,"hudedit_crosshair_outline_g","0");
    reg(chOutlineBlue,"hudedit_crosshair_outline_b","0");
    addon_log::write("hudedit",failedRegistrationCount==0?
        "settings complete: all CVar registrations returned pointers":
        "settings complete: some CVar registrations returned NULL");
}
bool canDraw() {
    return yes(active) && gEngfuncs.pfnGetScreenInfo &&
           gEngfuncs.pfnFillRGBA && gEngfuncs.pfnDrawConsoleString;
}
void fill(int x,int y,int w,int h,RGB color) {
    if (w>0 && h>0 && gEngfuncs.pfnFillRGBA)
        gEngfuncs.pfnFillRGBA(x,y,w,h,color.r,color.g,color.b,byte(val(opacity,255)));
}
void consoleText(int x,int y,const char* s,RGB color) {
    if (gEngfuncs.pfnDrawSetTextColor)
        gEngfuncs.pfnDrawSetTextColor(color.r/255.f,color.g/255.f,color.b/255.f);
    gEngfuncs.pfnDrawConsoleString(x,y,const_cast<char*>(s));
}
void plusIcon(int x,int y,RGB c) {
    fill(x+7,y,7,22,c);fill(x,y+7,21,8,c);
}
void shieldIcon(int x,int y,RGB c) {
    fill(x,y,20,3,c);fill(x,y+3,3,13,c);fill(x+17,y+3,3,13,c);
    fill(x+3,y+16,14,3,c);fill(x+7,y+19,6,3,c);
}
void segmentDigit(int x,int y,int digit,int sz,RGB color) {
    // Пиксельный шрифт из семи сегментов, масштабируется.
    static constexpr unsigned char segments[10] = {
        0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    if (digit<0||digit>9) return;
    const int t=std::max(1,sz), w=4*t, h=7*t;
    const auto s=segments[digit];
    if(s&0x01)fill(x+t,y,w,t,color);
    if(s&0x02)fill(x+w+t,y+t,t,3*t,color);
    if(s&0x04)fill(x+w+t,y+4*t,t,3*t,color);
    if(s&0x08)fill(x+t,y+h,w,t,color);
    if(s&0x10)fill(x,y+4*t,t,3*t,color);
    if(s&0x20)fill(x,y+t,t,3*t,color);
    if(s&0x40)fill(x+t,y+3*t,w,t,color);
}
void number(int x,int y,int value,RGB color,bool pixels,int scale) {
    char digits[32]={};
    std::snprintf(digits,sizeof(digits),"%d",value);
    if (!pixels) { consoleText(x,y,digits,color); return; }
    const int count=static_cast<int>(std::strlen(digits));
    for(int i=0;i<count;++i) {
        if(digits[i]=='-')fill(x+i*7*scale,y+4*scale,4*scale,scale,color);
        else segmentDigit(x+i*7*scale,y,digits[i]-'0',scale,color);
    }
}
void restoreSettings() {
    if (!gEngfuncs.Cvar_SetValue) return;
    if (suppressingOriginalSpeed) {
        gEngfuncs.Cvar_SetValue("sbxt_hud_speedometer",savedSpeedState);
        suppressingOriginalSpeed=false;
    }
    if (suppressingOriginalHud) {
        gEngfuncs.Cvar_SetValue("hud_draw",savedHudState);
        suppressingOriginalHud=false;
    }
    if(suppressingNativeCrosshair) {
        gEngfuncs.Cvar_SetValue("crosshair",savedNativeCrosshair);
        suppressingNativeCrosshair=false;
    }
}
void syncVisibility() {
    if (!gEngfuncs.Cvar_SetValue || !gEngfuncs.pfnGetCvarPointer) return;
    const bool needSpeed=yes(active)&&yes(speedEnable);
    if (auto* c=gEngfuncs.pfnGetCvarPointer("sbxt_hud_speedometer")) {
        if (needSpeed && !suppressingOriginalSpeed) {
            savedSpeedState=c->value;
            gEngfuncs.Cvar_SetValue("sbxt_hud_speedometer",0);
            suppressingOriginalSpeed=true;
        } else if (!needSpeed && suppressingOriginalSpeed) {
            gEngfuncs.Cvar_SetValue("sbxt_hud_speedometer",savedSpeedState);
            suppressingOriginalSpeed=false;
        }
    }
    const bool needCrosshair=yes(active)&&yes(chEnabled);
    if(auto* c=gEngfuncs.pfnGetCvarPointer("crosshair")) {
        if(needCrosshair && !suppressingNativeCrosshair) {
            savedNativeCrosshair=c->value;
            gEngfuncs.Cvar_SetValue("crosshair",0);
            suppressingNativeCrosshair=true;
        } else if(!needCrosshair && suppressingNativeCrosshair) {
            gEngfuncs.Cvar_SetValue("crosshair",savedNativeCrosshair);
            suppressingNativeCrosshair=false;
        }
    }
    // ВНИМАНИЕ: hud_draw отключает стандартный HUD целиком, включая ammo/map HUD.
    // Это общий режим замены; выборочное скрытие stock icons пока не обещаем.
    const bool needHide=yes(active)&&yes(hideStock);
    if (auto* c=gEngfuncs.pfnGetCvarPointer("hud_draw")) {
        if (needHide && !suppressingOriginalHud) {
            savedHudState=c->value;gEngfuncs.Cvar_SetValue("hud_draw",0);
            suppressingOriginalHud=true;
        } else if(!needHide && suppressingOriginalHud) {
            gEngfuncs.Cvar_SetValue("hud_draw",savedHudState);
            suppressingOriginalHud=false;
        }
    }
}
// All values control *visual* overlay only. Never modify viewangles or commands.
void drawCrosshair(int screenW,int screenH) {
    if((!yes(chEnabled) && !yes(chPreview)) || !yes(active) ||
       !gEngfuncs.pfnFillRGBA) return;
    const int x=screenW/2,y=screenH/2;
    const int size=std::max(1,std::min(100,static_cast<int>(val(chSize,12))));
    const int thick=std::max(1,std::min(20,static_cast<int>(val(chThickness,2))));
    const int extra=yes(chDynamic) ? std::min(25,static_cast<int>(std::fabs(speed)*.025f)) : 0;
    const int gap=std::max(0,std::min(100,static_cast<int>(val(chGap,5))+extra));
    const int alpha=byte(val(chAlpha,255));
    const RGB color=styled(yes(chUseTheme)||yes(forceAllColors)||globalRainbow(),chRed,chGreen,chBlue);
    const RGB outline={byte(val(chOutlineRed)),byte(val(chOutlineGreen)),byte(val(chOutlineBlue))};
    auto bar=[&](int bx,int by,int w,int h) {
        if(yes(chOutline)) gEngfuncs.pfnFillRGBA(bx-1,by-1,w+2,h+2,outline.r,outline.g,outline.b,alpha);
        gEngfuncs.pfnFillRGBA(bx,by,w,h,color.r,color.g,color.b,alpha);
    };
    // Static 4-way reticle; optional CS-style T shape.
    bar(x-gap-size,y-thick/2,size,thick);
    bar(x+gap,y-thick/2,size,thick);
    if(!yes(chTStyle))bar(x-thick/2,y-gap-size,thick,size);
    bar(x-thick/2,y+gap,thick,size);
    if(yes(chDot))bar(x-thick/2,y-thick/2,thick,thick);
}
void drawOverlay() {
    if (!canDraw()) return;
    SCREENINFO screen={};
    screen.iSize=sizeof(screen);
    if (!gEngfuncs.pfnGetScreenInfo(&screen)||screen.iWidth<=0||screen.iHeight<=0) return;
    drawingCustomOverlay=true;
    const RGB theme=mainColor();
    const int scale=std::max(1,std::min(5,static_cast<int>(val(speedScale,2))));
    if (yes(healthCustom) && health>=0) {
        const bool danger=yes(criticalPriority) && !globalRainbow() &&
            (health <= val(critical,25) || now()<damagedUntil);
        const RGB numberColor=danger && !yes(forceAllColors)?lowColor():styled(yes(forceAllColors)||globalRainbow()||yes(healthNTheme),healthNR,healthNG,healthNB);
        const RGB iconColor=danger && !yes(forceAllColors)?lowColor():styled(yes(forceAllColors)||globalRainbow()||yes(healthITheme),healthIR,healthIG,healthIB);
        if(yes(healthNumber))number(coord(healthNX,65),coord(healthNY,screen.iHeight-60),
                                   health,numberColor,true,2);
        if(yes(healthIcon))plusIcon(coord(healthIX,25),coord(healthIY,screen.iHeight-55),
                                   iconColor);
    }
    if (yes(armorCustom) && armor>=0) {
        if(yes(armorNumber))number(coord(armorNX,205),coord(armorNY,screen.iHeight-60),
                                  armor,styled(yes(forceAllColors)||globalRainbow()||yes(armorNTheme),armorNR,armorNG,armorNB),true,2);
        if(yes(armorIcon))shieldIcon(coord(armorIX,165),coord(armorIY,screen.iHeight-55),
                                   styled(yes(forceAllColors)||globalRainbow()||yes(armorITheme),armorIR,armorIG,armorIB));
    }
    if(yes(speedEnable)) {
        const int x=coord(speedX,screen.iWidth/2-22);
        const int y=coord(speedY,screen.iHeight-77);
        RGB speedColor=(yes(speedTheme)||yes(forceAllColors)||globalRainbow())?theme:RGB{byte(val(speedR,255)),
                               byte(val(speedG,255)),byte(val(speedB,255))};
        number(x,y,static_cast<int>(std::lround(speed)),speedColor,
               val(speedStyle,1)>=.5f,scale);
        if(yes(speedLabel))consoleText(x,y+scale*9,"u/s",speedColor);
    }
    drawCrosshair(screen.iWidth,screen.iHeight);
    drawingCustomOverlay=false;
}
void onView(ref_params_t* params) {
    if (oldView) oldView(params);
    if (params) speed=std::hypot(params->simvel[0],params->simvel[1]);
}
int onRedraw(float t,int intermission) {
    syncVisibility();
    cachedScreenHeight=0;
    if(gEngfuncs.pfnGetScreenInfo) {
        SCREENINFO info={};
        info.iSize=sizeof(info);
        if(gEngfuncs.pfnGetScreenInfo(&info)) {
            cachedScreenHeight=info.iHeight;cachedScreenWidth=info.iWidth;
        }
    }
    insideHudRedraw=!intermission;
    const int result=oldRedraw?oldRedraw(t,intermission):1;
    insideHudRedraw=false;
    if (!intermission) drawOverlay();
    return result;
}
void onHudInit() {
    if(oldInit)oldInit();
    if(!settingsRegistered) {
        settings();
        settingsRegistered=true;
    }
    if(!colorCommandRegistered && gEngfuncs.pfnAddCommand) {
        gEngfuncs.pfnAddCommand("hudedit_color",&changeColorCommand);
        colorCommandRegistered=true;
    }
    if(!msgHooksInstalled && g_pMetaHookAPI && g_pMetaHookAPI->HookUserMsg) {
        oldHealthMessage=g_pMetaHookAPI->HookUserMsg("Health",&onHealth);
        oldArmorMessage=g_pMetaHookAPI->HookUserMsg("Battery",&onArmor);
        msgHooksInstalled=true;
    }
}
void cleanup() {
    addon_log::write("hudedit","cleanup");
    restoreSettings();
    // При выгрузке DLL нельзя оставлять указатели usermsg-хуков в нашей памяти.
    if (msgHooksInstalled && g_pMetaHookAPI && g_pMetaHookAPI->HookUserMsg) {
        if (oldHealthMessage) g_pMetaHookAPI->HookUserMsg("Health",oldHealthMessage);
        if (oldArmorMessage) g_pMetaHookAPI->HookUserMsg("Battery",oldArmorMessage);
    }
    if (g_pMetaHookAPI) {
        if(redrawHook)g_pMetaHookAPI->UnHook(redrawHook);
        if(initHook)g_pMetaHookAPI->UnHook(initHook);
        if(viewHook)g_pMetaHookAPI->UnHook(viewHook);
        if(spriteHook)g_pMetaHookAPI->UnHook(spriteHook);
        if(scaleHook)g_pMetaHookAPI->UnHook(scaleHook);
        if(drawHook)g_pMetaHookAPI->UnHook(drawHook);
        if(drawAdditiveHook)g_pMetaHookAPI->UnHook(drawAdditiveHook);
        if(drawHolesHook)g_pMetaHookAPI->UnHook(drawHolesHook);
        if(textColorHook)g_pMetaHookAPI->UnHook(textColorHook);
        if(consoleHook)g_pMetaHookAPI->UnHook(consoleHook);
        if(fillHook)g_pMetaHookAPI->UnHook(fillHook);
    }
    redrawHook=initHook=viewHook=spriteHook=scaleHook=nullptr;
    drawHook=drawAdditiveHook=drawHolesHook=textColorHook=nullptr;
    consoleHook=fillHook=nullptr;
    nativeHooksEnabled=false;
    oldRedraw=nullptr;oldInit=nullptr;oldView=nullptr;oldSprite=nullptr;oldScale=nullptr;
    oldDraw=nullptr;oldDrawAdditive=nullptr;oldDrawHoles=nullptr;oldTextColor=nullptr;
    oldConsole=nullptr;oldFill=nullptr;
    insideHudRedraw=false;drawingCustomOverlay=false;cachedScreenHeight=cachedScreenWidth=0;
    // User-message hooks are scoped to the client DLL's lifetime; they are not
    // replaced with invalid stale function pointers after client reload.
    msgHooksInstalled=false;oldHealthMessage=oldArmorMessage=nullptr;
    // The engine still owns the registered CVars/commands on level transitions.
    // Do not re-register them on the same host instance.
    health=armor=-1;speed=0;
}
} // namespace


namespace {
void onClientShutdown();
void stopHud() {
    addon_log::write("hudedit","hudedit_stop requested");
    if(hudSessionActive && pendingExports) {
        if(pendingExports->HUD_Redraw==&onRedraw)
            pendingExports->HUD_Redraw=oldRedraw;
        if(pendingExports->V_CalcRefdef==&onView)
            pendingExports->V_CalcRefdef=oldView;
        if(pendingExports->HUD_Shutdown==&onClientShutdown)
            pendingExports->HUD_Shutdown=originalShutdown;
    }
    hudSessionActive=false;
    cleanup();
    originalShutdown=nullptr;
}
void onClientShutdown() {
    addon_log::write("hudedit","client HUD_Shutdown");
    ShutdownFn saved=originalShutdown;
    stopHud();
    pendingExports=nullptr;
    if(saved) saved();
}
void startHud() {
    addon_log::write("hudedit","hudedit_start requested");
    if(hudSessionActive) return;
    if(!pendingExports) {
        addon_log::write("hudedit","start rejected: LoadClient not ready");
        return;
    }
    auto* exp=pendingExports;
    if(!exp->HUD_Redraw || !exp->V_CalcRefdef || !exp->HUD_Shutdown) {
        addon_log::write("hudedit","start rejected: missing client export");
        return;
    }
    oldRedraw=exp->HUD_Redraw;
    oldView=exp->V_CalcRefdef;
    originalShutdown=exp->HUD_Shutdown;
    if(oldRedraw==&onRedraw || oldView==&onView ||
       originalShutdown==&onClientShutdown) {
        addon_log::write("hudedit","start rejected: recursive callback");
        oldRedraw=nullptr;oldView=nullptr;originalShutdown=nullptr;
        return;
    }
    // Both callback pointers are replaced only AFTER game startup.
    exp->HUD_Redraw=&onRedraw;
    exp->V_CalcRefdef=&onView;
    exp->HUD_Shutdown=&onClientShutdown;
    hudSessionActive=true;
    wasEverActivated=true;
    // Existing usermessage callbacks may be registered by this time.
    // UserMsg hooks have a different lifecycle and may leave callbacks inside
    // client.dll after a plugin unload. They are intentionally opt-in below.
    addon_log::write("hudedit","hudedit_start client callbacks attached");
}
void enableMessages() {
    addon_log::write("hudedit","hudedit_messages_on requested");
    if(!hudSessionActive || msgHooksInstalled || !g_pMetaHookAPI ||
       !g_pMetaHookAPI->HookUserMsg) return;
    oldHealthMessage=g_pMetaHookAPI->HookUserMsg("Health",&onHealth);
    oldArmorMessage=g_pMetaHookAPI->HookUserMsg("Battery",&onArmor);
    msgHooksInstalled=true;
}
void enableNativeHooks() {
    addon_log::write("hudedit","hudedit_native_on requested");
    if(nativeHooksEnabled || !hudSessionActive || !g_pMetaHookAPI) {
        addon_log::write("hudedit","native hooks not activated: call hudedit_start first");
        return;
    }
    if(gEngfuncs.pfnSPR_Set)
        spriteHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnSPR_Set),
            reinterpret_cast<void*>(&onSprite),reinterpret_cast<void**>(&oldSprite));
    if(gEngfuncs.pfnSPR_Draw)
        drawHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnSPR_Draw),
            reinterpret_cast<void*>(&onDraw),reinterpret_cast<void**>(&oldDraw));
    if(gEngfuncs.pfnSPR_DrawAdditive)
        drawAdditiveHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnSPR_DrawAdditive),
            reinterpret_cast<void*>(&onDrawAdditive),reinterpret_cast<void**>(&oldDrawAdditive));
    if(gEngfuncs.pfnSPR_DrawHoles)
        drawHolesHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnSPR_DrawHoles),
            reinterpret_cast<void*>(&onDrawHoles),reinterpret_cast<void**>(&oldDrawHoles));
    if(gEngfuncs.pfnDrawSetTextColor)
        textColorHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnDrawSetTextColor),
            reinterpret_cast<void*>(&onTextColor),reinterpret_cast<void**>(&oldTextColor));
    if(gEngfuncs.pfnDrawConsoleString)
        consoleHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnDrawConsoleString),
            reinterpret_cast<void*>(&onConsole),reinterpret_cast<void**>(&oldConsole));
    if(gEngfuncs.pfnFillRGBA)
        fillHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnFillRGBA),
            reinterpret_cast<void*>(&onFill),reinterpret_cast<void**>(&oldFill));
    nativeHooksEnabled=bool(spriteHook||drawHook||drawAdditiveHook||
        drawHolesHook||textColorHook||consoleHook||fillHook);
    addon_log::write("hudedit",nativeHooksEnabled?
        "native hooks installed (experimental)":"native hooks unavailable");
}
void disableNativeHooks() {
    addon_log::write("hudedit","hudedit_native_off requested");
    if(!g_pMetaHookAPI) return;
    for(hook_t* h : {spriteHook,drawHook,drawAdditiveHook,drawHolesHook,
                    textColorHook,consoleHook,fillHook}) {
        if(h) g_pMetaHookAPI->UnHook(h);
    }
    spriteHook=drawHook=drawAdditiveHook=drawHolesHook=textColorHook=consoleHook=fillHook=nullptr;
    oldSprite=nullptr;oldDraw=nullptr;oldDrawAdditive=nullptr;
    oldDrawHoles=nullptr;oldTextColor=nullptr;oldConsole=nullptr;oldFill=nullptr;
    nativeHooksEnabled=false;
}
} // namespace

namespace {
// Initialize advanced cvars ONLY after Sven Co-op main menu has opened,
// and the player explicitly enters hudedit_init.

void hudeditStatus() {
    const bool init=settingsRegistered;
    addon_log::write("hudedit",init ?
        "STATUS: settingsRegistered=1" : "STATUS: settingsRegistered=0");
    size_t missing=0;
    const char* keys[]={"hudedit_version","hudedit_inventory_x","hudedit_inventory_visible",
        "hudedit_flashlight_r","hudedit_speed_x","hudedit_crosshair_size"};
    if(gEngfuncs.Con_Printf)
        gEngfuncs.Con_Printf("HUD Edit: init %s; CVars=%u; failures=%u; overlay=%s\n",
            init?"complete":"NOT complete",
            static_cast<unsigned>(registeredCount),
            static_cast<unsigned>(failedRegistrationCount),
            hudSessionActive?"on":"off");
    for(const char* name:keys) {
        cvar_t* enginePtr=gEngfuncs.pfnGetCvarPointer ?
            gEngfuncs.pfnGetCvarPointer(name) : nullptr;
        cvar_t* storedPtr=nullptr;
        for(size_t i=0;i<registeredCount;++i) {
            if(registeredCVars[i].name &&
               std::strcmp(name,registeredCVars[i].name)==0) {
                storedPtr=registeredCVars[i].ptr;
                break;
            }
        }
        if(!enginePtr || !storedPtr) ++missing;
        if(gEngfuncs.Con_Printf)
            gEngfuncs.Con_Printf("HUD Edit: %-31s registered=%s engine_lookup=%s\n",
                name,storedPtr?"yes":"NO",enginePtr?"yes":"NO");
        if(!enginePtr || !storedPtr)
            addon_log::write("hudedit","STATUS: missing registered CVar or engine lookup");
    }
    if(gEngfuncs.Con_Printf)
        gEngfuncs.Con_Printf(missing ?
            "HUD Edit: missing CVars; send SvenBXT_addons_boot.log\n" :
            "HUD Edit: core CVars found. Use hudedit_start for overlay.\n");
}

void hudeditSet() {
    if(!gEngfuncs.Cmd_Argc || !gEngfuncs.Cmd_Argv ||
       gEngfuncs.Cmd_Argc()!=3) {
        if(gEngfuncs.Con_Printf)
            gEngfuncs.Con_Printf("Usage: hudedit_set inventory_x 200 (or hudedit_inventory_x 200)\n");
        return;
    }
    const char* shortName=gEngfuncs.Cmd_Argv(1);
    const char* requested=gEngfuncs.Cmd_Argv(2);
    if(!shortName||!requested) return;
    std::string name=shortName;
    if(name.rfind("hudedit_",0)!=0) name="hudedit_"+name;
    char* tail=nullptr;
    float value=std::strtof(requested,&tail);
    if(tail==requested || *tail || !std::isfinite(value) ||
       value < -30000.0f || value > 30000.0f) {
        if(gEngfuncs.Con_Printf)
            gEngfuncs.Con_Printf("HUD Edit: invalid number\n");
        return;
    }
    cvar_t* target=nullptr;
    for(size_t i=0;i<registeredCount;++i) {
        if(registeredCVars[i].name && name==registeredCVars[i].name) {
            target=registeredCVars[i].ptr;
            break;
        }
    }
    if(!target) {
        if(gEngfuncs.Con_Printf)
            gEngfuncs.Con_Printf("HUD Edit: %s not registered; run hudedit_init\n",name.c_str());
        return;
    }
    if(gEngfuncs.Cvar_SetValue)
        gEngfuncs.Cvar_SetValue(name.c_str(),value);
    bool temporary=false;
    if(std::fabs(target->value-value)>.001f) {
        // The console CVar resolver may be incompatible with the MetaHook
        // client registration on this Sven Co-op build. Keep the in-memory
        // control usable; this direct fallback is NOT archived/persistent.
        target->value=value;
        temporary=true;
        addon_log::write("hudedit","hudedit_set used temporary pointer fallback");
    }
    if(gEngfuncs.Con_Printf)
        gEngfuncs.Con_Printf("HUD Edit: %s = %.3f%s\n",name.c_str(),
            target->value,temporary?" (temporary, not saved)":"");
}
void hudeditHelp() {
    if(gEngfuncs.Con_Printf)
        gEngfuncs.Con_Printf(
            "HUD Edit: hudedit_init, hudedit_status, hudedit_help, hudedit_set NAME VALUE, hudedit_color R G B, hudedit_start, hudedit_stop\n"
            "Inventory: hudedit_inventory_x/y and hudedit_inventory_visible 0/1\n"
            "Flashlight: hudedit_flashlight_enabled 1, hudedit_flashlight_use_theme 0, hudedit_flashlight_r/g/b\n");
}
void lazyInitialize() {
    addon_log::write("hudedit","hudedit_init invoked");
    if(!settingsRegistered) {
        registeredCount=0;
        failedRegistrationCount=0;
    }
    if(!settingsRegistered) {
        if(!gEngfuncs.pfnRegisterVariable) {
            addon_log::write("hudedit","hudedit_init no CVar registration API");
            return;
        }
        settings();
        settingsRegistered=(failedRegistrationCount==0);
        if(!settingsRegistered)
            addon_log::write("hudedit","ERROR: incomplete CVar setup, retry hudedit_init");
    }
    if(!colorCommandRegistered && gEngfuncs.pfnAddCommand) {
        addon_log::write("hudedit","hudedit_init before extra commands");
        gEngfuncs.pfnAddCommand("hudedit_color",&changeColorCommand);
        gEngfuncs.pfnAddCommand("hudedit_start",&startHud);
        gEngfuncs.pfnAddCommand("hudedit_stop",&stopHud);
        gEngfuncs.pfnAddCommand("hudedit_messages_on",&enableMessages);
        gEngfuncs.pfnAddCommand("hudedit_native_on",&enableNativeHooks);
        gEngfuncs.pfnAddCommand("hudedit_native_off",&disableNativeHooks);
        gEngfuncs.pfnAddCommand("hudedit_set",&hudeditSet);
        colorCommandRegistered=true;
        addon_log::write("hudedit","hudedit_init extra commands complete");
    }
    addon_log::write("hudedit",settingsRegistered ?
        "hudedit_init completed: CVars registered" :
        "hudedit_init completed: CVAR REGISTRATION INCOMPLETE");
    hudeditStatus();
}
} // namespace

void IPluginsV4::Init(metahook_api_t* api,mh_interface_t* i,mh_enginesave_t* save) {
    g_pMetaHookAPI=api;g_pInterface=i;g_pMetaSave=save;
    addon_log::write("hudedit","V4 Init");
}
void IPluginsV4::LoadEngine(cl_enginefunc_t* engine) {
    addon_log::write("hudedit","LoadEngine entry");
    if(!engine) {
        addon_log::write("hudedit","LoadEngine engine ptr null");
        return;
    }
    gEngfuncs=*engine;
    // No calls to engine pfnRegisterVariable or pfnAddCommand in LoadEngine.
    // These were the only actions between begin and the failed startup log.
    addon_log::write("hudedit","LoadEngine completed (zero registrations)");
}
void IPluginsV4::LoadClient(cl_exportfuncs_t* exports) {
    addon_log::write("hudedit","LoadClient entry");
    pendingExports=exports;
    if(!safeCommandsRegistered && gEngfuncs.pfnAddCommand) {
        addon_log::write("hudedit","LoadClient before diagnostic commands");
        gEngfuncs.pfnAddCommand("hudedit_init",&lazyInitialize);
        gEngfuncs.pfnAddCommand("hudedit_status",&hudeditStatus);
        gEngfuncs.pfnAddCommand("hudedit_help",&hudeditHelp);
        safeCommandsRegistered=true;
        addon_log::write("hudedit","LoadClient init/status/help commands registered");
    }
    addon_log::write("hudedit","LoadClient completed (no engine hooks)");
}
void IPluginsV4::ExitGame(int) {
    addon_log::write("hudedit","V4 ExitGame");
    // The HUD_Shutdown callback normally performs the safe restore.
    // Do not dereference a potentially unloaded client export table here.
    cleanup();
    pendingExports=nullptr;hudSessionActive=false;
}
void IPluginsV4::Shutdown() {
    addon_log::write("hudedit","V4 Shutdown");
    cleanup();
    pendingExports=nullptr;hudSessionActive=false;
}
const char* IPluginsV4::GetVersion() { return "2.2.0-diagnostic"; }
EXPOSE_SINGLE_INTERFACE(IPluginsV4,IPluginsV4,METAHOOK_PLUGIN_API_VERSION_V4);

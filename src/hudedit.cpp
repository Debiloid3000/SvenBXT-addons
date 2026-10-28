#include <metahook.h>
#include <cvardef.h>
#include <ref_params.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "hudedit_layout.hpp"

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

hook_t* redrawHook = nullptr;
hook_t* initHook = nullptr;
hook_t* viewHook = nullptr;
hook_t* spriteHook = nullptr;
hook_t* scaleHook = nullptr;
hook_t* drawHook = nullptr;
hook_t* drawAdditiveHook = nullptr;
hook_t* drawHolesHook = nullptr;
hook_t* textColorHook = nullptr;
RedrawFn oldRedraw = nullptr;
InitFn oldInit = nullptr;
ViewFn oldView = nullptr;
SprFn oldSprite = nullptr;
ScaleFn oldScale = nullptr;
SprDrawFn oldDraw = nullptr;
SprDrawAdditiveFn oldDrawAdditive = nullptr;
SprDrawHolesFn oldDrawHoles = nullptr;
TextColorFn oldTextColor = nullptr;
pfnUserMsgHook oldHealthMessage = nullptr;
pfnUserMsgHook oldArmorMessage = nullptr;
bool msgHooksInstalled = false;
bool settingsRegistered = false;
bool colorCommandRegistered = false;
bool insideHudRedraw = false;
int cachedScreenHeight = 0;

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

int health = -1, armor = -1;
float speed = 0, damagedUntil = 0;
bool suppressingOriginalSpeed = false, suppressingOriginalHud = false;
float savedSpeedState = 1, savedHudState = 1;

float now() { return gEngfuncs.GetClientTime ? gEngfuncs.GetClientTime() : 0.f; }
float val(cvar_t* c, float fallback=0) { return c ? c->value : fallback; }
bool yes(cvar_t* c) { return val(c) >= .5f; }
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
    return !yes(forceAllColors) && ((health >= 0 && health <= val(critical,25)) || now() < damagedUntil)
        ? lowColor() : mainColor();
}
void transform(int& red,int& green,int& blue) {
    if (!yes(active)) return;
    RGB c;
    if (yes(forceAllColors)) c=mainColor();
    else if (red==100 && green==130 && blue==200) c=mainColor();
    else if (red==250 && green==0 && blue==0) c=lowColor();
    else return;
    red=c.r;green=c.g;blue=c.b;
}
void onSprite(int sprite, int red, int green, int blue) {
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
    if (insideHudRedraw && yes(active) && yes(forceAllColors)) {
        const RGB tint=mainColor();
        red=tint.r/255.f; green=tint.g/255.f; blue=tint.b/255.f;
    }
    if (oldTextColor) oldTextColor(red,green,blue);
}
int inventoryY(int x,int y) {
    const hudedit::InventoryRegion region {
        static_cast<int>(val(inventoryRegionX,0)),
        static_cast<int>(val(inventoryRegionY,0)),
        static_cast<int>(val(inventoryRegionW,210)),
        static_cast<int>(val(inventoryRegionH,170)),
        static_cast<int>(val(inventoryMargin,12))
    };
    // Опциональная экспериментальная зона инвентаря, не весь HUD!
    return hudedit::inventory_sprite_y(x,y,cachedScreenHeight,region,
                insideHudRedraw && yes(active) && yes(inventoryBottom));
}
void onDraw(int frame,int x,int y,const wrect_t* rect) {
    if (oldDraw) oldDraw(frame,x,inventoryY(x,y),rect);
}
void onDrawAdditive(int frame,int x,int y,const wrect_t* rect) {
    if (oldDrawAdditive) oldDrawAdditive(frame,x,inventoryY(x,y),rect);
}
void onDrawHoles(int frame,int x,int y,const wrect_t* rect) {
    if (oldDrawHoles) oldDrawHoles(frame,x,inventoryY(x,y),rect);
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
    if (gEngfuncs.pfnRegisterVariable)
        dest=gEngfuncs.pfnRegisterVariable(name,value,FCVAR_ARCHIVE);
}
void settings() {
    reg(active,"hudedit_enabled","1");
    reg(r,"hudedit_r","100"); reg(g,"hudedit_g","130"); reg(b,"hudedit_b","200");
    reg(lowR,"hudedit_critical_r","250");reg(lowG,"hudedit_critical_g","25");
    reg(lowB,"hudedit_critical_b","45");reg(critical,"hudedit_critical_hp","25");
    reg(rainbow,"hudedit_rainbow","0");reg(rainbowSpeed,"hudedit_rainbow_speed","0.10");
    reg(flashTime,"hudedit_damage_flash","0.5");
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
void drawOverlay() {
    if (!canDraw()) return;
    SCREENINFO screen={};
    screen.iSize=sizeof(screen);
    if (!gEngfuncs.pfnGetScreenInfo(&screen)||screen.iWidth<=0||screen.iHeight<=0) return;
    const RGB theme=mainColor();
    const int scale=std::max(1,std::min(5,static_cast<int>(val(speedScale,2))));
    if (yes(healthCustom) && health>=0) {
        if(yes(healthNumber))number(coord(healthNX,65),coord(healthNY,screen.iHeight-60),
                                   health,currentHealthColor(),true,2);
        if(yes(healthIcon))plusIcon(coord(healthIX,25),coord(healthIY,screen.iHeight-55),
                                   currentHealthColor());
    }
    if (yes(armorCustom) && armor>=0) {
        if(yes(armorNumber))number(coord(armorNX,205),coord(armorNY,screen.iHeight-60),
                                  armor,theme,true,2);
        if(yes(armorIcon))shieldIcon(coord(armorIX,165),coord(armorIY,screen.iHeight-55),theme);
    }
    if(yes(speedEnable)) {
        const int x=coord(speedX,screen.iWidth/2-22);
        const int y=coord(speedY,screen.iHeight-77);
        RGB speedColor=yes(speedTheme)?theme:RGB{byte(val(speedR,255)),
                               byte(val(speedG,255)),byte(val(speedB,255))};
        number(x,y,static_cast<int>(std::lround(speed)),speedColor,
               val(speedStyle,1)>=.5f,scale);
        if(yes(speedLabel))consoleText(x,y+scale*9,"u/s",speedColor);
    }
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
        if(gEngfuncs.pfnGetScreenInfo(&info)) cachedScreenHeight=info.iHeight;
    }
    insideHudRedraw=!intermission;
    const int result=oldRedraw?oldRedraw(t,intermission):1;
    insideHudRedraw=false;
    if (!intermission) drawOverlay();
    return result;
}
void onHudInit() {
    if(oldInit)oldInit();
    // Регистрируем настройки только после готовности клиентского HUD.
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
    }
    redrawHook=initHook=viewHook=spriteHook=scaleHook=nullptr;
    drawHook=drawAdditiveHook=drawHolesHook=textColorHook=nullptr;
    oldRedraw=nullptr;oldInit=nullptr;oldView=nullptr;oldSprite=nullptr;oldScale=nullptr;
    oldDraw=nullptr;oldDrawAdditive=nullptr;oldDrawHoles=nullptr;oldTextColor=nullptr;
    insideHudRedraw=false;cachedScreenHeight=0;
    // User-message hooks are scoped to the client DLL's lifetime; they are not
    // replaced with invalid stale function pointers after client reload.
    msgHooksInstalled=false;oldHealthMessage=oldArmorMessage=nullptr;
    settingsRegistered=false;
    health=armor=-1;speed=0;
}
} // namespace

void IPluginsV4::Init(metahook_api_t* api,mh_interface_t* i,mh_enginesave_t* save) {
    g_pMetaHookAPI=api;g_pInterface=i;g_pMetaSave=save;
}
void IPluginsV4::LoadEngine(cl_enginefunc_t* engine) {
    if (!engine) return;
    gEngfuncs=*engine;
    if (!g_pMetaHookAPI) return;
    if (gEngfuncs.pfnSPR_Set && !spriteHook)
        spriteHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnSPR_Set),
            reinterpret_cast<void*>(&onSprite),reinterpret_cast<void**>(&oldSprite));
    if (gEngfuncs.pfnSPR_Draw && !drawHook)
        drawHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnSPR_Draw),
            reinterpret_cast<void*>(&onDraw),reinterpret_cast<void**>(&oldDraw));
    if (gEngfuncs.pfnSPR_DrawAdditive && !drawAdditiveHook)
        drawAdditiveHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnSPR_DrawAdditive),
            reinterpret_cast<void*>(&onDrawAdditive),reinterpret_cast<void**>(&oldDrawAdditive));
    if (gEngfuncs.pfnSPR_DrawHoles && !drawHolesHook)
        drawHolesHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnSPR_DrawHoles),
            reinterpret_cast<void*>(&onDrawHoles),reinterpret_cast<void**>(&oldDrawHoles));
    if (gEngfuncs.pfnDrawSetTextColor && !textColorHook)
        textColorHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(gEngfuncs.pfnDrawSetTextColor),
            reinterpret_cast<void*>(&onTextColor),reinterpret_cast<void**>(&oldTextColor));
}
void IPluginsV4::LoadClient(cl_exportfuncs_t* exports) {
    if(!g_pMetaHookAPI||!exports) return;
    if(exports->HUD_Init && !initHook)
        initHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(exports->HUD_Init),
            reinterpret_cast<void*>(&onHudInit),reinterpret_cast<void**>(&oldInit));
    if(exports->HUD_Redraw && !redrawHook)
        redrawHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(exports->HUD_Redraw),
            reinterpret_cast<void*>(&onRedraw),reinterpret_cast<void**>(&oldRedraw));
    if(exports->V_CalcRefdef && !viewHook)
        viewHook=g_pMetaHookAPI->InlineHook(
            reinterpret_cast<void*>(exports->V_CalcRefdef),
            reinterpret_cast<void*>(&onView),reinterpret_cast<void**>(&oldView));

    // Современный MetaHook: использует символ из gamedata, без старых x86 offsets.
    if(!scaleHook && g_pMetaHookAPI->ResolveGameSymbol && g_pMetaHookAPI->GetClientBase) {
        void* target=nullptr;
        const auto status=g_pMetaHookAPI->ResolveGameSymbol(
            g_pMetaHookAPI->GetClientBase(),"R_ScaleColor",
            MH_GAMESYMBOL_KIND_FUNCTION,&target);
        if(status==MH_GAMESYMBOL_OK && target)
            scaleHook=g_pMetaHookAPI->InlineHook(target,reinterpret_cast<void*>(&onScale),
                                                reinterpret_cast<void**>(&oldScale));
    }
}
void IPluginsV4::ExitGame(int) { cleanup(); }
void IPluginsV4::Shutdown() { cleanup(); }
const char* IPluginsV4::GetVersion() { return "0.1.0"; }
EXPOSE_SINGLE_INTERFACE(IPluginsV4,IPluginsV4,METAHOOK_PLUGIN_API_VERSION_V4);

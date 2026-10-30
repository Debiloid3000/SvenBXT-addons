// Native Options pages. Panels are created only when Options is opened and
// all VGUI interfaces have been verified, never at DLL/engine load time.
#include <metahook.h>
#include <cvardef.h>
#include <IVGUI2Extension.h>
#include <ISurface2.h>
#include <IScheme2.h>
#include <IInput2.h>
#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/CvarTextEntry.h>
#include <vgui_controls/CvarToggleCheckButton.h>
#include <vgui_controls/Controls.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstring>
#include "startup_diagnostics.hpp"

extern cl_enginefunc_t gEngfuncs;
extern metahook_api_t* g_pMetaHookAPI;
extern mh_interface_t* g_pInterface;
extern IKeyValuesSystem* g_pKeyValuesSystem;
vgui::ISurface2* g_pVGuiSurface2=nullptr;
vgui::ISchemeManager2* g_pVGuiSchemeManager2=nullptr;
vgui::IInput2* g_pVGuiInput2=nullptr;

namespace {
IVGUI2Extension* extension=nullptr;
HMODULE extensionModule=nullptr;
bool controlsReady=false;
CreateInterfaceFn moduleFactory(HMODULE module) {
    return module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface")):nullptr;
}
IBaseInterface* fileSystemFactory(const char* version,int*) {
    return g_pInterface && !std::strcmp(version,FILESYSTEM_INTERFACE_VERSION)?g_pInterface->FileSystem:nullptr;
}
bool initializeControls() {
    if(controlsReady) return true;
    if(!extension || !g_pMetaHookAPI || !g_pMetaHookAPI->GetEngineFactory ||
       !g_pInterface || !g_pInterface->FileSystem) return false;
    const auto extFactory=moduleFactory(extensionModule);
    const auto vguiFactory=moduleFactory(GetModuleHandleA("vgui2.dll"));
    const auto engineFactory=g_pMetaHookAPI->GetEngineFactory();
    if(!extFactory || !vguiFactory || !engineFactory) return false;
    g_pVGuiSurface2=static_cast<vgui::ISurface2*>(extFactory(VGUI_SURFACE2_INTERFACE_VERSION,nullptr));
    g_pVGuiSchemeManager2=static_cast<vgui::ISchemeManager2*>(extFactory(VGUI_SCHEME2_INTERFACE_VERSION,nullptr));
    g_pVGuiInput2=static_cast<vgui::IInput2*>(extFactory(VGUI_INPUT2_INTERFACE_VERSION,nullptr));
    g_pKeyValuesSystem=static_cast<IKeyValuesSystem*>(vguiFactory(KEYVALUESSYSTEM_INTERFACE_VERSION,nullptr));
    if(!g_pVGuiSurface2 || !g_pVGuiSchemeManager2 || !g_pVGuiInput2 || !g_pKeyValuesSystem) return false;
    // Controls.cpp requires engine, vgui2 and filesystem factories IN THIS ORDER.
    CreateInterfaceFn factories[]={engineFactory,vguiFactory,&fileSystemFactory};
    controlsReady=vgui::VGui_InitInterfacesList("SvenBXTHudEdit",factories,3);
    addon_log::write("hudedit",controlsReady?"Options: VGUI runtime ready":"Options: VGUI interfaces unavailable");
    return controlsReady;
}
float value(const char* name,float fallback=0) {
    auto* c=gEngfuncs.pfnGetCvarPointer?gEngfuncs.pfnGetCvarPointer(name):nullptr;
    return c?c->value:fallback;
}
int clamped(const char* name,int fallback,int low,int high) {
    return (std::max)(low,(std::min)(high,static_cast<int>(value(name,static_cast<float>(fallback)))));
}
class SettingsPage : public vgui::PropertyPage {
    DECLARE_CLASS_SIMPLE(SettingsPage,vgui::PropertyPage);
    std::vector<CCvarTextEntry*> entries;
    std::vector<CCvarToggleCheckButton*> checks;
public:
    SettingsPage(vgui::Panel* parent,const char* name):BaseClass(parent,name) { SetSize(512,406); }
    void OnApplyChanges() override {
        for(auto* c:checks) c->ApplyChanges();
        for(auto* c:entries) c->ApplyChanges();
    }
    void OnResetData() override {
        for(auto* c:checks) c->Reset();
        for(auto* c:entries) c->Reset();
    }
protected:
    void label(const char* text,int x,int y,int width=250) {
        auto* c=new vgui::Label(this,text,text);c->SetBounds(x,y,width,24);
    }
    void input(const char* title,const char* name,int x,int y) {
        label(title,x,y,140);
        auto* c=new CCvarTextEntry(this,name,name);c->SetBounds(x+145,y,72,23);
        c->SetEnabled(gEngfuncs.pfnGetCvarPointer && gEngfuncs.pfnGetCvarPointer(name));
        entries.push_back(c);
    }
    void check(const char* title,const char* name,int x,int y) {
        auto* c=new CCvarToggleCheckButton(this,name,title,name);c->SetBounds(x,y,235,24);
        c->SetEnabled(gEngfuncs.pfnGetCvarPointer && gEngfuncs.pfnGetCvarPointer(name));
        checks.push_back(c);
    }
};
class HudEditPage final : public SettingsPage {
public:
    explicit HudEditPage(vgui::Panel* parent):SettingsPage(parent,"HudEditOptions") {
        label("HUD colors and layout",20,10,450);
        check("HUD Edit enabled","hudedit_enabled",20,40);
        check("Rainbow","hudedit_rainbow",255,40);
        check("Color all HUD","hudedit_all_colors",20,68);
        check("Critical HP color","hudedit_critical_priority",255,68);
        input("Theme red","hudedit_r",20,103);
        input("Theme green","hudedit_g",20,133);
        input("Theme blue","hudedit_b",20,163);
        check("Inventory visible","hudedit_inventory_visible",255,103);
        input("Inventory X (-1:auto)","hudedit_inventory_x",255,133);
        input("Inventory Y (-1:auto)","hudedit_inventory_y",255,163);
        check("HUD Edit speedometer","hudedit_speed",20,202);
        label("Replaces SvenBXT speed only while enabled",20,228,465);
        input("Speed X (-1:auto)","hudedit_speed_x",20,263);
        input("Speed Y (-1:auto)","hudedit_speed_y",20,293);
        check("Custom HP number","hudedit_health",255,263);
        check("Custom armor number","hudedit_armor",255,293);
        check("Move inventory bottom","hudedit_inventory_bottom",20,334);
        check("Flashlight region","hudedit_flashlight_enabled",255,334);
    }
};
class CrosshairPage final : public SettingsPage {
public:
    explicit CrosshairPage(vgui::Panel* parent):SettingsPage(parent,"HudEditCrosshair") {
        check("Custom crosshair","hudedit_crosshair",20,20);
        check("Use HUD theme","hudedit_crosshair_use_theme",255,20);
        check("Center dot","hudedit_crosshair_dot",20,50);
        check("Outline","hudedit_crosshair_outline",255,50);
        check("T style","hudedit_crosshair_t",20,80);
        check("Dynamic gap","hudedit_crosshair_dynamic",255,80);
        input("Size","hudedit_crosshair_size",20,122);
        input("Gap","hudedit_crosshair_gap",20,152);
        input("Thickness","hudedit_crosshair_thickness",20,182);
        input("Alpha (0..255)","hudedit_crosshair_alpha",20,212);
        input("Red","hudedit_crosshair_r",255,122);
        input("Green","hudedit_crosshair_g",255,152);
        input("Blue","hudedit_crosshair_b",255,182);
        label("Preview (applied settings)",20,268,220);
    }
    void Paint() override {
        SettingsPage::Paint();
        if(!vgui::surface()) return;
        auto* s=vgui::surface();const int cx=360,cy=305;
        s->DrawSetColor(13,20,29,255);s->DrawFilledRect(cx-55,cy-55,cx+55,cy+55);
        const int size=clamped("hudedit_crosshair_size",12,1,35);
        const int gap=clamped("hudedit_crosshair_gap",5,0,20);
        const int thick=clamped("hudedit_crosshair_thickness",2,1,8);
        const bool theme=value("hudedit_crosshair_use_theme")>=.5f;
        s->DrawSetColor(clamped(theme?"hudedit_r":"hudedit_crosshair_r",40,0,255),
            clamped(theme?"hudedit_g":"hudedit_crosshair_g",230,0,255),
            clamped(theme?"hudedit_b":"hudedit_crosshair_b",100,0,255),255);
        s->DrawFilledRect(cx-gap-size,cy-thick/2,cx-gap,cy+thick/2+1);
        s->DrawFilledRect(cx+gap,cy-thick/2,cx+gap+size,cy+thick/2+1);
        if(value("hudedit_crosshair_t")<.5f)
            s->DrawFilledRect(cx-thick/2,cy-gap-size,cx+thick/2+1,cy-gap);
        s->DrawFilledRect(cx-thick/2,cy+gap,cx+thick/2+1,cy+gap+size);
        if(value("hudedit_crosshair_dot")>=.5f) s->DrawFilledRect(cx-1,cy-1,cx+2,cy+2);
    }
};
class MovementPage final : public SettingsPage {
public:
    explicit MovementPage(vgui::Panel* parent):SettingsPage(parent,"MovementOptions") {
        const bool loaded=gEngfuncs.pfnGetCvarPointer && gEngfuncs.pfnGetCvarPointer("sbxtma_mode");
        if(!loaded) { label("SvenBXTMovement.dll is not loaded",20,20,470);return; }
        check("Movement enabled","sbxtma_enabled",20,20);
        check("Notifications","sbxtma_notifications",255,20);
        input("Mode: 0 off/1 info/2 assist","sbxtma_mode",20,64);
        check("Assist in multiplayer","sbxtma_allow_online",255,64);
        check("Edgebug","sbxtma_edgebug",20,110);
        check("Jumpbug","sbxtma_jumpbug",255,110);
        check("Vectorial strafe","sbxtma_strafe",20,144);
        check("PixelSurf auto","sbxtma_pixelsurf",255,144);
        check("Object boost auto (4 FPS)","sbxtma_objectboost",20,178);
        check("Collision prediction","sbxtma_prediction",255,178);
        label("Manual binds: +sbxtma_pixelsurf / +sbxtma_objectboost",20,235,475);
        label("0 disables an auto option; held binds still work.",20,268,475);
        label("Jumpbug/edgebug use an approximate landing forecast.",20,301,475);
    }
};
class OptionsCallbacks final : public IVGUI2Extension_GameUIOptionDialogCallbacks {
public:
    int GetAltitude() const override { return 0; }
    void COptionsDialog_ctor(IGameUIOptionsDialogCtorCallbackContext* ctx) override {
        if(!ctx || !initializeControls()) return;
        auto* parent=static_cast<vgui::Panel*>(ctx->GetDialog());
        ctx->AddPage(new HudEditPage(parent),"HUD Edit");
        ctx->AddPage(new CrosshairPage(parent),"Crosshair");
        ctx->AddPage(new MovementPage(parent),"Movement");
    }
    void COptionsSubVideo_ApplyVidSettings(void*&,bool&,VGUI2Extension_CallbackContext*) override {}
    void COptionsDialogSubPage_ctor(IGameUIOptionsDialogSubPageCtorCallbackContext*) override {}
    void COptionsSubPage_OnApplyChanges(void*&,const char*,VGUI2Extension_CallbackContext*) override {}
};
OptionsCallbacks callbacks;
} // namespace
void HudEditOptions_Register() {
    if(extension) return;
    auto module=GetModuleHandleA("VGUI2Extension.dll");
    auto factory=moduleFactory(module);
    if(!factory) return;
    auto* candidate=static_cast<IVGUI2Extension*>(factory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr));
    if(!candidate) return; // A different ABI must never be called speculatively.
    extension=candidate;extensionModule=module;
    extension->RegisterGameUIOptionDialogCallbacks(&callbacks);
    addon_log::write("hudedit","Options callbacks registered (panels deferred)");
}
void HudEditOptions_Unregister() {
    if(extension && GetModuleHandleA("VGUI2Extension.dll")==extensionModule)
        extension->UnregisterGameUIOptionDialogCallbacks(&callbacks);
    extension=nullptr;extensionModule=nullptr;controlsReady=false;
    // Panels are owned by GameUI. Do not invalidate VGUI adapters while its
    // property sheets may still be destroying their child controls.
}

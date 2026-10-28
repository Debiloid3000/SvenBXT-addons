// Optional native Options -> HUD Edit tab provided through MetaHookSv VGUI2Extension.
// Plugin functionality is independent: if VGUI2Extension is missing, only the tab is absent.
#include <metahook.h>
#include <cvardef.h>
#include <IVGUI2Extension.h>
#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/CvarTextEntry.h>
#include <vgui_controls/CvarToggleCheckButton.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>
#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <cstdio>

extern cl_enginefunc_t gEngfuncs;

namespace {
IVGUI2Extension* extension = nullptr;

float value(const char* name,float fallback) {
    if(!gEngfuncs.pfnGetCvarPointer) return fallback;
    auto* c=gEngfuncs.pfnGetCvarPointer(name);
    return c?c->value:fallback;
}
int clamped(const char* name,int fallback,int low,int high) {
    const int v=static_cast<int>(value(name,static_cast<float>(fallback)));
    return v<low ? low : (v>high ? high : v);
}
class HudEditPage : public vgui::PropertyPage {
    DECLARE_CLASS_SIMPLE(HudEditPage, vgui::PropertyPage);
public:
    HudEditPage(vgui::Panel* parent) : BaseClass(parent,"HudEditOptions") {
        SetSize(512,406);
        auto headline=new vgui::Label(this,"HUDHeader","HUD EDIT  |  RGB / Layout / Crosshair");
        headline->SetBounds(20,12,465,24);
        addCheck("Rainbow", "Rainbow: all HUD", "hudedit_rainbow",20,44);
        addCheck("Critical", "Critical HP overrides", "hudedit_critical_priority",245,44);
        addCheck("Inventory","Inventory visible","hudedit_inventory_visible",20,76);
        addCheck("Crosshair","Custom crosshair","hudedit_crosshair",245,76);
        addCheck("Dot","Center dot","hudedit_crosshair_dot",245,100);
        addCheck("Outline","Outline","hudedit_crosshair_outline",245,124);
        addCheck("Dynamic","Dynamic gap","hudedit_crosshair_dynamic",245,148);
        addCheck("Tstyle","T-style","hudedit_crosshair_t",245,172);
        addInput("Inventory X", "hudedit_inventory_x",20,113);
        addInput("Inventory Y", "hudedit_inventory_y",20,148);
        addInput("Speed X", "hudedit_speed_x",20,183);
        addInput("Speed Y", "hudedit_speed_y",20,218);
        addInput("Crosshair size", "hudedit_crosshair_size",20,253);
        addInput("Crosshair gap", "hudedit_crosshair_gap",20,288);
        addInput("Crosshair thickness","hudedit_crosshair_thickness",20,323);
        auto rgb=new vgui::Label(this,"ColorHelp","RGB: use hudedit_color R G B in console.");
        rgb->SetBounds(245,295,250,22);
        auto previewLabel=new vgui::Label(this,"PreviewText","Crosshair preview");
        previewLabel->SetBounds(274,195,190,20);
    }
    void OnApplyChanges() override {
        for(auto* p:checks) p->ApplyChanges();
        for(auto* p:entries) p->ApplyChanges();
    }
    void OnResetData() override {
        for(auto* p:checks) p->Reset();
        for(auto* p:entries) p->Reset();
    }
    void Paint() override {
        BaseClass::Paint();
        if (!vgui::surface()) return;
        const int cx=365,cy=251;
        auto* surface=vgui::surface();
        surface->DrawSetColor(13,20,29,255);
        surface->DrawFilledRect(cx-50,cy-50,cx+50,cy+50);
        const int size=clamped("hudedit_crosshair_size",12,1,35);
        const int gap=clamped("hudedit_crosshair_gap",5,0,20);
        const int thick=clamped("hudedit_crosshair_thickness",2,1,8);
        const int red=clamped("hudedit_crosshair_r",40,0,255);
        const int green=clamped("hudedit_crosshair_g",230,0,255);
        const int blue=clamped("hudedit_crosshair_b",100,0,255);
        surface->DrawSetColor(red,green,blue,255);
        surface->DrawFilledRect(cx-gap-size,cy-thick/2,cx-gap,cy+thick/2+1);
        surface->DrawFilledRect(cx+gap,cy-thick/2,cx+gap+size,cy+thick/2+1);
        if(value("hudedit_crosshair_t",0)<.5f)
            surface->DrawFilledRect(cx-thick/2,cy-gap-size,cx+thick/2+1,cy-gap);
        surface->DrawFilledRect(cx-thick/2,cy+gap,cx+thick/2+1,cy+gap+size);
        if(value("hudedit_crosshair_dot",0)>.5f)
            surface->DrawFilledRect(cx-1,cy-1,cx+2,cy+2);
    }
private:
    std::vector<CCvarTextEntry*> entries;
    std::vector<CCvarToggleCheckButton*> checks;
    void addInput(const char* label,const char* cvar,int x,int y) {
        auto* caption=new vgui::Label(this,cvar,label);
        caption->SetBounds(x,y,125,22);
        auto* entry=new CCvarTextEntry(this,cvar,cvar);
        entry->SetBounds(x+135,y,80,23);
        entries.push_back(entry);
    }
    void addCheck(const char* id,const char* label,const char* cvar,int x,int y) {
        auto* b=new CCvarToggleCheckButton(this,id,label,cvar);
        b->SetBounds(x,y,240,23);
        checks.push_back(b);
    }
};
class HudEditOptionsCallbacks final : public IVGUI2Extension_GameUIOptionDialogCallbacks {
public:
    int GetAltitude() const override { return 0; }
    void COptionsDialog_ctor(IGameUIOptionsDialogCtorCallbackContext* ctx) override {
        if(ctx) ctx->AddPage(new HudEditPage(static_cast<vgui::Panel*>(ctx->GetDialog())),"HUD Edit");
    }
    void COptionsSubVideo_ApplyVidSettings(void*&,bool&,VGUI2Extension_CallbackContext*) override {}
    void COptionsDialogSubPage_ctor(IGameUIOptionsDialogSubPageCtorCallbackContext*) override {}
    void COptionsSubPage_OnApplyChanges(void*&,const char*,VGUI2Extension_CallbackContext*) override {}
};
HudEditOptionsCallbacks optionsCallbacks;
} // namespace

void HudEditOptions_Register() {
    if(extension) return;
    HMODULE module=GetModuleHandleA("VGUI2Extension.dll");
    if(!module) return;
    auto factory=Sys_GetFactory(reinterpret_cast<HINTERFACEMODULE>(module));
    if(!factory) return;
    auto* candidate=reinterpret_cast<IVGUI2Extension*>(factory(VGUI2_EXTENSION_INTERFACE_VERSION,nullptr));
    if(!candidate) return;
    extension=candidate;
    extension->RegisterGameUIOptionDialogCallbacks(&optionsCallbacks);
}
void HudEditOptions_Unregister() {
    if(!extension) return;
    extension->UnregisterGameUIOptionDialogCallbacks(&optionsCallbacks);
    extension=nullptr;
}

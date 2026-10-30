#include <metahook.h>
#include <IVGUI2Extension.h>
#include <cstring>

// Minimal provider: the extension exists but VGUI/KeyValues/engine UI do not.
// Opening Options in this state must not allocate any panels or crash.
namespace {
class Extension final : public IVGUI2Extension {
public:
    IVGUI2Extension_GameUIOptionDialogCallbacks* options=nullptr;
    void RegisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks*) override {}
    void RegisterGameUICallbacks(IVGUI2Extension_GameUICallbacks*) override {}
    void RegisterGameUIOptionDialogCallbacks(IVGUI2Extension_GameUIOptionDialogCallbacks* p) override { options=p; }
    void RegisterGameUITaskBarCallbacks(IVGUI2Extension_GameUITaskBarCallbacks*) override {}
    void RegisterGameUIBasePanelCallbacks(IVGUI2Extension_GameUIBasePanelCallbacks*) override {}
    void RegisterGameConsoleCallbacks(IVGUI2Extension_GameConsoleCallbacks*) override {}
    void RegisterClientVGUICallbacks(IVGUI2Extension_ClientVGUICallbacks*) override {}
    void RegisterKeyValuesCallbacks(IVGUI2Extension_KeyValuesCallbacks*) override {}
    void UnregisterBaseUICallbacks(IVGUI2Extension_BaseUICallbacks*) override {}
    void UnregisterGameUICallbacks(IVGUI2Extension_GameUICallbacks*) override {}
    void UnregisterGameUIOptionDialogCallbacks(IVGUI2Extension_GameUIOptionDialogCallbacks* p) override { if(options==p) options=nullptr; }
    void UnregisterGameUITaskBarCallbacks(IVGUI2Extension_GameUITaskBarCallbacks*) override {}
    void UnregisterGameUIBasePanelCallbacks(IVGUI2Extension_GameUIBasePanelCallbacks*) override {}
    void UnregisterGameConsoleCallbacks(IVGUI2Extension_GameConsoleCallbacks*) override {}
    void UnregisterClientVGUICallbacks(IVGUI2Extension_ClientVGUICallbacks*) override {}
    void UnregisterKeyValuesCallbacks(IVGUI2Extension_KeyValuesCallbacks*) override {}
    const char* GetBaseDirectory() const override { return ""; }
    const char* GetCurrentLanguage() const override { return "english"; }
};
Extension extension;
}
extern "C" IBaseInterface* CreateInterface(const char* name,int*) {
    return !std::strcmp(name,VGUI2_EXTENSION_INTERFACE_VERSION)?&extension:nullptr;
}
extern "C" int TestRegistrationCount() { return extension.options?1:0; }
extern "C" void TestOpenOptions() {
    if(extension.options) extension.options->COptionsDialog_ctor(
        reinterpret_cast<IGameUIOptionsDialogCtorCallbackContext*>(1));
}

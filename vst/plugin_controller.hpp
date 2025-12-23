#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include <mutex>

namespace SpectrumEQ {

class PluginEditor;

/**
 * VST3 Edit Controller
 * Handles parameter management and UI
 */
class PluginController : public Steinberg::Vst::EditController {
public:
    // Create instance
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new PluginController());
    }

    // EditController overrides
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    
    // Parameter change notification (from host/automation)
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, 
                                                      Steinberg::Vst::ParamValue value) override;
    
    // Create custom editor UI
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    
    // Editor management
    void setEditor(PluginEditor* editor);
    void removeEditor(PluginEditor* editor);
    
private:
    PluginEditor* editor_ = nullptr;
    std::mutex editorMutex_;
};

} // namespace SpectrumEQ

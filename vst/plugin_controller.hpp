#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

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
    
    // Create custom editor UI
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    
private:
    PluginEditor* editor_ = nullptr;
};

} // namespace SpectrumEQ

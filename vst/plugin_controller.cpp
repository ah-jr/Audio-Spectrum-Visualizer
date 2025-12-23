#include "plugin_controller.hpp"
#include "plugin_editor.hpp"
#include "plugin_ids.hpp"
#include "eq_processor.hpp"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"

namespace SpectrumEQ {

Steinberg::tresult PLUGIN_API PluginController::initialize(Steinberg::FUnknown* context) {
    Steinberg::tresult result = EditController::initialize(context);
    if (result != Steinberg::kResultOk) {
        return result;
    }

    // Define parameters for each EQ band
    using Steinberg::Vst::ParameterInfo;
    
    // Band 1 - 60Hz
    parameters.addParameter(STR16("Band 1 Gain"), STR16("dB"), 0, 0.5,
        ParameterInfo::kCanAutomate, kBand1Gain, 0, STR16("60Hz"));
    parameters.addParameter(STR16("Band 1 Freq"), STR16("Hz"), 0, 0.159,
        ParameterInfo::kCanAutomate, kBand1Freq);
    parameters.addParameter(STR16("Band 1 Q"), STR16(""), 0, 0.424,
        ParameterInfo::kCanAutomate, kBand1Q);
    
    // Band 2 - 250Hz
    parameters.addParameter(STR16("Band 2 Gain"), STR16("dB"), 0, 0.5,
        ParameterInfo::kCanAutomate, kBand2Gain, 0, STR16("250Hz"));
    parameters.addParameter(STR16("Band 2 Freq"), STR16("Hz"), 0, 0.366,
        ParameterInfo::kCanAutomate, kBand2Freq);
    parameters.addParameter(STR16("Band 2 Q"), STR16(""), 0, 0.424,
        ParameterInfo::kCanAutomate, kBand2Q);
    
    // Band 3 - 1kHz
    parameters.addParameter(STR16("Band 3 Gain"), STR16("dB"), 0, 0.5,
        ParameterInfo::kCanAutomate, kBand3Gain, 0, STR16("1kHz"));
    parameters.addParameter(STR16("Band 3 Freq"), STR16("Hz"), 0, 0.567,
        ParameterInfo::kCanAutomate, kBand3Freq);
    parameters.addParameter(STR16("Band 3 Q"), STR16(""), 0, 0.424,
        ParameterInfo::kCanAutomate, kBand3Q);
    
    // Band 4 - 4kHz
    parameters.addParameter(STR16("Band 4 Gain"), STR16("dB"), 0, 0.5,
        ParameterInfo::kCanAutomate, kBand4Gain, 0, STR16("4kHz"));
    parameters.addParameter(STR16("Band 4 Freq"), STR16("Hz"), 0, 0.768,
        ParameterInfo::kCanAutomate, kBand4Freq);
    parameters.addParameter(STR16("Band 4 Q"), STR16(""), 0, 0.424,
        ParameterInfo::kCanAutomate, kBand4Q);
    
    // Band 5 - 12kHz
    parameters.addParameter(STR16("Band 5 Gain"), STR16("dB"), 0, 0.5,
        ParameterInfo::kCanAutomate, kBand5Gain, 0, STR16("12kHz"));
    parameters.addParameter(STR16("Band 5 Freq"), STR16("Hz"), 0, 0.926,
        ParameterInfo::kCanAutomate, kBand5Freq);
    parameters.addParameter(STR16("Band 5 Q"), STR16(""), 0, 0.424,
        ParameterInfo::kCanAutomate, kBand5Q);
    
    // Bypass parameter
    parameters.addParameter(STR16("Bypass"), nullptr, 1, 0,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypass);

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API PluginController::terminate() {
    std::lock_guard<std::mutex> lock(editorMutex_);
    editor_ = nullptr;
    return EditController::terminate();
}

void PluginController::setEditor(PluginEditor* editor) {
    std::lock_guard<std::mutex> lock(editorMutex_);
    editor_ = editor;
}

void PluginController::removeEditor(PluginEditor* editor) {
    std::lock_guard<std::mutex> lock(editorMutex_);
    if (editor_ == editor) {
        editor_ = nullptr;
    }
}

Steinberg::tresult PLUGIN_API PluginController::setParamNormalized(Steinberg::Vst::ParamID tag, 
                                                                    Steinberg::Vst::ParamValue value) {
    // Call base class first
    Steinberg::tresult result = EditController::setParamNormalized(tag, value);
    
    // Notify editor of parameter change (for automation sync)
    std::lock_guard<std::mutex> lock(editorMutex_);
    if (editor_) {
        editor_->onParameterChange(tag, value);
    }
    
    return result;
}

Steinberg::tresult PLUGIN_API PluginController::setComponentState(Steinberg::IBStream* state) {
    if (!state) return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);
    
    // Read version
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version)) return Steinberg::kResultFalse;
    
    // Read and set EQ parameters
    for (int band = 0; band < eq::NUM_BANDS; ++band) {
        double gain, freq, q;
        if (!streamer.readDouble(gain)) return Steinberg::kResultFalse;
        if (!streamer.readDouble(freq)) return Steinberg::kResultFalse;
        if (!streamer.readDouble(q)) return Steinberg::kResultFalse;
        
        // Convert to normalized values
        double gainNorm = (gain - eq::MIN_GAIN) / (eq::MAX_GAIN - eq::MIN_GAIN);
        
        double logMin = std::log10(eq::MIN_FREQ);
        double logMax = std::log10(eq::MAX_FREQ);
        double freqNorm = (std::log10(freq) - logMin) / (logMax - logMin);
        
        double qLogMin = std::log10(eq::MIN_Q);
        double qLogMax = std::log10(eq::MAX_Q);
        double qNorm = (std::log10(q) - qLogMin) / (qLogMax - qLogMin);
        
        setParamNormalized(getBandGainParam(band), gainNorm);
        setParamNormalized(getBandFreqParam(band), freqNorm);
        setParamNormalized(getBandQParam(band), qNorm);
    }
    
    // Read bypass
    Steinberg::int32 bypass;
    if (!streamer.readInt32(bypass)) return Steinberg::kResultFalse;
    setParamNormalized(kBypass, bypass != 0 ? 1.0 : 0.0);

    return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API PluginController::createView(Steinberg::FIDString name) {
    if (Steinberg::FIDStringsEqual(name, Steinberg::Vst::ViewType::kEditor)) {
        auto* editor = new PluginEditor(this);
        setEditor(editor);
        return editor;
    }
    return nullptr;
}

} // namespace SpectrumEQ


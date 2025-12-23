#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "eq_processor.hpp"
#include "fft.hpp"
#include <vector>
#include <mutex>

namespace SpectrumEQ {

/**
 * VST3 Audio Processor
 * Processes audio through the EQ and computes spectrum for visualization
 */
class PluginProcessor : public Steinberg::Vst::AudioEffect {
public:
    PluginProcessor();
    ~PluginProcessor() override;

    // Create instance
    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new PluginProcessor());
    }

    // AudioEffect overrides
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    // Get spectrum data for visualization (thread-safe)
    std::vector<float> getSpectrum();
    
    // Get EQ processor for UI
    const eq::EQProcessor& getEQ() const { return eq_; }

protected:
    void processParameterChanges(Steinberg::Vst::IParameterChanges* paramChanges);
    
    eq::EQProcessor eq_;
    
    // Spectrum analysis
    static constexpr size_t kFFTSize = 4096;
    std::vector<float> inputBuffer_;
    size_t inputBufferPos_ = 0;
    std::vector<float> spectrum_;
    std::mutex spectrumMutex_;
    
    double sampleRate_ = 44100.0;
};

} // namespace SpectrumEQ


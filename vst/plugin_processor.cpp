#include "plugin_processor.hpp"
#include "plugin_ids.hpp"
#include "shared_data.hpp"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fstreamer.h"

namespace SpectrumEQ {

PluginProcessor::PluginProcessor() {
    setControllerClass(kControllerUID);
    
    inputBuffer_.resize(kFFTSize, 0.0f);
    spectrum_.resize(kFFTSize / 2, 0.0f);
}

PluginProcessor::~PluginProcessor() = default;

Steinberg::tresult PLUGIN_API PluginProcessor::initialize(Steinberg::FUnknown* context) {
    Steinberg::tresult result = AudioEffect::initialize(context);
    if (result != Steinberg::kResultOk) {
        return result;
    }

    // Add audio inputs/outputs
    addAudioInput(STR16("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API PluginProcessor::terminate() {
    return AudioEffect::terminate();
}

Steinberg::tresult PLUGIN_API PluginProcessor::setActive(Steinberg::TBool state) {
    if (state) {
        eq_.reset();
        std::fill(inputBuffer_.begin(), inputBuffer_.end(), 0.0f);
        inputBufferPos_ = 0;
    }
    return AudioEffect::setActive(state);
}

Steinberg::tresult PLUGIN_API PluginProcessor::setupProcessing(Steinberg::Vst::ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate;
    eq_.setSampleRate(sampleRate_);
    return AudioEffect::setupProcessing(setup);
}

Steinberg::tresult PLUGIN_API PluginProcessor::canProcessSampleSize(Steinberg::int32 symbolicSampleSize) {
    if (symbolicSampleSize == Steinberg::Vst::kSample32 || 
        symbolicSampleSize == Steinberg::Vst::kSample64) {
        return Steinberg::kResultTrue;
    }
    return Steinberg::kResultFalse;
}

void PluginProcessor::processParameterChanges(Steinberg::Vst::IParameterChanges* paramChanges) {
    if (!paramChanges) return;

    Steinberg::int32 numParamsChanged = paramChanges->getParameterCount();
    for (Steinberg::int32 i = 0; i < numParamsChanged; ++i) {
        Steinberg::Vst::IParamValueQueue* paramQueue = paramChanges->getParameterData(i);
        if (!paramQueue) continue;

        Steinberg::Vst::ParamID paramId = paramQueue->getParameterId();
        Steinberg::int32 numPoints = paramQueue->getPointCount();
        
        if (numPoints > 0) {
            Steinberg::int32 sampleOffset;
            Steinberg::Vst::ParamValue value;
            
            // Get the last value
            if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == Steinberg::kResultTrue) {
                // Handle parameter based on ID
                if (paramId == kBypass) {
                    eq_.setBypass(value > 0.5);
                } else {
                    // Determine which band and parameter type
                    int band = paramId / 3;
                    int paramType = paramId % 3;
                    
                    if (band < eq::NUM_BANDS) {
                        switch (paramType) {
                            case 0: // Gain
                                eq_.setBandGain(band, eq::MIN_GAIN + value * (eq::MAX_GAIN - eq::MIN_GAIN));
                                break;
                            case 1: // Frequency (logarithmic)
                                {
                                    double logMin = std::log10(eq::MIN_FREQ);
                                    double logMax = std::log10(eq::MAX_FREQ);
                                    double freq = std::pow(10.0, logMin + value * (logMax - logMin));
                                    eq_.setBandFrequency(band, freq);
                                }
                                break;
                            case 2: // Q (logarithmic)
                                {
                                    double logMin = std::log10(eq::MIN_Q);
                                    double logMax = std::log10(eq::MAX_Q);
                                    double q = std::pow(10.0, logMin + value * (logMax - logMin));
                                    eq_.setBandQ(band, q);
                                }
                                break;
                        }
                    }
                }
            }
        }
    }
}

Steinberg::tresult PLUGIN_API PluginProcessor::process(Steinberg::Vst::ProcessData& data) {
    // Process parameter changes
    processParameterChanges(data.inputParameterChanges);

    // Check for valid audio
    if (data.numInputs == 0 || data.numOutputs == 0) {
        return Steinberg::kResultOk;
    }

    Steinberg::int32 numChannels = data.inputs[0].numChannels;
    Steinberg::int32 numSamples = data.numSamples;

    // Get audio buffers
    float** in = data.inputs[0].channelBuffers32;
    float** out = data.outputs[0].channelBuffers32;

    // Check for silence flags
    if (data.inputs[0].silenceFlags != 0) {
        data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
        for (Steinberg::int32 ch = 0; ch < numChannels; ++ch) {
            if (in[ch] != out[ch]) {
                memset(out[ch], 0, numSamples * sizeof(float));
            }
        }
        return Steinberg::kResultOk;
    }

    // Process audio
    if (numChannels >= 2) {
        // Stereo processing
        for (Steinberg::int32 i = 0; i < numSamples; ++i) {
            float left = in[0][i];
            float right = in[1][i];
            
            // Apply EQ
            eq_.process(left, right);
            
            out[0][i] = left;
            out[1][i] = right;
            
            // Fill spectrum buffer (mono mix)
            inputBuffer_[inputBufferPos_] = (left + right) * 0.5f;
            inputBufferPos_ = (inputBufferPos_ + 1) % kFFTSize;
        }
    } else if (numChannels == 1) {
        // Mono processing
        for (Steinberg::int32 i = 0; i < numSamples; ++i) {
            float sample = in[0][i];
            sample = eq_.processMono(sample);
            out[0][i] = sample;
            
            inputBuffer_[inputBufferPos_] = sample;
            inputBufferPos_ = (inputBufferPos_ + 1) % kFFTSize;
        }
    }

    // Compute spectrum periodically (every buffer)
    {
        std::lock_guard<std::mutex> lock(spectrumMutex_);
        
        // Simple magnitude calculation for visualization
        // (In a real implementation, you'd use the FFT class)
        static fft::ComplexVector fftData(kFFTSize);
        static std::vector<double> tempBuffer(kFFTSize);
        
        // Copy with windowing
        for (size_t i = 0; i < kFFTSize; ++i) {
            size_t idx = (inputBufferPos_ + i) % kFFTSize;
            double window = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (kFFTSize - 1)));
            tempBuffer[i] = inputBuffer_[idx] * window;
        }
        
        // Convert to complex and compute FFT using the shared fft namespace
        auto complexSpectrum = fft::transform(tempBuffer);
        auto magnitudes = fft::magnitude(complexSpectrum);
        
        // Copy to spectrum buffer (normalize)
        double normFactor = 2.0 / kFFTSize;
        for (size_t i = 0; i < kFFTSize / 2; ++i) {
            spectrum_[i] = static_cast<float>(magnitudes[i] * normFactor);
        }
        
        // Update shared buffer for editor
        SharedSpectrumData::instance().setSpectrum(spectrum_);
    }

    data.outputs[0].silenceFlags = 0;
    return Steinberg::kResultOk;
}

std::vector<float> PluginProcessor::getSpectrum() {
    std::lock_guard<std::mutex> lock(spectrumMutex_);
    return spectrum_;
}

Steinberg::tresult PLUGIN_API PluginProcessor::setState(Steinberg::IBStream* state) {
    if (!state) return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);
    
    // Read version
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version)) return Steinberg::kResultFalse;
    
    // Read EQ parameters
    for (int band = 0; band < eq::NUM_BANDS; ++band) {
        double gain, freq, q;
        if (!streamer.readDouble(gain)) return Steinberg::kResultFalse;
        if (!streamer.readDouble(freq)) return Steinberg::kResultFalse;
        if (!streamer.readDouble(q)) return Steinberg::kResultFalse;
        
        eq_.setBandGain(band, gain);
        eq_.setBandFrequency(band, freq);
        eq_.setBandQ(band, q);
    }
    
    // Read bypass
    Steinberg::int32 bypass;
    if (!streamer.readInt32(bypass)) return Steinberg::kResultFalse;
    eq_.setBypass(bypass != 0);

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API PluginProcessor::getState(Steinberg::IBStream* state) {
    if (!state) return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);
    
    // Write version
    streamer.writeInt32(1);
    
    // Write EQ parameters
    for (int band = 0; band < eq::NUM_BANDS; ++band) {
        streamer.writeDouble(eq_.getBandGain(band));
        streamer.writeDouble(eq_.getBandFrequency(band));
        streamer.writeDouble(eq_.getBandQ(band));
    }
    
    // Write bypass
    streamer.writeInt32(eq_.isBypassed() ? 1 : 0);

    return Steinberg::kResultOk;
}

} // namespace SpectrumEQ


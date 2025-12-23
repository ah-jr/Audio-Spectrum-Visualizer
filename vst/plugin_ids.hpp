#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace SpectrumEQ {

// Unique IDs for processor and controller
static const Steinberg::FUID kProcessorUID(0x84E8DE5F, 0x92544E63, 0x9B3F4D5C, 0x1A2B3C4D);
static const Steinberg::FUID kControllerUID(0x5D4C3B2A, 0x1F4E5D6C, 0x7B8A9B0C, 0x0D1E2F3A);

// Parameter IDs
enum ParamID : Steinberg::Vst::ParamID {
    // Band 1
    kBand1Gain = 0,
    kBand1Freq = 1,
    kBand1Q = 2,
    // Band 2
    kBand2Gain = 3,
    kBand2Freq = 4,
    kBand2Q = 5,
    // Band 3
    kBand3Gain = 6,
    kBand3Freq = 7,
    kBand3Q = 8,
    // Band 4
    kBand4Gain = 9,
    kBand4Freq = 10,
    kBand4Q = 11,
    // Band 5
    kBand5Gain = 12,
    kBand5Freq = 13,
    kBand5Q = 14,
    // Global
    kBypass = 15,
    
    kNumParams
};

// Helper to get param IDs for a band
inline Steinberg::Vst::ParamID getBandGainParam(int band) { return static_cast<Steinberg::Vst::ParamID>(kBand1Gain + band * 3); }
inline Steinberg::Vst::ParamID getBandFreqParam(int band) { return static_cast<Steinberg::Vst::ParamID>(kBand1Freq + band * 3); }
inline Steinberg::Vst::ParamID getBandQParam(int band) { return static_cast<Steinberg::Vst::ParamID>(kBand1Q + band * 3); }

} // namespace SpectrumEQ


#pragma once

/**
 * Shared EQ Processor - used by both standalone app and VST plugin
 * Header-only for easy inclusion
 */

#include <cmath>
#include <array>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace eq {

constexpr int NUM_BANDS = 5;
constexpr double DEFAULT_FREQUENCIES[NUM_BANDS] = {60.0, 250.0, 1000.0, 4000.0, 12000.0};
constexpr double MIN_FREQ = 20.0;
constexpr double MAX_FREQ = 20000.0;
constexpr double MIN_GAIN = -12.0;
constexpr double MAX_GAIN = 12.0;
constexpr double MIN_Q = 0.1;
constexpr double MAX_Q = 10.0;
constexpr double DEFAULT_Q = 0.707;

/**
 * Biquad filter for parametric EQ
 */
class BiquadFilter {
public:
    BiquadFilter() { reset(); }
    
    void reset() {
        for (int i = 0; i < 2; ++i) {
            x1_[i] = x2_[i] = y1_[i] = y2_[i] = 0.0;
        }
    }
    
    void setPeakingEQ(double sampleRate, double freq, double gainDb, double q) {
        // Clamp parameters
        freq = std::max(20.0, std::min(freq, sampleRate * 0.45));
        q = std::max(0.1, std::min(q, 10.0));
        
        double A = std::pow(10.0, gainDb / 40.0);
        double w0 = 2.0 * M_PI * freq / sampleRate;
        double cosw0 = std::cos(w0);
        double sinw0 = std::sin(w0);
        double alpha = sinw0 / (2.0 * q);
        
        double a0;
        b0_ = 1.0 + alpha * A;
        b1_ = -2.0 * cosw0;
        b2_ = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1_ = -2.0 * cosw0;
        a2_ = 1.0 - alpha / A;
        
        // Normalize
        b0_ /= a0;
        b1_ /= a0;
        b2_ /= a0;
        a1_ /= a0;
        a2_ /= a0;
    }
    
    float process(float input, int channel) {
        double x0 = static_cast<double>(input);
        double y0 = b0_ * x0 + b1_ * x1_[channel] + b2_ * x2_[channel]
                  - a1_ * y1_[channel] - a2_ * y2_[channel];
        
        x2_[channel] = x1_[channel];
        x1_[channel] = x0;
        y2_[channel] = y1_[channel];
        y1_[channel] = y0;
        
        return static_cast<float>(y0);
    }
    
    // Get frequency response magnitude at a given frequency
    double getMagnitudeAt(double freq, double sampleRate) const {
        double w = 2.0 * M_PI * freq / sampleRate;
        double cosw = std::cos(w);
        double cos2w = std::cos(2.0 * w);
        double sinw = std::sin(w);
        double sin2w = std::sin(2.0 * w);
        
        double numReal = b0_ + b1_ * cosw + b2_ * cos2w;
        double numImag = -b1_ * sinw - b2_ * sin2w;
        double denReal = 1.0 + a1_ * cosw + a2_ * cos2w;
        double denImag = -a1_ * sinw - a2_ * sin2w;
        
        double numMag = std::sqrt(numReal * numReal + numImag * numImag);
        double denMag = std::sqrt(denReal * denReal + denImag * denImag);
        
        return numMag / denMag;
    }
    
private:
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0;
    double a1_ = 0.0, a2_ = 0.0;
    double x1_[2] = {0, 0};
    double x2_[2] = {0, 0};
    double y1_[2] = {0, 0};
    double y2_[2] = {0, 0};
};

/**
 * 5-band parametric EQ processor
 */
class EQProcessor {
public:
    EQProcessor() {
        for (int i = 0; i < NUM_BANDS; ++i) {
            frequencies_[i] = DEFAULT_FREQUENCIES[i];
            gains_[i] = 0.0;
            qFactors_[i] = DEFAULT_Q;
        }
    }
    
    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate;
        updateAllFilters();
    }
    
    void reset() {
        for (auto& filter : filters_) {
            filter.reset();
        }
    }
    
    // Band setters
    void setBandGain(int band, double gainDb) {
        if (band >= 0 && band < NUM_BANDS) {
            gains_[band] = std::max(MIN_GAIN, std::min(MAX_GAIN, gainDb));
            updateFilter(band);
        }
    }
    
    void setBandFrequency(int band, double freq) {
        if (band >= 0 && band < NUM_BANDS) {
            frequencies_[band] = std::max(MIN_FREQ, std::min(MAX_FREQ, freq));
            updateFilter(band);
        }
    }
    
    void setBandQ(int band, double q) {
        if (band >= 0 && band < NUM_BANDS) {
            qFactors_[band] = std::max(MIN_Q, std::min(MAX_Q, q));
            updateFilter(band);
        }
    }
    
    void setBypass(bool bypass) { bypass_ = bypass; }
    
    // Band getters
    double getBandGain(int band) const { 
        return (band >= 0 && band < NUM_BANDS) ? gains_[band] : 0.0; 
    }
    double getBandFrequency(int band) const { 
        return (band >= 0 && band < NUM_BANDS) ? frequencies_[band] : 1000.0; 
    }
    double getBandQ(int band) const { 
        return (band >= 0 && band < NUM_BANDS) ? qFactors_[band] : DEFAULT_Q; 
    }
    bool isBypassed() const { return bypass_; }
    
    // Process stereo sample
    void process(float& left, float& right) {
        if (bypass_) return;
        
        for (int i = 0; i < NUM_BANDS; ++i) {
            if (std::abs(gains_[i]) > 0.01) {
                left = filters_[i].process(left, 0);
                right = filters_[i].process(right, 1);
            }
        }
    }
    
    // Process mono sample
    float processMono(float input) {
        if (bypass_) return input;
        
        for (int i = 0; i < NUM_BANDS; ++i) {
            if (std::abs(gains_[i]) > 0.01) {
                input = filters_[i].process(input, 0);
            }
        }
        return input;
    }
    
    // Process buffer (interleaved stereo)
    void processBlock(float* buffer, int numFrames, int numChannels) {
        if (bypass_) return;
        
        for (int frame = 0; frame < numFrames; ++frame) {
            for (int ch = 0; ch < numChannels && ch < 2; ++ch) {
                float& sample = buffer[frame * numChannels + ch];
                for (int band = 0; band < NUM_BANDS; ++band) {
                    if (std::abs(gains_[band]) > 0.01) {
                        sample = filters_[band].process(sample, ch);
                    }
                }
            }
        }
    }
    
    // Get combined frequency response magnitude at a frequency (in dB)
    double getResponseAt(double freq) const {
        double totalMag = 1.0;
        for (int i = 0; i < NUM_BANDS; ++i) {
            if (std::abs(gains_[i]) > 0.01) {
                totalMag *= filters_[i].getMagnitudeAt(freq, sampleRate_);
            }
        }
        return 20.0 * std::log10(totalMag);
    }
    
private:
    void updateFilter(int band) {
        filters_[band].setPeakingEQ(sampleRate_, frequencies_[band], gains_[band], qFactors_[band]);
    }
    
    void updateAllFilters() {
        for (int i = 0; i < NUM_BANDS; ++i) {
            updateFilter(i);
        }
    }
    
    double sampleRate_ = 44100.0;
    bool bypass_ = false;
    
    std::array<double, NUM_BANDS> frequencies_;
    std::array<double, NUM_BANDS> gains_;
    std::array<double, NUM_BANDS> qFactors_;
    std::array<BiquadFilter, NUM_BANDS> filters_;
};

} // namespace eq


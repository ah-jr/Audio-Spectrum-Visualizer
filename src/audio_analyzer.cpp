// Windows compatibility - must be before any Windows headers
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "audio_analyzer.hpp"
#include "fft.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>

// Use std:: versions explicitly to avoid Windows macro conflicts
using std::min;
using std::max;

namespace audio {

// Forward declaration
class AudioAnalyzer;

constexpr double PI = 3.14159265358979323846;

// Biquad filter for EQ
struct BiquadFilter {
    double b0, b1, b2;  // Feedforward coefficients
    double a1, a2;      // Feedback coefficients
    
    // Filter state (per channel)
    double x1[2] = {0, 0};
    double x2[2] = {0, 0};
    double y1[2] = {0, 0};
    double y2[2] = {0, 0};
    
    void reset() {
        for (int i = 0; i < 2; ++i) {
            x1[i] = x2[i] = y1[i] = y2[i] = 0.0;
        }
    }
    
    // Calculate peaking EQ coefficients (peakingEQ from Audio EQ Cookbook)
    void setPeakingEQ(double sampleRate, double freq, double gainDb, double q) {
        // Clamp frequency to valid range
        freq = (std::max)(20.0, (std::min)(freq, sampleRate * 0.45));
        q = (std::max)(0.1, (std::min)(q, 10.0));
        
        double A = std::pow(10.0, gainDb / 40.0);  // Square root of linear gain
        double w0 = 2.0 * PI * freq / sampleRate;
        double cosw0 = std::cos(w0);
        double sinw0 = std::sin(w0);
        double alpha = sinw0 / (2.0 * q);
        
        double a0;
        // Standard peaking EQ formula
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cosw0;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cosw0;
        a2 = 1.0 - alpha / A;
        
        // Normalize by a0
        b0 /= a0;
        b1 /= a0;
        b2 /= a0;
        a1 /= a0;
        a2 /= a0;
    }
    
    // Process a single sample
    float process(float input, int channel) {
        double x0 = static_cast<double>(input);
        double y0 = b0 * x0 + b1 * x1[channel] + b2 * x2[channel]
                  - a1 * y1[channel] - a2 * y2[channel];
        
        // Shift state
        x2[channel] = x1[channel];
        x1[channel] = x0;
        y2[channel] = y1[channel];
        y1[channel] = y0;
        
        return static_cast<float>(y0);
    }
};

// Implementation structure (defined before callback)
struct AudioAnalyzerImpl {
    ma_decoder decoder;
    ma_device device;
    ma_device_config deviceConfig;
    
    bool initialized = false;
    bool fileLoaded = false;
    std::atomic<bool> playing{false};
    
    std::string filename;
    uint32_t sampleRate = 44100;
    uint32_t channels = 2;
    uint64_t totalFrames = 0;
    std::atomic<uint64_t> currentFrame{0};
    
    float volume = 1.0f;
    
    AnalyzerConfig config;
    SpectrumData currentSpectrum;
    
    // Circular buffer for audio samples
    std::vector<float> sampleBuffer;
    size_t bufferWritePos = 0;
    std::mutex bufferMutex;
    
    // Smoothed spectrum
    std::vector<double> smoothedMagnitudes;
    
    // Band frequencies
    std::vector<double> bandFrequencies;
    std::vector<std::pair<size_t, size_t>> bandBins;
    
    // Equalizer
    EqualizerConfig eqConfig;
    BiquadFilter eqFilters[EqualizerConfig::NUM_BANDS];
    std::atomic<bool> eqNeedsUpdate{false};
    
    AudioAnalyzer* parent = nullptr;
    
    void updateEQFilters() {
        for (int i = 0; i < EqualizerConfig::NUM_BANDS; ++i) {
            eqFilters[i].setPeakingEQ(
                sampleRate,
                eqConfig.bands[i].frequency,
                eqConfig.bands[i].gain,
                eqConfig.bands[i].q
            );
        }
        eqNeedsUpdate = false;
    }
};

// Audio callback for miniaudio
static void audioCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

AudioAnalyzer::AudioAnalyzer() : pImpl(std::make_unique<AudioAnalyzerImpl>()) {
    pImpl->parent = this;
}

AudioAnalyzer::~AudioAnalyzer() {
    stop();
    
    if (pImpl->fileLoaded) {
        ma_device_uninit(&pImpl->device);
        ma_decoder_uninit(&pImpl->decoder);
    }
}

bool AudioAnalyzer::initialize() {
    pImpl->initialized = true;
    
    // Initialize sample buffer
    pImpl->sampleBuffer.resize(pImpl->config.fftSize * 2, 0.0f);
    pImpl->smoothedMagnitudes.resize(pImpl->config.numBands, 0.0);
    
    // Initialize spectrum data
    pImpl->currentSpectrum.magnitudes.resize(pImpl->config.numBands, 0.0);
    pImpl->currentSpectrum.frequencies.resize(pImpl->config.numBands, 0.0);
    
    updateBands();
    
    return true;
}

static void audioCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioAnalyzerImpl* impl = static_cast<AudioAnalyzerImpl*>(pDevice->pUserData);
    
    if (!impl || !impl->playing) {
        // Output silence
        memset(pOutput, 0, frameCount * pDevice->playback.channels * sizeof(float));
        return;
    }
    
    ma_uint64 framesRead;
    ma_decoder_read_pcm_frames(&impl->decoder, pOutput, frameCount, &framesRead);
    
    if (framesRead < frameCount) {
        // End of file reached
        impl->playing = false;
        ma_decoder_seek_to_pcm_frame(&impl->decoder, 0);
        impl->currentFrame = 0;
        
        // Fill remaining with silence
        float* output = static_cast<float*>(pOutput);
        for (size_t i = framesRead * pDevice->playback.channels; 
             i < frameCount * pDevice->playback.channels; ++i) {
            output[i] = 0.0f;
        }
    }
    
    float* output = static_cast<float*>(pOutput);
    uint32_t channels = pDevice->playback.channels;
    
    // Update EQ filters if needed
    if (impl->eqNeedsUpdate) {
        impl->updateEQFilters();
    }
    
    // Apply EQ processing
    if (impl->eqConfig.enabled) {
        for (size_t frame = 0; frame < framesRead; ++frame) {
            for (uint32_t ch = 0; ch < channels && ch < 2; ++ch) {
                float sample = output[frame * channels + ch];
                
                // Apply each EQ band in series
                for (int band = 0; band < EqualizerConfig::NUM_BANDS; ++band) {
                    if (impl->eqConfig.bands[band].enabled && 
                        std::abs(impl->eqConfig.bands[band].gain) > 0.01) {
                        sample = impl->eqFilters[band].process(sample, ch);
                    }
                }
                
                output[frame * channels + ch] = sample;
            }
        }
    }
    
    // Apply volume
    for (size_t i = 0; i < framesRead * channels; ++i) {
        output[i] *= impl->volume;
    }
    
    // Update current frame position
    impl->currentFrame += framesRead;
    
    // Copy samples to analysis buffer
    impl->parent->processAudioData(output, framesRead, channels);
    
    (void)pInput; // Unused
}

bool AudioAnalyzer::loadFile(const std::string& filepath) {
    // Stop any current playback
    stop();
    
    // Uninitialize previous decoder/device if loaded
    if (pImpl->fileLoaded) {
        ma_device_uninit(&pImpl->device);
        ma_decoder_uninit(&pImpl->decoder);
        pImpl->fileLoaded = false;
    }
    
    // Initialize decoder
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 2, 44100);
    
    ma_result result = ma_decoder_init_file(filepath.c_str(), &decoderConfig, &pImpl->decoder);
    if (result != MA_SUCCESS) {
        return false;
    }
    
    pImpl->sampleRate = pImpl->decoder.outputSampleRate;
    pImpl->channels = pImpl->decoder.outputChannels;
    
    // Get total frames
    ma_decoder_get_length_in_pcm_frames(&pImpl->decoder, &pImpl->totalFrames);
    
    // Initialize playback device
    pImpl->deviceConfig = ma_device_config_init(ma_device_type_playback);
    pImpl->deviceConfig.playback.format = ma_format_f32;
    pImpl->deviceConfig.playback.channels = pImpl->channels;
    pImpl->deviceConfig.sampleRate = pImpl->sampleRate;
    pImpl->deviceConfig.dataCallback = audioCallback;
    pImpl->deviceConfig.pUserData = pImpl.get();
    
    result = ma_device_init(nullptr, &pImpl->deviceConfig, &pImpl->device);
    if (result != MA_SUCCESS) {
        ma_decoder_uninit(&pImpl->decoder);
        return false;
    }
    
    // Extract filename
    size_t lastSlash = filepath.find_last_of("/\\");
    pImpl->filename = (lastSlash != std::string::npos) ? 
                      filepath.substr(lastSlash + 1) : filepath;
    
    pImpl->fileLoaded = true;
    pImpl->currentFrame = 0;
    
    // Update bands for new sample rate
    updateBands();
    
    // Initialize EQ filters for this sample rate
    pImpl->updateEQFilters();
    
    // Reset filter states
    for (int i = 0; i < EqualizerConfig::NUM_BANDS; ++i) {
        pImpl->eqFilters[i].reset();
    }
    
    return true;
}

void AudioAnalyzer::play() {
    if (!pImpl->fileLoaded) return;
    
    if (!pImpl->playing) {
        ma_device_start(&pImpl->device);
        pImpl->playing = true;
    }
}

void AudioAnalyzer::pause() {
    if (pImpl->playing) {
        pImpl->playing = false;
    }
}

void AudioAnalyzer::stop() {
    pImpl->playing = false;
    
    if (pImpl->fileLoaded) {
        ma_device_stop(&pImpl->device);
        ma_decoder_seek_to_pcm_frame(&pImpl->decoder, 0);
        pImpl->currentFrame = 0;
    }
    
    // Clear spectrum
    std::lock_guard<std::mutex> lock(pImpl->bufferMutex);
    std::fill(pImpl->smoothedMagnitudes.begin(), pImpl->smoothedMagnitudes.end(), 0.0);
    std::fill(pImpl->currentSpectrum.magnitudes.begin(), 
              pImpl->currentSpectrum.magnitudes.end(), 0.0);
}

void AudioAnalyzer::togglePlayPause() {
    if (pImpl->playing) {
        pause();
    } else {
        play();
    }
}

void AudioAnalyzer::seek(double positionSeconds) {
    if (!pImpl->fileLoaded) return;
    
    uint64_t frame = static_cast<uint64_t>(positionSeconds * pImpl->sampleRate);
    frame = (std::min)(frame, pImpl->totalFrames);
    
    ma_decoder_seek_to_pcm_frame(&pImpl->decoder, frame);
    pImpl->currentFrame = frame;
}

void AudioAnalyzer::setVolume(float volume) {
    pImpl->volume = std::clamp(volume, 0.0f, 1.0f);
}

double AudioAnalyzer::getPosition() const {
    if (pImpl->sampleRate == 0) return 0.0;
    return static_cast<double>(pImpl->currentFrame) / pImpl->sampleRate;
}

double AudioAnalyzer::getDuration() const {
    if (pImpl->sampleRate == 0) return 0.0;
    return static_cast<double>(pImpl->totalFrames) / pImpl->sampleRate;
}

bool AudioAnalyzer::isPlaying() const {
    return pImpl->playing;
}

bool AudioAnalyzer::isLoaded() const {
    return pImpl->fileLoaded;
}

uint32_t AudioAnalyzer::getSampleRate() const {
    return pImpl->sampleRate;
}

const std::string& AudioAnalyzer::getFilename() const {
    return pImpl->filename;
}

void AudioAnalyzer::setConfig(const AnalyzerConfig& config) {
    pImpl->config = config;
    
    // Resize buffers
    pImpl->sampleBuffer.resize(config.fftSize * 2, 0.0f);
    pImpl->smoothedMagnitudes.resize(config.numBands, 0.0);
    pImpl->currentSpectrum.magnitudes.resize(config.numBands, 0.0);
    pImpl->currentSpectrum.frequencies.resize(config.numBands, 0.0);
    
    updateBands();
}

const AnalyzerConfig& AudioAnalyzer::getConfig() const {
    return pImpl->config;
}

void AudioAnalyzer::processAudioData(const float* samples, size_t frameCount, uint32_t channels) {
    std::lock_guard<std::mutex> lock(pImpl->bufferMutex);
    
    // Mix to mono and copy to circular buffer
    for (size_t i = 0; i < frameCount; ++i) {
        float sample = 0.0f;
        for (uint32_t c = 0; c < channels; ++c) {
            sample += samples[i * channels + c];
        }
        sample /= static_cast<float>(channels);
        
        pImpl->sampleBuffer[pImpl->bufferWritePos] = sample;
        pImpl->bufferWritePos = (pImpl->bufferWritePos + 1) % pImpl->sampleBuffer.size();
    }
}

void AudioAnalyzer::computeSpectrum() {
    std::lock_guard<std::mutex> lock(pImpl->bufferMutex);
    
    size_t fftSize = pImpl->config.fftSize;
    
    // Extract samples from circular buffer
    std::vector<double> samples(fftSize);
    size_t readPos = (pImpl->bufferWritePos + pImpl->sampleBuffer.size() - fftSize) 
                     % pImpl->sampleBuffer.size();
    
    for (size_t i = 0; i < fftSize; ++i) {
        samples[i] = static_cast<double>(pImpl->sampleBuffer[(readPos + i) % pImpl->sampleBuffer.size()]);
    }
    
    // Compute RMS
    double rms = 0.0;
    double peak = 0.0;
    for (const auto& s : samples) {
        rms += s * s;
        peak = (std::max)(peak, std::abs(s));
    }
    rms = std::sqrt(rms / samples.size());
    pImpl->currentSpectrum.rmsLevel = rms;
    pImpl->currentSpectrum.peakLevel = peak;
    
    // Apply window
    samples = fft::applyHannWindow(samples);
    
    // Compute FFT
    auto spectrum = fft::transform(samples);
    auto magnitudes = fft::magnitude(spectrum);
    
    // Normalize magnitudes by FFT size (proper scaling for amplitude)
    double normFactor = 2.0 / fftSize;  // Factor of 2 because we only use half the spectrum
    for (auto& m : magnitudes) {
        m *= normFactor;
    }
    
    // Only use first half (positive frequencies)
    size_t halfSize = fftSize / 2;
    
    // Map to frequency bands
    double maxMag = 0.0;
    size_t peakBin = 0;
    
    for (size_t band = 0; band < pImpl->config.numBands; ++band) {
        auto [startBin, endBin] = pImpl->bandBins[band];
        
        if (startBin >= halfSize) {
            pImpl->currentSpectrum.magnitudes[band] = 0.0;
            continue;
        }
        
        endBin = (std::min)(endBin, halfSize - 1);
        
        // Use max magnitude in band (better for peaks) or average
        double bandMax = 0.0;
        double sum = 0.0;
        size_t count = 0;
        
        for (size_t bin = startBin; bin <= endBin; ++bin) {
            double mag = magnitudes[bin];
            sum += mag;
            count++;
            
            if (mag > bandMax) {
                bandMax = mag;
            }
            
            if (mag > maxMag) {
                maxMag = mag;
                peakBin = bin;
            }
        }
        
        // Use max for better peak representation (like professional analyzers)
        double bandMag = bandMax;
        
        // Apply smoothing
        pImpl->smoothedMagnitudes[band] = 
            pImpl->config.smoothingFactor * pImpl->smoothedMagnitudes[band] +
            (1.0 - pImpl->config.smoothingFactor) * bandMag;
        
        pImpl->currentSpectrum.magnitudes[band] = pImpl->smoothedMagnitudes[band];
    }
    
    // Calculate peak frequency
    double binWidth = static_cast<double>(pImpl->sampleRate) / fftSize;
    pImpl->currentSpectrum.peakFrequency = peakBin * binWidth;
}

void AudioAnalyzer::updateBands() {
    double minFreq = pImpl->config.minFrequency;
    double maxFreq = (std::min)(pImpl->config.maxFrequency, 
                               static_cast<double>(pImpl->sampleRate) / 2.0);
    size_t numBands = pImpl->config.numBands;
    size_t fftSize = pImpl->config.fftSize;
    
    pImpl->bandFrequencies.resize(numBands);
    pImpl->bandBins.resize(numBands);
    
    double binWidth = static_cast<double>(pImpl->sampleRate) / fftSize;
    
    if (pImpl->config.useLogScale) {
        // Logarithmic frequency scale
        double logMin = std::log10(minFreq);
        double logMax = std::log10(maxFreq);
        double logStep = (logMax - logMin) / numBands;
        
        for (size_t i = 0; i < numBands; ++i) {
            double freqLow = std::pow(10.0, logMin + i * logStep);
            double freqHigh = std::pow(10.0, logMin + (i + 1) * logStep);
            double freqCenter = std::sqrt(freqLow * freqHigh); // Geometric mean
            
            pImpl->bandFrequencies[i] = freqCenter;
            pImpl->currentSpectrum.frequencies[i] = freqCenter;
            
            size_t binLow = static_cast<size_t>(freqLow / binWidth);
            size_t binHigh = static_cast<size_t>(freqHigh / binWidth);
            
            // Ensure at least one bin per band
            if (binHigh <= binLow) binHigh = binLow + 1;
            
            pImpl->bandBins[i] = {binLow, binHigh};
        }
    } else {
        // Linear frequency scale
        double freqStep = (maxFreq - minFreq) / numBands;
        
        for (size_t i = 0; i < numBands; ++i) {
            double freqLow = minFreq + i * freqStep;
            double freqHigh = freqLow + freqStep;
            double freqCenter = (freqLow + freqHigh) / 2.0;
            
            pImpl->bandFrequencies[i] = freqCenter;
            pImpl->currentSpectrum.frequencies[i] = freqCenter;
            
            size_t binLow = static_cast<size_t>(freqLow / binWidth);
            size_t binHigh = static_cast<size_t>(freqHigh / binWidth);
            
            if (binHigh <= binLow) binHigh = binLow + 1;
            
            pImpl->bandBins[i] = {binLow, binHigh};
        }
    }
}

SpectrumData AudioAnalyzer::getSpectrum() {
    computeSpectrum();
    return pImpl->currentSpectrum;
}

EqualizerConfig& AudioAnalyzer::getEqualizer() {
    return pImpl->eqConfig;
}

const EqualizerConfig& AudioAnalyzer::getEqualizer() const {
    return pImpl->eqConfig;
}

void AudioAnalyzer::setEQBandGain(int bandIndex, double gainDb) {
    if (bandIndex >= 0 && bandIndex < EqualizerConfig::NUM_BANDS) {
        // Clamp gain to -12 to +12 dB
        gainDb = (std::max)(-12.0, (std::min)(12.0, gainDb));
        pImpl->eqConfig.bands[bandIndex].gain = gainDb;
        pImpl->eqNeedsUpdate = true;
    }
}

double AudioAnalyzer::getEQBandGain(int bandIndex) const {
    if (bandIndex >= 0 && bandIndex < EqualizerConfig::NUM_BANDS) {
        return pImpl->eqConfig.bands[bandIndex].gain;
    }
    return 0.0;
}

void AudioAnalyzer::setEQBandFrequency(int bandIndex, double frequency) {
    if (bandIndex >= 0 && bandIndex < EqualizerConfig::NUM_BANDS) {
        // Clamp frequency to audible range
        frequency = (std::max)(20.0, (std::min)(20000.0, frequency));
        pImpl->eqConfig.bands[bandIndex].frequency = frequency;
        pImpl->eqNeedsUpdate = true;
    }
}

double AudioAnalyzer::getEQBandFrequency(int bandIndex) const {
    if (bandIndex >= 0 && bandIndex < EqualizerConfig::NUM_BANDS) {
        return pImpl->eqConfig.bands[bandIndex].frequency;
    }
    return 1000.0;
}

void AudioAnalyzer::setEQBandQ(int bandIndex, double q) {
    if (bandIndex >= 0 && bandIndex < EqualizerConfig::NUM_BANDS) {
        // Clamp Q to reasonable range
        q = (std::max)(0.1, (std::min)(10.0, q));
        pImpl->eqConfig.bands[bandIndex].q = q;
        pImpl->eqNeedsUpdate = true;
    }
}

double AudioAnalyzer::getEQBandQ(int bandIndex) const {
    if (bandIndex >= 0 && bandIndex < EqualizerConfig::NUM_BANDS) {
        return pImpl->eqConfig.bands[bandIndex].q;
    }
    return 1.0;
}

void AudioAnalyzer::setEQEnabled(bool enabled) {
    pImpl->eqConfig.enabled = enabled;
}

bool AudioAnalyzer::isEQEnabled() const {
    return pImpl->eqConfig.enabled;
}

} // namespace audio


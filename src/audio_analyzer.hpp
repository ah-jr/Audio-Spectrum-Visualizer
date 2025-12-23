#pragma once

#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>

namespace audio {
// Forward declaration for pImpl
struct AudioAnalyzerImpl;
}

namespace audio {

/**
 * Equalizer band structure
 */
struct EQBand {
    double frequency;    // Center frequency in Hz
    double gain;         // Gain in dB (-12 to +12)
    double q;            // Q factor (bandwidth)
    bool enabled;
    
    EQBand(double freq = 1000.0, double g = 0.0, double qFactor = 1.0)
        : frequency(freq), gain(g), q(qFactor), enabled(true) {}
};

/**
 * 5-band Equalizer configuration
 */
struct EqualizerConfig {
    static constexpr int NUM_BANDS = 5;
    EQBand bands[NUM_BANDS] = {
        EQBand(60.0, 0.0, 0.7),      // Sub/Bass
        EQBand(250.0, 0.0, 0.7),     // Low-Mid
        EQBand(1000.0, 0.0, 0.7),    // Mid
        EQBand(4000.0, 0.0, 0.7),    // High-Mid
        EQBand(12000.0, 0.0, 0.7)    // Treble
    };
    bool enabled = true;
};

/**
 * Configuration for the audio analyzer
 */
struct AnalyzerConfig {
    size_t fftSize = 4096;           // FFT window size (power of 2)
    size_t hopSize = 1024;           // Hop size for overlapping windows
    double smoothingFactor = 0.7;     // Temporal smoothing (0-1)
    double minFrequency = 20.0;       // Minimum frequency to display
    double maxFrequency = 20000.0;    // Maximum frequency to display
    size_t numBands = 128;            // Number of frequency bands for visualization
    bool useLogScale = true;          // Use logarithmic frequency scale
};

/**
 * Represents the current spectrum analysis result
 */
struct SpectrumData {
    std::vector<double> magnitudes;   // Magnitude values per band
    std::vector<double> frequencies;  // Center frequency of each band
    double peakFrequency = 0.0;       // Dominant frequency
    double rmsLevel = 0.0;            // RMS level of current frame
    double peakLevel = 0.0;           // Peak level of current frame
};

/**
 * Audio Analyzer class
 * Loads and plays audio files while providing real-time spectrum analysis
 */
class AudioAnalyzer {
public:
    AudioAnalyzer();
    ~AudioAnalyzer();

    // Prevent copying
    AudioAnalyzer(const AudioAnalyzer&) = delete;
    AudioAnalyzer& operator=(const AudioAnalyzer&) = delete;

    /**
     * Initialize the audio system
     * @return True if successful
     */
    bool initialize();

    /**
     * Load an audio file
     * @param filepath Path to the audio file (WAV, MP3, FLAC, etc.)
     * @return True if loaded successfully
     */
    bool loadFile(const std::string& filepath);

    /**
     * Start playback
     */
    void play();

    /**
     * Pause playback
     */
    void pause();

    /**
     * Stop playback and reset position
     */
    void stop();

    /**
     * Toggle play/pause
     */
    void togglePlayPause();

    /**
     * Seek to a position
     * @param positionSeconds Position in seconds
     */
    void seek(double positionSeconds);

    /**
     * Set playback volume
     * @param volume Volume level (0.0 - 1.0)
     */
    void setVolume(float volume);

    /**
     * Get current playback position
     * @return Position in seconds
     */
    double getPosition() const;

    /**
     * Get total duration
     * @return Duration in seconds
     */
    double getDuration() const;

    /**
     * Check if audio is currently playing
     * @return True if playing
     */
    bool isPlaying() const;

    /**
     * Check if an audio file is loaded
     * @return True if file is loaded
     */
    bool isLoaded() const;

    /**
     * Get the sample rate of the loaded audio
     * @return Sample rate in Hz
     */
    uint32_t getSampleRate() const;

    /**
     * Get current spectrum data
     * @return SpectrumData struct with current analysis
     */
    SpectrumData getSpectrum();

    /**
     * Update analyzer configuration
     * @param config New configuration
     */
    void setConfig(const AnalyzerConfig& config);

    /**
     * Get current configuration
     * @return Current configuration
     */
    const AnalyzerConfig& getConfig() const;

    /**
     * Get the loaded filename
     * @return Filename string
     */
    const std::string& getFilename() const;

    /**
     * Process audio data (called internally during playback)
     */
    void processAudioData(const float* samples, size_t frameCount, uint32_t channels);

    /**
     * Get current equalizer configuration
     * @return Reference to equalizer config
     */
    EqualizerConfig& getEqualizer();
    const EqualizerConfig& getEqualizer() const;

    /**
     * Set EQ band gain
     * @param bandIndex Band index (0-4)
     * @param gainDb Gain in dB (-12 to +12)
     */
    void setEQBandGain(int bandIndex, double gainDb);

    /**
     * Get EQ band gain
     * @param bandIndex Band index (0-4)
     * @return Gain in dB
     */
    double getEQBandGain(int bandIndex) const;

    /**
     * Set EQ band frequency
     * @param bandIndex Band index (0-4)
     * @param frequency Frequency in Hz (20-20000)
     */
    void setEQBandFrequency(int bandIndex, double frequency);

    /**
     * Get EQ band frequency
     * @param bandIndex Band index (0-4)
     * @return Frequency in Hz
     */
    double getEQBandFrequency(int bandIndex) const;

    /**
     * Set EQ band Q factor
     * @param bandIndex Band index (0-4)
     * @param q Q factor (0.1 to 10.0)
     */
    void setEQBandQ(int bandIndex, double q);

    /**
     * Get EQ band Q factor
     * @param bandIndex Band index (0-4)
     * @return Q factor
     */
    double getEQBandQ(int bandIndex) const;

    /**
     * Enable/disable the equalizer
     * @param enabled True to enable
     */
    void setEQEnabled(bool enabled);

    /**
     * Check if equalizer is enabled
     * @return True if enabled
     */
    bool isEQEnabled() const;

private:
    std::unique_ptr<AudioAnalyzerImpl> pImpl;

    void computeSpectrum();
    void updateBands();
    double frequencyToBand(double freq) const;
};

} // namespace audio


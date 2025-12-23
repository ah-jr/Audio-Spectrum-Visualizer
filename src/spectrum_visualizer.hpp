#pragma once

#include "audio_analyzer.hpp"
#include <raylib.h>
#include <vector>
#include <string>

namespace viz {

/**
 * Visualization style
 */
enum class VisualizerStyle {
    Line,           // Filled line graph (like professional analyzers)
    Bars,           // Classic bar visualization
    Waves,          // Smooth wave visualization
    Circles,        // Circular spectrum
    Particles,      // Particle-based visualization
    Mirror          // Mirrored bars
};

/**
 * Color theme for visualization
 */
struct ColorTheme {
    Color background;
    Color barLow;
    Color barMid;
    Color barHigh;
    Color accent;
    Color text;
    Color textDim;
    
    static ColorTheme Neon();
    static ColorTheme Sunset();
    static ColorTheme Ocean();
    static ColorTheme Monochrome();
    static ColorTheme Cyberpunk();
};

/**
 * Visualizer configuration
 */
struct VisualizerConfig {
    int windowWidth = 1280;
    int windowHeight = 720;
    int targetFps = 60;
    
    VisualizerStyle style = VisualizerStyle::Bars;
    ColorTheme theme = ColorTheme::Cyberpunk();
    
    float barSpacing = 2.0f;       // Space between bars
    float barMinHeight = 4.0f;     // Minimum bar height
    float barRounding = 2.0f;      // Corner rounding for bars
    float sensitivity = 1.5f;       // Visual sensitivity multiplier
    float peakDecay = 0.02f;       // Peak indicator decay rate
    
    bool showPeaks = true;          // Show peak indicators
    bool showGrid = true;           // Show frequency grid
    bool showInfo = true;           // Show audio info
    bool showWaveform = false;      // Show waveform overlay
    bool mirrorVertical = false;    // Mirror visualization vertically
};

/**
 * Spectrum Visualizer class
 * Renders real-time audio spectrum using raylib
 */
class SpectrumVisualizer {
public:
    SpectrumVisualizer();
    ~SpectrumVisualizer();
    
    /**
     * Initialize the visualizer window
     * @param config Visualizer configuration
     * @return True if successful
     */
    bool initialize(const VisualizerConfig& config = VisualizerConfig());
    
    /**
     * Main render loop - call once per frame
     * @param spectrum Current spectrum data from analyzer
     * @param analyzer Reference to audio analyzer for metadata
     */
    void render(const audio::SpectrumData& spectrum, audio::AudioAnalyzer& analyzer);
    
    /**
     * Check if window should close
     * @return True if close requested
     */
    bool shouldClose() const;
    
    /**
     * Handle user input
     * @param analyzer Reference to audio analyzer for control
     */
    void handleInput(audio::AudioAnalyzer& analyzer);
    
    /**
     * Set visualization style
     * @param style New style
     */
    void setStyle(VisualizerStyle style);
    
    /**
     * Set color theme
     * @param theme New theme
     */
    void setTheme(const ColorTheme& theme);
    
    /**
     * Cycle to next theme
     */
    void nextTheme();
    
    /**
     * Cycle to next style
     */
    void nextStyle();
    
    /**
     * Get current configuration
     * @return Current configuration
     */
    const VisualizerConfig& getConfig() const;
    
    /**
     * Update configuration
     * @param config New configuration
     */
    void setConfig(const VisualizerConfig& config);
    
private:
    VisualizerConfig config_;
    std::vector<float> peaks_;
    std::vector<float> velocities_;
    std::vector<ColorTheme> themes_;
    size_t currentTheme_ = 0;
    
    Font font_;
    bool fontLoaded_ = false;
    
    // Particle system for particle visualization
    struct Particle {
        Vector2 position;
        Vector2 velocity;
        float life;
        float size;
        Color color;
    };
    std::vector<Particle> particles_;
    
    // Rendering methods
    void renderLine(const audio::SpectrumData& spectrum);
    void renderBars(const audio::SpectrumData& spectrum);
    void renderWaves(const audio::SpectrumData& spectrum);
    void renderCircles(const audio::SpectrumData& spectrum);
    void renderParticles(const audio::SpectrumData& spectrum);
    void renderMirror(const audio::SpectrumData& spectrum);
    
    // Peak hold for line visualization
    std::vector<float> peakHold_;
    std::vector<float> peakHoldDecay_;
    
    // EQ control state
    struct EQControl {
        float x, y;           // Screen position
        float frequency;      // Center frequency in Hz
        float gain;           // Current gain in dB
        float q;              // Q factor
        bool dragging;        // Is being dragged
        bool hovered;         // Mouse is over it
    };
    EQControl eqControls_[5];
    int draggedEQBand_ = -1;
    float dragStartX_ = 0;
    float dragStartY_ = 0;
    float dragStartFreq_ = 0;
    float dragStartGain_ = 0;
    
    // EQ control rendering
    void renderEQControls(audio::AudioAnalyzer& analyzer);
    void handleEQInput(audio::AudioAnalyzer& analyzer);
    void drawEQSpline(audio::AudioAnalyzer& analyzer, int marginLeft, int graphWidth, 
                      int marginTop, int graphHeight, int baseY);
    
    void renderGrid();
    void renderInfo(const audio::AudioAnalyzer& analyzer, const audio::SpectrumData& spectrum);
    void renderControls();
    void renderProgressBar(const audio::AudioAnalyzer& analyzer);
    
    void updatePeaks(const audio::SpectrumData& spectrum);
    void updateParticles(const audio::SpectrumData& spectrum);
    
    Color getBarColor(float normalizedFreq, float magnitude) const;
    Color lerpColor(Color a, Color b, float t) const;
    
    std::string formatTime(double seconds) const;
    std::string formatFrequency(double hz) const;
};

} // namespace viz


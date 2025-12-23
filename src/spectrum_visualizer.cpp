#include "spectrum_visualizer.hpp"
#include "shared_colors.hpp"
#include "eq_processor.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdio>

// Safe min/max macros (avoid potential macro conflicts)
#define SAFE_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define SAFE_MAX(a, b) (((a) > (b)) ? (a) : (b))

namespace viz {

// Helper to convert shared RGBA to raylib Color
static Color toRaylibColor(const colors::RGBA& c) {
    return {c.r, c.g, c.b, c.a};
}

// Helper to convert shared ThemeColors to ColorTheme
static ColorTheme fromSharedTheme(const colors::ThemeColors& t) {
    return {
        toRaylibColor(t.background),
        toRaylibColor(t.barLow),
        toRaylibColor(t.barMid),
        toRaylibColor(t.barHigh),
        toRaylibColor(t.accent),
        toRaylibColor(t.text),
        toRaylibColor(t.textDim)
    };
}

// Color theme implementations using shared colors
ColorTheme ColorTheme::Neon() { return fromSharedTheme(colors::themes::Neon); }
ColorTheme ColorTheme::Sunset() { return fromSharedTheme(colors::themes::Sunset); }
ColorTheme ColorTheme::Ocean() { return fromSharedTheme(colors::themes::Ocean); }
ColorTheme ColorTheme::Monochrome() { return fromSharedTheme(colors::themes::Monochrome); }
ColorTheme ColorTheme::Cyberpunk() { return fromSharedTheme(colors::themes::Cyberpunk); }

SpectrumVisualizer::SpectrumVisualizer() {
    // Initialize theme list
    themes_ = {
        ColorTheme::Cyberpunk(),
        ColorTheme::Neon(),
        ColorTheme::Sunset(),
        ColorTheme::Ocean(),
        ColorTheme::Monochrome()
    };
}

SpectrumVisualizer::~SpectrumVisualizer() {
    if (fontLoaded_) {
        UnloadFont(font_);
    }
    CloseWindow();
}

bool SpectrumVisualizer::initialize(const VisualizerConfig& config) {
    config_ = config;
    
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(config_.windowWidth, config_.windowHeight, "Audio Spectrum Visualizer");
    SetTargetFPS(config_.targetFps);
    
    // Try to load a nice font, fall back to default
    font_ = GetFontDefault();
    fontLoaded_ = false;
    
    // Initialize peaks array
    peaks_.resize(256, 0.0f);
    velocities_.resize(256, 0.0f);
    
    return true;
}

bool SpectrumVisualizer::shouldClose() const {
    return WindowShouldClose();
}

void SpectrumVisualizer::handleInput(audio::AudioAnalyzer& analyzer) {
    // Space - play/pause
    if (IsKeyPressed(KEY_SPACE)) {
        analyzer.togglePlayPause();
    }
    
    // Left/Right arrows - seek
    if (IsKeyPressed(KEY_RIGHT)) {
        analyzer.seek(analyzer.getPosition() + 5.0);
    }
    if (IsKeyPressed(KEY_LEFT)) {
        analyzer.seek(SAFE_MAX(0.0, analyzer.getPosition() - 5.0));
    }
    
    // Up/Down arrows - volume
    if (IsKeyPressed(KEY_UP)) {
        // Volume is internal, we could expose it
    }
    if (IsKeyPressed(KEY_DOWN)) {
        // Volume control
    }
    
    // T - change theme
    if (IsKeyPressed(KEY_T)) {
        nextTheme();
    }
    
    // S - change style
    if (IsKeyPressed(KEY_S)) {
        nextStyle();
    }
    
    // G - toggle grid
    if (IsKeyPressed(KEY_G)) {
        config_.showGrid = !config_.showGrid;
    }
    
    // I - toggle info
    if (IsKeyPressed(KEY_I)) {
        config_.showInfo = !config_.showInfo;
    }
    
    // P - toggle peaks
    if (IsKeyPressed(KEY_P)) {
        config_.showPeaks = !config_.showPeaks;
    }
    
    // M - toggle mirror
    if (IsKeyPressed(KEY_M)) {
        config_.mirrorVertical = !config_.mirrorVertical;
    }
    
    // E - toggle EQ
    if (IsKeyPressed(KEY_E)) {
        analyzer.setEQEnabled(!analyzer.isEQEnabled());
    }
    
    // R - reset EQ to defaults
    if (IsKeyPressed(KEY_R)) {
        for (int i = 0; i < eq::NUM_BANDS; ++i) {
            analyzer.setEQBandGain(i, 0.0);
            analyzer.setEQBandFrequency(i, eq::DEFAULT_FREQUENCIES[i]);
            analyzer.setEQBandQ(i, eq::DEFAULT_Q);
        }
    }
    
    // Handle control bar interactions
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        int height = GetScreenHeight();
        int width = GetScreenWidth();
        
        int controlBarHeight = 70;
        int controlBarY = height - controlBarHeight;
        int buttonSize = 36;
        int buttonSpacing = 10;
        int buttonY = controlBarY + 20;
        int leftMargin = 20;
        
        // Check if click is in control bar area
        if (mouse.y >= controlBarY) {
            int buttonX = leftMargin;
            
            // Stop button
            Rectangle stopBtn = {static_cast<float>(buttonX), static_cast<float>(buttonY), 
                                 static_cast<float>(buttonSize), static_cast<float>(buttonSize)};
            if (CheckCollisionPointRec(mouse, stopBtn)) {
                analyzer.stop();
            }
            
            buttonX += buttonSize + buttonSpacing;
            
            // Play/Pause button
            Rectangle playBtn = {static_cast<float>(buttonX), static_cast<float>(buttonY), 
                                 static_cast<float>(buttonSize), static_cast<float>(buttonSize)};
            if (CheckCollisionPointRec(mouse, playBtn)) {
                analyzer.togglePlayPause();
            }
            
            buttonX += buttonSize + buttonSpacing + 15;
            
            // Progress bar click to seek
            if (analyzer.isLoaded()) {
                int progressBarX = buttonX;
                int progressBarWidth = width - progressBarX - 180;
                int progressBarY = buttonY + 8;
                int progressBarHeight = 20;
                
                Rectangle progressBar = {static_cast<float>(progressBarX), static_cast<float>(progressBarY),
                                         static_cast<float>(progressBarWidth), static_cast<float>(progressBarHeight)};
                if (CheckCollisionPointRec(mouse, progressBar)) {
                    float progress = (mouse.x - progressBarX) / progressBarWidth;
                    progress = std::clamp(progress, 0.0f, 1.0f);
                    analyzer.seek(progress * analyzer.getDuration());
                }
            }
        }
    }
}

void SpectrumVisualizer::render(const audio::SpectrumData& spectrum, audio::AudioAnalyzer& analyzer) {
    // Handle EQ input first (before BeginDrawing for proper mouse handling)
    handleEQInput(analyzer);
    
    BeginDrawing();
    
    // Clear with gradient background
    ClearBackground(config_.theme.background);
    
    // Draw subtle gradient overlay
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), 
                           Fade(config_.theme.barMid, 0.05f), 
                           Fade(config_.theme.background, 0.0f));
    
    // Update peaks
    updatePeaks(spectrum);
    
    // Render based on style
    switch (config_.style) {
        case VisualizerStyle::Line:
            // Line mode has its own grid built-in
            renderLine(spectrum);
            renderEQControls(analyzer);
            break;
        case VisualizerStyle::Bars:
            if (config_.showGrid) renderGrid();
            renderBars(spectrum);
            break;
        case VisualizerStyle::Waves:
            if (config_.showGrid) renderGrid();
            renderWaves(spectrum);
            break;
        case VisualizerStyle::Circles:
            if (config_.showGrid) renderGrid();
            renderCircles(spectrum);
            break;
        case VisualizerStyle::Particles:
            if (config_.showGrid) renderGrid();
            updateParticles(spectrum);
            renderParticles(spectrum);
            break;
        case VisualizerStyle::Mirror:
            if (config_.showGrid) renderGrid();
            renderMirror(spectrum);
            break;
    }
    
    // Render UI elements
    renderProgressBar(analyzer);
    
    if (config_.showInfo) {
        renderInfo(analyzer, spectrum);
    }
    
    renderControls();
    
    EndDrawing();
}

void SpectrumVisualizer::renderLine(const audio::SpectrumData& spectrum) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    
    size_t numBands = spectrum.magnitudes.size();
    if (numBands == 0) return;
    
    // Define drawing area with margins
    int marginLeft = 55;
    int marginRight = 15;
    int marginTop = 50;
    int marginBottom = 90;  // Space for control bar (70px) + some padding
    
    int graphWidth = width - marginLeft - marginRight;
    int graphHeight = height - marginTop - marginBottom;
    int baseY = height - marginBottom;
    
    // dB range settings
    float dbMin = -60.0f;  // Bottom of display
    float dbMax = 0.0f;    // Top of display
    float dbRange = dbMax - dbMin;
    
    // Initialize peak hold if needed
    if (peakHold_.size() != numBands) {
        peakHold_.resize(numBands, dbMin);
        peakHoldDecay_.resize(numBands, 0.0f);
    }
    
    // Draw dB scale on left side
    Color gridColor = Fade(config_.theme.textDim, 0.3f);
    for (float db = dbMin; db <= dbMax; db += 6.0f) {
        float yNorm = (db - dbMin) / dbRange;
        int y = baseY - static_cast<int>(yNorm * graphHeight);
        
        // Horizontal grid line
        DrawLine(marginLeft, y, width - marginRight, y, gridColor);
        
        // dB label
        char label[16];
        snprintf(label, sizeof(label), "%+.0f", db);
        DrawText(label, 5, y - 7, 14, config_.theme.textDim);
    }
    
    // Draw frequency scale on bottom
    const double freqMarkers[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (int i = 0; i < 10; ++i) {
        float logPos = (std::log10(freqMarkers[i]) - std::log10(20.0)) / 
                       (std::log10(20000.0) - std::log10(20.0));
        int x = marginLeft + static_cast<int>(logPos * graphWidth);
        
        // Vertical grid line
        DrawLine(x, marginTop, x, baseY, gridColor);
        
        // Frequency label
        std::string label = formatFrequency(freqMarkers[i]);
        int textWidth = MeasureText(label.c_str(), 12);
        DrawText(label.c_str(), x - textWidth / 2, baseY + 8, 12, config_.theme.textDim);
    }
    
    // Convert magnitudes to dB and build points
    std::vector<Vector2> points(numBands);
    std::vector<float> dbValues(numBands);
    
    for (size_t i = 0; i < numBands; ++i) {
        // Calculate x position (logarithmic frequency scale)
        float freqNorm = static_cast<float>(i) / (numBands - 1);
        float x = marginLeft + freqNorm * graphWidth;
        
        // Convert magnitude to dB
        float mag = static_cast<float>(spectrum.magnitudes[i]) * config_.sensitivity;
        float db;
        if (mag > 0.00001f) {
            db = 20.0f * std::log10(mag);
            db = std::clamp(db, dbMin, dbMax);
        } else {
            db = dbMin;
        }
        dbValues[i] = db;
        
        // Update peak hold
        if (db > peakHold_[i]) {
            peakHold_[i] = db;
            peakHoldDecay_[i] = 0.0f;
        } else {
            peakHoldDecay_[i] += 0.15f;
            peakHold_[i] -= peakHoldDecay_[i] * 0.1f;
            peakHold_[i] = SAFE_MAX(peakHold_[i], dbMin);
        }
        
        // Calculate y position
        float yNorm = (db - dbMin) / dbRange;
        float y = baseY - yNorm * graphHeight;
        
        points[i] = {x, y};
    }
    
    // Draw filled area under the curve
    for (size_t i = 0; i < numBands - 1; ++i) {
        float normalizedFreq = static_cast<float>(i) / numBands;
        float mag = (dbValues[i] - dbMin) / dbRange;
        Color fillColor = Fade(getBarColor(normalizedFreq, mag), 0.6f);
        
        // Draw filled quad from this point to next point down to baseline
        Vector2 v1 = points[i];
        Vector2 v2 = points[i + 1];
        Vector2 v3 = {points[i + 1].x, static_cast<float>(baseY)};
        Vector2 v4 = {points[i].x, static_cast<float>(baseY)};
        
        DrawTriangle(v1, v4, v3, fillColor);
        DrawTriangle(v1, v3, v2, fillColor);
    }
    
    // Draw the main spectrum line (brighter)
    for (size_t i = 0; i < numBands - 1; ++i) {
        float normalizedFreq = static_cast<float>(i) / numBands;
        float mag = (dbValues[i] - dbMin) / dbRange;
        Color lineColor = getBarColor(normalizedFreq, mag);
        
        DrawLineEx(points[i], points[i + 1], 2.0f, lineColor);
    }
    
    // Draw peak hold line (white/gray)
    if (config_.showPeaks) {
        for (size_t i = 0; i < numBands - 1; ++i) {
            float freqNorm1 = static_cast<float>(i) / (numBands - 1);
            float freqNorm2 = static_cast<float>(i + 1) / (numBands - 1);
            
            float x1 = marginLeft + freqNorm1 * graphWidth;
            float x2 = marginLeft + freqNorm2 * graphWidth;
            
            float yNorm1 = (peakHold_[i] - dbMin) / dbRange;
            float yNorm2 = (peakHold_[i + 1] - dbMin) / dbRange;
            
            float y1 = baseY - yNorm1 * graphHeight;
            float y2 = baseY - yNorm2 * graphHeight;
            
            DrawLineEx({x1, y1}, {x2, y2}, 1.5f, Fade(config_.theme.text, 0.7f));
        }
    }
    
    // Draw border
    DrawRectangleLines(marginLeft, marginTop, graphWidth, graphHeight, config_.theme.textDim);
}

void SpectrumVisualizer::renderBars(const audio::SpectrumData& spectrum) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    int controlBarHeight = 70;
    
    size_t numBands = spectrum.magnitudes.size();
    if (numBands == 0) return;
    
    float barWidth = (width - config_.barSpacing * (numBands - 1)) / numBands;
    float maxHeight = (height - controlBarHeight) * 0.7f;
    
    int baseY = config_.mirrorVertical ? (height - controlBarHeight) / 2 : height - controlBarHeight - 10;
    
    for (size_t i = 0; i < numBands; ++i) {
        float x = i * (barWidth + config_.barSpacing);
        float magnitude = static_cast<float>(spectrum.magnitudes[i]) * config_.sensitivity;
        magnitude = std::clamp(magnitude, 0.0f, 1.0f);
        
        float barHeight = SAFE_MAX(config_.barMinHeight, magnitude * maxHeight);
        
        float normalizedFreq = static_cast<float>(i) / numBands;
        Color barColor = getBarColor(normalizedFreq, magnitude);
        
        // Add glow effect
        Color glowColor = Fade(barColor, 0.3f);
        DrawRectangle(static_cast<int>(x - 2), baseY - static_cast<int>(barHeight) - 2, 
                      static_cast<int>(barWidth + 4), static_cast<int>(barHeight + 4), glowColor);
        
        // Main bar with rounded corners
        Rectangle rect = {x, static_cast<float>(baseY - barHeight), barWidth, barHeight};
        DrawRectangleRounded(rect, config_.barRounding / barWidth, 4, barColor);
        
        // Mirror if enabled
        if (config_.mirrorVertical) {
            Rectangle mirrorRect = {x, static_cast<float>(baseY), barWidth, barHeight};
            Color mirrorColor = Fade(barColor, 0.5f);
            DrawRectangleRounded(mirrorRect, config_.barRounding / barWidth, 4, mirrorColor);
        }
        
        // Peak indicator
        if (config_.showPeaks && i < peaks_.size()) {
            float peakHeight = peaks_[i] * maxHeight;
            int peakY = baseY - static_cast<int>(peakHeight);
            DrawRectangle(static_cast<int>(x), peakY - 3, static_cast<int>(barWidth), 3, 
                          config_.theme.accent);
        }
    }
}

void SpectrumVisualizer::renderWaves(const audio::SpectrumData& spectrum) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    
    size_t numBands = spectrum.magnitudes.size();
    if (numBands == 0) return;
    
    int centerY = height / 2;
    float maxAmplitude = height * 0.35f;
    
    // Draw multiple wave layers
    for (int layer = 2; layer >= 0; --layer) {
        float layerOffset = layer * 0.1f;
        float layerAlpha = 0.3f + layer * 0.25f;
        
        for (size_t i = 0; i < numBands - 1; ++i) {
            float x1 = static_cast<float>(i) * width / numBands;
            float x2 = static_cast<float>(i + 1) * width / numBands;
            
            float mag1 = static_cast<float>(spectrum.magnitudes[i]) * config_.sensitivity;
            float mag2 = static_cast<float>(spectrum.magnitudes[i + 1]) * config_.sensitivity;
            
            mag1 = std::clamp(mag1, 0.0f, 1.0f);
            mag2 = std::clamp(mag2, 0.0f, 1.0f);
            
            float y1 = centerY - (mag1 + layerOffset) * maxAmplitude;
            float y2 = centerY - (mag2 + layerOffset) * maxAmplitude;
            
            float normalizedFreq = static_cast<float>(i) / numBands;
            Color lineColor = Fade(getBarColor(normalizedFreq, mag1), layerAlpha);
            
            DrawLineEx({x1, y1}, {x2, y2}, 3.0f - layer, lineColor);
            
            // Mirror
            DrawLineEx({x1, centerY + (mag1 + layerOffset) * maxAmplitude}, 
                       {x2, centerY + (mag2 + layerOffset) * maxAmplitude}, 
                       3.0f - layer, Fade(lineColor, 0.5f));
        }
    }
}

void SpectrumVisualizer::renderCircles(const audio::SpectrumData& spectrum) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    
    size_t numBands = spectrum.magnitudes.size();
    if (numBands == 0) return;
    
    Vector2 center = {width / 2.0f, height / 2.0f};
    float baseRadius = SAFE_MIN(width, height) * 0.15f;
    float maxRadius = SAFE_MIN(width, height) * 0.35f;
    
    // Draw outer glow
    float avgMag = 0.0f;
    for (const auto& m : spectrum.magnitudes) {
        avgMag += static_cast<float>(m);
    }
    avgMag /= numBands;
    avgMag *= config_.sensitivity;
    
    DrawCircleGradient(static_cast<int>(center.x), static_cast<int>(center.y), 
                       baseRadius + avgMag * maxRadius * 1.5f, 
                       Fade(config_.theme.barMid, 0.0f), 
                       Fade(config_.theme.barMid, 0.2f));
    
    // Draw spectrum as radial bars
    for (size_t i = 0; i < numBands; ++i) {
        float angle = (static_cast<float>(i) / numBands) * 2.0f * PI - PI / 2;
        float magnitude = static_cast<float>(spectrum.magnitudes[i]) * config_.sensitivity;
        magnitude = std::clamp(magnitude, 0.0f, 1.0f);
        
        float innerRadius = baseRadius;
        float outerRadius = baseRadius + magnitude * maxRadius;
        
        Vector2 inner = {
            center.x + std::cos(angle) * innerRadius,
            center.y + std::sin(angle) * innerRadius
        };
        Vector2 outer = {
            center.x + std::cos(angle) * outerRadius,
            center.y + std::sin(angle) * outerRadius
        };
        
        float normalizedFreq = static_cast<float>(i) / numBands;
        Color barColor = getBarColor(normalizedFreq, magnitude);
        
        DrawLineEx(inner, outer, 3.0f, barColor);
    }
    
    // Center circle
    DrawCircle(static_cast<int>(center.x), static_cast<int>(center.y), 
               baseRadius * 0.3f, config_.theme.background);
    DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y), 
                    baseRadius * 0.3f, config_.theme.accent);
}

void SpectrumVisualizer::updateParticles(const audio::SpectrumData& spectrum) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    
    // Calculate average magnitude for particle spawning
    float avgMag = 0.0f;
    size_t numBands = spectrum.magnitudes.size();
    for (const auto& m : spectrum.magnitudes) {
        avgMag += static_cast<float>(m);
    }
    if (numBands > 0) avgMag /= numBands;
    avgMag *= config_.sensitivity;
    
    // Spawn new particles based on magnitude
    int particlesToSpawn = static_cast<int>(avgMag * 10);
    for (int i = 0; i < particlesToSpawn && particles_.size() < 1000; ++i) {
        Particle p;
        p.position = {static_cast<float>(GetRandomValue(0, width)), 
                      static_cast<float>(height + 10)};
        p.velocity = {static_cast<float>(GetRandomValue(-50, 50)) / 50.0f, 
                      -avgMag * 8.0f - 2.0f};
        p.life = 1.0f;
        p.size = 2.0f + avgMag * 5.0f;
        
        float normalizedPos = p.position.x / width;
        p.color = getBarColor(normalizedPos, avgMag);
        
        particles_.push_back(p);
    }
    
    // Update existing particles
    float dt = GetFrameTime();
    for (auto& p : particles_) {
        p.position.x += p.velocity.x * dt * 60.0f;
        p.position.y += p.velocity.y * dt * 60.0f;
        p.velocity.y += 0.05f; // Gravity
        p.life -= dt * 0.5f;
        p.size *= 0.99f;
    }
    
    // Remove dead particles
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [](const Particle& p) { return p.life <= 0 || p.size < 0.5f; }),
        particles_.end()
    );
}

void SpectrumVisualizer::renderParticles(const audio::SpectrumData& spectrum) {
    // Also render a subtle bar visualization behind particles
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    size_t numBands = spectrum.magnitudes.size();
    
    if (numBands > 0) {
        float barWidth = static_cast<float>(width) / numBands;
        for (size_t i = 0; i < numBands; ++i) {
            float magnitude = static_cast<float>(spectrum.magnitudes[i]) * config_.sensitivity * 0.5f;
            magnitude = std::clamp(magnitude, 0.0f, 1.0f);
            float barHeight = magnitude * height * 0.4f;
            
            float normalizedFreq = static_cast<float>(i) / numBands;
            Color barColor = Fade(getBarColor(normalizedFreq, magnitude), 0.2f);
            
            DrawRectangle(static_cast<int>(i * barWidth), height - static_cast<int>(barHeight) - 60,
                          static_cast<int>(barWidth), static_cast<int>(barHeight), barColor);
        }
    }
    
    // Render particles
    for (const auto& p : particles_) {
        Color color = Fade(p.color, p.life);
        DrawCircle(static_cast<int>(p.position.x), static_cast<int>(p.position.y), 
                   p.size, color);
    }
}

void SpectrumVisualizer::renderMirror(const audio::SpectrumData& spectrum) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    
    size_t numBands = spectrum.magnitudes.size();
    if (numBands == 0) return;
    
    float barWidth = (width / 2.0f - config_.barSpacing * numBands) / numBands;
    float maxHeight = height * 0.45f;
    int centerX = width / 2;
    int baseY = height / 2;
    
    for (size_t i = 0; i < numBands; ++i) {
        float magnitude = static_cast<float>(spectrum.magnitudes[i]) * config_.sensitivity;
        magnitude = std::clamp(magnitude, 0.0f, 1.0f);
        float barHeight = SAFE_MAX(config_.barMinHeight, magnitude * maxHeight);
        
        float normalizedFreq = static_cast<float>(i) / numBands;
        Color barColor = getBarColor(normalizedFreq, magnitude);
        
        // Right side
        float xRight = centerX + i * (barWidth + config_.barSpacing);
        Rectangle rectRight = {xRight, baseY - barHeight / 2, barWidth, barHeight};
        DrawRectangleRounded(rectRight, 0.3f, 4, barColor);
        
        // Left side (mirrored)
        float xLeft = centerX - (i + 1) * (barWidth + config_.barSpacing);
        Rectangle rectLeft = {xLeft, baseY - barHeight / 2, barWidth, barHeight};
        DrawRectangleRounded(rectLeft, 0.3f, 4, barColor);
        
        // Glow
        DrawRectangle(static_cast<int>(xRight), static_cast<int>(baseY - barHeight / 2 - 10), 
                      static_cast<int>(barWidth), 10, Fade(barColor, 0.3f));
        DrawRectangle(static_cast<int>(xLeft), static_cast<int>(baseY - barHeight / 2 - 10), 
                      static_cast<int>(barWidth), 10, Fade(barColor, 0.3f));
    }
}

void SpectrumVisualizer::renderGrid() {
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    int controlBarHeight = 70;  // Match control bar
    
    Color gridColor = Fade(config_.theme.textDim, 0.2f);
    
    // Frequency markers
    const double freqMarkers[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    const int numMarkers = 10;
    
    for (int i = 0; i < numMarkers; ++i) {
        // Logarithmic position
        float logPos = (std::log10(freqMarkers[i]) - std::log10(20.0)) / 
                       (std::log10(20000.0) - std::log10(20.0));
        int x = static_cast<int>(logPos * width);
        
        DrawLine(x, 0, x, height - controlBarHeight - 5, gridColor);
        
        std::string label = formatFrequency(freqMarkers[i]);
        DrawText(label.c_str(), x - 15, height - controlBarHeight - 18, 10, config_.theme.textDim);
    }
}

void SpectrumVisualizer::renderInfo(const audio::AudioAnalyzer& analyzer, 
                                    const audio::SpectrumData& spectrum) {
    // For Line style, info is minimal since control bar shows file info
    if (config_.style == VisualizerStyle::Line) {
        // Just show peak frequency and level in top-left
        int padding = 10;
        
        std::string peakStr = "Peak: " + formatFrequency(spectrum.peakFrequency);
        DrawText(peakStr.c_str(), padding, padding, 11, config_.theme.accent);
        
        std::stringstream levelStream;
        levelStream << std::fixed << std::setprecision(1) 
                    << (20.0 * std::log10(SAFE_MAX(0.0001, spectrum.rmsLevel))) << " dB";
        DrawText(levelStream.str().c_str(), padding + 120, padding, 11, config_.theme.textDim);
        
        // FPS in corner
        std::string fpsStr = std::to_string(GetFPS()) + " FPS";
        DrawText(fpsStr.c_str(), GetScreenWidth() - 55, padding, 10, Fade(config_.theme.textDim, 0.5f));
        return;
    }
    
    // For other styles, show more info
    int padding = 12;
    
    if (analyzer.isLoaded()) {
        DrawText(analyzer.getFilename().c_str(), padding, padding, 16, config_.theme.text);
        
        std::string sampleRateStr = std::to_string(analyzer.getSampleRate()) + " Hz";
        DrawText(sampleRateStr.c_str(), padding, padding + 20, 12, config_.theme.textDim);
    } else {
        DrawText("Drop an audio file or press O to open", padding, padding, 16, config_.theme.text);
    }
    
    std::string peakStr = "Peak: " + formatFrequency(spectrum.peakFrequency);
    DrawText(peakStr.c_str(), padding, padding + 40, 12, config_.theme.accent);
    
    std::stringstream rmsStream;
    rmsStream << "Level: " << std::fixed << std::setprecision(1) 
              << (20.0 * std::log10(SAFE_MAX(0.0001, spectrum.rmsLevel))) << " dB";
    DrawText(rmsStream.str().c_str(), padding, padding + 55, 12, config_.theme.textDim);
    
    // FPS and style
    std::string fpsStr = std::to_string(GetFPS()) + " FPS";
    DrawText(fpsStr.c_str(), GetScreenWidth() - 60, padding, 12, config_.theme.textDim);
    
    const char* styleNames[] = {"LINE", "BARS", "WAVES", "CIRCLES", "PARTICLES", "MIRROR"};
    DrawText(styleNames[static_cast<int>(config_.style)], 
             GetScreenWidth() - 80, padding + 18, 12, config_.theme.accent);
}

void SpectrumVisualizer::renderControls() {
    // Controls are now rendered in the control bar - this function is kept for other styles
    if (config_.style == VisualizerStyle::Line) return;
    
    int y = GetScreenHeight() - 85;  // Above control bar
    Color textColor = Fade(config_.theme.textDim, 0.6f);
    DrawText("[S] Style  [T] Theme  [G] Grid", 10, y, 11, textColor);
}

void SpectrumVisualizer::renderProgressBar(const audio::AudioAnalyzer& analyzer) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    
    // Bottom control bar area
    int controlBarHeight = 70;
    int controlBarY = height - controlBarHeight;
    
    // Draw control bar background
    DrawRectangle(0, controlBarY, width, controlBarHeight, Fade(config_.theme.background, 0.95f));
    DrawLine(0, controlBarY, width, controlBarY, Fade(config_.theme.textDim, 0.3f));
    
    // Layout constants
    int buttonSize = 36;
    int buttonSpacing = 10;
    int buttonY = controlBarY + 20;
    int leftMargin = 20;
    
    // === Playback Buttons ===
    int buttonX = leftMargin;
    
    // Stop button
    Rectangle stopBtn = {static_cast<float>(buttonX), static_cast<float>(buttonY), 
                         static_cast<float>(buttonSize), static_cast<float>(buttonSize)};
    bool stopHovered = CheckCollisionPointRec(GetMousePosition(), stopBtn);
    Color stopColor = stopHovered ? config_.theme.accent : config_.theme.textDim;
    DrawRectangleRounded(stopBtn, 0.2f, 4, Fade(stopColor, 0.2f));
    DrawRectangleRoundedLines(stopBtn, 0.2f, 4, stopColor);
    // Stop icon (square)
    DrawRectangle(buttonX + 10, buttonY + 10, 16, 16, stopColor);
    
    buttonX += buttonSize + buttonSpacing;
    
    // Play/Pause button
    Rectangle playBtn = {static_cast<float>(buttonX), static_cast<float>(buttonY), 
                         static_cast<float>(buttonSize), static_cast<float>(buttonSize)};
    bool playHovered = CheckCollisionPointRec(GetMousePosition(), playBtn);
    Color playColor = playHovered ? config_.theme.accent : config_.theme.text;
    DrawRectangleRounded(playBtn, 0.2f, 4, Fade(playColor, 0.3f));
    DrawRectangleRoundedLines(playBtn, 0.2f, 4, playColor);
    
    if (analyzer.isPlaying()) {
        // Pause icon (two bars)
        DrawRectangle(buttonX + 10, buttonY + 8, 6, 20, playColor);
        DrawRectangle(buttonX + 20, buttonY + 8, 6, 20, playColor);
    } else {
        // Play icon (triangle)
        Vector2 v1 = {static_cast<float>(buttonX + 12), static_cast<float>(buttonY + 8)};
        Vector2 v2 = {static_cast<float>(buttonX + 12), static_cast<float>(buttonY + 28)};
        Vector2 v3 = {static_cast<float>(buttonX + 28), static_cast<float>(buttonY + 18)};
        DrawTriangle(v1, v2, v3, playColor);
    }
    
    buttonX += buttonSize + buttonSpacing + 15;
    
    // === Progress Bar ===
    int progressBarX = buttonX;
    int progressBarWidth = width - progressBarX - 180;  // Leave space for time display
    int progressBarY = buttonY + 8;
    int progressBarHeight = 20;
    
    if (analyzer.isLoaded()) {
        // Progress bar background
        Rectangle progressBg = {static_cast<float>(progressBarX), static_cast<float>(progressBarY),
                                static_cast<float>(progressBarWidth), static_cast<float>(progressBarHeight)};
        DrawRectangleRounded(progressBg, 0.3f, 4, Fade(config_.theme.textDim, 0.3f));
        
        // Progress fill
        double progress = analyzer.getPosition() / SAFE_MAX(0.001, analyzer.getDuration());
        int fillWidth = static_cast<int>(progressBarWidth * progress);
        if (fillWidth > 0) {
            Rectangle progressFill = {static_cast<float>(progressBarX), static_cast<float>(progressBarY),
                                      static_cast<float>(fillWidth), static_cast<float>(progressBarHeight)};
            DrawRectangleRounded(progressFill, 0.3f, 4, config_.theme.accent);
        }
        
        // Progress handle
        int handleX = progressBarX + fillWidth;
        DrawCircle(handleX, progressBarY + progressBarHeight / 2, 8, config_.theme.accent);
        DrawCircle(handleX, progressBarY + progressBarHeight / 2, 4, config_.theme.text);
        
        // Time display
        int timeX = progressBarX + progressBarWidth + 15;
        std::string currentTime = formatTime(analyzer.getPosition());
        std::string totalTime = formatTime(analyzer.getDuration());
        std::string timeStr = currentTime + " / " + totalTime;
        DrawText(timeStr.c_str(), timeX, progressBarY + 2, 16, config_.theme.text);
        
        // Filename (top of control bar)
        DrawText(analyzer.getFilename().c_str(), progressBarX, controlBarY + 4, 12, config_.theme.textDim);
    } else {
        // No file loaded message
        DrawText("No audio file loaded - Drop a file or press O to open", 
                 progressBarX, progressBarY + 2, 14, config_.theme.textDim);
    }
    
    // === Keyboard shortcuts (bottom right) ===
    const char* shortcuts = "[SPACE] Play  [S] Style  [T] Theme  [O] Open";
    int shortcutsWidth = MeasureText(shortcuts, 10);
    DrawText(shortcuts, width - shortcutsWidth - 10, controlBarY + controlBarHeight - 14, 10, 
             Fade(config_.theme.textDim, 0.5f));
}

void SpectrumVisualizer::updatePeaks(const audio::SpectrumData& spectrum) {
    size_t numBands = spectrum.magnitudes.size();
    
    if (peaks_.size() != numBands) {
        peaks_.resize(numBands, 0.0f);
        velocities_.resize(numBands, 0.0f);
    }
    
    for (size_t i = 0; i < numBands; ++i) {
        float magnitude = static_cast<float>(spectrum.magnitudes[i]) * config_.sensitivity;
        magnitude = std::clamp(magnitude, 0.0f, 1.0f);
        
        if (magnitude > peaks_[i]) {
            peaks_[i] = magnitude;
            velocities_[i] = 0.0f;
        } else {
            velocities_[i] += config_.peakDecay;
            peaks_[i] -= velocities_[i];
            peaks_[i] = SAFE_MAX(peaks_[i], 0.0f);
        }
    }
}

Color SpectrumVisualizer::getBarColor(float normalizedFreq, float magnitude) const {
    // Interpolate between low, mid, and high colors based on frequency
    Color result;
    
    if (normalizedFreq < 0.33f) {
        float t = normalizedFreq / 0.33f;
        result = lerpColor(config_.theme.barLow, config_.theme.barMid, t);
    } else if (normalizedFreq < 0.66f) {
        float t = (normalizedFreq - 0.33f) / 0.33f;
        result = lerpColor(config_.theme.barMid, config_.theme.barHigh, t);
    } else {
        result = config_.theme.barHigh;
    }
    
    // Boost brightness based on magnitude
    float brightness = 0.6f + magnitude * 0.4f;
    result.r = static_cast<unsigned char>(SAFE_MIN(255.0f, result.r * brightness));
    result.g = static_cast<unsigned char>(SAFE_MIN(255.0f, result.g * brightness));
    result.b = static_cast<unsigned char>(SAFE_MIN(255.0f, result.b * brightness));
    
    return result;
}

Color SpectrumVisualizer::lerpColor(Color a, Color b, float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    return {
        static_cast<unsigned char>(a.r + (b.r - a.r) * t),
        static_cast<unsigned char>(a.g + (b.g - a.g) * t),
        static_cast<unsigned char>(a.b + (b.b - a.b) * t),
        static_cast<unsigned char>(a.a + (b.a - a.a) * t)
    };
}

std::string SpectrumVisualizer::formatTime(double seconds) const {
    int mins = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    
    std::stringstream ss;
    ss << mins << ":" << std::setw(2) << std::setfill('0') << secs;
    return ss.str();
}

std::string SpectrumVisualizer::formatFrequency(double hz) const {
    if (hz >= 1000) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << (hz / 1000.0) << " kHz";
        return ss.str();
    } else {
        return std::to_string(static_cast<int>(hz)) + " Hz";
    }
}

void SpectrumVisualizer::setStyle(VisualizerStyle style) {
    config_.style = style;
}

void SpectrumVisualizer::setTheme(const ColorTheme& theme) {
    config_.theme = theme;
}

void SpectrumVisualizer::nextTheme() {
    currentTheme_ = (currentTheme_ + 1) % themes_.size();
    config_.theme = themes_[currentTheme_];
}

void SpectrumVisualizer::nextStyle() {
    int styleInt = static_cast<int>(config_.style);
    styleInt = (styleInt + 1) % 6;  // Now 6 styles
    config_.style = static_cast<VisualizerStyle>(styleInt);
}

const VisualizerConfig& SpectrumVisualizer::getConfig() const {
    return config_;
}

void SpectrumVisualizer::setConfig(const VisualizerConfig& config) {
    config_ = config;
}

void SpectrumVisualizer::handleEQInput(audio::AudioAnalyzer& analyzer) {
    if (config_.style != VisualizerStyle::Line) return;
    
    Vector2 mouse = GetMousePosition();
    bool mouseDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool mousePressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool mouseReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    float mouseWheel = GetMouseWheelMove();
    
    // Screen dimensions - match renderLine margins
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    
    int marginLeft = 55;
    int marginRight = 15;
    int marginTop = 50;
    int marginBottom = 90;
    
    int graphWidth = width - marginLeft - marginRight;
    int graphHeight = height - marginTop - marginBottom;
    int baseY = height - marginBottom;
    
    // dB range for gain
    float dbMin = -12.0f;
    float dbMax = 12.0f;
    float dbRange = dbMax - dbMin;
    
    // Frequency range (logarithmic)
    float freqMin = 20.0f;
    float freqMax = 20000.0f;
    float logFreqMin = std::log10(freqMin);
    float logFreqMax = std::log10(freqMax);
    float logFreqRange = logFreqMax - logFreqMin;
    
    const auto& eqConfig = analyzer.getEqualizer();
    
    // Update EQ control positions and check for interaction
    for (int i = 0; i < 5; ++i) {
        // Calculate X position based on frequency (logarithmic)
        float freq = static_cast<float>(eqConfig.bands[i].frequency);
        float logPos = (std::log10(freq) - logFreqMin) / logFreqRange;
        float x = marginLeft + logPos * graphWidth;
        
        // Calculate Y position based on gain (inverted: positive gain = higher on screen)
        float gain = static_cast<float>(eqConfig.bands[i].gain);
        float yNorm = (gain - dbMin) / dbRange;  // 0 at -12dB, 1 at +12dB
        float y = baseY - yNorm * graphHeight;
        
        float q = static_cast<float>(eqConfig.bands[i].q);
        
        eqControls_[i].x = x;
        eqControls_[i].y = y;
        eqControls_[i].frequency = freq;
        eqControls_[i].gain = gain;
        eqControls_[i].q = q;
        
        // Check if mouse is hovering over this control
        float controlRadius = 18.0f;
        float dx = mouse.x - x;
        float dy = mouse.y - y;
        float dist = std::sqrt(dx * dx + dy * dy);
        
        eqControls_[i].hovered = (dist < controlRadius);
        
        // Handle mouse wheel for Q adjustment when hovering
        if (eqControls_[i].hovered && std::abs(mouseWheel) > 0.01f) {
            float newQ = q + mouseWheel * 0.2f;
            newQ = std::clamp(newQ, 0.1f, 10.0f);
            analyzer.setEQBandQ(i, newQ);
        }
        
        // Start dragging
        if (mousePressed && eqControls_[i].hovered && draggedEQBand_ == -1) {
            draggedEQBand_ = i;
            eqControls_[i].dragging = true;
            dragStartX_ = mouse.x;
            dragStartY_ = mouse.y;
            dragStartFreq_ = freq;
            dragStartGain_ = gain;
        }
    }
    
    // Handle dragging (both X for frequency and Y for gain)
    if (draggedEQBand_ >= 0 && mouseDown) {
        // Calculate new frequency from mouse X position (logarithmic)
        float xNorm = (mouse.x - marginLeft) / graphWidth;
        xNorm = std::clamp(xNorm, 0.0f, 1.0f);
        float logFreq = logFreqMin + xNorm * logFreqRange;
        float newFreq = std::pow(10.0f, logFreq);
        
        // Calculate new gain from mouse Y position
        float yNorm = static_cast<float>(baseY - mouse.y) / graphHeight;
        yNorm = std::clamp(yNorm, 0.0f, 1.0f);
        float newGain = dbMin + yNorm * dbRange;
        
        // Snap gain to 0 if close
        if (std::abs(newGain) < 0.3f) {
            newGain = 0.0f;
        }
        
        // Snap frequency to common values if close
        const float snapFreqs[] = {20, 30, 40, 50, 60, 80, 100, 125, 160, 200, 250, 315, 
                                   400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 
                                   3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000};
        for (float snapFreq : snapFreqs) {
            float logDiff = std::abs(std::log10(newFreq) - std::log10(snapFreq));
            if (logDiff < 0.02f) {
                newFreq = snapFreq;
                break;
            }
        }
        
        analyzer.setEQBandFrequency(draggedEQBand_, newFreq);
        analyzer.setEQBandGain(draggedEQBand_, newGain);
    }
    
    // Stop dragging
    if (mouseReleased && draggedEQBand_ >= 0) {
        eqControls_[draggedEQBand_].dragging = false;
        draggedEQBand_ = -1;
    }
}

void SpectrumVisualizer::drawEQSpline(audio::AudioAnalyzer& analyzer, int marginLeft, 
                                      int graphWidth, int marginTop, int graphHeight, int baseY) {
    const auto& eqConfig = analyzer.getEqualizer();
    int centerY = marginTop + graphHeight / 2;
    
    // Frequency range (logarithmic)
    float logFreqMin = std::log10(20.0f);
    float logFreqMax = std::log10(20000.0f);
    float logFreqRange = logFreqMax - logFreqMin;
    
    // dB range
    float dbMin = -12.0f;
    float dbMax = 12.0f;
    float dbRange = dbMax - dbMin;
    
    // Calculate the combined EQ response at each pixel
    // This simulates the actual frequency response of cascaded biquad filters
    int numPoints = graphWidth;
    std::vector<float> response(numPoints);
    
    for (int px = 0; px < numPoints; ++px) {
        float xNorm = static_cast<float>(px) / (numPoints - 1);
        float logFreq = logFreqMin + xNorm * logFreqRange;
        float freq = std::pow(10.0f, logFreq);
        
        // Sum contributions from all EQ bands
        float totalGainDb = 0.0f;
        
        for (int band = 0; band < 5; ++band) {
            float bandFreq = eqControls_[band].frequency;
            float bandGain = eqControls_[band].gain;
            float bandQ = eqControls_[band].q;
            
            if (std::abs(bandGain) < 0.01f) continue;
            
            // Calculate the response of a peaking filter at this frequency
            // Using simplified magnitude response formula for peaking EQ
            float logDist = std::log10(freq) - std::log10(bandFreq);
            float bandwidth = 1.0f / bandQ;  // Wider Q = narrower bandwidth
            
            // Bell curve in log-frequency domain
            float bellShape = std::exp(-(logDist * logDist) / (2.0f * bandwidth * bandwidth * 0.1f));
            totalGainDb += bandGain * bellShape;
        }
        
        response[px] = totalGainDb;
    }
    
    // Draw filled area under the curve
    for (int px = 0; px < numPoints - 1; ++px) {
        float gain1 = response[px];
        float gain2 = response[px + 1];
        
        float yNorm1 = (gain1 - dbMin) / dbRange;
        float yNorm2 = (gain2 - dbMin) / dbRange;
        
        float y1 = baseY - yNorm1 * graphHeight;
        float y2 = baseY - yNorm2 * graphHeight;
        
        float x1 = marginLeft + px;
        float x2 = marginLeft + px + 1;
        
        // Determine color based on gain (boost = green, cut = red/orange)
        float avgGain = (gain1 + gain2) / 2.0f;
        Color fillColor;
        if (avgGain >= 0) {
            fillColor = Fade(config_.theme.barMid, 0.25f);
        } else {
            fillColor = Fade(config_.theme.barLow, 0.25f);
        }
        
        // Draw quad from curve to center line
        Vector2 v1 = {x1, y1};
        Vector2 v2 = {x2, y2};
        Vector2 v3 = {x2, static_cast<float>(centerY)};
        Vector2 v4 = {x1, static_cast<float>(centerY)};
        
        DrawTriangle(v1, v4, v3, fillColor);
        DrawTriangle(v1, v3, v2, fillColor);
    }
    
    // Draw the curve line
    for (int px = 0; px < numPoints - 1; ++px) {
        float gain1 = response[px];
        float gain2 = response[px + 1];
        
        float yNorm1 = (gain1 - dbMin) / dbRange;
        float yNorm2 = (gain2 - dbMin) / dbRange;
        
        float y1 = baseY - yNorm1 * graphHeight;
        float y2 = baseY - yNorm2 * graphHeight;
        
        float x1 = marginLeft + px;
        float x2 = marginLeft + px + 1;
        
        Color lineColor = Fade(config_.theme.accent, 0.8f);
        DrawLineEx({x1, y1}, {x2, y2}, 2.0f, lineColor);
    }
}

void SpectrumVisualizer::renderEQControls(audio::AudioAnalyzer& analyzer) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    
    // Match margins with renderLine
    int marginLeft = 55;
    int marginRight = 15;
    int marginTop = 50;
    int marginBottom = 90;
    
    int graphWidth = width - marginLeft - marginRight;
    int graphHeight = height - marginTop - marginBottom;
    int baseY = height - marginBottom;
    
    const auto& eqConfig = analyzer.getEqualizer();
    
    // Draw EQ response curve
    drawEQSpline(analyzer, marginLeft, graphWidth, marginTop, graphHeight, baseY);
    
    // Draw control points
    for (int i = 0; i < 5; ++i) {
        float x = eqControls_[i].x;
        float y = eqControls_[i].y;
        float gain = eqControls_[i].gain;
        float freq = eqControls_[i].frequency;
        float q = eqControls_[i].q;
        
        // Draw Q indicator circle
        float qRadius = 22.0f / q;
        qRadius = std::clamp(qRadius, 5.0f, 45.0f);
        
        if (std::abs(gain) > 0.1f) {
            DrawCircleLines(static_cast<int>(x), static_cast<int>(y), 
                           qRadius, Fade(config_.theme.textDim, 0.2f));
        }
        
        // Control knob size
        float radius = 8.0f;
        Color knobColor;
        
        if (eqControls_[i].dragging) {
            knobColor = config_.theme.accent;
            radius = 12.0f;
        } else if (eqControls_[i].hovered) {
            knobColor = Fade(config_.theme.accent, 0.9f);
            radius = 10.0f;
        } else {
            knobColor = (std::abs(gain) > 0.1f) ? config_.theme.barMid : config_.theme.textDim;
        }
        
        // Glow
        DrawCircle(static_cast<int>(x), static_cast<int>(y), 
                   radius + 2, Fade(knobColor, 0.2f));
        
        // Main knob
        DrawCircle(static_cast<int>(x), static_cast<int>(y), radius, knobColor);
        
        // Inner dot
        DrawCircle(static_cast<int>(x), static_cast<int>(y), 2.0f, config_.theme.background);
        
        // Tooltip when hovered or dragging
        if (eqControls_[i].hovered || eqControls_[i].dragging) {
            char freqText[32];
            if (freq >= 1000) {
                snprintf(freqText, sizeof(freqText), "%.1fk", freq / 1000.0f);
            } else {
                snprintf(freqText, sizeof(freqText), "%.0f", freq);
            }
            
            char infoText[64];
            snprintf(infoText, sizeof(infoText), "%sHz  %+.1fdB  Q%.1f", freqText, gain, q);
            
            int infoWidth = MeasureText(infoText, 11);
            int tooltipX = static_cast<int>(x) - infoWidth / 2;
            int tooltipY = static_cast<int>(y) - radius - 22;
            
            // Keep on screen
            tooltipX = SAFE_MAX(5, SAFE_MIN(width - infoWidth - 10, tooltipX));
            tooltipY = SAFE_MAX(5, tooltipY);
            
            Rectangle tooltipRect = {static_cast<float>(tooltipX - 4), static_cast<float>(tooltipY - 2), 
                                     static_cast<float>(infoWidth + 8), 16.0f};
            DrawRectangleRounded(tooltipRect, 0.3f, 4, Fade(config_.theme.background, 0.9f));
            DrawRectangleRoundedLines(tooltipRect, 0.3f, 4, Fade(config_.theme.accent, 0.5f));
            DrawText(infoText, tooltipX, tooltipY, 11, config_.theme.accent);
        }
    }
    
    // EQ status badge (top-right)
    const char* eqLabel = eqConfig.enabled ? "EQ" : "EQ OFF";
    Color eqColor = eqConfig.enabled ? config_.theme.accent : config_.theme.textDim;
    int eqWidth = MeasureText(eqLabel, 11);
    Rectangle eqBadge = {static_cast<float>(width - eqWidth - 22), 8.0f,
                         static_cast<float>(eqWidth + 14), 16.0f};
    DrawRectangleRounded(eqBadge, 0.4f, 4, Fade(eqColor, 0.15f));
    DrawText(eqLabel, width - eqWidth - 15, 10, 11, eqColor);
}

} // namespace viz


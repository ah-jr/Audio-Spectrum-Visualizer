#pragma once

/**
 * Shared color themes for both standalone (raylib) and VST3 (OpenGL) versions
 * 
 * This file contains the color data as simple RGBA values that can be
 * converted to the appropriate type by each platform.
 */

#include <cstdint>

namespace colors {

// Simple RGBA color structure (platform-independent)
// Named RGBA to avoid conflict with Windows RGB macro
struct RGBA {
    uint8_t r, g, b, a;
    
    constexpr RGBA(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) 
        : r(r_), g(g_), b(b_), a(a_) {}
};

// Theme data structure
struct ThemeColors {
    RGBA background;
    RGBA barLow;
    RGBA barMid;
    RGBA barHigh;
    RGBA accent;
    RGBA text;
    RGBA textDim;
};

// ============================================================================
// Theme Definitions (single source of truth)
// ============================================================================

namespace themes {

constexpr ThemeColors Cyberpunk = {
    {13, 2, 33, 255},        // background - dark purple
    {255, 0, 110, 255},      // barLow - hot pink
    {0, 255, 255, 255},      // barMid - cyan
    {255, 255, 0, 255},      // barHigh - yellow
    {255, 0, 255, 255},      // accent - magenta
    {0, 255, 255, 255},      // text - cyan
    {100, 80, 120, 255}      // textDim
};

constexpr ThemeColors Neon = {
    {20, 25, 20, 255},       // background - dark gray-green
    {0, 220, 60, 255},       // barLow - bright green
    {50, 255, 100, 255},     // barMid - lighter green
    {150, 255, 150, 255},    // barHigh - pale green
    {200, 255, 200, 255},    // accent - white-green
    {220, 255, 220, 255},    // text - light green tint
    {80, 120, 80, 255}       // textDim - dim green
};

constexpr ThemeColors Sunset = {
    {25, 20, 35, 255},       // background
    {255, 100, 50, 255},     // barLow - orange
    {255, 50, 100, 255},     // barMid - coral
    {180, 50, 255, 255},     // barHigh - purple
    {255, 200, 100, 255},    // accent
    {255, 240, 230, 255},    // text
    {140, 120, 130, 255}     // textDim
};

constexpr ThemeColors Ocean = {
    {10, 25, 40, 255},       // background - deep ocean
    {0, 180, 180, 255},      // barLow - teal
    {0, 120, 255, 255},      // barMid - ocean blue
    {100, 200, 255, 255},    // barHigh - light blue
    {0, 255, 200, 255},      // accent - aqua
    {200, 230, 255, 255},    // text
    {80, 120, 150, 255}      // textDim
};

constexpr ThemeColors Monochrome = {
    {10, 10, 10, 255},       // background
    {100, 100, 100, 255},    // barLow
    {180, 180, 180, 255},    // barMid
    {255, 255, 255, 255},    // barHigh
    {200, 200, 200, 255},    // accent
    {255, 255, 255, 255},    // text
    {100, 100, 100, 255}     // textDim
};

// Array of all themes for easy iteration
constexpr int NUM_THEMES = 5;

inline const ThemeColors& getTheme(int index) {
    switch (index % NUM_THEMES) {
        case 0: return Cyberpunk;
        case 1: return Neon;
        case 2: return Sunset;
        case 3: return Ocean;
        case 4: return Monochrome;
        default: return Sunset;
    }
}

inline const char* getThemeName(int index) {
    switch (index % NUM_THEMES) {
        case 0: return "Cyberpunk";
        case 1: return "Neon";
        case 2: return "Sunset";
        case 3: return "Ocean";
        case 4: return "Monochrome";
        default: return "Sunset";
    }
}

} // namespace themes

} // namespace colors


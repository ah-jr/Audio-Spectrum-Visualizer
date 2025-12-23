#pragma once

/**
 * VST3 Plugin Editor - Win32/OpenGL based GUI
 * Provides the same visual appearance as the standalone raylib version
 */

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "gl_renderer.hpp"
#include "eq_processor.hpp"
#include "fft.hpp"
#include "shared_colors.hpp"
#include <vector>
#include <mutex>
#include <atomic>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace SpectrumEQ {

// Forward declaration
class PluginController;

// Color theme using shared colors
struct ColorTheme {
    gl::Color background;
    gl::Color barLow;
    gl::Color barMid;
    gl::Color barHigh;
    gl::Color accent;
    gl::Color text;
    gl::Color textDim;
    
    // Convert from shared theme
    static ColorTheme fromShared(const colors::ThemeColors& t) {
        return {
            {t.background.r, t.background.g, t.background.b, t.background.a},
            {t.barLow.r, t.barLow.g, t.barLow.b, t.barLow.a},
            {t.barMid.r, t.barMid.g, t.barMid.b, t.barMid.a},
            {t.barHigh.r, t.barHigh.g, t.barHigh.b, t.barHigh.a},
            {t.accent.r, t.accent.g, t.accent.b, t.accent.a},
            {t.text.r, t.text.g, t.text.b, t.text.a},
            {t.textDim.r, t.textDim.g, t.textDim.b, t.textDim.a}
        };
    }
    
    // Theme factories using shared colors
    static ColorTheme Cyberpunk() { return fromShared(colors::themes::Cyberpunk); }
    static ColorTheme Neon() { return fromShared(colors::themes::Neon); }
    static ColorTheme Sunset() { return fromShared(colors::themes::Sunset); }
    static ColorTheme Ocean() { return fromShared(colors::themes::Ocean); }
    static ColorTheme Monochrome() { return fromShared(colors::themes::Monochrome); }
    
    // Get theme by index (matching shared order)
    static ColorTheme byIndex(int index) { return fromShared(colors::themes::getTheme(index)); }
};

// EQ Control state
struct EQControl {
    float x = 0;
    float y = 0;
    float frequency = 1000.0f;
    float gain = 0.0f;
    float q = 0.707f;
    bool hovered = false;
    bool dragging = false;
};

/**
 * VST3 Plugin View (Editor UI)
 */
class PluginEditor : public Steinberg::IPlugView {
public:
    PluginEditor(PluginController* controller);
    virtual ~PluginEditor();
    
    // IPlugView interface
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API removed() override;
    Steinberg::tresult PLUGIN_API onWheel(float distance) override;
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key, Steinberg::int16 keyCode, Steinberg::int16 modifiers) override;
    Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16 key, Steinberg::int16 keyCode, Steinberg::int16 modifiers) override;
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;
    Steinberg::tresult PLUGIN_API onFocus(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setFrame(Steinberg::IPlugFrame* frame) override;
    Steinberg::tresult PLUGIN_API canResize() override;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;
    
    // IUnknown
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
    
    // Spectrum data update (called from processor)
    void updateSpectrum(const std::vector<float>& spectrum);
    
    // EQ parameter update (called from controller for automation)
    void updateEQParameter(int band, double gain, double freq, double q);
    void setBypass(bool bypass);
    
    // Called when parameters change from host/automation
    void onParameterChange(Steinberg::Vst::ParamID id, double normalizedValue);
    
private:
    // Win32 window handling
    bool createWindow(void* parent);
    void destroyWindow();
    bool createOpenGLContext();
    void destroyOpenGLContext();
    
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    
    // Rendering
    void render();
    void renderSpectrum();
    void renderEQControls();
    void renderEQCurve();
    void renderGrid();
    void renderThemeSelector();
    
    // Input handling
    void handleMouseMove(int x, int y);
    void handleMouseDown(int x, int y);
    void handleMouseUp(int x, int y);
    void handleMouseWheel(float delta);
    
    // Parameter sync
    void syncParametersFromController();
    
    // Settings persistence (registry)
    void loadSettings();
    void saveSettings();
    
    // Utilities
    gl::Color getBarColor(float normalizedFreq, float magnitude) const;
    std::string formatFrequency(double hz) const;
    
    // Controller reference
    PluginController* controller_ = nullptr;
    Steinberg::IPlugFrame* plugFrame_ = nullptr;
    
    // Reference counting
    std::atomic<Steinberg::int32> refCount_{1};
    
    // Window
#ifdef _WIN32
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC hglrc_ = nullptr;
#endif
    
    // Renderer
    gl::Renderer renderer_;
    ColorTheme theme_;
    int themeIndex_ = 2;  // Default to Sunset (matches standalone)
    bool themeDropdownOpen_ = false;
    
    // Size
    int width_ = 1280;
    int height_ = 720;
    bool settingsLoaded_ = false;
    
    // Spectrum data
    std::vector<float> spectrum_;
    std::vector<float> peakHold_;
    std::vector<float> peakDecay_;
    std::mutex spectrumMutex_;
    
    // EQ controls
    EQControl eqControls_[5];
    int draggedBand_ = -1;
    bool eqEnabled_ = true;
    
    // Mouse state
    int mouseX_ = 0;
    int mouseY_ = 0;
    bool mouseDown_ = false;
    float pendingWheelDelta_ = 0.0f;
    
    // Animation
    UINT_PTR timerId_ = 0;
    static constexpr int TIMER_ID = 1;
    static constexpr int FRAME_INTERVAL = 16; // ~60 FPS
};

} // namespace SpectrumEQ


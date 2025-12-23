#include "plugin_editor.hpp"
#include "plugin_controller.hpp"
#include "plugin_ids.hpp"
#include "shared_data.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Registry key for settings
static const wchar_t* REGISTRY_KEY = L"SOFTWARE\\SpectrumEQ";
static const wchar_t* REG_WINDOW_WIDTH = L"WindowWidth";
static const wchar_t* REG_WINDOW_HEIGHT = L"WindowHeight";
static const wchar_t* REG_THEME_INDEX = L"ThemeIndex";

namespace SpectrumEQ {

// Window class name
static const wchar_t* WINDOW_CLASS = L"SpectrumEQEditor";
static std::atomic<int> windowClassRegistered{0};

PluginEditor::PluginEditor(PluginController* controller) 
    : controller_(controller)
{
    // Load settings from registry (theme, window size)
    loadSettings();
    theme_ = ColorTheme::byIndex(themeIndex_);
    
    // Initialize EQ controls using shared constants
    for (int i = 0; i < eq::NUM_BANDS; ++i) {
        eqControls_[i].frequency = static_cast<float>(eq::DEFAULT_FREQUENCIES[i]);
        eqControls_[i].gain = 0.0f;
        eqControls_[i].q = static_cast<float>(eq::DEFAULT_Q);
    }
    
    spectrum_.resize(256, 0.0f);
    peakHold_.resize(256, -60.0f);
    peakDecay_.resize(256, 0.0f);
}

PluginEditor::~PluginEditor() {
    // Notify controller we're being destroyed
    if (controller_) {
        controller_->removeEditor(this);
    }
    destroyWindow();
}

// IUnknown implementation
Steinberg::uint32 PLUGIN_API PluginEditor::addRef() {
    return ++refCount_;
}

Steinberg::uint32 PLUGIN_API PluginEditor::release() {
    Steinberg::int32 count = --refCount_;
    if (count == 0) {
        delete this;
    }
    return count;
}

Steinberg::tresult PLUGIN_API PluginEditor::queryInterface(const Steinberg::TUID iid, void** obj) {
    if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::IPlugView::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
        addRef();
        *obj = static_cast<Steinberg::IPlugView*>(this);
        return Steinberg::kResultOk;
    }
    *obj = nullptr;
    return Steinberg::kNoInterface;
}

// IPlugView implementation
Steinberg::tresult PLUGIN_API PluginEditor::isPlatformTypeSupported(Steinberg::FIDString type) {
#ifdef _WIN32
    if (strcmp(type, Steinberg::kPlatformTypeHWND) == 0) {
        return Steinberg::kResultTrue;
    }
#endif
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API PluginEditor::attached(void* parent, Steinberg::FIDString type) {
#ifdef _WIN32
    if (strcmp(type, Steinberg::kPlatformTypeHWND) != 0) {
        return Steinberg::kResultFalse;
    }
    
    if (!createWindow(parent)) {
        return Steinberg::kResultFalse;
    }
    
    if (!createOpenGLContext()) {
        destroyWindow();
        return Steinberg::kResultFalse;
    }
    
    renderer_.initialize(width_, height_);
    
    // Sync EQ parameters from controller
    syncParametersFromController();
    
    // Start render timer
    timerId_ = SetTimer(hwnd_, TIMER_ID, FRAME_INTERVAL, nullptr);
    
    return Steinberg::kResultOk;
#else
    return Steinberg::kResultFalse;
#endif
}

Steinberg::tresult PLUGIN_API PluginEditor::removed() {
#ifdef _WIN32
    // Save settings before closing
    saveSettings();
    
    if (timerId_) {
        KillTimer(hwnd_, TIMER_ID);
        timerId_ = 0;
    }
    
    renderer_.shutdown();
    destroyOpenGLContext();
    destroyWindow();
#endif
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API PluginEditor::onWheel(float distance) {
    handleMouseWheel(distance);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API PluginEditor::onKeyDown(Steinberg::char16 key, Steinberg::int16 keyCode, Steinberg::int16 modifiers) {
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API PluginEditor::onKeyUp(Steinberg::char16 key, Steinberg::int16 keyCode, Steinberg::int16 modifiers) {
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API PluginEditor::getSize(Steinberg::ViewRect* size) {
    if (!size) return Steinberg::kInvalidArgument;
    size->left = 0;
    size->top = 0;
    size->right = width_;
    size->bottom = height_;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API PluginEditor::onSize(Steinberg::ViewRect* newSize) {
    if (!newSize) return Steinberg::kInvalidArgument;
    
    width_ = newSize->right - newSize->left;
    height_ = newSize->bottom - newSize->top;
    
#ifdef _WIN32
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, 0, 0, width_, height_, 
                    SWP_NOMOVE | SWP_NOZORDER);
    }
#endif
    
    renderer_.resize(width_, height_);
    
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API PluginEditor::onFocus(Steinberg::TBool state) {
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API PluginEditor::setFrame(Steinberg::IPlugFrame* frame) {
    plugFrame_ = frame;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API PluginEditor::canResize() {
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API PluginEditor::checkSizeConstraint(Steinberg::ViewRect* rect) {
    if (!rect) return Steinberg::kInvalidArgument;
    
    // Minimum size
    int w = rect->right - rect->left;
    int h = rect->bottom - rect->top;
    
    if (w < 600) rect->right = rect->left + 600;
    if (h < 350) rect->bottom = rect->top + 350;
    
    return Steinberg::kResultOk;
}

// Spectrum update from processor
void PluginEditor::updateSpectrum(const std::vector<float>& spectrum) {
    std::lock_guard<std::mutex> lock(spectrumMutex_);
    if (spectrum_.size() != spectrum.size()) {
        spectrum_.resize(spectrum.size());
        peakHold_.resize(spectrum.size(), -60.0f);
        peakDecay_.resize(spectrum.size(), 0.0f);
    }
    spectrum_ = spectrum;
}

// EQ parameter update from controller
void PluginEditor::updateEQParameter(int band, double gain, double freq, double q) {
    if (band >= 0 && band < 5) {
        eqControls_[band].gain = static_cast<float>(gain);
        eqControls_[band].frequency = static_cast<float>(freq);
        eqControls_[band].q = static_cast<float>(q);
    }
}

void PluginEditor::setBypass(bool bypass) {
    eqEnabled_ = !bypass;
}

#ifdef _WIN32

bool PluginEditor::createWindow(void* parent) {
    HWND parentHwnd = static_cast<HWND>(parent);
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    
    // Register window class if not already done
    if (windowClassRegistered.fetch_add(1) == 0) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = windowProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = WINDOW_CLASS;
        
        if (!RegisterClassExW(&wc)) {
            windowClassRegistered--;
            return false;
        }
    }
    
    // Create child window
    hwnd_ = CreateWindowExW(
        0,
        WINDOW_CLASS,
        L"SpectrumEQ",
        WS_CHILD | WS_VISIBLE,
        0, 0, width_, height_,
        parentHwnd,
        nullptr,
        hInstance,
        this
    );
    
    if (!hwnd_) {
        return false;
    }
    
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    
    return true;
}

void PluginEditor::destroyWindow() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool PluginEditor::createOpenGLContext() {
    hdc_ = GetDC(hwnd_);
    if (!hdc_) return false;
    
    // Try to get a multisampled pixel format first
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    
    int pixelFormat = ChoosePixelFormat(hdc_, &pfd);
    if (pixelFormat == 0) {
        ReleaseDC(hwnd_, hdc_);
        hdc_ = nullptr;
        return false;
    }
    
    if (!SetPixelFormat(hdc_, pixelFormat, &pfd)) {
        ReleaseDC(hwnd_, hdc_);
        hdc_ = nullptr;
        return false;
    }
    
    hglrc_ = wglCreateContext(hdc_);
    if (!hglrc_) {
        ReleaseDC(hwnd_, hdc_);
        hdc_ = nullptr;
        return false;
    }
    
    if (!wglMakeCurrent(hdc_, hglrc_)) {
        wglDeleteContext(hglrc_);
        ReleaseDC(hwnd_, hdc_);
        hglrc_ = nullptr;
        hdc_ = nullptr;
        return false;
    }
    
    return true;
}

void PluginEditor::destroyOpenGLContext() {
    if (hglrc_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(hglrc_);
        hglrc_ = nullptr;
    }
    if (hdc_) {
        ReleaseDC(hwnd_, hdc_);
        hdc_ = nullptr;
    }
}

LRESULT CALLBACK PluginEditor::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PluginEditor* editor = reinterpret_cast<PluginEditor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    
    if (editor) {
        return editor->handleMessage(msg, wParam, lParam);
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PluginEditor::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TIMER:
            if (wParam == TIMER_ID) {
                render();
            }
            return 0;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            render();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        
        case WM_MOUSEMOVE:
            handleMouseMove(LOWORD(lParam), HIWORD(lParam));
            return 0;
            
        case WM_LBUTTONDOWN:
            SetCapture(hwnd_);
            handleMouseDown(LOWORD(lParam), HIWORD(lParam));
            return 0;
            
        case WM_LBUTTONUP:
            ReleaseCapture();
            handleMouseUp(LOWORD(lParam), HIWORD(lParam));
            return 0;
            
        case WM_MOUSEWHEEL:
            handleMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f);
            return 0;
            
        case WM_SIZE:
            width_ = LOWORD(lParam);
            height_ = HIWORD(lParam);
            renderer_.resize(width_, height_);
            return 0;
            
        case WM_KEYDOWN:
            // 'T' key to cycle themes (alternative to dropdown)
            if (wParam == 'T') {
                themeIndex_ = (themeIndex_ + 1) % colors::themes::NUM_THEMES;
                theme_ = ColorTheme::byIndex(themeIndex_);
                themeDropdownOpen_ = false;
            }
            // 'E' key to toggle EQ bypass
            else if (wParam == 'E') {
                eqEnabled_ = !eqEnabled_;
                if (controller_) {
                    controller_->setParamNormalized(kBypass, eqEnabled_ ? 0.0 : 1.0);
                    controller_->performEdit(kBypass, eqEnabled_ ? 0.0 : 1.0);
                }
            }
            return 0;
    }
    
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

#endif // _WIN32

void PluginEditor::render() {
#ifdef _WIN32
    if (!hwnd_ || !hdc_ || !hglrc_) return;
    
    wglMakeCurrent(hdc_, hglrc_);
#endif
    
    // Fetch latest spectrum data from processor
    {
        std::vector<float> newSpectrum;
        if (SharedSpectrumData::instance().getSpectrum(newSpectrum)) {
            std::lock_guard<std::mutex> lock(spectrumMutex_);
            spectrum_ = std::move(newSpectrum);
            
            // Resize peak hold arrays if needed
            if (peakHold_.size() != spectrum_.size()) {
                peakHold_.resize(spectrum_.size(), -60.0f);
                peakDecay_.resize(spectrum_.size(), 0.0f);
            }
        }
    }
    
    renderer_.beginFrame();
    renderer_.clearBackground(theme_.background);
    
    // Draw gradient overlay
    renderer_.drawRectangleGradientV(0, 0, width_, height_,
        gl::Renderer::fade(theme_.barMid, 0.05f),
        gl::Renderer::fade(theme_.background, 0.0f));
    
    renderGrid();
    renderSpectrum();
    renderEQCurve();
    renderEQControls();
    renderThemeSelector();
    
    // EQ status badge (moved to right of theme selector)
    const char* eqLabel = eqEnabled_ ? "EQ" : "EQ OFF";
    gl::Color eqColor = eqEnabled_ ? theme_.accent : theme_.textDim;
    int eqWidth = renderer_.measureText(eqLabel, 11);
    gl::Rectangle eqBadge(static_cast<float>(width_ - eqWidth - 22), 8.0f,
                          static_cast<float>(eqWidth + 14), 16.0f);
    renderer_.drawRectangleRounded(eqBadge, 0.4f, 4, gl::Renderer::fade(eqColor, 0.15f));
    renderer_.drawText(eqLabel, width_ - eqWidth - 15, 10, 11, eqColor);
    
    renderer_.endFrame();
    
#ifdef _WIN32
    SwapBuffers(hdc_);
#endif
}

void PluginEditor::renderGrid() {
    int marginLeft = 55;
    int marginRight = 15;
    int marginTop = 40;
    int marginBottom = 40;
    
    int graphWidth = width_ - marginLeft - marginRight;
    int graphHeight = height_ - marginTop - marginBottom;
    int baseY = height_ - marginBottom;
    
    float dbMin = -60.0f;
    float dbMax = 0.0f;
    float dbRange = dbMax - dbMin;
    
    gl::Color gridColor = gl::Renderer::fade(theme_.textDim, 0.3f);
    
    // dB scale
    for (float db = dbMin; db <= dbMax; db += 6.0f) {
        float yNorm = (db - dbMin) / dbRange;
        int y = baseY - static_cast<int>(yNorm * graphHeight);
        
        renderer_.drawLine(marginLeft, y, width_ - marginRight, y, gridColor);
        
        char label[16];
        snprintf(label, sizeof(label), "%+.0f", db);
        renderer_.drawText(label, 5, y - 5, 12, theme_.textDim);
    }
    
    // Frequency scale
    const double freqMarkers[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (int i = 0; i < 10; ++i) {
        float logPos = (std::log10(freqMarkers[i]) - std::log10(20.0)) / 
                       (std::log10(20000.0) - std::log10(20.0));
        int x = marginLeft + static_cast<int>(logPos * graphWidth);
        
        renderer_.drawLine(x, marginTop, x, baseY, gridColor);
        
        std::string label = formatFrequency(freqMarkers[i]);
        int textWidth = renderer_.measureText(label.c_str(), 11);
        renderer_.drawText(label.c_str(), x - textWidth / 2, baseY + 8, 11, theme_.textDim);
    }
    
    // Border
    renderer_.drawRectangleLines(marginLeft, marginTop, graphWidth, graphHeight, theme_.textDim);
}

void PluginEditor::renderSpectrum() {
    std::lock_guard<std::mutex> lock(spectrumMutex_);
    
    if (spectrum_.empty()) return;
    
    int marginLeft = 55;
    int marginRight = 15;
    int marginTop = 40;
    int marginBottom = 40;
    
    int graphWidth = width_ - marginLeft - marginRight;
    int graphHeight = height_ - marginTop - marginBottom;
    int baseY = height_ - marginBottom;
    
    float dbMin = -60.0f;
    float dbMax = 0.0f;
    float dbRange = dbMax - dbMin;
    
    // Map linear FFT bins to logarithmic display
    // Assuming 44100 Hz sample rate, FFT size gives us bin resolution
    size_t fftSize = spectrum_.size() * 2;  // spectrum_ is half of FFT
    float sampleRate = 44100.0f;  // Typical sample rate
    float binFreqRes = sampleRate / fftSize;
    
    float minFreq = 20.0f;
    float maxFreq = 20000.0f;
    float logMinFreq = std::log10(minFreq);
    float logMaxFreq = std::log10(maxFreq);
    float logRange = logMaxFreq - logMinFreq;
    
    // Number of display bands (logarithmically spaced)
    const int numDisplayBands = 256;
    std::vector<gl::Vector2> points(numDisplayBands);
    std::vector<float> dbValues(numDisplayBands);
    
    // Resize peak hold arrays if needed
    if (peakHold_.size() != numDisplayBands) {
        peakHold_.resize(numDisplayBands, dbMin);
        peakDecay_.resize(numDisplayBands, 0.0f);
    }
    
    for (int i = 0; i < numDisplayBands; ++i) {
        // Calculate frequency for this display band (logarithmic)
        float t = static_cast<float>(i) / (numDisplayBands - 1);
        float logFreq = logMinFreq + t * logRange;
        float freq = std::pow(10.0f, logFreq);
        
        // Find the corresponding FFT bin
        int bin = static_cast<int>(freq / binFreqRes);
        bin = std::clamp(bin, 0, static_cast<int>(spectrum_.size()) - 1);
        
        // Get magnitude (average nearby bins for smoothing)
        // Use more bins at higher frequencies for better averaging
        float mag = 0.0f;
        int smoothRange = (std::max)(1, bin / 8);
        int count = 0;
        for (int j = -smoothRange; j <= smoothRange; ++j) {
            int idx = bin + j;
            if (idx >= 0 && idx < static_cast<int>(spectrum_.size())) {
                mag += spectrum_[idx];
                count++;
            }
        }
        if (count > 0) mag /= count;
        
        // Apply slope compensation (+4.5 dB per octave from 1kHz)
        // This compensates for natural pink noise characteristics of audio
        // and makes the display more balanced like professional analyzers
        float octavesFrom1k = std::log2(freq / 1000.0f);
        float slopeCompensation = octavesFrom1k * 4.5f;  // +4.5 dB per octave
        
        // Convert to dB with slope compensation
        float db;
        if (mag > 0.00001f) {
            db = 20.0f * std::log10(mag) + slopeCompensation;
            db = std::clamp(db, dbMin, dbMax);
        } else {
            db = dbMin;
        }
        dbValues[i] = db;
        
        // Peak hold
        if (db > peakHold_[i]) {
            peakHold_[i] = db;
            peakDecay_[i] = 0.0f;
        } else {
            peakDecay_[i] += 0.15f;
            peakHold_[i] -= peakDecay_[i] * 0.1f;
            peakHold_[i] = (std::max)(peakHold_[i], dbMin);
        }
        
        // X position (already logarithmic from the loop)
        float x = marginLeft + t * graphWidth;
        
        // Y position from dB
        float yNorm = (db - dbMin) / dbRange;
        float y = baseY - yNorm * graphHeight;
        
        points[i] = {x, y};
    }
    
    size_t numBands = numDisplayBands;
    
    // Filled area
    for (size_t i = 0; i < numBands - 1; ++i) {
        float normalizedFreq = static_cast<float>(i) / numBands;
        float mag = (dbValues[i] - dbMin) / dbRange;
        gl::Color fillColor = gl::Renderer::fade(getBarColor(normalizedFreq, mag), 0.6f);
        
        gl::Vector2 v1 = points[i];
        gl::Vector2 v2 = points[i + 1];
        gl::Vector2 v3 = {points[i + 1].x, static_cast<float>(baseY)};
        gl::Vector2 v4 = {points[i].x, static_cast<float>(baseY)};
        
        renderer_.drawTriangle(v1, v4, v3, fillColor);
        renderer_.drawTriangle(v1, v3, v2, fillColor);
    }
    
    // Main line
    for (size_t i = 0; i < numBands - 1; ++i) {
        float normalizedFreq = static_cast<float>(i) / numBands;
        float mag = (dbValues[i] - dbMin) / dbRange;
        gl::Color lineColor = getBarColor(normalizedFreq, mag);
        
        renderer_.drawLineEx(points[i], points[i + 1], 2.0f, lineColor);
    }
    
    // Peak hold line
    for (size_t i = 0; i < numBands - 1; ++i) {
        float freqNorm1 = static_cast<float>(i) / (numBands - 1);
        float freqNorm2 = static_cast<float>(i + 1) / (numBands - 1);
        
        float x1 = marginLeft + freqNorm1 * graphWidth;
        float x2 = marginLeft + freqNorm2 * graphWidth;
        
        float yNorm1 = (peakHold_[i] - dbMin) / dbRange;
        float yNorm2 = (peakHold_[i + 1] - dbMin) / dbRange;
        
        float y1 = baseY - yNorm1 * graphHeight;
        float y2 = baseY - yNorm2 * graphHeight;
        
        renderer_.drawLineEx({x1, y1}, {x2, y2}, 1.5f, gl::Renderer::fade(theme_.text, 0.7f));
    }
}

void PluginEditor::renderEQCurve() {
    int marginLeft = 55;
    int marginRight = 15;
    int marginTop = 40;
    int marginBottom = 40;
    
    int graphWidth = width_ - marginLeft - marginRight;
    int graphHeight = height_ - marginTop - marginBottom;
    int baseY = height_ - marginBottom;
    int centerY = marginTop + graphHeight / 2;
    
    float logFreqMin = std::log10(20.0f);
    float logFreqMax = std::log10(20000.0f);
    float logFreqRange = logFreqMax - logFreqMin;
    
    float dbMin = -12.0f;
    float dbMax = 12.0f;
    float dbRange = dbMax - dbMin;
    
    int numPoints = graphWidth;
    std::vector<float> response(numPoints);
    
    for (int px = 0; px < numPoints; ++px) {
        float xNorm = static_cast<float>(px) / (numPoints - 1);
        float logFreq = logFreqMin + xNorm * logFreqRange;
        float freq = std::pow(10.0f, logFreq);
        
        float totalGainDb = 0.0f;
        
        for (int band = 0; band < 5; ++band) {
            float bandFreq = eqControls_[band].frequency;
            float bandGain = eqControls_[band].gain;
            float bandQ = eqControls_[band].q;
            
            if (std::abs(bandGain) < 0.01f) continue;
            
            float logDist = std::log10(freq) - std::log10(bandFreq);
            float bandwidth = 1.0f / bandQ;
            
            float bellShape = std::exp(-(logDist * logDist) / (2.0f * bandwidth * bandwidth * 0.1f));
            totalGainDb += bandGain * bellShape;
        }
        
        response[px] = totalGainDb;
    }
    
    // Filled area
    for (int px = 0; px < numPoints - 1; ++px) {
        float gain1 = response[px];
        float gain2 = response[px + 1];
        
        float yNorm1 = (gain1 - dbMin) / dbRange;
        float yNorm2 = (gain2 - dbMin) / dbRange;
        
        float y1 = baseY - yNorm1 * graphHeight;
        float y2 = baseY - yNorm2 * graphHeight;
        
        float x1 = static_cast<float>(marginLeft + px);
        float x2 = static_cast<float>(marginLeft + px + 1);
        
        float avgGain = (gain1 + gain2) / 2.0f;
        gl::Color fillColor = avgGain >= 0 
            ? gl::Renderer::fade(theme_.barMid, 0.25f)
            : gl::Renderer::fade(theme_.barLow, 0.25f);
        
        gl::Vector2 v1 = {x1, y1};
        gl::Vector2 v2 = {x2, y2};
        gl::Vector2 v3 = {x2, static_cast<float>(centerY)};
        gl::Vector2 v4 = {x1, static_cast<float>(centerY)};
        
        renderer_.drawTriangle(v1, v4, v3, fillColor);
        renderer_.drawTriangle(v1, v3, v2, fillColor);
    }
    
    // Curve line
    for (int px = 0; px < numPoints - 1; ++px) {
        float yNorm1 = (response[px] - dbMin) / dbRange;
        float yNorm2 = (response[px + 1] - dbMin) / dbRange;
        
        float y1 = baseY - yNorm1 * graphHeight;
        float y2 = baseY - yNorm2 * graphHeight;
        
        float x1 = static_cast<float>(marginLeft + px);
        float x2 = static_cast<float>(marginLeft + px + 1);
        
        renderer_.drawLineEx({x1, y1}, {x2, y2}, 2.0f, gl::Renderer::fade(theme_.accent, 0.8f));
    }
}

void PluginEditor::renderEQControls() {
    int marginLeft = 55;
    int marginRight = 15;
    int marginTop = 40;
    int marginBottom = 40;
    
    int graphWidth = width_ - marginLeft - marginRight;
    int graphHeight = height_ - marginTop - marginBottom;
    int baseY = height_ - marginBottom;
    
    float logFreqMin = std::log10(20.0f);
    float logFreqMax = std::log10(20000.0f);
    float logFreqRange = logFreqMax - logFreqMin;
    
    float dbMin = -12.0f;
    float dbMax = 12.0f;
    float dbRange = dbMax - dbMin;
    
    for (int i = 0; i < 5; ++i) {
        // Calculate position
        float logPos = (std::log10(eqControls_[i].frequency) - logFreqMin) / logFreqRange;
        float x = marginLeft + logPos * graphWidth;
        
        float yNorm = (eqControls_[i].gain - dbMin) / dbRange;
        float y = baseY - yNorm * graphHeight;
        
        eqControls_[i].x = x;
        eqControls_[i].y = y;
        
        // Q indicator
        float qRadius = 22.0f / eqControls_[i].q;
        qRadius = std::clamp(qRadius, 5.0f, 45.0f);
        
        if (std::abs(eqControls_[i].gain) > 0.1f) {
            renderer_.drawCircleLines(static_cast<int>(x), static_cast<int>(y), 
                                     qRadius, gl::Renderer::fade(theme_.textDim, 0.2f));
        }
        
        // Control knob
        float radius = 8.0f;
        gl::Color knobColor;
        
        if (eqControls_[i].dragging) {
            knobColor = theme_.accent;
            radius = 12.0f;
        } else if (eqControls_[i].hovered) {
            knobColor = gl::Renderer::fade(theme_.accent, 0.9f);
            radius = 10.0f;
        } else {
            knobColor = std::abs(eqControls_[i].gain) > 0.1f ? theme_.barMid : theme_.textDim;
        }
        
        // Glow
        renderer_.drawCircle(static_cast<int>(x), static_cast<int>(y), 
                            radius + 2, gl::Renderer::fade(knobColor, 0.2f));
        
        // Main knob
        renderer_.drawCircle(static_cast<int>(x), static_cast<int>(y), radius, knobColor);
        
        // Inner dot
        renderer_.drawCircle(static_cast<int>(x), static_cast<int>(y), 2.0f, theme_.background);
        
        // Tooltip
        if (eqControls_[i].hovered || eqControls_[i].dragging) {
            char freqText[32];
            if (eqControls_[i].frequency >= 1000) {
                snprintf(freqText, sizeof(freqText), "%.1fk", eqControls_[i].frequency / 1000.0f);
            } else {
                snprintf(freqText, sizeof(freqText), "%.0f", eqControls_[i].frequency);
            }
            
            char infoText[64];
            snprintf(infoText, sizeof(infoText), "%sHz  %+.1fdB  Q%.1f", 
                    freqText, eqControls_[i].gain, eqControls_[i].q);
            
            int infoWidth = renderer_.measureText(infoText, 11);
            int tooltipX = static_cast<int>(x) - infoWidth / 2;
            int tooltipY = static_cast<int>(y) - static_cast<int>(radius) - 22;
            
            tooltipX = (std::max)(5, (std::min)(width_ - infoWidth - 10, tooltipX));
            tooltipY = (std::max)(5, tooltipY);
            
            gl::Rectangle tooltipRect(static_cast<float>(tooltipX - 4), static_cast<float>(tooltipY - 2),
                                      static_cast<float>(infoWidth + 8), 16.0f);
            renderer_.drawRectangleRounded(tooltipRect, 0.3f, 4, 
                                          gl::Renderer::fade(theme_.background, 0.9f));
            renderer_.drawRectangleRoundedLines(tooltipRect, 0.3f, 4, 
                                               gl::Renderer::fade(theme_.accent, 0.5f));
            renderer_.drawText(infoText, tooltipX, tooltipY, 11, theme_.accent);
        }
    }
}

void PluginEditor::handleMouseMove(int x, int y) {
    mouseX_ = x;
    mouseY_ = y;
    
    // Update hover states
    for (int i = 0; i < 5; ++i) {
        float dx = x - eqControls_[i].x;
        float dy = y - eqControls_[i].y;
        float dist = std::sqrt(dx * dx + dy * dy);
        eqControls_[i].hovered = (dist < 18.0f);
    }
    
    // Handle dragging
    if (draggedBand_ >= 0 && mouseDown_) {
        int marginLeft = 55;
        int marginRight = 15;
        int marginTop = 40;
        int marginBottom = 40;
        
        int graphWidth = width_ - marginLeft - marginRight;
        int graphHeight = height_ - marginTop - marginBottom;
        int baseY = height_ - marginBottom;
        
        float logFreqMin = std::log10(20.0f);
        float logFreqMax = std::log10(20000.0f);
        float logFreqRange = logFreqMax - logFreqMin;
        
        float dbMin = -12.0f;
        float dbMax = 12.0f;
        float dbRange = dbMax - dbMin;
        
        // Calculate new frequency
        float xNorm = static_cast<float>(x - marginLeft) / graphWidth;
        xNorm = std::clamp(xNorm, 0.0f, 1.0f);
        float logFreq = logFreqMin + xNorm * logFreqRange;
        float newFreq = std::pow(10.0f, logFreq);
        
        // Calculate new gain
        float yNorm = static_cast<float>(baseY - y) / graphHeight;
        yNorm = std::clamp(yNorm, 0.0f, 1.0f);
        float newGain = dbMin + yNorm * dbRange;
        
        // Snap gain to 0
        if (std::abs(newGain) < 0.3f) newGain = 0.0f;
        
        // Snap frequency
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
        
        // Update controller
        if (controller_) {
            // Convert to normalized values and send to controller
            double freqNorm = (std::log10(newFreq) - logFreqMin) / logFreqRange;
            double gainNorm = (newGain - dbMin) / dbRange;
            
            controller_->setParamNormalized(getBandFreqParam(draggedBand_), freqNorm);
            controller_->setParamNormalized(getBandGainParam(draggedBand_), gainNorm);
            controller_->performEdit(getBandFreqParam(draggedBand_), freqNorm);
            controller_->performEdit(getBandGainParam(draggedBand_), gainNorm);
        }
        
        eqControls_[draggedBand_].frequency = newFreq;
        eqControls_[draggedBand_].gain = newGain;
    }
}

void PluginEditor::handleMouseDown(int x, int y) {
    mouseDown_ = true;
    
    // Theme dropdown handling
    int selectorX = 10;
    int selectorY = 8;
    int selectorWidth = 120;
    int itemHeight = 22;
    
    bool overThemeButton = (x >= selectorX && x < selectorX + selectorWidth &&
                            y >= selectorY && y < selectorY + itemHeight);
    
    if (themeDropdownOpen_) {
        // Check if clicking on a theme option
        int dropdownY = selectorY + itemHeight + 2;
        
        for (int i = 0; i < colors::themes::NUM_THEMES; ++i) {
            int itemY = dropdownY + 2 + i * itemHeight;
            
            if (x >= selectorX && x < selectorX + selectorWidth &&
                y >= itemY && y < itemY + itemHeight) {
                // Select this theme
                themeIndex_ = i;
                theme_ = ColorTheme::byIndex(themeIndex_);
                themeDropdownOpen_ = false;
                return;
            }
        }
        
        // Clicked outside dropdown - close it
        themeDropdownOpen_ = false;
        if (!overThemeButton) {
            // Continue to check EQ controls
        } else {
            return;
        }
    } else if (overThemeButton) {
        // Open dropdown
        themeDropdownOpen_ = true;
        return;
    }
    
    // Check if clicking on an EQ control
    for (int i = 0; i < 5; ++i) {
        if (eqControls_[i].hovered) {
            draggedBand_ = i;
            eqControls_[i].dragging = true;
            break;
        }
    }
}

void PluginEditor::handleMouseUp(int x, int y) {
    mouseDown_ = false;
    
    if (draggedBand_ >= 0) {
        eqControls_[draggedBand_].dragging = false;
        draggedBand_ = -1;
    }
}

void PluginEditor::handleMouseWheel(float delta) {
    // Adjust Q of hovered band
    for (int i = 0; i < 5; ++i) {
        if (eqControls_[i].hovered) {
            float newQ = eqControls_[i].q + delta * 0.2f;
            newQ = std::clamp(newQ, 0.1f, 10.0f);
            
            eqControls_[i].q = newQ;
            
            // Update controller
            if (controller_) {
                float qLogMin = std::log10(0.1f);
                float qLogMax = std::log10(10.0f);
                double qNorm = (std::log10(newQ) - qLogMin) / (qLogMax - qLogMin);
                
                controller_->setParamNormalized(getBandQParam(i), qNorm);
                controller_->performEdit(getBandQParam(i), qNorm);
            }
            break;
        }
    }
}

gl::Color PluginEditor::getBarColor(float normalizedFreq, float magnitude) const {
    gl::Color result;
    
    if (normalizedFreq < 0.33f) {
        float t = normalizedFreq / 0.33f;
        result = gl::Renderer::lerpColor(theme_.barLow, theme_.barMid, t);
    } else if (normalizedFreq < 0.66f) {
        float t = (normalizedFreq - 0.33f) / 0.33f;
        result = gl::Renderer::lerpColor(theme_.barMid, theme_.barHigh, t);
    } else {
        result = theme_.barHigh;
    }
    
    float brightness = 0.6f + magnitude * 0.4f;
    result.r = static_cast<uint8_t>((std::min)(255.0f, result.r * brightness));
    result.g = static_cast<uint8_t>((std::min)(255.0f, result.g * brightness));
    result.b = static_cast<uint8_t>((std::min)(255.0f, result.b * brightness));
    
    return result;
}

std::string PluginEditor::formatFrequency(double hz) const {
    if (hz >= 1000) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(1) << (hz / 1000.0) << "k";
        return ss.str();
    } else {
        return std::to_string(static_cast<int>(hz));
    }
}

void PluginEditor::syncParametersFromController() {
    if (!controller_) return;
    
    // Sync EQ parameters from controller
    float logFreqMin = std::log10(20.0f);
    float logFreqMax = std::log10(20000.0f);
    float logFreqRange = logFreqMax - logFreqMin;
    
    float dbMin = -12.0f;
    float dbMax = 12.0f;
    float dbRange = dbMax - dbMin;
    
    float qLogMin = std::log10(0.1f);
    float qLogMax = std::log10(10.0f);
    float qLogRange = qLogMax - qLogMin;
    
    for (int i = 0; i < 5; ++i) {
        // Get normalized values from controller
        double gainNorm = controller_->getParamNormalized(getBandGainParam(i));
        double freqNorm = controller_->getParamNormalized(getBandFreqParam(i));
        double qNorm = controller_->getParamNormalized(getBandQParam(i));
        
        // Convert to actual values
        eqControls_[i].gain = static_cast<float>(dbMin + gainNorm * dbRange);
        eqControls_[i].frequency = std::pow(10.0f, logFreqMin + static_cast<float>(freqNorm) * logFreqRange);
        eqControls_[i].q = std::pow(10.0f, qLogMin + static_cast<float>(qNorm) * qLogRange);
    }
    
    // Sync bypass state
    double bypassNorm = controller_->getParamNormalized(kBypass);
    eqEnabled_ = bypassNorm < 0.5;
}

void PluginEditor::onParameterChange(Steinberg::Vst::ParamID id, double normalizedValue) {
    // Handle parameter changes from host/automation
    float logFreqMin = std::log10(20.0f);
    float logFreqMax = std::log10(20000.0f);
    float logFreqRange = logFreqMax - logFreqMin;
    
    float dbMin = -12.0f;
    float dbMax = 12.0f;
    float dbRange = dbMax - dbMin;
    
    float qLogMin = std::log10(0.1f);
    float qLogMax = std::log10(10.0f);
    float qLogRange = qLogMax - qLogMin;
    
    // Check each band's parameters
    for (int i = 0; i < 5; ++i) {
        if (id == getBandGainParam(i)) {
            eqControls_[i].gain = static_cast<float>(dbMin + normalizedValue * dbRange);
            return;
        }
        if (id == getBandFreqParam(i)) {
            eqControls_[i].frequency = std::pow(10.0f, logFreqMin + static_cast<float>(normalizedValue) * logFreqRange);
            return;
        }
        if (id == getBandQParam(i)) {
            eqControls_[i].q = std::pow(10.0f, qLogMin + static_cast<float>(normalizedValue) * qLogRange);
            return;
        }
    }
    
    // Check bypass
    if (id == kBypass) {
        eqEnabled_ = normalizedValue < 0.5;
    }
}

void PluginEditor::loadSettings() {
#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD value, size = sizeof(DWORD);
        
        if (RegQueryValueExW(hKey, REG_WINDOW_WIDTH, nullptr, nullptr, 
                             reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS) {
            width_ = static_cast<int>(value);
        }
        
        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_WINDOW_HEIGHT, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS) {
            height_ = static_cast<int>(value);
        }
        
        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_THEME_INDEX, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS) {
            themeIndex_ = static_cast<int>(value) % colors::themes::NUM_THEMES;
        }
        
        RegCloseKey(hKey);
        settingsLoaded_ = true;
    }
    
    // Enforce minimum size
    if (width_ < 600) width_ = 800;
    if (height_ < 350) height_ = 450;
#endif
}

void PluginEditor::saveSettings() {
#ifdef _WIN32
    HKEY hKey;
    DWORD disposition;
    
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                        &hKey, &disposition) == ERROR_SUCCESS) {
        DWORD value;
        
        value = static_cast<DWORD>(width_);
        RegSetValueExW(hKey, REG_WINDOW_WIDTH, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
        
        value = static_cast<DWORD>(height_);
        RegSetValueExW(hKey, REG_WINDOW_HEIGHT, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
        
        value = static_cast<DWORD>(themeIndex_);
        RegSetValueExW(hKey, REG_THEME_INDEX, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
        
        RegCloseKey(hKey);
    }
#endif
}

void PluginEditor::renderThemeSelector() {
    // Theme selector in top-left corner
    int selectorX = 10;
    int selectorY = 8;
    int selectorWidth = 120;
    int itemHeight = 22;
    
    // Current theme button
    const char* currentTheme = colors::themes::getThemeName(themeIndex_);
    
    gl::Rectangle btnRect(static_cast<float>(selectorX), static_cast<float>(selectorY),
                          static_cast<float>(selectorWidth), static_cast<float>(itemHeight));
    
    // Check if mouse is over button
    bool overButton = (mouseX_ >= selectorX && mouseX_ < selectorX + selectorWidth &&
                       mouseY_ >= selectorY && mouseY_ < selectorY + itemHeight);
    
    // Draw button background
    gl::Color btnBg = overButton ? gl::Renderer::fade(theme_.accent, 0.3f) 
                                  : gl::Renderer::fade(theme_.textDim, 0.2f);
    renderer_.drawRectangleRounded(btnRect, 0.3f, 4, btnBg);
    renderer_.drawRectangleRoundedLines(btnRect, 0.3f, 4, theme_.textDim);
    
    // Draw current theme name
    renderer_.drawText(currentTheme, selectorX + 10, selectorY + 5, 12, theme_.text);
    
    // Draw dropdown arrow
    int arrowX = selectorX + selectorWidth - 18;
    int arrowY = selectorY + 10;
    renderer_.drawTriangle(
        {static_cast<float>(arrowX), static_cast<float>(arrowY)},
        {static_cast<float>(arrowX + 10), static_cast<float>(arrowY)},
        {static_cast<float>(arrowX + 5), static_cast<float>(arrowY + 6)},
        theme_.text
    );
    
    // Draw dropdown if open
    if (themeDropdownOpen_) {
        int dropdownY = selectorY + itemHeight + 2;
        
        // Dropdown background
        gl::Rectangle dropBg(static_cast<float>(selectorX), static_cast<float>(dropdownY),
                             static_cast<float>(selectorWidth), 
                             static_cast<float>(colors::themes::NUM_THEMES * itemHeight + 4));
        renderer_.drawRectangleRounded(dropBg, 0.2f, 4, gl::Renderer::fade(theme_.background, 0.95f));
        renderer_.drawRectangleRoundedLines(dropBg, 0.2f, 4, theme_.textDim);
        
        // Draw each theme option
        for (int i = 0; i < colors::themes::NUM_THEMES; ++i) {
            int itemY = dropdownY + 2 + i * itemHeight;
            
            bool overItem = (mouseX_ >= selectorX && mouseX_ < selectorX + selectorWidth &&
                            mouseY_ >= itemY && mouseY_ < itemY + itemHeight);
            
            if (overItem) {
                gl::Rectangle itemRect(static_cast<float>(selectorX + 2), static_cast<float>(itemY),
                                       static_cast<float>(selectorWidth - 4), static_cast<float>(itemHeight));
                renderer_.drawRectangleRounded(itemRect, 0.3f, 4, gl::Renderer::fade(theme_.accent, 0.3f));
            }
            
            // Theme name with color preview
            const char* themeName = colors::themes::getThemeName(i);
            const auto& themeData = colors::themes::getTheme(i);
            
            // Color swatch
            gl::Color swatch = {themeData.barLow.r, themeData.barLow.g, themeData.barLow.b, 255};
            gl::Rectangle swatchRect(static_cast<float>(selectorX + 8), static_cast<float>(itemY + 5),
                                     12.0f, 12.0f);
            renderer_.drawRectangleRounded(swatchRect, 0.5f, 4, swatch);
            
            // Theme name
            gl::Color textColor = (i == themeIndex_) ? theme_.accent : theme_.text;
            renderer_.drawText(themeName, selectorX + 26, itemY + 5, 12, textColor);
        }
    }
}

} // namespace SpectrumEQ


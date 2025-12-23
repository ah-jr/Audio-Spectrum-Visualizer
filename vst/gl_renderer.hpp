#pragma once

/**
 * OpenGL Renderer - raylib-compatible drawing API for VST3
 * This provides the same drawing functions as raylib but uses raw OpenGL
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <GL/gl.h>
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>

namespace gl {

// ============================================================================
// Types (matching raylib)
// ============================================================================

struct Color {
    uint8_t r, g, b, a;
    
    Color() : r(0), g(0), b(0), a(255) {}
    Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) 
        : r(r_), g(g_), b(b_), a(a_) {}
};

struct Vector2 {
    float x, y;
    
    Vector2() : x(0), y(0) {}
    Vector2(float x_, float y_) : x(x_), y(y_) {}
};

struct Rectangle {
    float x, y, width, height;
    
    Rectangle() : x(0), y(0), width(0), height(0) {}
    Rectangle(float x_, float y_, float w_, float h_) 
        : x(x_), y(y_), width(w_), height(h_) {}
};

// ============================================================================
// Renderer Class
// ============================================================================

class Renderer {
public:
    Renderer();
    ~Renderer();
    
    // Initialization
    bool initialize(int width, int height);
    void resize(int width, int height);
    void shutdown();
    
    // Frame control
    void beginFrame();
    void endFrame();
    
    // Screen info
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    
    // Background
    void clearBackground(Color color);
    
    // Basic shapes
    void drawRectangle(int x, int y, int w, int h, Color color);
    void drawRectangleRec(Rectangle rec, Color color);
    void drawRectangleRounded(Rectangle rec, float roundness, int segments, Color color);
    void drawRectangleRoundedLines(Rectangle rec, float roundness, int segments, Color color);
    void drawRectangleLines(int x, int y, int w, int h, Color color);
    void drawRectangleGradientV(int x, int y, int w, int h, Color top, Color bottom);
    
    // Lines
    void drawLine(int x1, int y1, int x2, int y2, Color color);
    void drawLineEx(Vector2 start, Vector2 end, float thick, Color color);
    
    // Circles
    void drawCircle(int cx, int cy, float radius, Color color);
    void drawCircleLines(int cx, int cy, float radius, Color color);
    void drawCircleGradient(int cx, int cy, float radius, Color inner, Color outer);
    
    // Triangles
    void drawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color);
    
    // Text
    void drawText(const char* text, int x, int y, int fontSize, Color color);
    int measureText(const char* text, int fontSize);
    
    // Utilities
    static Color fade(Color color, float alpha);
    static Color lerpColor(Color a, Color b, float t);
    static bool checkCollisionPointRec(Vector2 point, Rectangle rec);
    
private:
    void setColor(Color color);
    void drawRoundedCorner(float cx, float cy, float radius, float startAngle, int segments, Color color);
    
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
    
    // Simple bitmap font data (embedded)
    static const int FONT_CHAR_WIDTH = 6;
    static const int FONT_CHAR_HEIGHT = 10;
};

// ============================================================================
// Constants
// ============================================================================

constexpr float PI = 3.14159265358979323846f;

// Predefined colors
namespace Colors {
    const Color Black = {0, 0, 0, 255};
    const Color White = {255, 255, 255, 255};
    const Color Red = {255, 0, 0, 255};
    const Color Green = {0, 255, 0, 255};
    const Color Blue = {0, 0, 255, 255};
    const Color Gray = {128, 128, 128, 255};
    const Color DarkGray = {40, 40, 40, 255};
}

} // namespace gl


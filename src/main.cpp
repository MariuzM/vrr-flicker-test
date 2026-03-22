#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double>;

enum TestMode {
    TEST_TEARING = 0,
    TEST_VRR_FLICKER,
    TEST_FRAME_SWEEP,
    TEST_BRIGHTNESS,
    TEST_UFO,
    TEST_COUNT
};

static const char* TEST_NAMES[] = {
    "Screen Tearing",
    "VRR Flicker",
    "Frame Rate Sweep",
    "Brightness",
    "UFO Motion"
};

enum VRRPattern {
    VRR_ALTERNATE = 0,
    VRR_RANDOM,
    VRR_SINE,
    VRR_STEP,
    VRR_PATTERN_COUNT
};

static const char* VRR_PATTERN_NAMES[] = {
    "Alternating",
    "Random",
    "Sine Wave",
    "Step (Hold)"
};

enum VRRScene {
    VRR_SCENE_GRAY = 0,
    VRR_SCENE_GRADIENT,
    VRR_SCENE_SPLIT,
    VRR_SCENE_ZONES,
    VRR_SCENE_COUNT
};

static const char* VRR_SCENE_NAMES[] = {
    "Uniform Gray",
    "Gradient Bands",
    "Split Comparison",
    "Diagnostic Zones"
};

struct AppState {
    TestMode currentTest = TEST_TEARING;
    bool showUI = true;
    bool fullscreen = false;
    int windowedX = 100, windowedY = 100, windowedW = 1280, windowedH = 720;

    bool vsync = false;
    int targetFps = 60;

    float barSpeed = 800.0f;
    float barWidth = 80.0f;
    int barColorMode = 0;
    int tearingStyle = 0;
    int tearingRows = 5;
    bool tearingCheckerboard = true;

    int vrrLowFps = 30;
    int vrrHighFps = 144;
    VRRPattern vrrPattern = VRR_ALTERNATE;
    int vrrAlternateFrames = 1;
    VRRScene vrrScene = VRR_SCENE_ZONES;
    float vrrGrayLevel = 0.5f;
    int vrrStepHoldFrames = 30;
    bool vrrStepOnHigh = true;
    int vrrStepCounter = 0;
    float vrrActualFps = 0.0f;
    std::vector<float> vrrFpsHistory;
    static constexpr int VRR_FPS_HISTORY = 200;

    int sweepMinFps = 30;
    int sweepMaxFps = 144;
    float sweepSpeed = 10.0f;
    float sweepPosition = 0.0f;
    bool sweepUp = true;

    float brightnessBase = 0.5f;
    float brightnessAmplitude = 0.3f;
    float brightnessFrequency = 1.0f;

    float ufoSpeed = 600.0f;
    float ufoSize = 50.0f;

    float barPosition = 0.0f;
    float ufoPosition = 0.0f;
    double totalTime = 0.0;
    int frameCount = 0;
    float currentFps = 0.0f;
    float currentFrameTime = 0.0f;
    float minFrameTime = 999.0f;
    float maxFrameTime = 0.0f;
    float avgFrameTime = 0.0f;
    bool vrrCurrentHigh = false;
    int vrrFrameCounter = 0;

    std::vector<float> frameTimeHistory;
    static constexpr int MAX_HISTORY = 300;
};

static void drawRect(float x, float y, float w, float h,
                     float r, float g, float b, float a = 1.0f) {
    if (a < 1.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
    if (a < 1.0f) {
        glDisable(GL_BLEND);
    }
}

static void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    int i = static_cast<int>(h * 6.0f);
    float f = h * 6.0f - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
        default: r = g = b = 0; break;
    }
}

static void preciseSleep(double seconds) {
    if (seconds <= 0) return;
    auto start = Clock::now();
    if (seconds > 0.002) {
        std::this_thread::sleep_for(Duration(seconds - 0.002));
    }
    while (Duration(Clock::now() - start).count() < seconds) {}
}

static void drawCheckerboard(float x, float y, float w, float h, float cellSize,
                             float r1, float g1, float b1,
                             float r2, float g2, float b2, float offsetX) {
    float ox = fmod(offsetX, cellSize * 2.0f);
    if (ox < 0) ox += cellSize * 2.0f;
    for (float cx = x - cellSize * 2.0f + ox; cx < x + w + cellSize; cx += cellSize) {
        for (float cy = y; cy < y + h; cy += cellSize) {
            int col = static_cast<int>((cx - x + cellSize * 100.0f) / cellSize);
            int row = static_cast<int>((cy - y) / cellSize);
            bool even = (col + row) % 2 == 0;
            if (even)
                drawRect(cx, cy, cellSize, cellSize, r1, g1, b1);
            else
                drawRect(cx, cy, cellSize, cellSize, r2, g2, b2);
        }
    }
}

static void renderTearingTest(AppState& state, int w, int h, double dt) {
    state.barPosition += state.barSpeed * static_cast<float>(dt);

    float fh = static_cast<float>(h);
    float fw = static_cast<float>(w);
    int rows = std::max(1, state.tearingRows);
    float rowHeight = fh / static_cast<float>(rows);

    if (state.tearingStyle == 0) {
        for (int i = 0; i < rows; i++) {
            float speedMul = static_cast<float>(i + 1);
            float offset = fmod(state.barPosition * speedMul, state.barWidth * 2.0f);
            float rowY = static_cast<float>(i) * rowHeight;

            float bgShade = (i % 2 == 0) ? 0.05f : 0.15f;
            drawRect(0, rowY, fw, rowHeight, bgShade, bgShade, bgShade);

            float totalWidth = state.barWidth * 2.0f;
            for (float x = -totalWidth + offset; x < fw + totalWidth; x += totalWidth) {
                float r = 1.0f, g = 1.0f, b = 1.0f;
                if (state.barColorMode == 1) {
                    r = (i % 2 == 0) ? 1.0f : 0.2f;
                    g = (i % 2 == 0) ? 0.2f : 1.0f;
                    b = 0.1f;
                } else if (state.barColorMode == 2) {
                    hsvToRgb(static_cast<float>(i) / static_cast<float>(rows), 1.0f, 1.0f, r, g, b);
                } else if (state.barColorMode == 3) {
                    float hue = fmod(x / fw + static_cast<float>(i) * 0.15f, 1.0f);
                    if (hue < 0) hue += 1.0f;
                    hsvToRgb(hue, 1.0f, 1.0f, r, g, b);
                }
                drawRect(x, rowY, state.barWidth, rowHeight, r, g, b);
            }
        }
    } else if (state.tearingStyle == 1) {
        for (int i = 0; i < rows; i++) {
            float speedMul = (i % 2 == 0) ? 1.0f : -1.0f;
            speedMul *= static_cast<float>(i + 1);
            float offset = state.barPosition * speedMul;
            float rowY = static_cast<float>(i) * rowHeight;
            float cellSize = state.barWidth * 0.5f;

            float r1, g1, b1, r2, g2, b2;
            if (i % 2 == 0) {
                r1 = 1; g1 = 1; b1 = 1; r2 = 0; g2 = 0; b2 = 0;
            } else {
                r1 = 0; g1 = 0; b1 = 0; r2 = 1; g2 = 1; b2 = 1;
            }
            drawCheckerboard(0, rowY, fw, rowHeight, cellSize, r1, g1, b1, r2, g2, b2, offset);
        }
    } else {
        for (int i = 0; i < rows; i++) {
            float speedMul = static_cast<float>(i + 1);
            float offset = fmod(state.barPosition * speedMul, fw);
            float rowY = static_cast<float>(i) * rowHeight;

            float bgShade = (i % 2 == 0) ? 0.0f : 0.1f;
            drawRect(0, rowY, fw, rowHeight, bgShade, bgShade, bgShade);

            float r, g, b;
            hsvToRgb(static_cast<float>(i) / static_cast<float>(rows), 0.9f, 1.0f, r, g, b);

            float gradW = fw * 0.5f;
            float gx = fmod(offset, fw + gradW) - gradW;
            int steps = 32;
            float stepW = gradW / static_cast<float>(steps);
            for (int j = 0; j < steps; j++) {
                float t = static_cast<float>(j) / static_cast<float>(steps);
                float alpha = sinf(t * static_cast<float>(M_PI));
                drawRect(gx + static_cast<float>(j) * stepW, rowY,
                         stepW + 1.0f, rowHeight,
                         r * alpha, g * alpha, b * alpha);
            }
        }
    }

    if (state.tearingCheckerboard && state.tearingStyle != 1) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (int i = 0; i < rows; i++) {
            float rowY = static_cast<float>(i) * rowHeight;
            glColor4f(1.0f, 1.0f, 1.0f, 0.08f);
            glBegin(GL_LINES);
            glVertex2f(0, rowY);
            glVertex2f(fw, rowY);
            glEnd();
        }
        glDisable(GL_BLEND);
    }
}

static double getVRRTargetFrameTime(AppState& state) {
    int range = state.vrrHighFps - state.vrrLowFps;
    if (range < 1) range = 1;

    switch (state.vrrPattern) {
        case VRR_ALTERNATE:
            state.vrrFrameCounter++;
            if (state.vrrFrameCounter >= state.vrrAlternateFrames) {
                state.vrrFrameCounter = 0;
                state.vrrCurrentHigh = !state.vrrCurrentHigh;
            }
            return 1.0 / (state.vrrCurrentHigh ? state.vrrHighFps : state.vrrLowFps);

        case VRR_RANDOM:
            return 1.0 / (state.vrrLowFps + rand() % (range + 1));

        case VRR_SINE: {
            float normalized = (sinf(static_cast<float>(state.totalTime) * 2.0f) + 1.0f) * 0.5f;
            float fps = state.vrrLowFps + normalized * range;
            return 1.0 / fps;
        }

        case VRR_STEP: {
            state.vrrStepCounter++;
            if (state.vrrStepCounter >= state.vrrStepHoldFrames) {
                state.vrrStepCounter = 0;
                state.vrrStepOnHigh = !state.vrrStepOnHigh;
            }
            return 1.0 / (state.vrrStepOnHigh ? state.vrrHighFps : state.vrrLowFps);
        }

        default:
            return 1.0 / 60.0;
    }
}

static void renderVRRFlickerTest(AppState& state, int w, int h) {
    float fw = static_cast<float>(w);
    float fh = static_cast<float>(h);
    float g = state.vrrGrayLevel;

    switch (state.vrrScene) {
        case VRR_SCENE_GRAY:
            drawRect(0, 0, fw, fh, g, g, g);
            break;

        case VRR_SCENE_GRADIENT: {
            int bands = 8;
            float bandH = fh / static_cast<float>(bands);
            for (int i = 0; i < bands; i++) {
                float t = static_cast<float>(i) / static_cast<float>(bands - 1);
                float shade = t;
                drawRect(0, static_cast<float>(i) * bandH, fw, bandH + 1,
                         shade, shade, shade);
            }
            break;
        }

        case VRR_SCENE_SPLIT: {
            float half = fw * 0.5f;
            drawRect(0, 0, half, fh, g, g, g);

            int steps = 16;
            float stepH = fh / static_cast<float>(steps);
            for (int i = 0; i < steps; i++) {
                float t = static_cast<float>(i) / static_cast<float>(steps - 1);
                float shade = 0.1f + t * 0.8f;
                drawRect(half, static_cast<float>(i) * stepH, half, stepH + 1,
                         shade, shade, shade);
            }

            glColor4f(0.3f, 0.3f, 0.3f, 1.0f);
            glBegin(GL_LINES);
            glVertex2f(half, 0);
            glVertex2f(half, fh);
            glEnd();
            break;
        }

        case VRR_SCENE_ZONES:
        default: {
            float topH = fh * 0.4f;
            drawRect(0, 0, fw, topH, g, g, g);

            float midY = topH;
            float midH = fh * 0.2f;
            int cols = 12;
            float colW = fw / static_cast<float>(cols);
            for (int i = 0; i < cols; i++) {
                float shade = (i % 2 == 0) ? 0.0f : 1.0f;
                drawRect(static_cast<float>(i) * colW, midY, colW + 1, midH,
                         shade, shade, shade);
            }

            float botY = midY + midH;
            float botH = fh - botY;
            int gradSteps = 64;
            float stepW = fw / static_cast<float>(gradSteps);
            for (int i = 0; i < gradSteps; i++) {
                float t = static_cast<float>(i) / static_cast<float>(gradSteps - 1);
                drawRect(static_cast<float>(i) * stepW, botY, stepW + 1, botH * 0.5f,
                         t, t, t);
            }

            float lastRowY = botY + botH * 0.5f;
            float lastRowH = botH * 0.5f;
            drawRect(0, lastRowY, fw * 0.25f, lastRowH, 0.2f, 0.2f, 0.2f);
            drawRect(fw * 0.25f, lastRowY, fw * 0.25f, lastRowH, 0.4f, 0.4f, 0.4f);
            drawRect(fw * 0.5f, lastRowY, fw * 0.25f, lastRowH, 0.6f, 0.6f, 0.6f);
            drawRect(fw * 0.75f, lastRowY, fw * 0.25f, lastRowH, 0.8f, 0.8f, 0.8f);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(1.0f, 1.0f, 1.0f, 0.05f);
            glBegin(GL_LINES);
            glVertex2f(0, midY);
            glVertex2f(fw, midY);
            glVertex2f(0, botY);
            glVertex2f(fw, botY);
            glVertex2f(0, lastRowY);
            glVertex2f(fw, lastRowY);
            glEnd();
            glDisable(GL_BLEND);
            break;
        }
    }

    state.vrrActualFps = state.currentFps;
    state.vrrFpsHistory.push_back(state.vrrActualFps);
    if (static_cast<int>(state.vrrFpsHistory.size()) > AppState::VRR_FPS_HISTORY)
        state.vrrFpsHistory.erase(state.vrrFpsHistory.begin());
}

static void renderFrameSweepTest(AppState& state, int w, int h, double dt) {
    int range = state.sweepMaxFps - state.sweepMinFps;
    if (range < 1) range = 1;

    float delta = state.sweepSpeed * static_cast<float>(dt) / static_cast<float>(range);
    if (state.sweepUp) {
        state.sweepPosition += delta;
        if (state.sweepPosition >= 1.0f) {
            state.sweepPosition = 1.0f;
            state.sweepUp = false;
        }
    } else {
        state.sweepPosition -= delta;
        if (state.sweepPosition <= 0.0f) {
            state.sweepPosition = 0.0f;
            state.sweepUp = true;
        }
    }

    float gray = 0.5f;
    drawRect(0, 0, static_cast<float>(w), static_cast<float>(h), gray, gray, gray);

    float barH = 20.0f;
    float barY = static_cast<float>(h) - barH - 40.0f;
    drawRect(0, barY, static_cast<float>(w), barH, 0.2f, 0.2f, 0.2f);
    drawRect(0, barY, static_cast<float>(w) * state.sweepPosition, barH, 0.0f, 0.7f, 0.0f);
}

static void renderBrightnessTest(AppState& state, int w, int h) {
    float brightness = state.brightnessBase +
        state.brightnessAmplitude * sinf(static_cast<float>(state.totalTime) *
            state.brightnessFrequency * 2.0f * static_cast<float>(M_PI));
    brightness = std::max(0.0f, std::min(1.0f, brightness));
    drawRect(0, 0, static_cast<float>(w), static_cast<float>(h),
             brightness, brightness, brightness);
}

static void renderUFOTest(AppState& state, int w, int h, double dt) {
    for (int y = 0; y < h; y += 40) {
        float shade = ((y / 40) % 2 == 0) ? 0.12f : 0.08f;
        drawRect(0, static_cast<float>(y), static_cast<float>(w), 40.0f,
                 shade, shade, shade);
    }

    glColor4f(0.2f, 0.2f, 0.2f, 1.0f);
    for (int x = 0; x < w; x += 100) {
        glBegin(GL_LINES);
        glVertex2f(static_cast<float>(x), 0);
        glVertex2f(static_cast<float>(x), static_cast<float>(h));
        glEnd();
    }

    state.ufoPosition += state.ufoSpeed * static_cast<float>(dt);
    if (state.ufoPosition > w + state.ufoSize) {
        state.ufoPosition = -state.ufoSize;
    }

    float ufoX = state.ufoPosition;
    float ufoY = (static_cast<float>(h) - state.ufoSize) / 2.0f;

    drawRect(ufoX + 3, ufoY + 3, state.ufoSize, state.ufoSize, 0, 0, 0, 0.3f);
    drawRect(ufoX, ufoY, state.ufoSize, state.ufoSize, 0.1f, 0.8f, 0.2f);
    drawRect(ufoX + 4, ufoY + 4, state.ufoSize * 0.4f, state.ufoSize * 0.4f,
             0.3f, 1.0f, 0.5f);
}

static void renderControlPanel(AppState& state, GLFWwindow* window) {
    (void)window;

    if (!state.showUI) {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin("##minimal", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("FPS: %.1f | Frame: %.2fms | Press F1 for controls",
            state.currentFps, state.currentFrameTime * 1000.0f);
        ImGui::End();
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("VRR Flicker & Tearing Test");

    int currentTest = static_cast<int>(state.currentTest);
    ImGui::Combo("Test", &currentTest, TEST_NAMES, TEST_COUNT);
    state.currentTest = static_cast<TestMode>(currentTest);

    ImGui::Separator();

    if (ImGui::Checkbox("VSync", &state.vsync)) {
        glfwSwapInterval(state.vsync ? 1 : 0);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(V key)");

    bool showTargetFps = !state.vsync &&
        (state.currentTest == TEST_TEARING || state.currentTest == TEST_UFO);
    if (showTargetFps) {
        ImGui::SliderInt("Target FPS", &state.targetFps, 10, 500);
    }

    ImGui::Separator();

    switch (state.currentTest) {
        case TEST_TEARING: {
            ImGui::Text("Tearing Settings");
            const char* styles[] = {"Multi-Speed Bars", "Checkerboard", "Color Gradient"};
            ImGui::Combo("Style", &state.tearingStyle, styles, 3);
            ImGui::SliderInt("Rows", &state.tearingRows, 1, 12);
            ImGui::SliderFloat("Speed (px/s)", &state.barSpeed, 100.0f, 5000.0f);
            ImGui::SliderFloat("Bar/Cell Width", &state.barWidth, 10.0f, 300.0f);
            if (state.tearingStyle == 0) {
                const char* colors[] = {"White", "Alternating", "Per-Row Hue", "Rainbow"};
                ImGui::Combo("Color", &state.barColorMode, colors, 4);
            }
            if (state.tearingStyle != 1)
                ImGui::Checkbox("Row Dividers", &state.tearingCheckerboard);
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Each row scrolls at a different speed. Tearing appears as "
                "horizontal misalignment between rows. Disable VSync to test.");
            break;
        }

        case TEST_VRR_FLICKER: {
            ImGui::Text("VRR Flicker Settings");

            int scene = static_cast<int>(state.vrrScene);
            ImGui::Combo("Scene", &scene, VRR_SCENE_NAMES, VRR_SCENE_COUNT);
            state.vrrScene = static_cast<VRRScene>(scene);

            ImGui::SliderFloat("Gray Level", &state.vrrGrayLevel, 0.0f, 1.0f);

            ImGui::Separator();
            ImGui::Text("Frame Pacing");

            ImGui::SliderInt("Low FPS", &state.vrrLowFps, 10, 500);
            ImGui::SliderInt("High FPS", &state.vrrHighFps, 10, 500);
            if (state.vrrLowFps > state.vrrHighFps)
                state.vrrLowFps = state.vrrHighFps;

            int pat = static_cast<int>(state.vrrPattern);
            ImGui::Combo("Pattern", &pat, VRR_PATTERN_NAMES, VRR_PATTERN_COUNT);
            state.vrrPattern = static_cast<VRRPattern>(pat);

            if (state.vrrPattern == VRR_ALTERNATE)
                ImGui::SliderInt("Alternate Every N", &state.vrrAlternateFrames, 1, 60);
            if (state.vrrPattern == VRR_STEP)
                ImGui::SliderInt("Hold Frames", &state.vrrStepHoldFrames, 5, 300);

            ImGui::Separator();

            float targetFps = 0;
            if (!state.vrrFpsHistory.empty())
                targetFps = state.vrrFpsHistory.back();
            ImGui::Text("Delivering: %.1f FPS", targetFps);
            ImGui::Text("Range: %d - %d FPS", state.vrrLowFps, state.vrrHighFps);

            if (!state.vrrFpsHistory.empty()) {
                float lo = static_cast<float>(state.vrrLowFps) * 0.8f;
                float hi = static_cast<float>(state.vrrHighFps) * 1.2f;
                ImGui::PlotLines("##vrrfps", state.vrrFpsHistory.data(),
                    static_cast<int>(state.vrrFpsHistory.size()), 0,
                    "FPS over time", lo, hi, ImVec2(-1, 80));
            }

            ImGui::Spacing();
            ImGui::TextWrapped(
                "Varies frame delivery rate to trigger VRR flicker. "
                "Enable VSync + FreeSync/G-Sync in your driver. Run fullscreen (F11). "
                "Flicker seen as brightness pulsing (common on VA panels at low Hz). "
                "'Diagnostic Zones' scene shows gray, B/W bars, and gradient — "
                "flicker is most visible in the uniform gray area.");
            break;
        }

        case TEST_FRAME_SWEEP: {
            ImGui::Text("Frame Sweep Settings");
            ImGui::SliderInt("Min FPS", &state.sweepMinFps, 10, 240);
            ImGui::SliderInt("Max FPS", &state.sweepMaxFps, 10, 240);
            if (state.sweepMinFps > state.sweepMaxFps)
                state.sweepMinFps = state.sweepMaxFps;
            ImGui::SliderFloat("Sweep Speed", &state.sweepSpeed, 1.0f, 120.0f);

            float currentSweepFps = state.sweepMinFps +
                state.sweepPosition * (state.sweepMaxFps - state.sweepMinFps);
            ImGui::Text("Current: %.1f FPS", currentSweepFps);
            ImGui::ProgressBar(state.sweepPosition);
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Sweeps frame rate from min to max and back. "
                "Identifies at which refresh rates VRR flicker occurs.");
            break;
        }

        case TEST_BRIGHTNESS: {
            ImGui::Text("Brightness Settings");
            ImGui::SliderFloat("Base", &state.brightnessBase, 0.0f, 1.0f);
            ImGui::SliderFloat("Amplitude", &state.brightnessAmplitude, 0.0f, 0.5f);
            ImGui::SliderFloat("Frequency (Hz)", &state.brightnessFrequency, 0.1f, 30.0f);
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Oscillates screen brightness via content changes. "
                "Tests if visible flicker comes from VRR or panel response.");
            break;
        }

        case TEST_UFO: {
            ImGui::Text("UFO Settings");
            ImGui::SliderFloat("Speed (px/s)", &state.ufoSpeed, 100.0f, 5000.0f);
            ImGui::SliderFloat("Size (px)", &state.ufoSize, 10.0f, 200.0f);
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Moving object for observing motion smoothness, "
                "judder, and frame pacing quality.");
            break;
        }

        default:
            break;
    }

    ImGui::Separator();

    ImGui::Text("FPS: %.1f", state.currentFps);
    ImGui::Text("Frame: %.2f ms", state.currentFrameTime * 1000.0);
    ImGui::Text("Min: %.2f  Max: %.2f  Avg: %.2f ms",
        state.minFrameTime * 1000.0, state.maxFrameTime * 1000.0,
        state.avgFrameTime * 1000.0);

    if (!state.frameTimeHistory.empty()) {
        ImGui::PlotLines("##ft", state.frameTimeHistory.data(),
            static_cast<int>(state.frameTimeHistory.size()), 0,
            "Frame Times (ms)", 0.0f, 50.0f, ImVec2(-1, 60));
    }

    if (ImGui::Button("Reset Stats")) {
        state.minFrameTime = 999.0f;
        state.maxFrameTime = 0.0f;
        state.frameTimeHistory.clear();
    }

    ImGui::Separator();
    ImGui::TextDisabled("F1: UI | F11: Fullscreen | ESC: Quit | 1-5: Tests | V: VSync");

    ImGui::End();
}

static void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state) return;

    if (ImGui::GetIO().WantCaptureKeyboard) return;

    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_F1:
        case GLFW_KEY_TAB:
            state->showUI = !state->showUI;
            break;
        case GLFW_KEY_F11: {
            state->fullscreen = !state->fullscreen;
            if (state->fullscreen) {
                glfwGetWindowPos(window, &state->windowedX, &state->windowedY);
                glfwGetWindowSize(window, &state->windowedW, &state->windowedH);
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);
                glfwSetWindowMonitor(window, monitor, 0, 0,
                    mode->width, mode->height, mode->refreshRate);
            } else {
                glfwSetWindowMonitor(window, nullptr,
                    state->windowedX, state->windowedY,
                    state->windowedW, state->windowedH, 0);
            }
            glfwSwapInterval(state->vsync ? 1 : 0);
            break;
        }
        case GLFW_KEY_V:
            state->vsync = !state->vsync;
            glfwSwapInterval(state->vsync ? 1 : 0);
            break;
        case GLFW_KEY_1: state->currentTest = TEST_TEARING; break;
        case GLFW_KEY_2: state->currentTest = TEST_VRR_FLICKER; break;
        case GLFW_KEY_3: state->currentTest = TEST_FRAME_SWEEP; break;
        case GLFW_KEY_4: state->currentTest = TEST_BRIGHTNESS; break;
        case GLFW_KEY_5: state->currentTest = TEST_UFO; break;
        default: break;
    }
}

int main() {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#endif

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* vidmode = glfwGetVideoMode(monitor);

    GLFWwindow* window = glfwCreateWindow(vidmode->width, vidmode->height,
        "VRR Flicker & Screen Tearing Test", monitor, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    AppState state;
    state.fullscreen = true;
    state.windowedW = vidmode->width / 2;
    state.windowedH = vidmode->height / 2;
    state.windowedX = vidmode->width / 4;
    state.windowedY = vidmode->height / 4;
    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, keyCallback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    if (xscale > 1.0f) {
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * xscale;
        io.Fonts->AddFontDefault(&cfg);
        ImGui::GetStyle().ScaleAllSizes(xscale);
    }

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    auto lastTime = Clock::now();

    while (!glfwWindowShouldClose(window)) {
        auto frameStart = Clock::now();
        double dt = Duration(frameStart - lastTime).count();
        lastTime = frameStart;

        state.totalTime += dt;
        state.frameCount++;

        glfwPollEvents();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w <= 0 || h <= 0) {
            std::this_thread::sleep_for(Duration(0.01));
            continue;
        }

        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, w, h, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        switch (state.currentTest) {
            case TEST_TEARING: renderTearingTest(state, w, h, dt); break;
            case TEST_VRR_FLICKER: renderVRRFlickerTest(state, w, h); break;
            case TEST_FRAME_SWEEP: renderFrameSweepTest(state, w, h, dt); break;
            case TEST_BRIGHTNESS: renderBrightnessTest(state, w, h); break;
            case TEST_UFO: renderUFOTest(state, w, h, dt); break;
            default: break;
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        renderControlPanel(state, window);
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        auto afterSwap = Clock::now();
        double elapsed = Duration(afterSwap - frameStart).count();

        bool applyTiming = !state.vsync ||
            state.currentTest == TEST_VRR_FLICKER ||
            state.currentTest == TEST_FRAME_SWEEP;

        if (applyTiming) {
            double targetTime;
            if (state.currentTest == TEST_VRR_FLICKER) {
                targetTime = getVRRTargetFrameTime(state);
            } else if (state.currentTest == TEST_FRAME_SWEEP) {
                float sweepFps = state.sweepMinFps +
                    state.sweepPosition * (state.sweepMaxFps - state.sweepMinFps);
                targetTime = 1.0 / std::max(1.0f, sweepFps);
            } else {
                targetTime = 1.0 / std::max(1, state.targetFps);
            }
            double remaining = targetTime - elapsed;
            if (remaining > 0) preciseSleep(remaining);
        }

        auto frameEnd = Clock::now();
        state.currentFrameTime = static_cast<float>(Duration(frameEnd - frameStart).count());
        if (state.currentFrameTime > 0)
            state.currentFps = 1.0f / state.currentFrameTime;

        if (state.frameCount > 5) {
            if (state.currentFrameTime < state.minFrameTime)
                state.minFrameTime = state.currentFrameTime;
            if (state.currentFrameTime > state.maxFrameTime)
                state.maxFrameTime = state.currentFrameTime;
        }

        state.frameTimeHistory.push_back(state.currentFrameTime * 1000.0f);
        if (static_cast<int>(state.frameTimeHistory.size()) > AppState::MAX_HISTORY)
            state.frameTimeHistory.erase(state.frameTimeHistory.begin());

        if (!state.frameTimeHistory.empty()) {
            float sum = 0;
            for (float ft : state.frameTimeHistory) sum += ft;
            state.avgFrameTime = (sum / static_cast<float>(state.frameTimeHistory.size())) / 1000.0f;
        }
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

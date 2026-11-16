#include "Engine/UI/UIPanel.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Engine {

std::vector<UIQuad> UIPanel::GenerateQuads(const glm::vec2& resolvedPos, const glm::vec2& resolvedSize, float borderScale) const {
    std::vector<UIQuad> quads;
    if (resolvedSize.x <= 0.0f || resolvedSize.y <= 0.0f) {
        return quads;
    }

    if (renderMode == UIRenderMode::StretchQuad) {
        UIQuad q;
        q.Color = color;
        q.Vertices[0] = { resolvedPos, { 0.0f, 0.0f } };
        q.Vertices[1] = { { resolvedPos.x + resolvedSize.x, resolvedPos.y }, { 1.0f, 0.0f } };
        q.Vertices[2] = { resolvedPos + resolvedSize, { 1.0f, 1.0f } };
        q.Vertices[3] = { { resolvedPos.x, resolvedPos.y + resolvedSize.y }, { 0.0f, 1.0f } };
        quads.push_back(q);
        return quads;
    }

    if (renderMode == UIRenderMode::NineSlice) {
        const float texW = (texture && texture->GetWidth() > 0) ? static_cast<float>(texture->GetWidth()) : 512.0f;
        const float texH = (texture && texture->GetHeight() > 0) ? static_cast<float>(texture->GetHeight()) : 512.0f;

        float Ls = BorderLeft() * borderScale;
        float Rs = BorderRight() * borderScale;
        float Ts = BorderTop() * borderScale;
        float Bs = BorderBottom() * borderScale;

        if (Ls + Rs > resolvedSize.x && (Ls + Rs) > 0.0f) {
            const float s = resolvedSize.x / (Ls + Rs);
            Ls *= s;
            Rs *= s;
        }
        if (Bs + Ts > resolvedSize.y && (Bs + Ts) > 0.0f) {
            const float s = resolvedSize.y / (Bs + Ts);
            Bs *= s;
            Ts *= s;
        }

        const float x[4] = {
            resolvedPos.x,
            resolvedPos.x + Ls,
            resolvedPos.x + resolvedSize.x - Rs,
            resolvedPos.x + resolvedSize.x
        };
        const float y[4] = {
            resolvedPos.y,
            resolvedPos.y + Bs,
            resolvedPos.y + resolvedSize.y - Ts,
            resolvedPos.y + resolvedSize.y
        };

        const float u[4] = {
            0.0f,
            std::clamp(BorderLeft() / texW, 0.0f, 1.0f),
            std::clamp(1.0f - (BorderRight() / texW), 0.0f, 1.0f),
            1.0f
        };
        const float v[4] = {
            0.0f,
            std::clamp(BorderBottom() / texH, 0.0f, 1.0f),
            std::clamp(1.0f - (BorderTop() / texH), 0.0f, 1.0f),
            1.0f
        };

        quads.reserve(9);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                UIQuad q;
                q.Color = color;
                q.Vertices[0] = { { x[col],     y[row] },     { u[col],     v[row] } };
                q.Vertices[1] = { { x[col + 1], y[row] },     { u[col + 1], v[row] } };
                q.Vertices[2] = { { x[col + 1], y[row + 1] }, { u[col + 1], v[row + 1] } };
                q.Vertices[3] = { { x[col],     y[row + 1] }, { u[col],     v[row + 1] } };
                quads.push_back(q);
            }
        }
        return quads;
    }

    if (renderMode == UIRenderMode::HorizontalFill) {
        const float f = std::clamp(fillAmount, 0.0f, 1.0f);
        if (f <= 0.0f) {
            return quads;
        }

        const float effectiveW = resolvedSize.x * f;
        const float xStart = invertFill ? (resolvedPos.x + resolvedSize.x - effectiveW) : resolvedPos.x;
        const float xEnd   = invertFill ? (resolvedPos.x + resolvedSize.x) : (resolvedPos.x + effectiveW);
        const float uStart = invertFill ? (1.0f - f) : 0.0f;
        const float uEnd   = invertFill ? 1.0f : f;

        UIQuad q;
        q.Color = color;
        q.Vertices[0] = { { xStart, resolvedPos.y },                  { uStart, 0.0f } };
        q.Vertices[1] = { { xEnd,   resolvedPos.y },                  { uEnd,   0.0f } };
        q.Vertices[2] = { { xEnd,   resolvedPos.y + resolvedSize.y }, { uEnd,   1.0f } };
        q.Vertices[3] = { { xStart, resolvedPos.y + resolvedSize.y }, { uStart, 1.0f } };
        quads.push_back(q);
        return quads;
    }

    if (renderMode == UIRenderMode::VerticalFill) {
        const float f = std::clamp(fillAmount, 0.0f, 1.0f);
        if (f <= 0.0f) {
            return quads;
        }

        const float effectiveH = resolvedSize.y * f;
        const float yStart = invertFill ? (resolvedPos.y + resolvedSize.y - effectiveH) : resolvedPos.y;
        const float yEnd   = invertFill ? (resolvedPos.y + resolvedSize.y) : (resolvedPos.y + effectiveH);
        const float vStart = invertFill ? (1.0f - f) : 0.0f;
        const float vEnd   = invertFill ? 1.0f : f;

        UIQuad q;
        q.Color = color;
        q.Vertices[0] = { { resolvedPos.x,                  yStart }, { 0.0f, vStart } };
        q.Vertices[1] = { { resolvedPos.x + resolvedSize.x, yStart }, { 1.0f, vStart } };
        q.Vertices[2] = { { resolvedPos.x + resolvedSize.x, yEnd },   { 1.0f, vEnd } };
        q.Vertices[3] = { { resolvedPos.x,                  yEnd },   { 0.0f, vEnd } };
        quads.push_back(q);
        return quads;
    }

    if (renderMode == UIRenderMode::RadialSweepCW) {
        const float f = std::clamp(fillAmount, 0.0f, 1.0f);
        if (f <= 0.0f) {
            return quads;
        }

        const glm::vec2 h = 0.5f * resolvedSize;
        const glm::vec2 C = resolvedPos + h;

        auto toUV = [&](const glm::vec2& p) -> glm::vec2 {
            const float u = (resolvedSize.x > 0.0f) ? (p.x - resolvedPos.x) / resolvedSize.x : 0.0f;
            const float v = (resolvedSize.y > 0.0f) ? (p.y - resolvedPos.y) / resolvedSize.y : 0.0f;
            return { std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f) };
        };

        const glm::vec2 TC = { C.x,       C.y + h.y }; // Top-Center (12 o'clock)
        const glm::vec2 RC = { C.x + h.x, C.y       }; // Right-Center (3 o'clock)
        const glm::vec2 BC = { C.x,       C.y - h.y }; // Bottom-Center (6 o'clock)
        const glm::vec2 LC = { C.x - h.x, C.y       }; // Left-Center (9 o'clock)

        const glm::vec2 TR = { C.x + h.x, C.y + h.y }; // Top-Right corner
        const glm::vec2 BR = { C.x + h.x, C.y - h.y }; // Bottom-Right corner
        const glm::vec2 BL = { C.x - h.x, C.y - h.y }; // Bottom-Left corner
        const glm::vec2 TL = { C.x - h.x, C.y + h.y }; // Top-Left corner

        constexpr float kTwoPi = static_cast<float>(2.0 * std::numbers::pi);
        constexpr float kHalfPi = static_cast<float>(0.5 * std::numbers::pi);
        const float totalAngle = f * kTwoPi;

        struct QuadrantDef {
            glm::vec2 Start;
            glm::vec2 Corner;
            glm::vec2 End;
            glm::vec2 BL;
            glm::vec2 BR;
            glm::vec2 TR;
            glm::vec2 TL;
        };

        const QuadrantDef quadrants[4] = {
            { TC, TR, RC, C,  RC, TR, TC }, // Q0: Top-Right
            { RC, BR, BC, BC, BR, RC, C  }, // Q1: Bottom-Right
            { BC, BL, LC, BL, BC, C,  LC }, // Q2: Bottom-Left
            { LC, TL, TC, LC, C,  TC, TL }  // Q3: Top-Left
        };

        for (int qIdx = 0; qIdx < 4; ++qIdx) {
            const float qStartAngle = static_cast<float>(qIdx) * kHalfPi;
            const float qEndAngle   = qStartAngle + kHalfPi;

            if (totalAngle <= qStartAngle) {
                break;
            }

            const auto& qDef = quadrants[qIdx];

            if (totalAngle >= qEndAngle - 1e-5f) {
                // Fully covered quadrant - emit exact rectangular quad
                UIQuad q;
                q.Color = color;
                q.Vertices[0] = { qDef.BL, toUV(qDef.BL) };
                q.Vertices[1] = { qDef.BR, toUV(qDef.BR) };
                q.Vertices[2] = { qDef.TR, toUV(qDef.TR) };
                q.Vertices[3] = { qDef.TL, toUV(qDef.TL) };
                quads.push_back(q);
            } else {
                // Partially covered quadrant
                const float alpha = totalAngle - qStartAngle; // in [0 .. pi/2)
                const float alphaCorner = (qIdx % 2 == 0)
                    ? std::atan2(h.x, h.y)
                    : std::atan2(h.y, h.x);

                glm::vec2 P{ 0.0f };
                bool pastCorner = false;

                if (qIdx == 0) { // Top-Right: Start = TC, Corner = TR, End = RC
                    if (alpha <= alphaCorner) {
                        P = { C.x + h.y * std::tan(alpha), C.y + h.y };
                    } else {
                        pastCorner = true;
                        P = { C.x + h.x, C.y + h.x / std::tan(alpha) };
                    }
                } else if (qIdx == 1) { // Bottom-Right: Start = RC, Corner = BR, End = BC
                    if (alpha <= alphaCorner) {
                        P = { C.x + h.x, C.y - h.x * std::tan(alpha) };
                    } else {
                        pastCorner = true;
                        P = { C.x + h.y / std::tan(alpha), C.y - h.y };
                    }
                } else if (qIdx == 2) { // Bottom-Left: Start = BC, Corner = BL, End = LC
                    if (alpha <= alphaCorner) {
                        P = { C.x - h.y * std::tan(alpha), C.y - h.y };
                    } else {
                        pastCorner = true;
                        P = { C.x - h.x, C.y - h.x / std::tan(alpha) };
                    }
                } else { // Top-Left: Start = LC, Corner = TL, End = TC
                    if (alpha <= alphaCorner) {
                        P = { C.x - h.x, C.y + h.x * std::tan(alpha) };
                    } else {
                        pastCorner = true;
                        P = { C.x - h.y / std::tan(alpha), C.y + h.y };
                    }
                }

                UIQuad q;
                q.Color = color;
                if (!pastCorner) {
                    // Triangle (C, qDef.Start, P) represented as quad with duplicate vertex P
                    q.Vertices[0] = { C, toUV(C) };
                    q.Vertices[1] = { qDef.Start, toUV(qDef.Start) };
                    q.Vertices[2] = { P, toUV(P) };
                    q.Vertices[3] = { P, toUV(P) };
                } else {
                    // Convex polygon (C, qDef.Start, qDef.Corner, P)
                    q.Vertices[0] = { C, toUV(C) };
                    q.Vertices[1] = { qDef.Start, toUV(qDef.Start) };
                    q.Vertices[2] = { qDef.Corner, toUV(qDef.Corner) };
                    q.Vertices[3] = { P, toUV(P) };
                }
                quads.push_back(q);
                break;
            }
        }
        return quads;
    }

    return quads;
}

} // namespace Engine

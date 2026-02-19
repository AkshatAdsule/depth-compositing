/**
 * Test Image Generator for Deep Compositor Demo
 * 
 * Generates synthetic deep EXR files for testing:
 * 1. sphere_front.exr - Red sphere at depth Z=5-10, semi-transparent (alpha 0.7)
 * 2. sphere_back.exr - Blue sphere at depth Z=15-20, semi-transparent (alpha 0.7)
 * 3. ground_plane.exr - Green ground plane at depth Z=25, opaque (alpha 1.0)
 * 
 * All images: 512x512 resolution
 */

#include "deep_image.h"
#include "deep_writer.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <iostream>

using namespace deep_compositor;

// Image dimensions
constexpr int IMAGE_WIDTH = 512;
constexpr int IMAGE_HEIGHT = 512;

// Sphere parameters
struct SphereParams {
    float centerX;      // Center in normalized coords [0, 1]
    float centerY;      // Center in normalized coords [0, 1]
    float radius;       // Radius in normalized coords
    float depthNear;    // Near depth (entry point)
    float depthFar;     // Far depth (exit point)
    float red;          // Color R
    float green;        // Color G
    float blue;         // Color B
    float alpha;        // Opacity
};

/**
 * Ray-sphere intersection test
 * Returns true if ray hits sphere, with entry/exit depths
 */
bool raySphereIntersect(float rayX, float rayY,
                        const SphereParams& sphere,
                        float& depthEntry, float& depthExit) {
    // Calculate distance from ray to sphere center (in XY plane)
    float dx = rayX - sphere.centerX;
    float dy = rayY - sphere.centerY;
    float distSq = dx * dx + dy * dy;
    float radiusSq = sphere.radius * sphere.radius;
    
    if (distSq > radiusSq) {
        // Ray misses sphere
        return false;
    }
    
    // Calculate depth offset based on distance from center
    // Uses sphere equation: z = sqrt(r^2 - (x-cx)^2 - (y-cy)^2)
    float depthOffset = std::sqrt(radiusSq - distSq);
    
    // Interpolate depths
    float depthCenter = (sphere.depthNear + sphere.depthFar) / 2.0f;
    float depthRange = (sphere.depthFar - sphere.depthNear) / 2.0f;
    
    // Scale depth offset to the sphere's depth range
    float normalizedOffset = depthOffset / sphere.radius;
    
    depthEntry = depthCenter - normalizedOffset * depthRange;
    depthExit = depthCenter + normalizedOffset * depthRange;
    
    return true;
}

/**
 * Generate a deep image containing a sphere
 */
DeepImage generateSphere(const SphereParams& sphere) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);
    
    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            // Normalize coordinates to [0, 1]
            float normX = (static_cast<float>(x) + 0.5f) / IMAGE_WIDTH;
            float normY = (static_cast<float>(y) + 0.5f) / IMAGE_HEIGHT;
            
            float depthEntry, depthExit;
            if (raySphereIntersect(normX, normY, sphere, depthEntry, depthExit)) {
                DeepPixel& pixel = img.pixel(x, y);
                
                // For semi-transparent spheres, create entry and exit samples
                if (sphere.alpha < 0.99f) {
                    // Entry sample (front surface)
                    DeepSample entry;
                    entry.depth = depthEntry;
                    // Premultiply colors
                    entry.red = sphere.red * sphere.alpha * 0.5f;
                    entry.green = sphere.green * sphere.alpha * 0.5f;
                    entry.blue = sphere.blue * sphere.alpha * 0.5f;
                    entry.alpha = sphere.alpha * 0.5f;
                    pixel.addSample(entry);
                    
                    // Exit sample (back surface)
                    DeepSample exit;
                    exit.depth = depthExit;
                    exit.red = sphere.red * sphere.alpha * 0.5f;
                    exit.green = sphere.green * sphere.alpha * 0.5f;
                    exit.blue = sphere.blue * sphere.alpha * 0.5f;
                    exit.alpha = sphere.alpha * 0.5f;
                    pixel.addSample(exit);
                } else {
                    // Opaque sphere: single sample at entry point
                    DeepSample sample;
                    sample.depth = depthEntry;
                    sample.red = sphere.red;
                    sample.green = sphere.green;
                    sample.blue = sphere.blue;
                    sample.alpha = 1.0f;
                    pixel.addSample(sample);
                }
            }
        }
    }
    
    return img;
}

/**
 * Generate a deep image containing a volumetric sphere
 * (single sample per hit pixel spanning [depthEntry, depthExit])
 */
DeepImage generateVolumetricSphere(const SphereParams& sphere) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            float normX = (static_cast<float>(x) + 0.5f) / IMAGE_WIDTH;
            float normY = (static_cast<float>(y) + 0.5f) / IMAGE_HEIGHT;

            float depthEntry, depthExit;
            if (raySphereIntersect(normX, normY, sphere, depthEntry, depthExit)) {
                // Single volumetric sample spanning the full depth range
                DeepSample sample;
                sample.depth      = depthEntry;
                sample.depth_back = depthExit;
                sample.red   = sphere.red * sphere.alpha;
                sample.green = sphere.green * sphere.alpha;
                sample.blue  = sphere.blue * sphere.alpha;
                sample.alpha = sphere.alpha;
                img.pixel(x, y).addSample(sample);
            }
        }
    }

    return img;
}

/**
 * Generate a deep image containing a fog slab (volumetric)
 * covering a circular region at the given depth range.
 */
DeepImage generateFogSlab(float centerX, float centerY, float radius,
                          float depthFront, float depthBack,
                          float r, float g, float b, float alpha) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            float normX = (static_cast<float>(x) + 0.5f) / IMAGE_WIDTH;
            float normY = (static_cast<float>(y) + 0.5f) / IMAGE_HEIGHT;

            float dx = normX - centerX;
            float dy = normY - centerY;
            if (dx * dx + dy * dy <= radius * radius) {
                DeepSample sample;
                sample.depth      = depthFront;
                sample.depth_back = depthBack;
                sample.red   = r * alpha;
                sample.green = g * alpha;
                sample.blue  = b * alpha;
                sample.alpha = alpha;
                img.pixel(x, y).addSample(sample);
            }
        }
    }

    return img;
}

/**
 * Generate a deep image containing a ground plane
 */
DeepImage generateGroundPlane(float depth, float r, float g, float b, float alpha) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            DeepPixel& pixel = img.pixel(x, y);

            // Point sample: depth_back = depth
            DeepSample sample(depth, r * alpha, g * alpha, b * alpha, alpha);
            pixel.addSample(sample);
        }
    }

    return img;
}

/**
 * Generate a deep image containing an opaque wall (point sample)
 * covering a circular region at a single depth.
 */
DeepImage generateWall(float centerX, float centerY, float radius,
                       float depth, float r, float g, float b) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            float normX = (static_cast<float>(x) + 0.5f) / IMAGE_WIDTH;
            float normY = (static_cast<float>(y) + 0.5f) / IMAGE_HEIGHT;

            float dx = normX - centerX;
            float dy = normY - centerY;
            if (dx * dx + dy * dy <= radius * radius) {
                // Opaque point sample
                DeepSample sample(depth, r, g, b, 1.0f);
                img.pixel(x, y).addSample(sample);
            }
        }
    }

    return img;
}

/**
 * Generate full-frame layered volumetric fog with uniform XY density and color.
 * Extinction changes only with traveled depth through the fog volume.
 */
DeepImage generateUniformLayeredFog(float depthFront, float depthBack,
                                    int sliceCount,
                                    float baseR, float baseG, float baseB,
                                    float sliceAlphaNear, float sliceAlphaFar) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);
    if (sliceCount < 1) {
        return img;
    }

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            DeepPixel& pixel = img.pixel(x, y);
            for (int i = 0; i < sliceCount; ++i) {
                float t0 = static_cast<float>(i) / sliceCount;
                float t1 = static_cast<float>(i + 1) / sliceCount;
                float tc = 0.5f * (t0 + t1);

                float z0 = depthFront + t0 * (depthBack - depthFront);
                float z1 = depthFront + t1 * (depthBack - depthFront);

                // Increase per-slice density with depth to amplify distance fade.
                float sliceAlpha = sliceAlphaNear + (sliceAlphaFar - sliceAlphaNear) * tc;
                sliceAlpha = std::clamp(sliceAlpha, 0.0f, 0.95f);

                DeepSample sample;
                sample.depth = z0;
                sample.depth_back = z1;
                sample.red = baseR * sliceAlpha;
                sample.green = baseG * sliceAlpha;
                sample.blue = baseB * sliceAlpha;
                sample.alpha = sliceAlpha;
                pixel.addSample(sample);
            }
        }
    }

    return img;
}

/**
 * Generate a stylized 3-face rod (side + top + front cap) with depth ramped along length.
 */
DeepImage generateSlantedRectangle(float startX, float startY,
                                   float endX, float endY,
                                   float widthAtStart, float widthAtEnd,
                                   float depthNearAtStart, float depthFarAtEnd,
                                   float r, float g, float b, float alpha) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);

    float dx = endX - startX;
    float dy = endY - startY;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-6f) {
        return img;
    }

    float dirX = dx / length;
    float dirY = dy / length;
    float perpX = -dirY;
    float perpY = dirX;
    float halfLength = 0.5f * length;
    float midX = 0.5f * (startX + endX);
    float midY = 0.5f * (startY + endY);
    float capLength = 0.16f * widthAtStart;      // visible front face depth in screen space
    float topThicknessScale = 0.28f;             // top face thickness as fraction of local width

    auto shade = [](float v, float m) {
        return std::clamp(v * m, 0.0f, 1.0f);
    };

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            float normX = (static_cast<float>(x) + 0.5f) / IMAGE_WIDTH;
            float normY = (static_cast<float>(y) + 0.5f) / IMAGE_HEIGHT;

            float relX = normX - midX;
            float relY = normY - midY;

            float along = relX * dirX + relY * dirY;
            float across = relX * perpX + relY * perpY;
            float t = std::clamp((along + halfLength) / length, 0.0f, 1.0f);
            float localWidth = widthAtStart + t * (widthAtEnd - widthAtStart);
            float localHalfWidth = 0.5f * localWidth;
            float topThickness = topThicknessScale * localWidth;

            bool inSideAlong = std::abs(along) <= halfLength;
            bool inSideAcross = across >= -localHalfWidth && across <= localHalfWidth;
            bool inTopAcross = across >= -(localHalfWidth + topThickness) && across < -localHalfWidth;
            bool inFrontCap = along >= -(halfLength + capLength) && along < -halfLength &&
                              across >= -(0.5f * widthAtStart + topThicknessScale * widthAtStart) &&
                              across <=  (0.5f * widthAtStart);

            if (inFrontCap) {
                // Front face: nearest cap of the rod.
                float frontDepth = depthNearAtStart - 0.8f;
                DeepSample sample(frontDepth,
                                  shade(r, 0.72f) * alpha,
                                  shade(g, 0.72f) * alpha,
                                  shade(b, 0.72f) * alpha,
                                  alpha);
                img.pixel(x, y).addSample(sample);
            } else if (inSideAlong && inTopAcross) {
                // Top face: slightly brighter and slightly nearer than side.
                float depth = depthNearAtStart + t * (depthFarAtEnd - depthNearAtStart) - 0.45f;
                DeepSample sample(depth,
                                  shade(r, 1.18f) * alpha,
                                  shade(g, 1.18f) * alpha,
                                  shade(b, 1.18f) * alpha,
                                  alpha);
                img.pixel(x, y).addSample(sample);
            } else if (inSideAlong && inSideAcross) {
                // Side face: main visible area.
                float depth = depthNearAtStart + t * (depthFarAtEnd - depthNearAtStart);
                DeepSample sample(depth, r * alpha, g * alpha, b * alpha, alpha);
                img.pixel(x, y).addSample(sample);
            }
        }
    }

    return img;
}

/**
 * Generate a semi-transparent rectangular pane with bilinearly-interpolated depth.
 * Each pixel in the rectangle gets a point sample at the interpolated depth.
 * This simulates a flat pane tilted in depth space.
 */
DeepImage generateTiltedPane(float left, float top, float right, float bottom,
                             float depthTL, float depthTR,
                             float depthBL, float depthBR,
                             float r, float g, float b, float alpha) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            float normX = (static_cast<float>(x) + 0.5f) / IMAGE_WIDTH;
            float normY = (static_cast<float>(y) + 0.5f) / IMAGE_HEIGHT;

            if (normX < left || normX > right || normY < top || normY > bottom)
                continue;

            float tx = (normX - left) / (right - left);
            float ty = (normY - top) / (bottom - top);

            float depthTop = depthTL + tx * (depthTR - depthTL);
            float depthBot = depthBL + tx * (depthBR - depthBL);
            float depth = depthTop + ty * (depthBot - depthTop);

            DeepSample sample(depth, r * alpha, g * alpha, b * alpha, alpha);
            img.pixel(x, y).addSample(sample);
        }
    }

    return img;
}

/**
 * Generate a cone-shaped volumetric beam.
 * The cone has a narrow apex and expands to a wider base.
 * Each pixel in the cone's 2D footprint gets a volumetric sample.
 * Alpha fades quadratically toward the edges for a soft-light effect.
 */
DeepImage generateVolumetricCone(float apexX, float apexY, float apexDepth,
                                 float baseX, float baseY, float baseDepth,
                                 float apexRadius, float baseRadius,
                                 float r, float g, float b, float alpha) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);

    float axisX = baseX - apexX;
    float axisY = baseY - apexY;
    float axisLen = std::sqrt(axisX * axisX + axisY * axisY);
    if (axisLen < 1e-6f) return img;

    float dirX = axisX / axisLen;
    float dirY = axisY / axisLen;
    float depthRange = baseDepth - apexDepth;

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            float normX = (static_cast<float>(x) + 0.5f) / IMAGE_WIDTH;
            float normY = (static_cast<float>(y) + 0.5f) / IMAGE_HEIGHT;

            float relX = normX - apexX;
            float relY = normY - apexY;

            // Project onto cone axis: t in [0,1] from apex to base
            float t = (relX * dirX + relY * dirY) / axisLen;
            if (t < 0.0f || t > 1.0f) continue;

            // Perpendicular distance from axis
            float projX = apexX + t * axisX;
            float projY = apexY + t * axisY;
            float perpDist = std::sqrt((normX - projX) * (normX - projX) +
                                       (normY - projY) * (normY - projY));

            // Cone radius at this t
            float coneRadius = apexRadius + t * (baseRadius - apexRadius);
            if (perpDist > coneRadius) continue;

            // Depth at cone center varies linearly along axis
            float centerDepth = apexDepth + t * depthRange;

            // Volumetric thickness from circular cross-section (chord)
            float normalizedPerp = perpDist / std::max(coneRadius, 1e-6f);
            float halfChord = std::sqrt(std::max(0.0f, 1.0f - normalizedPerp * normalizedPerp));

            // Scale thickness: beam gets physically thicker toward the base
            float beamThickness = coneRadius * (std::abs(depthRange) / axisLen);
            float depthEntry = centerDepth - halfChord * beamThickness;
            float depthExit  = centerDepth + halfChord * beamThickness;

            if (depthExit <= depthEntry) depthExit = depthEntry + 0.01f;

            // Quadratic edge fade for soft-light look
            float edgeFade = 1.0f - normalizedPerp * normalizedPerp;
            float localAlpha = alpha * edgeFade;

            DeepSample sample;
            sample.depth      = depthEntry;
            sample.depth_back = depthExit;
            sample.red   = r * localAlpha;
            sample.green = g * localAlpha;
            sample.blue  = b * localAlpha;
            sample.alpha = localAlpha;
            img.pixel(x, y).addSample(sample);
        }
    }

    return img;
}

/**
 * Generate an opaque torus ring whose depth varies sinusoidally around
 * the circumference. Two rings with different phase angles will interlock:
 * one passes in front then behind the other at different angular positions.
 */
DeepImage generateTorusRing(float centerX, float centerY, float centerDepth,
                            float majorRadius, float minorRadius,
                            float depthAmplitude, float phaseAngle,
                            float r, float g, float b) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);

    float innerR = majorRadius - minorRadius;
    float outerR = majorRadius + minorRadius;

    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            float normX = (static_cast<float>(x) + 0.5f) / IMAGE_WIDTH;
            float normY = (static_cast<float>(y) + 0.5f) / IMAGE_HEIGHT;

            float dx = normX - centerX;
            float dy = normY - centerY;
            float dist = std::sqrt(dx * dx + dy * dy);

            // Check annular footprint
            if (dist < innerR || dist > outerR) continue;

            // Tube cross-section: distance from the major circle
            float tubeDist = dist - majorRadius;
            if (std::abs(tubeDist) > minorRadius) continue;

            // Front surface offset from tube center
            float halfChord = std::sqrt(minorRadius * minorRadius - tubeDist * tubeDist);

            // Angle around the ring center
            float angle = std::atan2(dy, dx);

            // Depth varies sinusoidally — simulates a tilted ring
            float depthOffset = depthAmplitude * std::sin(angle - phaseAngle);

            // Tube's cross-sectional depth contribution (scaled to depth units)
            float tubeDepthScale = depthAmplitude / majorRadius;
            float frontDepth = centerDepth + depthOffset - halfChord * tubeDepthScale;

            DeepSample sample(frontDepth, r, g, b, 1.0f);
            img.pixel(x, y).addSample(sample);
        }
    }

    return img;
}

/**
 * Generate all demo-specific images for the showcase.
 */
void generateDemo(const std::string& outputDir, bool outputFlat) {
    log("=== Generating Demo Scene Images ===");

    // ---- Scene 1: Nebula -- three overlapping volumetric fog spheres ----
    {
        log("\n[Nebula] Red-orange volumetric sphere...");
        SphereParams s{};
        s.centerX = 0.33f;  s.centerY = 0.45f;  s.radius = 0.28f;
        s.depthNear = 4.0f; s.depthFar = 20.0f;
        s.red = 1.0f; s.green = 0.2f; s.blue = 0.05f; s.alpha = 0.6f;
        DeepImage img = generateVolumetricSphere(s);
        writeDeepEXR(img, outputDir + "/nebula_red.exr");
        log("  -> " + outputDir + "/nebula_red.exr  (depth 4-20, alpha 0.6)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/nebula_red.flat.exr");
    }
    {
        log("[Nebula] Green volumetric sphere...");
        SphereParams s{};
        s.centerX = 0.67f;  s.centerY = 0.45f;  s.radius = 0.28f;
        s.depthNear = 8.0f; s.depthFar = 24.0f;
        s.red = 0.15f; s.green = 1.0f; s.blue = 0.2f; s.alpha = 0.55f;
        DeepImage img = generateVolumetricSphere(s);
        writeDeepEXR(img, outputDir + "/nebula_green.exr");
        log("  -> " + outputDir + "/nebula_green.exr  (depth 8-24, alpha 0.55)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/nebula_green.flat.exr");
    }
    {
        log("[Nebula] Blue-violet volumetric sphere...");
        SphereParams s{};
        s.centerX = 0.50f;  s.centerY = 0.72f;  s.radius = 0.25f;
        s.depthNear = 2.0f; s.depthFar = 16.0f;
        s.red = 0.15f; s.green = 0.15f; s.blue = 1.0f; s.alpha = 0.5f;
        DeepImage img = generateVolumetricSphere(s);
        writeDeepEXR(img, outputDir + "/nebula_blue.exr");
        log("  -> " + outputDir + "/nebula_blue.exr  (depth 2-16, alpha 0.5)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/nebula_blue.flat.exr");
    }

    // ---- Scene 2: Crystal -- opaque sphere inside volumetric fog ----
    {
        log("\n[Crystal] Purple volumetric fog sphere...");
        SphereParams s{};
        s.centerX = 0.5f;  s.centerY = 0.5f;  s.radius = 0.40f;
        s.depthNear = 3.0f; s.depthFar = 25.0f;
        s.red = 0.6f; s.green = 0.1f; s.blue = 0.8f; s.alpha = 0.7f;
        DeepImage img = generateVolumetricSphere(s);
        writeDeepEXR(img, outputDir + "/purple_fog.exr");
        log("  -> " + outputDir + "/purple_fog.exr  (depth 3-25, alpha 0.7)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/purple_fog.flat.exr");
    }
    {
        log("[Crystal] Gold opaque sphere...");
        SphereParams s{};
        s.centerX = 0.5f;  s.centerY = 0.5f;  s.radius = 0.15f;
        s.depthNear = 10.0f; s.depthFar = 16.0f;
        s.red = 1.0f; s.green = 0.85f; s.blue = 0.2f; s.alpha = 1.0f;
        DeepImage img = generateSphere(s);
        writeDeepEXR(img, outputDir + "/gold_sphere.exr");
        log("  -> " + outputDir + "/gold_sphere.exr  (depth 10-16, opaque)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/gold_sphere.flat.exr");
    }

    // ---- Shared dark backdrop ----
    {
        log("\n[Shared] Dark backdrop...");
        DeepImage img = generateGroundPlane(30.0f, 0.03f, 0.03f, 0.08f, 1.0f);
        writeDeepEXR(img, outputDir + "/backdrop.exr");
        log("  -> " + outputDir + "/backdrop.exr  (depth 30, near-black)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/backdrop.flat.exr");
    }

    // ---- Scene 3: Fog Slice -- uniform fog + diagonal bar with depth ramp ----
    {
        log("\n[Fog Slice] Uniform layered fog field...");
        DeepImage img = generateUniformLayeredFog(
            4.0f, 30.0f,           // front/back depth
            40,                    // many slices for strong depth extinction
            0.10f, 0.14f, 0.22f,   // dark cool fog tint (uniform over image)
            0.060f, 0.085f         // heavy per-slice alpha (near -> far)
        );
        writeDeepEXR(img, outputDir + "/fog_steep_gradient.exr");
        log("  -> " + outputDir + "/fog_steep_gradient.exr  (uniform fog, depth 4-30, strong z extinction)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/fog_steep_gradient.flat.exr");
    }
    {
        log("[Fog Slice] 3-face diagonal rod (side + top + front cap)...");
        DeepImage img = generateSlantedRectangle(
            0.03f, 0.80f,          // start (left, near)
            1.10f, 0.16f,          // end (right, far) extends beyond frame
            0.18f, 0.07f,          // tapered width: near wider -> far narrower
            5.0f, 26.0f,           // near depth -> far depth
            1.00f, 0.64f, 0.20f,   // warm color for contrast against cool fog
            0.82f                  // readable near, but fades strongly with distance
        );
        writeDeepEXR(img, outputDir + "/diagonal_slice.exr");
        log("  -> " + outputDir + "/diagonal_slice.exr  (depth 5->26 along diagonal)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/diagonal_slice.flat.exr");
    }

    // ---- Scene 5: Stained Glass -- three depth-tilted transparent panes ----
    {
        log("\n[Stained Glass] Red tilted pane (near-left to far-right)...");
        DeepImage img = generateTiltedPane(
            0.05f, 0.1f, 0.95f, 0.9f,
            5.0f, 25.0f, 5.0f, 25.0f,
            1.0f, 0.15f, 0.1f, 0.45f);
        writeDeepEXR(img, outputDir + "/stained_red.exr");
        log("  -> " + outputDir + "/stained_red.exr  (depth 5->25 left-to-right)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/stained_red.flat.exr");
    }
    {
        log("[Stained Glass] Green tilted pane (far-left to near-right)...");
        DeepImage img = generateTiltedPane(
            0.05f, 0.1f, 0.95f, 0.9f,
            25.0f, 5.0f, 25.0f, 5.0f,
            0.1f, 1.0f, 0.15f, 0.45f);
        writeDeepEXR(img, outputDir + "/stained_green.exr");
        log("  -> " + outputDir + "/stained_green.exr  (depth 25->5 left-to-right)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/stained_green.flat.exr");
    }
    {
        log("[Stained Glass] Blue tilted pane (near-top to far-bottom)...");
        DeepImage img = generateTiltedPane(
            0.05f, 0.1f, 0.95f, 0.9f,
            5.0f, 5.0f, 25.0f, 25.0f,
            0.1f, 0.15f, 1.0f, 0.45f);
        writeDeepEXR(img, outputDir + "/stained_blue.exr");
        log("  -> " + outputDir + "/stained_blue.exr  (depth 5->25 top-to-bottom)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/stained_blue.flat.exr");
    }

    // ---- Scene 6: Lighthouse -- volumetric beam through fog ----
    {
        log("\n[Lighthouse] Blue-gray fog bank...");
        SphereParams s{};
        s.centerX = 0.5f;  s.centerY = 0.5f;  s.radius = 0.48f;
        s.depthNear = 3.0f; s.depthFar = 28.0f;
        s.red = 0.20f; s.green = 0.25f; s.blue = 0.35f; s.alpha = 0.65f;
        DeepImage img = generateVolumetricSphere(s);
        writeDeepEXR(img, outputDir + "/lighthouse_fog.exr");
        log("  -> " + outputDir + "/lighthouse_fog.exr  (depth 3-28, alpha 0.65)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/lighthouse_fog.flat.exr");
    }
    {
        log("[Lighthouse] Bright cone beam...");
        DeepImage img = generateVolumetricCone(
            0.15f, 0.20f, 5.0f,
            0.85f, 0.75f, 22.0f,
            0.02f, 0.20f,
            1.0f, 0.95f, 0.7f, 0.50f);
        writeDeepEXR(img, outputDir + "/lighthouse_beam.exr");
        log("  -> " + outputDir + "/lighthouse_beam.exr  (cone depth 5->22)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/lighthouse_beam.flat.exr");
    }

    // ---- Scene 7: Rings -- interlocking chain links ----
    {
        log("\n[Rings] Gold ring...");
        DeepImage img = generateTorusRing(
            0.28f, 0.5f, 15.0f,
            0.18f, 0.028f,
            6.0f, 0.0f,
            1.0f, 0.85f, 0.2f);
        writeDeepEXR(img, outputDir + "/ring_gold.exr");
        log("  -> " + outputDir + "/ring_gold.exr  (depth ~9-21, phase 0)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/ring_gold.flat.exr");
    }
    {
        log("[Rings] Silver ring...");
        DeepImage img = generateTorusRing(
            0.50f, 0.5f, 15.0f,
            0.18f, 0.04f,
            6.0f, 1.5708f,
            0.85f, 0.85f, 0.9f);
        writeDeepEXR(img, outputDir + "/ring_silver.exr");
        log("  -> " + outputDir + "/ring_silver.exr  (depth ~9-21, phase pi/2)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/ring_silver.flat.exr");
    }
    {
        log("[Rings] Copper ring...");
        DeepImage img = generateTorusRing(
            0.73f, 0.5f, 15.0f,
            0.18f, 0.028f,
            6.0f, 3.14159f,
            0.85f, 0.5f, 0.25f);
        writeDeepEXR(img, outputDir + "/ring_copper.exr");
        log("  -> " + outputDir + "/ring_copper.exr  (depth ~9-21, phase pi)");
        if (outputFlat) writeFlatEXR(img, outputDir + "/ring_copper.flat.exr");
    }
}

void printUsage(const char* programName) {
    std::cout << "Test Image Generator for Deep Compositor\n\n"
              << "Usage: " << programName << " [options]\n\n"
              << "Options:\n"
              << "  --output DIR    Output directory (default: test_data)\n"
              << "  --demo          Generate demo showcase images only\n"
              << "  --flat          Also output flattened EXR files for visualization\n"
              << "  --verbose, -v   Verbose output\n"
              << "  --help, -h      Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string outputDir = "test_data";
    bool verbose = false;
    bool outputFlat = false;
    bool demoMode = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--flat") {
            outputFlat = true;
        } else if (arg == "--demo") {
            demoMode = true;
        } else if (arg == "--output" && i + 1 < argc) {
            outputDir = argv[++i];
        }
    }

    setVerbose(verbose);

    Timer timer;

    if (demoMode) {
        log("Output directory: " + outputDir);
        generateDemo(outputDir, outputFlat);
        log("\nDemo images generated in " + timer.elapsedString());
        return 0;
    }

    log("Generating test deep EXR images...");
    log("Output directory: " + outputDir);
    
    // ========================================================================
    // Generate sphere_front.exr - Red sphere, semi-transparent
    // ========================================================================
    {
        log("\nGenerating sphere_front.exr...");
        
        SphereParams sphere;
        sphere.centerX = 0.4f;   // Slightly left of center
        sphere.centerY = 0.45f;  // Slightly above center
        sphere.radius = 0.25f;
        sphere.depthNear = 5.0f;
        sphere.depthFar = 10.0f;
        sphere.red = 1.0f;
        sphere.green = 0.2f;
        sphere.blue = 0.2f;
        sphere.alpha = 0.7f;
        
        DeepImage img = generateSphere(sphere);

        std::string path = outputDir + "/sphere_front.exr";
        writeDeepEXR(img, path);

        log("  Created: " + path);
        log("  Resolution: " + std::to_string(IMAGE_WIDTH) + "x" + std::to_string(IMAGE_HEIGHT));
        log("  Samples: " + formatNumber(img.totalSampleCount()));
        log("  Depth range: 5.0 - 10.0");

        if (outputFlat) {
            std::string flatPath = outputDir + "/sphere_front.flat.exr";
            writeFlatEXR(img, flatPath);
            log("  Created flat: " + flatPath);
        }
    }
    
    // ========================================================================
    // Generate sphere_back.exr - Blue sphere, semi-transparent
    // ========================================================================
    {
        log("\nGenerating sphere_back.exr...");
        
        SphereParams sphere;
        sphere.centerX = 0.6f;   // Slightly right of center
        sphere.centerY = 0.55f;  // Slightly below center
        sphere.radius = 0.25f;
        sphere.depthNear = 15.0f;
        sphere.depthFar = 20.0f;
        sphere.red = 0.2f;
        sphere.green = 0.2f;
        sphere.blue = 1.0f;
        sphere.alpha = 0.7f;
        
        DeepImage img = generateSphere(sphere);

        std::string path = outputDir + "/sphere_back.exr";
        writeDeepEXR(img, path);

        log("  Created: " + path);
        log("  Resolution: " + std::to_string(IMAGE_WIDTH) + "x" + std::to_string(IMAGE_HEIGHT));
        log("  Samples: " + formatNumber(img.totalSampleCount()));
        log("  Depth range: 15.0 - 20.0");

        if (outputFlat) {
            std::string flatPath = outputDir + "/sphere_back.flat.exr";
            writeFlatEXR(img, flatPath);
            log("  Created flat: " + flatPath);
        }
    }
    
    // ========================================================================
    // Generate ground_plane.exr - Green ground, opaque
    // ========================================================================
    {
        log("\nGenerating ground_plane.exr...");
        
        DeepImage img = generateGroundPlane(
            25.0f,   // depth
            0.2f,    // red
            0.6f,    // green
            0.2f,    // blue
            1.0f     // alpha (opaque)
        );

        std::string path = outputDir + "/ground_plane.exr";
        writeDeepEXR(img, path);

        log("  Created: " + path);
        log("  Resolution: " + std::to_string(IMAGE_WIDTH) + "x" + std::to_string(IMAGE_HEIGHT));
        log("  Samples: " + formatNumber(img.totalSampleCount()));
        log("  Depth: 25.0");

        if (outputFlat) {
            std::string flatPath = outputDir + "/ground_plane.flat.exr";
            writeFlatEXR(img, flatPath);
            log("  Created flat: " + flatPath);
        }
    }
    
    // ========================================================================
    // Generate overlapping fog volumes (red fog + blue fog)
    // ========================================================================
    {
        log("\nGenerating fog_red.exr (volumetric)...");

        // Red fog: depth 5-15, alpha 0.5, circular region
        DeepImage img = generateFogSlab(
            0.4f, 0.5f, 0.3f,   // center X, Y, radius
            5.0f, 15.0f,         // depth front, back
            1.0f, 0.1f, 0.1f,   // color R, G, B
            0.5f                 // alpha
        );

        std::string path = outputDir + "/fog_red.exr";
        writeDeepEXR(img, path);
        log("  Created: " + path);
        log("  Volumetric depth range: 5.0 - 15.0");

        if (outputFlat) {
            writeFlatEXR(img, outputDir + "/fog_red.flat.exr");
        }
    }

    {
        log("\nGenerating fog_blue.exr (volumetric)...");

        // Blue fog: depth 10-20, alpha 0.5, overlapping circular region
        DeepImage img = generateFogSlab(
            0.6f, 0.5f, 0.3f,   // center X, Y, radius
            10.0f, 20.0f,        // depth front, back
            0.1f, 0.1f, 1.0f,   // color R, G, B
            0.5f                 // alpha
        );

        std::string path = outputDir + "/fog_blue.exr";
        writeDeepEXR(img, path);
        log("  Created: " + path);
        log("  Volumetric depth range: 10.0 - 20.0");

        if (outputFlat) {
            writeFlatEXR(img, outputDir + "/fog_blue.flat.exr");
        }
    }

    // ========================================================================
    // Generate volumetric sphere
    // ========================================================================
    {
        log("\nGenerating sphere_volume.exr (volumetric)...");

        SphereParams sphere;
        sphere.centerX = 0.5f;
        sphere.centerY = 0.5f;
        sphere.radius = 0.25f;
        sphere.depthNear = 8.0f;
        sphere.depthFar = 16.0f;
        sphere.red = 0.8f;
        sphere.green = 0.4f;
        sphere.blue = 0.1f;
        sphere.alpha = 0.6f;

        DeepImage img = generateVolumetricSphere(sphere);

        std::string path = outputDir + "/sphere_volume.exr";
        writeDeepEXR(img, path);
        log("  Created: " + path);
        log("  Volumetric depth range: 8.0 - 16.0");

        if (outputFlat) {
            writeFlatEXR(img, outputDir + "/sphere_volume.flat.exr");
        }
    }

    // ========================================================================
    // Generate point-inside-volume test (wall inside fog)
    // ========================================================================
    {
        log("\nGenerating wall_in_fog.exr (opaque wall at depth 10)...");

        DeepImage img = generateWall(
            0.5f, 0.5f, 0.3f,   // center X, Y, radius
            10.0f,               // wall depth
            0.9f, 0.9f, 0.1f    // color (yellow)
        );

        std::string path = outputDir + "/wall_in_fog.exr";
        writeDeepEXR(img, path);
        log("  Created: " + path);
        log("  Point depth: 10.0");

        if (outputFlat) {
            writeFlatEXR(img, outputDir + "/wall_in_fog.flat.exr");
        }
    }

    {
        log("\nGenerating fog_around_wall.exr (fog slab depth 5-15)...");

        DeepImage img = generateFogSlab(
            0.5f, 0.5f, 0.3f,
            5.0f, 15.0f,
            0.2f, 0.8f, 0.2f,   // green fog
            0.6f
        );

        std::string path = outputDir + "/fog_around_wall.exr";
        writeDeepEXR(img, path);
        log("  Created: " + path);
        log("  Volumetric depth range: 5.0 - 15.0");

        if (outputFlat) {
            writeFlatEXR(img, outputDir + "/fog_around_wall.flat.exr");
        }
    }

    log("\nDone! Generated 8 test images in " + timer.elapsedString());

    return 0;
}

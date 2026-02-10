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

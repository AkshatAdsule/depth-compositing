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
 * Generate a deep image containing a ground plane
 */
DeepImage generateGroundPlane(float depth, float r, float g, float b, float alpha) {
    DeepImage img(IMAGE_WIDTH, IMAGE_HEIGHT);
    
    for (int y = 0; y < IMAGE_HEIGHT; ++y) {
        for (int x = 0; x < IMAGE_WIDTH; ++x) {
            DeepPixel& pixel = img.pixel(x, y);
            
            DeepSample sample;
            sample.depth = depth;
            // Premultiply colors
            sample.red = r * alpha;
            sample.green = g * alpha;
            sample.blue = b * alpha;
            sample.alpha = alpha;
            
            pixel.addSample(sample);
        }
    }
    
    return img;
}

void printUsage(const char* programName) {
    std::cout << "Test Image Generator for Deep Compositor\n\n"
              << "Usage: " << programName << " [options]\n\n"
              << "Options:\n"
              << "  --output DIR    Output directory (default: test_data)\n"
              << "  --flat          Also output flattened EXR files for visualization\n"
              << "  --verbose, -v   Verbose output\n"
              << "  --help, -h      Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string outputDir = "test_data";
    bool verbose = false;
    bool outputFlat = false;

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
        } else if (arg == "--output" && i + 1 < argc) {
            outputDir = argv[++i];
        }
    }
    
    setVerbose(verbose);
    
    log("Generating test deep EXR images...");
    log("Output directory: " + outputDir);
    
    Timer timer;
    
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
    
    log("\nDone! Generated 3 test images in " + timer.elapsedString());
    
    return 0;
}

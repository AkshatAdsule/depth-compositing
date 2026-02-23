#include "deep_writer.h"
#include <iostream>
#include <string>
#include <vector>



/**
 * Generates two 16x16 deep EXR files:
 * 1. z_ascending.exr  : Z depth increases as Y increases (0.0 at top, 15.0 at bottom)
 * 2. z_descending.exr : Z depth decreases as Y increases (15.0 at top, 0.0 at bottom)
 */
void generateDepthSortingTests() {
    const int width = 16;
    const int height = 16;

    deep_compositor::DeepImage imgAsc(width, height);
    deep_compositor::DeepImage imgDesc(width, height);

    for (int y = 0; y < height; ++y) {
        // Z values for the two different tests
        float zAscending  = static_cast<float>(y);
        float zDescending = 15.0f - static_cast<float>(y);

        for (int x = 0; x < width; ++x) {
            // Setup Ascending Sample
            deep_compositor::DeepSample sAsc;
            sAsc.red = 0.0f; sAsc.green = 1.0f; sAsc.blue = 0.0f; sAsc.alpha = 1.0f;
            sAsc.depth = zAscending;
            sAsc.depth_back = zAscending + 0.1f;
            imgAsc.pixel(x, y).addSample(sAsc);

            // Setup Descending Sample
            deep_compositor::DeepSample sDesc;
            sDesc.red = 1.0f; sDesc.green = 0.0f; sDesc.blue = 0.0f; sDesc.alpha = 1.0f;
            sDesc.depth = zDescending;
            sDesc.depth_back = zDescending + 0.1f;
            imgDesc.pixel(x, y).addSample(sDesc);
        }
    }

    try {
        deep_compositor::writeDeepEXR(imgAsc, "z_ascending.exr");
        std::cout << "Created z_ascending.exr (Green)" << std::endl;

        deep_compositor::writeDeepEXR(imgDesc, "z_descending.exr");
        std::cout << "Created z_descending.exr (Red)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Sorting test failed: " << e.what() << std::endl;
    }
}


/**
 * Generates a 16x16 deep EXR test file with 1 sample per pixel.
 * Channels: R, G, B, A, Z, ZBack
 */
void generateTestDeepEXR(const std::string& filename) {
    const int width = 16;
    const int height = 16;

    // 1. Initialize the DeepImage container
    deep_compositor::DeepImage testImg(width, height);

    for (int y = 0; y < height; ++y) {
        // n samples for the nth line (Row 0 = 0 samples, Row 1 = 1 sample...)
        int samplesPerPixelInThisRow = y; 

        for (int x = 0; x < width; ++x) {
            // for (int s = 0; s < samplesPerPixelInThisRow; ++s) {
                deep_compositor::DeepSample sample;
                
                // Color changes slightly per sample so they aren't identical
                sample.red   = static_cast<float>(x) / 15.0f;
                sample.green = static_cast<float>(y) / 15.0f;
                sample.blue  = 0.0f;
                sample.alpha = 0.5f; // Semi-transparent to test compositing

                // Space samples out in depth: Sample 0 is at Z=10, Sample 1 at Z=11...
                sample.depth      =  0;
                sample.depth_back =  0;

                testImg.pixel(x, y).addSample(sample);
            // }
        }
    }

    // 3. Use your provided writeDeepEXR function to save the file
    try {
        deep_compositor::writeDeepEXR(testImg, filename);
        std::cout << "Successfully generated: " << filename << std::endl;
        std::cout << "Dimensions: " << width << "x" << height << " (Deep Scanline)" << std::endl;
        std::cout << "Total Samples: " << testImg.totalSampleCount() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error writing test file: " << e.what() << std::endl;
    }
}

int main() {
    // generateDepthSortingTests();
    generateTestDeepEXR("test_grid3.exr");
    return 0;
}
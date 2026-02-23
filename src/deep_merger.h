#pragma once

#include "deep_pipeline.h"


#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>


struct RawSample {
    float r, g, b, a, z, z_back;
    // For volumetric splitting, you might need z_back. 


    // Used for std::sort
    bool operator<(const RawSample& other) const {
        if (z != other.z) return z < other.z;
        return z_back < other.z_back;
    }
};


// Takes raw sample data from multiple input rows for a single pixel, merges them, and writes to the output row
void mergePixelsDirect(
    int x, int y,
    const std::vector<const float*>& pixelDataPtrs,
    const std::vector<unsigned int>& pixelSampleCounts,
    DeepRow& outputRow
) {
    // 1. Collect all raw samples into a temporary flat vector
    // We reuse this vector across pixels to avoid re-allocation
    static thread_local std::vector<RawSample> staging;
    staging.clear();

    for (size_t i = 0; i < pixelDataPtrs.size(); ++i) {
        const float* data = pixelDataPtrs[i];
        unsigned int count = pixelSampleCounts[i];

        for (unsigned int s = 0; s < count; ++s) {
            // Offset: sample_index * 6 channels
            const float* sData = data + (s * 6);
            // Assuming order: R, G, B, A, Z, zback
            staging.push_back({sData[0], sData[1], sData[2], sData[3], sData[4], sData[5]}); // Using Z for ZBack as well if not present
        }
    }

    if (staging.empty()) {
        outputRow.sampleCounts[x] = 0;
        return;
    }

    // 2. [Your Volumetric Splitting/Sorting Logic Here]
    std::sort(staging.begin(), staging.end());

    // 3. Write results back to the outputRow

     float* outPtr = outputRow.getPixelData(x); 
    
    // For this example, let's just write the sorted samples back
    for (size_t s = 0; s < staging.size(); ++s) {
        float* dest = outPtr + (s * 6); 
        dest[0] = staging[s].r;
        dest[1] = staging[s].g;
        dest[2] = staging[s].b;
        dest[3] = staging[s].a;
        dest[4] = staging[s].z;
        dest[5] = staging[s].z_back; // If you have ZBack, write it here
    }

    // Update the output sample count for this pixel
    outputRow.sampleCounts[x] = static_cast<unsigned int>(staging.size());
}
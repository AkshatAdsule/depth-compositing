#pragma once

#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <ImfDeepFrameBuffer.h> // OpenEXR

// A single row's worth of deep data for one file
struct DeepRow {
    // Using the "One big block" optimization to avoid fragmentation
    float* allSamples = nullptr; 
    int width = 0;
    std::vector<unsigned int> sampleCounts;
    size_t totalSamplesInRow = 0;
    size_t currentCapacity = 0;


    // Normal Allocate given a size
    void allocate(size_t width, int maxSamples) {
        this->width = width;
        ensureCapacity((size_t)maxSamples * 5);
        sampleCounts.assign(width, 0); 
    }


    // writes data from the input files directly into the output row's memory
    void allocate(size_t width, const unsigned int* counts) {
        this->width = width;
        sampleCounts.assign(counts, counts + width);
        totalSamplesInRow = 0;
        for(auto c : sampleCounts) totalSamplesInRow += c;
        
        // Allocate one contiguous block for all RGBAZ data
        // 5 channels: R, G, B, A, Z
        allSamples = new float[totalSamplesInRow * 5];
        currentCapacity = totalSamplesInRow * 5;
    }
    


    const float* getPixelData(int x) const {
        size_t offset = 0;
        for (int i = 0; i < x; ++i) {
            offset += sampleCounts[i];
        }
        // Multiply by 5 because of RGBAZ interleaving
        return allSamples + (offset * 5);
    }

    // Non-const version for writing
    float* getPixelData(int x) {
        size_t offset = 0;
        for (int i = 0; i < x; ++i) offset += sampleCounts[i];
        return allSamples + (offset * 5);
    }

    unsigned int getSampleCount(int x) const {
        return sampleCounts[x];
    }


    // Cleanup
    void clear() {
        if (allSamples) delete[] allSamples;
        allSamples = nullptr;
        sampleCounts.clear();
    }
    
    ~DeepRow() { clear(); }
private:

    void ensureCapacity(size_t required) {
        if (required > currentCapacity) {
            if (allSamples) delete[] allSamples;
            allSamples = new float[required];
            currentCapacity = required;
        }
    }
};

// enum class RowStatus { Empty, Loaded, Merged, Written };

void flattenRow(const DeepRow& deepRow, std::vector<float>& rgbOutput) {
    float* samplePtr = deepRow.allSamples;

    for (int x = 0; x < deepRow.width; ++x) {
        float accR = 0, accG = 0, accB = 0, accA = 0;
        int numSamples = deepRow.sampleCounts[x];

        for (int s = 0; s < numSamples; ++s) {
            float r = samplePtr[0];
            float g = samplePtr[1];
            float b = samplePtr[2];
            float a = samplePtr[3];
            // Depth (samplePtr[4]) is used for sorting, but not for the Over math

            // Front-to-Back "Over" Operator
            float weight = a * (1.0f - accA);
            accR += r * weight;
            accG += g * weight;
            accB += b * weight;
            accA += weight;

            samplePtr += 5; // Move to next interleaved sample

            if (accA >= 0.999f) {
                // Optimization: If we are fully opaque, 
                // skip the rest of the samples for this pixel
                samplePtr += (numSamples - 1 - s) * 5;
                break;
            }
        }
        
        // Store in a standard 3-channel RGB buffer
        rgbOutput[x * 3 + 0] = accR;
        rgbOutput[x * 3 + 1] = accG;
        rgbOutput[x * 3 + 2] = accB;
    }
}
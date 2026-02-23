#pragma once
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineInputFile.h> // For reading deep EXR files
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <ImfDeepScanLineInputFile.h> // OpenEXR

namespace deep_compositor {

/**
 * A single deep sample containing depth and premultiplied RGBA values
 */


 struct DeepSample {
    float depth;       // Z front (depth from camera)
    float depth_back;  // Z back. Equal to depth for point/hard-surface samples.
    float red;         // Premultiplied red
    float green;       // Premultiplied green
    float blue;        // Premultiplied blue
    float alpha;       // Coverage/opacity

    DeepSample()
        : depth(0.0f), depth_back(0.0f), red(0.0f), green(0.0f), blue(0.0f), alpha(0.0f) {}

    // Zero-thickness convenience constructor (depth_back = depth)
    DeepSample(float z, float r, float g, float b, float a)
        : depth(z), depth_back(z), red(r), green(g), blue(b), alpha(a) {}

    // Full volumetric constructor
    DeepSample(float z_front, float z_back, float r, float g, float b, float a)
        : depth(z_front), depth_back(z_back), red(r), green(g), blue(b), alpha(a) {}

    bool isVolume() const { return depth_back > depth; }
    float thickness() const { return depth_back - depth; }

    /**
     * Compare samples by depth (for sorting front-to-back),
     * with depth_back as tiebreaker
     */
    bool operator<(const DeepSample& other) const {
        if (depth != other.depth) return depth < other.depth;
        return depth_back < other.depth_back;
    }

    /**
     * Check if two samples are at approximately the same depth range
     */
    bool isNearDepth(const DeepSample& other, float epsilon = 0.001f) const {
        return std::abs(depth - other.depth) < epsilon
            && std::abs(depth_back - other.depth_back) < epsilon;
    }
};

/**
 * A pixel containing multiple deep samples, sorted by depth
 */
class DeepPixel {
public:
    DeepPixel() = default;
    
    /**
     * Add a sample to this pixel, maintaining depth sort order
     */
    void addSample(const DeepSample& sample);
    
    /**
     * Add multiple samples at once
     */
    void addSamples(const std::vector<DeepSample>& newSamples);
    
    /**
     * Get the number of samples in this pixel
     */
    size_t sampleCount() const { return samples_.size(); }
    
    /**
     * Check if this pixel has any samples
     */
    bool isEmpty() const { return samples_.empty(); }
    
    /**
     * Get all samples (const)
     */
    const std::vector<DeepSample>& samples() const { return samples_; }
    
    /**
     * Get all samples (mutable)
     */
    std::vector<DeepSample>& samples() { return samples_; }
    
    /**
     * Get a specific sample by index
     */
    const DeepSample& operator[](size_t index) const { return samples_[index]; }
    DeepSample& operator[](size_t index) { return samples_[index]; }
    
    /**
     * Clear all samples
     */
    void clear() { samples_.clear(); }
    
    /**
     * Sort samples by depth (front to back)
     */
    void sortByDepth();
    
    /**
     * Merge samples that are within epsilon depth of each other
     */
    void mergeSamplesWithinEpsilon(float epsilon = 0.001f);
    
    /**
     * Get the minimum depth in this pixel
     */
    float minDepth() const;
    
    /**
     * Get the maximum depth in this pixel
     */
    float maxDepth() const;
    
    /**
     * Validate that samples are sorted correctly
     */
    bool isValidSortOrder() const;

private:
    std::vector<DeepSample> samples_;  // Sorted by depth (front to back)
};

/**
 * A 2D deep image containing a grid of deep pixels
 */


class DeepImage {
public:
    DeepImage();
    DeepImage(int width, int height);
    
    /**
     * Resize the image (clears all existing data)
     */
    void resize(int width, int height);
    
    /**
     * Get image dimensions
     */
    int width() const { return width_; }
    int height() const { return height_; }
    
    /**
     * Access a pixel at (x, y)
     */
    DeepPixel& pixel(int x, int y);
    const DeepPixel& pixel(int x, int y) const;
    
    /**
     * Alternative accessors using operator()
     */
    DeepPixel& operator()(int x, int y) { return pixel(x, y); }
    const DeepPixel& operator()(int x, int y) const { return pixel(x, y); }
    
    /**
     * Get total number of samples across all pixels
     */
    size_t totalSampleCount() const;
    
    /**
     * Get average samples per pixel
     */
    float averageSamplesPerPixel() const;
    
    /**
     * Get global depth range
     */
    void depthRange(float& minDepth, float& maxDepth) const;
    
    /**
     * Get the number of non-empty pixels
     */
    size_t nonEmptyPixelCount() const;
    
    /**
     * Sort all pixels by depth
     */
    void sortAllPixels();
    
    /**
     * Validate all pixels have correct depth ordering
     */
    bool isValid() const;
    
    /**
     * Estimate memory usage in bytes
     */
    size_t estimatedMemoryUsage() const;
    
    /**
     * Clear all pixels
     */
    void clear();

private:
    int width_;
    int height_;
    std::vector<DeepPixel> pixels_;  // Stored row-major: index = y * width + x
    
    /**
     * Convert (x, y) to linear index
     */
    size_t index(int x, int y) const;
    
    /**
     * Check if coordinates are valid
     */
    bool isValidCoord(int x, int y) const;
};



class DeepInfo {
public:   
    DeepInfo();
    DeepInfo(const std::string& filename) 
        : file_(filename.c_str()) // This opens the file immediately
    {
        // Once the file is open, we extract the metadata (width/height)
        Imath::Box2i dw = file_.header().dataWindow();
        width_  = dw.max.x - dw.min.x + 1;
        height_ = dw.max.y - dw.min.y + 1;
        printf("Loaded Deep EXR: %s (%dx%d)\n", filename.c_str(), width_, height_);
        printf("Number of parts in file: %d\n", file_.header().hasType());
        // We can verify if it's deep, though DeepScanLineInputFile 
        // will throw an error if you point it at a non-deep file anyway.
    }

    int width() const { return width_; }
    int height() const { return height_; }
    bool isDeep() const { return isDeep_; }

    Imf::DeepScanLineInputFile& getFile()  { return file_; }

    // Temporary buffer for sample counts of a single row
    const unsigned int* getSampleCountsForRow(int y) {
        fetchSampleCounts(y);
        return tempSampleCounts.data();  // return pointer to the start of the row's sample counts
    }

    /**
     * This function uses the OpenEXR IMF library to efficiently load deep image data:
     * - Resizes the temporary buffer to accommodate one row of sample count integers
     * - Calculates the byte offset corresponding to the requested row
     * - Configures a DeepFrameBuffer with a sample count slice pointing to the appropriate
     *   memory location using pointer arithmetic
     * - Reads only the sample counts (not the actual sample data) for the specified row
     * 
     * The pointer arithmetic adjusts the base memory address by the total offset so that
     * the IMF library can correctly map pixel coordinates to buffer locations.
     * 
 
     */
    void fetchSampleCounts(int y) {
        // Resize buffer to fit one row of integers
        tempSampleCounts.resize(width_);

        Imath::Box2i dw = file_.header().dataWindow();
        int minX = dw.min.x;

        Imf::DeepFrameBuffer countBuffer;
        // We point to the start of our vector, but tell OpenEXR 
        // that this memory represents pixel (minX, y)
        char* base = (char*)(tempSampleCounts.data()) 
                    - (minX * sizeof(unsigned int)); 
                    // Note: We don't subtract y because we only read one row (y, y)

        countBuffer.insertSampleCountSlice(Imf::Slice(
            Imf::UINT, 
            base, 
            sizeof(unsigned int), // xStride
            0                     // yStride (0 because we read 1 row)
        ));

        file_.setFrameBuffer(countBuffer);
        file_.readPixelSampleCounts(y, y);
    }

private:
    int width_;
    int height_;

    std::vector<unsigned int> tempSampleCounts;
    
    Imf::DeepScanLineInputFile file_;

    bool isDeep_;
    bool isValidCoord(int x, int y) const {
        return (x >= 0 && x < width_ && y >= 0 && y < height_);
    }

    // Helper to convert (x, y) to linear index for any internal arrays
    size_t index(int x, int y) const { return static_cast<size_t>(y) * width_ + x; }
};

} // namespace deep_compositor

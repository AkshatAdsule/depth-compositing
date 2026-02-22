#include "deep_compositor.h"
#include "deep_volume.h"
#include "utils.h"
#include "deep_reader.h"
#include "indicators.h"
#include "deep_pipeline.h"
#include "deep_merger.h"

#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>

#include <algorithm>
#include <stdexcept>


using namespace indicators;

namespace deep_compositor {


// Main Pipeline Function
// 1. Load deep EXR files into DeepImage objects
// 2. Merge samples across images based on depth proximity
// 3. Output merged deep EXR, flattened EXR, and PNG preview


enum RowStatus {
    EMPTY,
    LOADED,
    MERGED,
    FLATTENED,
    ERROR
};




std::vector<float> processAllEXR(const Options& opts){
    
    
    std::vector<std::unique_ptr<DeepInfo>> imagesInfo;
    int numFiles = opts.inputFiles.size();
     // Track status of each row for synchronization



    // images.reserve(opts.inputFiles.size());
    

    // Move into a new for pipeline functionality

    // 1. Load deep EXR files into DeepImage objects
    // 2. Merge samples across images based on depth proximity
    // 3. Output merged deep EXR, flattened EXR, and PNG preview



    // ========================================================================
    // Preload stage - validate files and load metadata
    // ========================================================================

    // show_console_cursor(false);
    // BlockProgressBar bar{option::BarWidth{80},
    //                      option::Start{"["},
    //                      option::End{"]"},
    //                      option::ShowPercentage{true},
    //                      option::ShowElapsedTime{true},
    //                      option::ShowRemainingTime{true},
    //                      option::MaxProgress{3}};

     for (size_t i = 0; i < opts.inputFiles.size(); ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1)); 
        const std::string& filename = opts.inputFiles[i];
        // printf("Preloading [%zu/%zu]: %s\n", i + 1, opts.inputFiles.size(), filename.c_str());
        logVerbose("  [" + std::to_string(i + 1) + "/" + 
                   std::to_string(opts.inputFiles.size()) + "] " + filename);

        try {
            // Check if it's a deep EXR
            if (!isDeepEXR(filename)) {
                logError("File is not a deep EXR: " + filename);
                return {};
            }
            
            auto img = std::make_unique<DeepInfo>(filename); 
            
            // Log statistics
            std::string stats = "    " + std::to_string(img->width()) + "x" + 
                               std::to_string(img->height());
            // printf("%s\n", stats.c_str());
            logVerbose(stats);
            
            // Validate dimensions match
            if (!imagesInfo.empty()) {
                if (img->width() != imagesInfo[0]->width() || 
                    img->height() != imagesInfo[0]->height()) {
                    logError("Image dimensions mismatch: " + filename);
                    logError("  Expected: " + std::to_string(imagesInfo[0]->width()) + "x" + 
                            std::to_string(imagesInfo[0]->height()));
                    logError("  Got: " + std::to_string(img->width()) + "x" + 
                            std::to_string(img->height()));
                    return {};
                }
            }
            
            imagesInfo.push_back(std::move(img));
            
        } catch (const DeepReaderException& e) {
            logError("Failed to load " + filename + ": " + e.what());
            return {};
        } catch (const std::exception& e) {
            logError("Unexpected error loading " + filename + ": " + e.what());
            return {};
        }
        if (!isDeepEXR(filename)) {
            logError("File is not a deep EXR: " + filename);
            return {};
        }
        // bar.tick();
    }


    // ========================================================================
    // Key State Variables - validate files and load metadata
    // ========================================================================
    
    int height = imagesInfo[0]->height();
    int width = imagesInfo[0]->width();


    const int chunkSize = 16;
    const int windowSize = 32;
    
    std::atomic<int> loaded_scanlines{0};
    std::atomic<int> merged_scanlines{0};
    std::atomic<int> next_scanline{0};
    std::atomic<int> scanlines_completed{0};

    std::mutex merge_mutex; // For synchronizing progress bar updates

    std::vector<std::atomic<int>> rowStatus(imagesInfo[0]->height());
    std::vector<std::vector<DeepRow>> m_inputBuffer;
    std::vector<DeepRow> m_mergedBuffer;

    m_inputBuffer.resize(numFiles);
    for (int i = 0; i < numFiles; ++i) {
        // Resize the inner vector to the size of the sliding window
        m_inputBuffer[i].resize(windowSize);
    }



    

    // ========================================================================
    // Stage 1 LOAD - Load lines in chunks of 16
    // ========================================================================
    // show_console_cursor(false);
    // BlockProgressBar loadBar{option::BarWidth{80},
    //                      option::Start{"["},
    //                      option::End{"]"},
    //                      option::ShowPercentage{true},
    //                      option::ShowElapsedTime{true},
    //                      option::ShowRemainingTime{true},
    //                      option::MaxProgress{3}};

    auto loader_worker = [&]() {
        for (int yStart = 0; yStart < height; yStart += chunkSize) {
            int yEnd = std::min(yStart + chunkSize - 1, height - 1);

            // 1. SAFETY THROTTLE: Wait for the writer to clear the slots we need
            // We check the first row of the chunk; if it's cleared, the chunk is safe.
            if (yStart >= windowSize) {
                while (rowStatus[yStart - windowSize].load() < 3 /* Written */) {
                    std::this_thread::yield(); 
                }
            }

            // 2. LOAD CHUNK FROM EACH FILE
            for (int i = 0; i < numFiles; ++i) {
                Imf::DeepScanLineInputFile& file = imagesInfo[i]->getFile(); // The Imf::DeepScanLineInputFile
                
                // Part A: Read Sample Counts for the whole chunk
                std::vector<unsigned int> chunkCounts(width * (yEnd - yStart + 1));
                file.readPixelSampleCounts(yStart, yEnd); 

                // Part B: Setup Memory and Read Actual Data
                for (int y = yStart; y <= yEnd; ++y) {

                    int slot = y % windowSize; // Wrapping slot index for the sliding window

                    DeepRow& row = m_inputBuffer[i][slot];
                    
                    // Fetch the counts OpenEXR just read for this specific row
                    const unsigned int* rowCounts = imagesInfo[i]->getSampleCountsForRow(y);
                    row.allocate(width, rowCounts);

                    // Prepare the FrameBuffer for this row
                    Imf::DeepFrameBuffer frameBuffer;
                    
                    // Offset the pointer so row Y writes to index 0
                    char* basePtr = (char*)(row.allSamples);
                    size_t xStride = 5 * sizeof(float); // 5 channels (RGBAZ)
                    size_t yStride = xStride * width;
                    size_t sampleStride = 5 * sizeof(float);
                    size_t fSize = sizeof(float);

                    frameBuffer.insert("R", Imf::DeepSlice(
                        Imf::FLOAT, 
                        basePtr - (y * yStride) + (0 * fSize), // Offset back to "virtual" row 0
                        xStride, 
                        yStride,
                        sampleStride // Sample stride
                    ));
                    frameBuffer.insert("G", Imf::DeepSlice(
                        Imf::FLOAT, 
                        basePtr - (y * yStride) + (1 * fSize), // Offset back to "virtual" row 0
                        xStride, 
                        yStride,
                        sampleStride// Sample stride
                    ));
                    frameBuffer.insert("B", Imf::DeepSlice(
                        Imf::FLOAT, 
                        basePtr - (y * yStride) + (2 * fSize), // Offset back to "virtual" row 0
                        xStride, 
                        yStride,
                        sampleStride // Sample stride
                    ));
                    frameBuffer.insert("A", Imf::DeepSlice(
                        Imf::FLOAT, 
                        basePtr - (y * yStride) + (3 * fSize), // Offset back to "virtual" row 0
                        xStride, 
                        yStride,
                        sampleStride // Sample stride
                    ));
                    frameBuffer.insert("Z", Imf::DeepSlice(
                        Imf::FLOAT, 
                        basePtr - (y * yStride) + (4 * fSize), // Offset back to "virtual" row 0
                        xStride, 
                        yStride,
                        sampleStride // Sample stride
                    ));
                    // ... repeat insert for "R", "G", "B", "A" if stored separately ...

                    file.setFrameBuffer(frameBuffer);
                    file.readPixels(y, y); 
                }
            }

            // 3. UPDATE STATUS: Mark all 16 rows as 'Loaded' (1)
            for (int y = yStart; y <= yEnd; ++y) {
                rowStatus[y].store(1); 
                loaded_scanlines.fetch_add(16);
            }
            printf("Progress: Loaded rows %d to %d\n", yStart, yEnd);
            
        }

    };
    

    // ========================================================================
    // Stage 2 Merge - Merge all possible lines in parallel with a thread pool
    // ========================================================================
    BlockProgressBar mergeBar{option::BarWidth{80},
                         option::Start{"["},
                         option::End{"]"},
                         option::ShowPercentage{true},
                         option::ShowElapsedTime{true},
                         option::ShowRemainingTime{true},
                         option::MaxProgress{3}};

    
    auto merger_worker = [&]() {
        while (true) {

            // Atomic "Grab" - Thread-safe row selection
            int y = next_scanline.fetch_add(1);
            if (y >= height) break;

            while (rowStatus[y].load() < 1) { // 1 = Loaded
                std::this_thread::yield();
            }

            int slot = y % windowSize;


            DeepRow& outputRow = m_mergedBuffer[slot];
            
            int totalPossibleSamplesInRow = 0;

            for (int x = 0; x < width; ++x) {
                int maxSamplesForPixel = 0;
                for (int i = 0; i < numFiles; ++i) {
                    // Worst case: Volumetric splitting could potentially double samples, 
                    // but let's start with the sum of all inputs.
                    maxSamplesForPixel += m_inputBuffer[i][slot].sampleCounts[x];
                }
                // Safety buffer for volumetric splitting (e.g., 2x)
                totalPossibleSamplesInRow += (maxSamplesForPixel * 2); 
            }

            // Now allocate the merged row once
            m_mergedBuffer[slot].allocate(width, totalPossibleSamplesInRow);


            // Process scanline
            for (int x = 0; x < width; ++x) {
                // Gather pixel pointers from all inputs
                std::vector<const float*> pixelDataPtrs;
                std::vector<unsigned int> pixelSampleCounts;

                for (int i = 0; i < numFiles; ++i) {
                    DeepRow& inputRow = m_inputBuffer[i][slot];
                    
                    // Get pointer to the start of this pixel's samples
                    // You'll need a helper in DeepRow to find the offset for pixel x
                    pixelDataPtrs.push_back(inputRow.getPixelData(x));
                    pixelSampleCounts.push_back(inputRow.getSampleCount(x));
                }
                    


                // Merge pixels

                // Needs implementation of mergePixels that can take raw pointers and counts, and a merge threshold for merging nearby samples.
                // float threshold = opts.enableMerging ? opts.mergeThreshold : 0.0f;
                
                // result.pixel(x, y) = mergePixels(pixelDataPtrs, pixelSampleCounts, threshold);


                mergePixelsDirect(x, y, pixelDataPtrs, pixelSampleCounts, outputRow);
            }
            rowStatus[y].store(2); // Mark as Merged
            merged_scanlines.fetch_add(1);
            // Thread-safe progress update
            int done = scanlines_completed.fetch_add(1) + 1;
            printf("\rMerging: %d/%d rows completed", done, height);
            // {
            //     std::lock_guard<std::mutex> lock(merge_mutex);
            //     // mergeBar.set_progress(static_cast<double>(done) / height);

            // }
        }
    };



    // ========================================================================
    // Stage 3 Write - Load lines in chunks of 16
    // ========================================================================

    BlockProgressBar writeBar{option::BarWidth{80},
                         option::Start{"["},
                         option::End{"]"},
                         option::ShowPercentage{true},
                         option::ShowElapsedTime{true},
                         option::ShowRemainingTime{true},
                         option::MaxProgress{3}};

    
    auto writer_worker = [&]() {

        std::vector<float> finalImage(width * height * 3, 0.0f);

        for (int y = 0; y < height; ++y) {
            // 1. WAIT: Ensure Merger has finished this row
            // We are looking for Status 2 (Merged)
            while (rowStatus[y].load() < 2) {
                std::this_thread::yield();
            }

            int slot = y % windowSize;
            const DeepRow& deepRow = m_mergedBuffer[slot];

            // 2. FLATTEN: Convert merged deep data to flat RGBA
            std::vector<float> rowRGB(width * 3);

            // 4. EXECUTE YOUR FUNCTION
            flattenRow(deepRow, rowRGB);


            std::copy(rowRGB.begin(), rowRGB.end(), finalImage.begin() + (y * width * 3));


            const_cast<DeepRow&>(deepRow).clear();

            rowStatus[y].store(3);
            
            if (y % 100 == 0) printf("Progress: Flattened and cleared row %d\n", y);
        }

        // Return the final flattened image data as a vector of floats (RGB interleaved)

    };

    



    int thread_count = std::thread::hardware_concurrency();
    thread_count = 3; // For now, just use 3 threads for the 3 stages. Later we can make this more dynamic and efficient by having a shared thread pool and dynamic scheduling of tasks.
    
    
    std::vector<std::thread> threads;
    threads.emplace_back(loader_worker);
    threads.emplace_back(merger_worker);
    threads.emplace_back(writer_worker);

    // for (int t = 0; t < thread_count; ++t) {
    //     threads.emplace_back(render_worker);
    // }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }



    show_console_cursor(true);
    std::vector<float> result; // At the very end read from the merged DeepImage and return the flattened RGBA data as a vector of floats
    return result;


    


}


bool validateDimensions(const std::vector<DeepImage>& inputs) {
    if (inputs.empty()) {
        return true;
    }
    
    int width = inputs[0].width();
    int height = inputs[0].height();
    
    for (size_t i = 1; i < inputs.size(); ++i) {
        if (inputs[i].width() != width || inputs[i].height() != height) {
            return false;
        }
    }
    
    return true;
}

bool validateDimensions(const std::vector<const DeepImage*>& inputs) {
    if (inputs.empty()) {
        return true;
    }
    
    int width = inputs[0]->width();
    int height = inputs[0]->height();
    
    for (size_t i = 1; i < inputs.size(); ++i) {
        if (inputs[i]->width() != width || inputs[i]->height() != height) {
            return false;
        }
    }
    
    return true;
}

DeepPixel mergePixels(const std::vector<const DeepPixel*>& pixels,
                      float mergeThreshold) {
    return mergePixelsVolumetric(pixels, mergeThreshold);
}

DeepImage deepMerge(const std::vector<DeepImage>& inputs,
                    const CompositorOptions& options,
                    CompositorStats* stats, const std::vector<float>& zOffsets) {
    // Convert to pointer version
    std::vector<const DeepImage*> ptrs;
    ptrs.reserve(inputs.size());
    for (const auto& img : inputs) {
        ptrs.push_back(&img);
    }
    
    return deepMerge(ptrs, options, stats, zOffsets);
}

DeepImage deepMerge(const std::vector<const DeepImage*>& inputs,
                    const CompositorOptions& options,
                    CompositorStats* stats, const std::vector<float>& zOffsets) {
    // Timer timer;
    
    // // Handle empty input
    // if (inputs.empty()) {
    //     if (stats) {
    //         stats->inputImageCount = 0;
    //     }
    //     return DeepImage();
    // }
    
    // // Validate dimensions
    // if (!validateDimensions(inputs)) {
    //     throw std::runtime_error("Input images have mismatched dimensions");
    // }
    
    // int width = inputs[0]->width();
    // int height = inputs[0]->height();
    
    // // Calculate input statistics
    // size_t totalInputSamples = 0;
    // float minDepth = std::numeric_limits<float>::infinity();
    // float maxDepth = -std::numeric_limits<float>::infinity();
    
    // for (size_t i = 0; i < inputs.size(); ++i) {
    //     const auto* img = inputs[i];

    //     float offset = (i < zOffsets.size()) ? zOffsets[i] : 0.0f;
    //     if (zOffsets.size() > 0) {
    //         // Apply Z offset to each pixel in the image
    //         if (zOffsets[i] != 0) {
    //             for (int y = 0; y < img->height(); ++y) {
    //                 for (int x = 0; x < img->width(); ++x) {
    //                     DeepPixel& pixel = const_cast<DeepPixel&>(img->pixel(x, y));
    //                     for (auto& sample : pixel.samples()) {
    //                         sample.depth += offset;
    //                         sample.depth_back += offset;
    //                     }
    //                 }
    //             }
    //         }
    //     }
    //     // totalInputSamples += img->totalSampleCount();
        
    //     // float imgMin, imgMax;
    //     // img->depthRange(imgMin, imgMax);
    //     // minDepth = std::min(minDepth, imgMin);
    //     // maxDepth = std::max(maxDepth, imgMax);
    // }
    
    // logVerbose("  Merging " + std::to_string(inputs.size()) + " images...");
    // logVerbose("    Input samples: " + formatNumber(totalInputSamples));
    
    // Create output image
    DeepImage result(0, 0);
    
    // // Prepare pixel pointer arrays for each input
    // // std::vector<const DeepPixel*> pixelPtrs(inputs.size());

    
    // std::atomic<int> next_scanline(0);
    // std::atomic<int> scanlines_completed(0);
    // std::mutex progress_mutex;

    // auto render_worker = [&]() {
    //     std::vector<const DeepPixel*> pixelPtrs(inputs.size());
    //     while (true) {

    //         // Atomic "Grab" - Thread-safe row selection
    //         int y = next_scanline.fetch_add(1);
    //         if (y >= height) break;

    //         // Process scanline
    //         for (int x = 0; x < width; ++x) {
    //             // Gather pixel pointers from all inputs
    //             for (size_t i = 0; i < inputs.size(); ++i) {
    //                 pixelPtrs[i] = &(inputs[i]->pixel(x, y));
    //             }
                
    //             // Merge pixels
    //             float threshold = options.enableMerging ? options.mergeThreshold : 0.0f;
    //             result.pixel(x, y) = mergePixels(pixelPtrs, threshold);
    //         }

    //         // Thread-safe progress update
    //         int done = scanlines_completed.fetch_add(1) + 1;
    //         {
    //             std::lock_guard<std::mutex> lock(progress_mutex);
    //             // Update progress if needed
    //         }
    //     }
    // };
    
    
    // // Spawn and join worker threads
    // int thread_count = std::thread::hardware_concurrency();

    // std::clog << "[Session] Rendering with " << thread_count << " threads...\n";

    // std::vector<std::thread> threads;

    // for (int t = 0; t < thread_count; ++t) {
    //     threads.emplace_back(render_worker);
    // }

    // for (auto& thread : threads) {
    //     thread.join();
    // }
    



    // double mergeTime = timer.elapsedMs();
    
    // // Calculate output statistics
    // size_t totalOutputSamples = result.totalSampleCount();
    
    // logVerbose("    Output samples: " + formatNumber(totalOutputSamples));
    // logVerbose("    Depth range: " + std::to_string(minDepth) + " to " + std::to_string(maxDepth));
    // logVerbose("    Merge time: " + std::to_string(static_cast<int>(mergeTime)) + " ms");
    
    // // Fill stats if requested
    // if (stats) {
    //     stats->inputImageCount = inputs.size();
    //     stats->totalInputSamples = totalInputSamples;
    //     stats->totalOutputSamples = totalOutputSamples;
    //     stats->minDepth = minDepth;
    //     stats->maxDepth = maxDepth;
    //     stats->mergeTimeMs = mergeTime;
    // }
    
    return result;
}

} // namespace deep_compositor

#include "deep_compositor.h"
#include "deep_volume.h"
#include "utils.h"
#include "deep_reader.h"
#include "indicators.h"
#include "deep_pipeline.h"
#include "deep_merger.h"


#include <OpenEXR/ImfDeepScanLineInputFile.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfMultiPartInputFile.h>
#include <ImfDeepScanLineInputFile.h> // OpenEXR

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

    // ========================================================================
    // Preload stage - validate files and load metadata
    // ========================================================================

    printf("Preloading input files... \n");
     for (size_t i = 0; i < opts.inputFiles.size(); ++i) {

        // std::this_thread::sleep_for(std::chrono::seconds(1)); 
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

   

    std::vector<std::atomic<int>> rowStatus(imagesInfo[0]->height());
    std::vector<std::vector<DeepRow>> m_inputBuffer;
    std::vector<DeepRow> m_mergedBuffer;

     std::mutex merge_mutex; // For synchronizing progress bar updates

    // Initialize all row statuses to EMPTY (0)
    std::vector<std::atomic<int>> rowStatus(height);
    for (int i = 0; i < height; ++i) {
        rowStatus[i].store(EMPTY);
    }

    m_mergedBuffer.resize(windowSize); // One slot per scanline in the sliding window

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
        // printf("Loading EXR data in chunks of %d scanlines...\n", chunkSize);
        int load_y = 0;
        while (load_y < height) {
            int slot = load_y % windowSize;

            // Yeild if y-windowSize hasn't been merged and written yet, meaning the merger worker hasn't caught up to the loader
        
            //  Prevent processing more than 16 files
            if (load_y >= windowSize) {
                while (rowStatus[load_y - windowSize].load() < FLATTENED) {
                    std::this_thread::yield(); 
                }
            }


            // 2. LOAD CHUNK FROM EACH FILE
            for (int i = 0; i < numFiles; ++i) {
                Imf::DeepScanLineInputFile& file = imagesInfo[i]->getFile(); // The Imf::DeepScanLineInputFile 
                DeepRow& row = m_inputBuffer[i][slot]; // Gets the index where we will write data
              
                printf("Row: %d sample counts: ", load_y);

                // Loads row and gets the numbers corresponding to how many samples are in each pixel of that row
                const unsigned int* tempCounts = imagesInfo[i]->getSampleCountsForRow(load_y);
                if (tempCounts == nullptr) {
                    printf("ERROR: rowCounts is NULL! Expect strange behaviour.\n");
                }
                // Custom allocation at the rowCounts pointer
                row.allocate(width, tempCounts); // Allocates space based on how big that row is

                char* permanentCountPtr = (char*)row.sampleCounts.data();
                char* basePtr = (char*)(row.allSamples);  // Location of block of memory

                if (row.allSamples == nullptr) {
                    printf("FATAL: row.allSamples is NULL for Row %d even though samples > 0!\n", load_y);
                } else {

                    printf("Row %d: basePtr %p is writeable. Capacity: %zu floats\n", 
                            load_y, (void*)row.allSamples, row.currentCapacity);
                }
                
                size_t sampleStride = 6 * sizeof(float);
                size_t xStride = 0; // Since we're using a single contiguous block, xStride is 0
                size_t yStride = 0; // Since we're using a single contiguous block, yStride is 0
                size_t fSize = sizeof(float);


                std::vector<float*> rPtrs(width), gPtrs(width), bPtrs(width), 
                aPtrs(width), zPtrs(width), zbPtrs(width);

                float* currentPixelPtr = row.allSamples;
                for (int x = 0; x < width; ++x) {
                    rPtrs[x]  = currentPixelPtr + 0; // Points to R
                    gPtrs[x]  = currentPixelPtr + 1; // Points to G
                    bPtrs[x]  = currentPixelPtr + 2; // Points to B
                    aPtrs[x]  = currentPixelPtr + 3; // Points to A
                    zPtrs[x]  = currentPixelPtr + 4; // Points to Z
                    zbPtrs[x] = currentPixelPtr + 5; // Points to ZBack
                    
                    // Move to the next pixel: jump by (samples in this pixel * 6 channels)
                    currentPixelPtr += row.sampleCounts[x] * 6;
                }


                std::vector<float*> pixelPointers(width);
                Imf::DeepFrameBuffer frameBuffer; 

                frameBuffer.insertSampleCountSlice(Imf::Slice(
                    Imf::UINT,
                    (char*)row.sampleCounts.data(), 
                    sizeof(unsigned int), // xStride: move to next int
                    0                     // yStride: 0
                ));

                char* basePointers = (char*)pixelPointers.data();
                size_t ptrSize = sizeof(float*);


                xStride = sizeof(float*);

                frameBuffer.insert("R", Imf::DeepSlice(Imf::FLOAT, (char*)rPtrs.data(),  xStride, 0, sampleStride));
                frameBuffer.insert("G", Imf::DeepSlice(Imf::FLOAT, (char*)gPtrs.data(),  xStride, 0, sampleStride));
                frameBuffer.insert("B", Imf::DeepSlice(Imf::FLOAT, (char*)bPtrs.data(),  xStride, 0, sampleStride));
                frameBuffer.insert("A", Imf::DeepSlice(Imf::FLOAT, (char*)aPtrs.data(),  xStride, 0, sampleStride));
                frameBuffer.insert("Z", Imf::DeepSlice(Imf::FLOAT, (char*)zPtrs.data(),  xStride, 0, sampleStride));
                frameBuffer.insert("ZBack", Imf::DeepSlice(Imf::FLOAT, (char*)zbPtrs.data(), xStride, 0, sampleStride));

                // printf("Setting frame buffer ");
                printf("Attempting to read RGBAZ... \n");
                file.setFrameBuffer(frameBuffer);
                file.readPixels(load_y, load_y); 

                std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Simulate load time for testing
                // }
                // y.fetch_add(1); // Update loaded scanline count for progress tracking
                
            }
            

            // Update status after N rows
            rowStatus[load_y].store(LOADED);  // Update status to Loaded
            loaded_scanlines.fetch_add(1); 

            // Increment outer loop
            load_y++;
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
        int merge_y = 0;
   
        printf("Merging scanlines in parallel...\n");
        while (merge_y < height) {
            // Atomic "Grab" - Thread-safe row selection

            // Hard Coded safety in case it catches up. 
            while (rowStatus[merge_y].load() < LOADED) { // Wait until the loader has loaded this row
                std::this_thread::yield();
            }

            int slot = merge_y % windowSize;
            printf("Merging row %d (slot %d)...\n", merge_y, slot);
  
            DeepRow& outputRow = m_mergedBuffer[slot];
            
            int totalPossibleSamplesInRow = 0;
            int maxSamplesForPixel = 0;

            for (int i = 0; i < numFiles; ++i) {
                // Worst case: Volumetric splitting could potentially double samples, 
                // but let's start with the sum of all inputs.
                maxSamplesForPixel += m_inputBuffer[i][slot].totalSamplesInRow;
            }

            // Safety buffer for volumetric splitting (e.g., 2x)
            totalPossibleSamplesInRow += (maxSamplesForPixel * 1);  
            printf("Row %d: Max samples for pixel = %d, Total possible samples in row = %d\n", 
                    merge_y, maxSamplesForPixel, totalPossibleSamplesInRow);

            // Now allocate the merged row once
            m_mergedBuffer[slot].allocate(width, totalPossibleSamplesInRow);
            printf("Allocated output row with capacity for %d samples\n", totalPossibleSamplesInRow);


            // Process scanline x at a time to keep memory usage low and allow for early merging
            for (int x = 0; x < width; ++x) {
                std::vector<const float*> pixelDataPtrs;
                std::vector<unsigned int> pixelSampleCounts;

                for (int i = 0; i < numFiles; ++i) {
                    DeepRow& inputRow = m_inputBuffer[i][slot];
                    pixelDataPtrs.push_back(inputRow.getPixelData(x));  // 
                    pixelSampleCounts.push_back(inputRow.getSampleCount(x));
                }
 
                // Merge pixels
                
                mergePixelsDirect(x, merge_y, pixelDataPtrs, pixelSampleCounts, outputRow);
                
            }


            rowStatus[merge_y].store(MERGED); // Mark as Merged

            merge_y ++;
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Simulate merge time for testing
            
        }
    

    // }
    };



    // ========================================================================
    // Stage 3 Write - Save lines in chunks of 16
    // ========================================================================

    std::vector<float> finalImage(width * height * 4, 0.0f); // RGBA output buffer
    auto writer_worker = [&]() {

        int write_y = 0;

        while (write_y < height) {

            // Ensure Merger has finished this row
            while (rowStatus[write_y].load() < MERGED) {
                std::this_thread::yield();
            }

            int slot = write_y % windowSize;
            const DeepRow& deepRow = m_mergedBuffer[slot];

            // FLATTEN: Convert merged deep data to flat RGBA
            std::vector<float> rowRGB(width * 4);
            flattenRow(deepRow, rowRGB);

            std::copy(rowRGB.begin(), rowRGB.end(), finalImage.begin() + (write_y * width * 4)); // 4 channels (RGBA)


            const_cast<DeepRow&>(deepRow).clear();            
            
            
           
            // Update status and progress bar
            printf("\rWriting: %d/%d rows completed", write_y + 1, height);
            rowStatus[write_y].store(FLATTENED); // Set back to Loaded so loader can reuse the slot for the next row
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            write_y++;
        }

        // Return the final flattened image data as a vector of floats (RGB interleaved)
        
    };

    



    int thread_count = std::thread::hardware_concurrency();
    thread_count = 3; // For now, just use 3 threads for the 3 stages. Later we can make this more dynamic and efficient by having a shared thread pool and dynamic scheduling of tasks.
    
    
    std::vector<std::thread> threads;
    threads.emplace_back(loader_worker);
    threads.emplace_back(merger_worker);
    threads.emplace_back(writer_worker);

    // Wait for all threads to complete
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    printf("\nPipeline complete!\n");
    return finalImage;


    // show_console_cursor(true);
    // std::vector<float> result; // At the very end read from the merged DeepImage and return the flattened RGBA data as a vector of floats
    // return result;


    


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

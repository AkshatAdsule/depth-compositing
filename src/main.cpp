#include "deep_image.h"
#include "deep_reader.h"
#include "deep_writer.h"
#include "deep_compositor.h"
#include "utils.h"
#include "deep_options.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstring>

namespace {

const char* VERSION = "1.0";


bool isFloat(const std::string& s) {
    std::istringstream iss(s);
    float f;
    // Try to read a float, check if successful and no non-whitespace characters remain
    if (!(iss >> f)) {
        return false; // Conversion failed
    }
    // Check if there are any non-whitespace characters left in the stream
    // Use std::ws to consume any trailing whitespace
    return (iss >> std::ws).eof();
}

void printUsage(const char* programName) {
    std::cout << "Deep Image Compositor v" << VERSION << "\n\n"
              << "Usage: " << programName << " [options] <input1.exr> [input2.exr ...] <output_prefix>\n\n"
              << "Options:\n"
              << "  --deep-output        Write merged deep EXR (default: off)\n"
              << "  --flat-output        Write flattened EXR (default: on)\n"
              << "  --no-flat-output     Don't write flattened EXR\n"
              << "  --png-output         Write PNG preview (default: on)\n"
              << "  --no-png-output      Don't write PNG preview\n"
              << "  --verbose, -v        Detailed logging\n"
              << "  --merge-threshold N  Depth epsilon for merging samples (default: 0.001)\n"
              << "  --help, -h           Show this help message\n\n"
              << "Example:\n"
              << "  " << programName << " --deep-output --verbose \\\n"
              << "      test_data/sphere_front.exr \\\n"
              << "      test_data/sphere_back.exr \\\n"
              << "      test_data/ground_plane.exr \\\n"
              << "      output/result\n\n"
              << "Outputs:\n"
              << "  <output_prefix>_merged.exr  (deep EXR, if --deep-output)\n"
              << "  <output_prefix>_flat.exr    (standard EXR)\n"
              << "  <output_prefix>.png         (preview image)\n";
}

bool parseArgs(int argc, char* argv[], Options& opts) {
    if (argc < 2) {
        return false;
    }
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            opts.showHelp = true;
            return true;
        } else if (arg == "--verbose" || arg == "-v") {
            opts.verbose = true;
        } else if (arg == "--deep-output") {
            opts.deepOutput = true;
        } else if (arg == "--flat-output") {
            opts.flatOutput = true;
        } else if (arg == "--no-flat-output") {
            opts.flatOutput = false;
        } else if (arg == "--png-output") {
            opts.pngOutput = true;
        } else if (arg == "--no-png-output") {
            opts.pngOutput = false;
        } else if (arg == "--mod-offset") {
            opts.modOffset = true;
        } else if (arg == "--merge-threshold") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --merge-threshold requires a value\n";
                return false;
            }
            try {
                opts.mergeThreshold = std::stof(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid merge threshold value\n";
                return false;
            }
        } else if (arg[0] == '-' and !isFloat(arg)) {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            return false;
        } else {
            // Positional argument (input file or output prefix)

            if (opts.modOffset && isFloat(arg)) {
                if (opts.inputFiles.size() != opts.inputZOffsets.size() + 1) {
                    std::cerr << "Error: Mismatched position of Z offset value\n";
                    return false;
                }
                try {
                    float offset = std::stof(arg);
                    opts.inputZOffsets.push_back(offset);
                    continue; // Don't treat this as an input file
                } catch (...) {
                    std::cerr << "Error: Invalid Z offset value: " << arg << "\n";
                    return false;
                }
            } else{
                
                if (opts.modOffset && (opts.inputFiles.size() == opts.inputZOffsets.size() + 1)) {
                    opts.inputZOffsets.push_back(0); // Default offset for this file
                }
                opts.inputFiles.push_back(arg); // Could be input file or output prefix, we'll determine later
            }
           
            
        }
       

    }
    if (opts.modOffset && (opts.inputFiles.size() != opts.inputZOffsets.size() )) {
        opts.inputZOffsets.push_back(0); // Default offset for last file if not provided
    }
    // std::cerr << "There are " << opts.inputFiles.size() << " input files and " << opts.inputZOffsets.size() << " Z offsets\n";
    
    // for (size_t i = 0; i < opts.inputZOffsets.size(); ++i) {
    //     std::cerr << "Z Offset " << i << ": " << opts.inputZOffsets[i] << "\n";
    // }

    // Need at least one input and one output prefix
    if (opts.inputFiles.size() < 2) {
        std::cerr << "Error: Need at least one input file and an output prefix\n";
        return false;
    }
    
    // Last positional arg is output prefix
    opts.outputPrefix = opts.inputFiles.back();
    opts.inputFiles.pop_back();
    
    return true;
}

} // anonymous namespace




int main(int argc, char* argv[]) {
    using namespace deep_compositor;
    
    Options opts;
    
    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 1;
    }
    
    if (opts.showHelp) {
        printUsage(argv[0]);
        return 0;
    }
    
    // Set verbose mode
    setVerbose(opts.verbose);
    
    log("Deep Compositor v" + std::string(VERSION));
    
    Timer totalTimer;

    log("Loading inputs...");
    Timer loadTimer;
    // 
    for (size_t i = 0; i < opts.inputFiles.size(); ++i) {
        const std::string& filename = opts.inputFiles[i];
        if (!isDeepEXR(filename)) {
            logError("File is not a deep EXR: " + filename);
            return 1;
        }
    }


     std::vector<float> finalImage = processAllEXR(opts);


     
    // return 0; // We will return the flattened RGBA data as a vector of floats at the end of this function, but for now we just want to test loading and merging
   
    // logVerbose("  Load time: " + loadTimer.elapsedString());
    
    // log("\nMerging...");
    
    // CompositorOptions compOpts;
    // compOpts.mergeThreshold = opts.mergeThreshold;
    // compOpts.enableMerging = (opts.mergeThreshold > 0.0f);
    
    // CompositorStats stats;
    
    // // DeepImage merged = deepMerge(images, compOpts, &stats, opts.inputZOffsets);
    
    // log("  Combined: " + formatNumber(stats.totalOutputSamples) + " total samples");
    // log("  Depth range: " + std::to_string(stats.minDepth) + " to " + 
    //     std::to_string(stats.maxDepth));
    // log("  Merge time: " + std::to_string(static_cast<int>(stats.mergeTimeMs)) + " ms");
    






    // ========================================================================
    // Flatten Phase
    // ========================================================================
    // std::vector<float> flatRgba;
    
    // if (opts.flatOutput || opts.pngOutput) {
    //     log("\nFlattening...");
    //     Timer flattenTimer;
        
    //     flatRgba = flattenImage(merged);
        
    //     logVerbose("  Flatten time: " + flattenTimer.elapsedString());
    // }
    
    // ========================================================================
    // Write Phase
    // ========================================================================
    log("\nWriting outputs...");
    Timer writeTimer;
    
    try {
        // // Write deep output if requested
        // if (opts.deepOutput) {
        //     std::string deepPath = opts.outputPrefix + "_merged.exr";
        //     writeDeepEXR(merged, deepPath);
        //     log("  Wrote: " + deepPath);
        // }
        
        // // Write flat EXR if requested
        // if (opts.flatOutput) {
        //     std::string flatPath = opts.outputPrefix + "_flat.exr";
        //     writeFlatEXR(flatRgba, merged.width(), merged.height(), flatPath);
        //     log("  Wrote: " + flatPath);
        // }
        
        // Write PNG if requested
        if (opts.pngOutput) {
            std::string pngPath = opts.outputPrefix + ".png";
            
            if (hasPNGSupport()) {
                writePNG(finalImage, 16, 16, pngPath);
                log("  Wrote: " + pngPath);
            } else {
                log("  Skipped PNG (libpng not available)");
            }
        }
        
    } catch (const DeepWriterException& e) {
        logError("Failed to write output: " + std::string(e.what()));
        return 1;
    }
    
    // logVerbose("  Write time: " + writeTimer.elapsedString());
    
    // ========================================================================
    // Summary
    // ========================================================================
    log("\nDone! Total time: " + totalTimer.elapsedString());
    
    return 0;
}

 auto loader_worker = [&]() {
        printf("Loading EXR data in chunks of %d scanlines...\n", chunkSize);
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
                Imath::Box2i dw = file.header().dataWindow();

                const Imf::ChannelList& channels = file.header().channels();
                bool hasR = channels.findChannel("R") != nullptr;
                bool hasG = channels.findChannel("G") != nullptr;
                bool hasB = channels.findChannel("B") != nullptr;
                bool hasA = channels.findChannel("A") != nullptr;
                bool hasZ = channels.findChannel("Z") != nullptr;
                bool hasZBack = channels.findChannel("ZBack") != nullptr;

                if (!hasZBack) {
                    logError("Warning: File " + std::to_string(i) + " is missing ZBack channel. This may cause compositing artifacts.");
                }

                if (!hasR || !hasG || !hasB || !hasA || !hasZ) {
                    std::string missing;
                    if (!hasR) missing += "R ";
                    if (!hasG) missing += "G ";
                    if (!hasB) missing += "B ";
                    if (!hasA) missing += "A ";
                    if (!hasZ) missing += "Z ";
                    throw DeepReaderException("Missing required channels: " + missing);
                }


                printf("FILE DATA WINDOW: min:(%d, %d) max:(%d, %d)\n", dw.min.x, dw.min.y, dw.max.x, dw.max.y);
                // Part A: Read Sample Counts for the whole chunk
                // std::vector<unsigned int> chunkCounts(width * (yEnd - yStart + 1));
                // imagesInfo[i]->loadSampleCounts(0);
                // file.readPixelSampleCounts(yStart, yEnd); 

                // Part B: Setup Memory and Read Actual Data
                
                for (int y = yStart; y <= yEnd; ++y) {

                   

                    int slot = y % windowSize; // Wrapping slot index for the sliding window

                    DeepRow& row = m_inputBuffer[i][slot]; // Gets the index where we will write data
                    
                    // Fetch the counts OpenEXR just read for this specific row
                    printf("Row: %d sample counts: ", y);

                     // Goes through all X and loads the data into the 
                    // imagesInfo[i]->loadSampleCounts(y); 

                    // Loads row and gets the numbers corresponding to how many samples are in each pixel of that row
                    const unsigned int* tempCounts = imagesInfo[i]->getSampleCountsForRow(y);

                    // DEBUG PRINT
                    printf("File %d, Row %d: tempCounts pointer is %p\n", i, y, (void*)tempCounts);

                    if (tempCounts == nullptr) {
                        printf("ERROR: rowCounts is NULL! The previous loadSampleCounts(y) failed or wasn't saved.\n");
                        // This will cause the segfault if we continue
                    }

        
                    // Custom allocation at the rowCounts pointer
                    row.allocate(width, tempCounts); // Allocates space based on how big that row is

                    


                    char* permanentCountPtr = (char*)row.sampleCounts.data();
                    
                    // Offset the pointer so row Y writes to index 0
                    char* basePtr = (char*)(row.allSamples);  // Location of block of memory

                    if (row.allSamples == nullptr) {
                        printf("FATAL: row.allSamples is NULL for Row %d even though samples > 0!\n", y);
                    } else {
                        // Try to "touch" the memory manually. 
                        // If it segfaults HERE, the allocation itself is the problem.
                        // row.allSamples[0] = 0.0f; 
                        printf("Row %d: basePtr %p is writeable. Capacity: %zu floats\n", 
                                y, (void*)row.allSamples, row.currentCapacity);
                    }
                        
                    size_t xStride = 0; // Since we're using a single contiguous block, xStride is 0
                    size_t yStride = 0; // Since we're using a single contiguous block, yStride is 0
                    size_t fSize = sizeof(float);


                    Imath::Box2i dw = file.header().dataWindow();
                    int minX = dw.min.x;

                    size_t countYStride = sizeof(unsigned int) * width;
                    size_t sampleStride = 6 * sizeof(float);
                    size_t dataYStride  = 6 * sizeof(float) * width;

                    // Prepare the FrameBuffer for this row 
                    yStride = 0;
                    xStride = sampleStride;

                    char* virtualCountPtr = ((char*)row.sampleCounts.data()) - (y * countYStride);

                    char* virtualDataPtr = basePtr - (minX * sampleStride);

                    Imf::DeepFrameBuffer frameBuffer; 
                    frameBuffer.insertSampleCountSlice(Imf::Slice(
                        Imf::UINT, 
                        (char*)(row.sampleCounts.data()) - (minX * sizeof(unsigned int)), 
                        sizeof(unsigned int), 
                        0 
                    ));

                    // char* virtualRowPtr = basePtr - (static_cast<size_t>(y) * yStride);
                    frameBuffer.insert("R", Imf::DeepSlice(
                        Imf::FLOAT, 
                        virtualDataPtr + (0 * fSize), // Offset back to "virtual" row 0
                        xStride, 
                        yStride,
                        sampleStride // Sample stride
                    ));
                    // // printf("Attempting to read g");
                    frameBuffer.insert("G", Imf::DeepSlice(
                        Imf::FLOAT, 
                        virtualDataPtr + (1 * fSize), // Offset back to "virtual" row 0
                        xStride, 
                        yStride,
                        sampleStride// Sample stride
                    ));
                    // printf("Attempting to read b");
                    frameBuffer.insert("B", Imf::DeepSlice(
                        Imf::FLOAT, 
                        virtualDataPtr + (2 * fSize), // Offset back to "virtual" row 0
                        xStride, 
                        yStride,
                        sampleStride // Sample stride
                    ));
                    // printf("Attempting to read a");
                    frameBuffer.insert("A", Imf::DeepSlice(
                        Imf::FLOAT, 
                        virtualDataPtr + (3 * fSize), // Offset back to "virtual" row 0
                        xStride, 
                        yStride,
                        sampleStride // Sample stride
                    ));
                    // printf("Attempting to read z");
                    frameBuffer.insert("Z", Imf::DeepSlice(
                        Imf::FLOAT, 
                        virtualDataPtr + (4 * fSize), // Offset back to "virtual" row 0
                        xStride, 
                        yStride,
                        sampleStride // Sample stride
                    ));
                    frameBuffer.insert("ZBack", Imf::DeepSlice( 
                        Imf::FLOAT, 
                        virtualDataPtr + (5 * fSize), 
                        xStride, 
                        yStride, 
                        sampleStride 
                    ));
                    // ... repeat insert for "R", "G", "B", "A" if stored separately ...

                    // printf("Setting frame buffer ");
                    printf("Attempting to read RGBAZ... \n");
                    file.setFrameBuffer(frameBuffer);
                    file.readPixels(y, y); 
                    // file.setFrameBuffer(Imf::DeepFrameBuffer());

                    if (row.allSamples == nullptr || row.currentCapacity == 0) {
                            printf("Row is empty or not allocated\n");
                            continue;
                    }

                    // Print first sample
                    printf("First sample (pixel 0): ");
                    float* firstSample = row.allSamples;
                    printf("RGBA=(%.3f, %.3f, %.3f, %.3f) Z=%.3f ZBack=%.3f\n",
                            firstSample[0], firstSample[1], firstSample[2], firstSample[3],
                            firstSample[4], firstSample[5]);

                    printf("ROW CAPACITY: %zu floats\n", row.currentCapacity);
                    // Print last sample
                    printf("Last sample: ");
                    float* lastSample = row.allSamples + (row.currentCapacity - 6);
                    printf("RGBA=(%.3f, %.3f, %.3f, %.3f) Z=%.3f ZBack=%.3f\n",
                            lastSample[0], lastSample[1], lastSample[2], lastSample[3],
                            lastSample[4], lastSample[5]);
                    // Check if all samples are zero
                    bool allZero = true;
                    for (size_t i = 0; i < row.currentCapacity; ++i) {
                        if (row.allSamples[i] != 0.0f) {
                            allZero = false;
                            break;
                        }
                    }
                    if (allZero) {
                        printf("ALL 0\n");
                    }
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
    



      // for (int debugY : {std::min(yStart + 1, yEnd), yEnd}) {
            //     int debugSlot = debugY % windowSize;
            //     printf("\n=== Debug: Row %d ===\n", debugY);
                
            //     for (int i = 0; i < numFiles; ++i) {
            //         const DeepRow& debugRow = m_inputBuffer[i][debugSlot];
            //         printf("File %d, Row %d:\n", i, debugY);
                    
            //         // Print first pixel's samples
            //         const float* firstPixel = debugRow.getPixelData(0);
            //         unsigned int firstPixelSamples = debugRow.getSampleCount(0);
            //         printf("  Pixel 0 (%u samples):\n", firstPixelSamples);
            //         for (unsigned int s = 0; s < firstPixelSamples; ++s) {
            //             printf("    Sample %u: R=%.3f G=%.3f B=%.3f A=%.3f Z=%.3f\n",
            //                    s,
            //                    firstPixel[s * 6 + 0],
            //                    firstPixel[s * 6 + 1],
            //                    firstPixel[s * 6 + 2],
            //                    firstPixel[s * 6 + 3],
            //                    firstPixel[s * 6 + 4]);
            //         }
                    
            //         // Print last pixel's samples
            //         const float* lastPixel = debugRow.getPixelData(width - 1);
            //         unsigned int lastPixelSamples = debugRow.getSampleCount(width - 1);
            //         printf("  Pixel %d (%u samples):\n", width - 1, lastPixelSamples);
            //         for (unsigned int s = 0; s < lastPixelSamples; ++s) {
            //             printf("    Sample %u: R=%.3f G=%.3f B=%.3f A=%.3f Z=%.3f\n",
            //                    s,
            //                    lastPixel[s * 6 + 0],
            //                    lastPixel[s * 6 + 1],
            //                    lastPixel[s * 6 + 2],
            //                    lastPixel[s * 6 + 3],
            //                    lastPixel[s * 6 + 4]);
            //         }
            //     }
            // }

# Test Data for Deep Compositor Demo

This directory contains test deep EXR images for validating the deep compositor.

## Generated Images

The `generate_test_images` tool creates three test images:

### 1. sphere_front.exr
- **Description**: Red semi-transparent sphere in the foreground
- **Resolution**: 512x512
- **Depth range**: Z = 5.0 to 10.0
- **Color**: Red (R=1.0, G=0.2, B=0.2)
- **Alpha**: 0.7 (semi-transparent)
- **Samples per hit pixel**: 2 (entry and exit surfaces)

### 2. sphere_back.exr
- **Description**: Blue semi-transparent sphere in the background
- **Resolution**: 512x512
- **Depth range**: Z = 15.0 to 20.0
- **Color**: Blue (R=0.2, G=0.2, B=1.0)
- **Alpha**: 0.7 (semi-transparent)
- **Samples per hit pixel**: 2 (entry and exit surfaces)

### 3. ground_plane.exr
- **Description**: Green opaque ground plane at the back
- **Resolution**: 512x512
- **Depth**: Z = 25.0 (constant)
- **Color**: Green (R=0.2, G=0.6, B=0.2)
- **Alpha**: 1.0 (opaque)
- **Samples per pixel**: 1

## Generating Test Images

```bash
# From the build directory
./generate_test_images --output ../test_data --verbose
```

## Expected Compositing Results

When compositing all three images:

1. **Overlap region** (both spheres): Red-purple-blue color gradient
2. **Front sphere only**: Red tint with ground showing through
3. **Back sphere only**: Blue tint with ground showing through
4. **Background**: Green ground plane
5. **Outside all objects**: Transparent (alpha = 0)

## Validation Tests

### Test 1: Two Overlapping Spheres
```bash
./deep_compositor sphere_front.exr sphere_back.exr output/two_spheres --verbose
```
- Expected: Correct depth ordering, red sphere in front of blue

### Test 2: All Three Layers
```bash
./deep_compositor sphere_front.exr sphere_back.exr ground_plane.exr output/all_layers --verbose
```
- Expected: Both spheres composite over green background

### Test 3: Order Independence
```bash
./deep_compositor ground_plane.exr sphere_back.exr sphere_front.exr output/reversed --verbose
```
- Expected: Identical output to Test 2 (proves depth-correct compositing)

## Technical Details

### Deep Sample Format
- **Channels**: R, G, B, A, Z
- **Data type**: float32
- **Alpha**: Premultiplied
- **Depth ordering**: Front-to-back (smallest Z first)

### Sphere Generation Algorithm
1. For each pixel, cast ray from camera
2. Compute ray-sphere intersection
3. If hit, calculate entry and exit depths
4. For semi-transparent spheres: create 2 samples (entry/exit)
5. For opaque surfaces: create 1 sample at entry point

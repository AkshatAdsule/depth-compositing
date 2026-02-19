#!/usr/bin/env bash
# ============================================================================
# Deep Volumetric Image Compositing -- Algorithm Demo
#
# Generates volumetric test scenes and composites them to demonstrate
# Beer-Lambert volume splitting and uniform interspersion blending.
# ============================================================================
set -euo pipefail

cd "$(dirname "$0")"

# -- Formatting --------------------------------------------------------------
BOLD='\033[1m'
DIM='\033[2m'
GREEN='\033[32m'
BLUE='\033[34m'
CYAN='\033[36m'
MAGENTA='\033[35m'
YELLOW='\033[33m'
RED='\033[31m'
RESET='\033[0m'

banner() {
    echo ""
    echo -e "${BOLD}${CYAN}+-----------------------------------------------------------+${RESET}"
    echo -e "${BOLD}${CYAN}|   Deep Volumetric Image Compositing  -  Algorithm Demo    |${RESET}"
    echo -e "${BOLD}${CYAN}|                                                           |${RESET}"
    echo -e "${BOLD}${CYAN}|${RESET}   Beer-Lambert Splitting  /  Uniform Interspersion        ${BOLD}${CYAN}|${RESET}"
    echo -e "${BOLD}${CYAN}|${RESET}   Front-to-Back Over Compositing                          ${BOLD}${CYAN}|${RESET}"
    echo -e "${BOLD}${CYAN}+-----------------------------------------------------------+${RESET}"
    echo ""
}

section() { echo "" ; echo -e "${BOLD}${BLUE}--- $1 ---${RESET}"; }
ok()      { echo -e "  ${GREEN}ok${RESET}  $1"; }
info()    { echo -e "  ${DIM}$1${RESET}"; }

# -- Paths -------------------------------------------------------------------
BUILD="build"
GEN="$BUILD/generate_test_images"
COMP="$BUILD/deep_compositor"
DEMO="demo"
INPUTS="$DEMO/inputs"
SCENES="$DEMO/scenes"

# ============================================================================
banner

# -- Build -------------------------------------------------------------------
section "1/5  Building project"
cmake -B "$BUILD" -S . > /dev/null 2>&1 || {
    rm -rf "$BUILD"
    cmake -B "$BUILD" -S . > /dev/null 2>&1
}
cmake --build "$BUILD" --parallel > /dev/null 2>&1
ok "Build successful"

# -- Generate demo images ----------------------------------------------------
section "2/5  Generating volumetric test images"
mkdir -p "$INPUTS" "$SCENES"
"$GEN" --demo --output "$INPUTS" 2>&1 | while IFS= read -r line; do
    echo -e "  ${DIM}${line}${RESET}"
done
ok "16 deep EXR files generated in $INPUTS/"

# -- Render individual input previews ----------------------------------------
section "3/5  Rendering individual input previews"
for exr in "$INPUTS"/*.exr; do
    [[ "$exr" == *.flat.exr ]] && continue
    name=$(basename "$exr" .exr)
    "$COMP" --no-flat-output "$exr" "$INPUTS/${name}_preview" > /dev/null 2>&1 || true
    ok "$name"
done

# -- Composite scenes --------------------------------------------------------
section "4/5  Compositing demo scenes"

echo ""
echo -e "  ${BOLD}${RED}Scene 1${RESET}${BOLD}: Nebula  --  Overlapping Volumetric Fog Spheres${RESET}"
info "Three volumetric spheres (red/green/blue) with overlapping depth ranges."
info "Where volumes overlap, Beer-Lambert splitting creates sub-intervals"
info "that are blended via uniform interspersion -> secondary colors appear."
info ""
info "  Red-orange  [depth  4-20]  alpha 0.60   center-left"
info "  Green       [depth  8-24]  alpha 0.55   center-right"
info "  Blue-violet [depth  2-16]  alpha 0.50   bottom-center"
info ""
"$COMP" --deep-output \
    "$INPUTS/nebula_red.exr" \
    "$INPUTS/nebula_green.exr" \
    "$INPUTS/nebula_blue.exr" \
    "$INPUTS/backdrop.exr" \
    "$SCENES/nebula" > /dev/null 2>&1
ok "-> ${BOLD}$SCENES/nebula.png${RESET}"

echo ""
echo -e "  ${BOLD}${MAGENTA}Scene 2${RESET}${BOLD}: Crystal  --  Opaque Sphere Inside Volumetric Fog${RESET}"
info "An opaque gold sphere (depth 10-16) sits inside a large purple"
info "volumetric fog (depth 3-25). The merge splits the fog at the"
info "sphere boundary. Front fog tints the sphere purple; back fog"
info "is fully occluded by the opaque surface."
info ""
"$COMP" --deep-output \
    "$INPUTS/purple_fog.exr" \
    "$INPUTS/gold_sphere.exr" \
    "$INPUTS/backdrop.exr" \
    "$SCENES/crystal" > /dev/null 2>&1
ok "-> ${BOLD}$SCENES/crystal.png${RESET}"

echo ""
echo -e "  ${BOLD}${YELLOW}Scene 3${RESET}${BOLD}: Combined  --  All Elements Together${RESET}"
info "All six layers composited in a single pass: three nebula fogs"
info "+ purple fog + gold sphere + backdrop.  The algorithm handles"
info "all pairwise volume overlaps simultaneously."
info ""
"$COMP" --deep-output \
    "$INPUTS/nebula_red.exr" \
    "$INPUTS/nebula_green.exr" \
    "$INPUTS/nebula_blue.exr" \
    "$INPUTS/purple_fog.exr" \
    "$INPUTS/gold_sphere.exr" \
    "$INPUTS/backdrop.exr" \
    "$SCENES/combined" > /dev/null 2>&1
ok "-> ${BOLD}$SCENES/combined.png${RESET}"

echo ""
echo -e "  ${BOLD}${CYAN}Scene 4${RESET}${BOLD}: Fog Slice  --  Steep Fog Gradient + Diagonal Bar${RESET}"
info "A full-frame uniform fog field (no top/bottom gradient), represented"
info "as layered deep slices that accumulate extinction with traveled depth."
info "A long slender rectangle cuts through the fog, with depth ramping"
info "from near on the left to far on the right."
info "Expected: near-left bright/readable, far-right significantly dimmer."
info ""
"$COMP" --deep-output \
    "$INPUTS/fog_steep_gradient.exr" \
    "$INPUTS/diagonal_slice.exr" \
    "$INPUTS/backdrop.exr" \
    "$SCENES/fog_slice" > /dev/null 2>&1
ok "-> ${BOLD}$SCENES/fog_slice.png${RESET}"

echo ""
echo -e "  ${BOLD}${GREEN}Scene 5${RESET}${BOLD}: Stained Glass  --  Intersecting Transparent Panes${RESET}"
info "Three semi-transparent colored panes (red/green/blue), each tilted"
info "in depth so they cross through each other.  No single global layer"
info "ordering works -- deep compositing resolves the per-pixel depth order."
info ""
info "  Red pane    [depth 5->25 left-to-right]   alpha 0.45"
info "  Green pane  [depth 25->5 left-to-right]   alpha 0.45"
info "  Blue pane   [depth 5->25 top-to-bottom]   alpha 0.45"
info ""
"$COMP" --deep-output \
    "$INPUTS/stained_red.exr" \
    "$INPUTS/stained_green.exr" \
    "$INPUTS/stained_blue.exr" \
    "$INPUTS/backdrop.exr" \
    "$SCENES/stained_glass" > /dev/null 2>&1
ok "-> ${BOLD}$SCENES/stained_glass.png${RESET}"

echo ""
echo -e "  ${BOLD}${YELLOW}Scene 6${RESET}${BOLD}: Lighthouse  --  Light Beam Through Fog${RESET}"
info "A bright volumetric cone beam cuts diagonally through a large"
info "volumetric fog bank.  The two volumes overlap at varying depths."
info "Deep compositing splits and interleaves them correctly."
info ""
"$COMP" --deep-output \
    "$INPUTS/lighthouse_fog.exr" \
    "$INPUTS/lighthouse_beam.exr" \
    "$INPUTS/backdrop.exr" \
    "$SCENES/lighthouse" > /dev/null 2>&1
ok "-> ${BOLD}$SCENES/lighthouse.png${RESET}"

echo ""
echo -e "  ${BOLD}${MAGENTA}Scene 7${RESET}${BOLD}: Rings  --  Interlocking Chain Links${RESET}"
info "Three opaque torus rings interlinked like a chain.  Each ring's"
info "depth varies sinusoidally around its circumference so that where"
info "two rings overlap, one passes in front then behind the other."
info "Impossible to composite correctly without per-pixel depth."
info ""
"$COMP" --deep-output \
    "$INPUTS/ring_gold.exr" \
    "$INPUTS/ring_silver.exr" \
    "$INPUTS/ring_copper.exr" \
    "$INPUTS/backdrop.exr" \
    "$SCENES/rings" > /dev/null 2>&1
ok "-> ${BOLD}$SCENES/rings.png${RESET}"

# -- Summary -----------------------------------------------------------------
section "5/5  Done"

echo ""
echo -e "  ${BOLD}Demo outputs:${RESET}"
echo ""
echo -e "    ${RED}Nebula${RESET}            $SCENES/nebula.png"
echo -e "    ${MAGENTA}Crystal in Fog${RESET}    $SCENES/crystal.png"
echo -e "    ${YELLOW}Combined${RESET}          $SCENES/combined.png"
echo -e "    ${CYAN}Fog Slice${RESET}         $SCENES/fog_slice.png"
echo -e "    ${GREEN}Stained Glass${RESET}     $SCENES/stained_glass.png"
echo -e "    ${YELLOW}Lighthouse${RESET}        $SCENES/lighthouse.png"
echo -e "    ${MAGENTA}Rings${RESET}             $SCENES/rings.png"
echo ""
echo -e "    ${DIM}Input previews    $INPUTS/*_preview.png${RESET}"
echo -e "    ${DIM}Deep EXR data     $SCENES/*_merged.exr${RESET}"
echo ""
echo -e "  ${BOLD}${CYAN}Key things to notice:${RESET}"
echo ""
echo -e "    ${CYAN}Nebula${RESET}   - Overlap regions show ${BOLD}secondary colors${RESET} (yellow,"
echo -e "             cyan, magenta) from physically-correct volume blending."
echo -e "             Front volumes contribute more color (depth ordering)."
echo ""
echo -e "    ${CYAN}Crystal${RESET}  - Gold sphere is visible through ${BOLD}purple haze${RESET}."
echo -e "             Back half of fog is ${BOLD}fully occluded${RESET} by the opaque sphere."
echo -e "             Front fog tints the sphere, exactly as expected."
echo ""
echo -e "    ${CYAN}Combined${RESET} - Six overlapping layers resolved in ${BOLD}one pass${RESET}."
echo -e "             Every pairwise volume overlap handled correctly."
echo ""
echo -e "    ${CYAN}Stained Glass${RESET} - Depth ordering ${BOLD}changes across the image${RESET}."
echo -e "             Red is in front on the left, green on the right."
echo -e "             Where all three cross, complex color mixing occurs."
echo ""
echo -e "    ${CYAN}Lighthouse${RESET} - Volumetric beam ${BOLD}interpenetrates${RESET} volumetric fog."
echo -e "             The compositor splits both volumes at their overlap"
echo -e "             boundaries for physically correct blending."
echo ""
echo -e "    ${CYAN}Rings${RESET}    - Each ring passes ${BOLD}in front then behind${RESET} its neighbor."
echo -e "             No single layer order works.  Deep compositing resolves"
echo -e "             the per-pixel depth correctly."
echo ""
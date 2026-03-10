# DXR & Path Tracing Implementation Plan (ArchViz Final Frame Quality)

This document outlines the architectural and technical steps to upgrade the DXR renderer to a full Path Tracing pipeline using ReSTIR and DLSS-D.

## 1. Architectural Strategy: The Unified DXR Path
Instead of multiple ray-tracing modes, we provide two primary modes for the entire engine:
1. **Raster Mode:** Fast rasterization for scene traversal and high-speed editing.
2. **DXR Mode:** A unified ray-traced path that scales from interactive performance to final frame quality.
    *   **Interactive (Dynamic):** 1 sample per pixel (spp) with high-speed ReSTIR + DLSS-D when moving.
    *   **Progressive (Static):** Auto-accumulation of hundreds of spp for "Final Frame" noise-free exports when the camera is stationary.

## 2. Component Breakdown

### A. The Path Tracing Core (`path_tracer.hlsl`)
*   **Recursive Integrator:** Move from simple ray-casts to a loop-based path tracer with Russian Roulette termination.
*   **Multi-Bounce GI:** Support for at least 4–8 bounces to handle dark interior corners typical in ArchViz.
*   **Advanced Materials:** Full BSDF support (GGX for metals/dielectrics, perfect Fresnel for glass refractions).

### B. ReSTIR DI (Direct Illumination)
*   **Light Sampling:** Unified sampling for Point, Spot, Area (Emissive Meshes), and Sun.
*   **Initial Candidates:** Use BRDF and light-power importance sampling.
*   **Spatio-Temporal Resampling:** Reuse light candidates from neighbor pixels and previous frames to eliminate shadow noise.

### C. ReSTIR GI (Global Illumination)
*   **Indirect Reservoir:** Store and resample indirect light paths.
*   **World-Space Re-projection:** Ensure GI stays stable when the camera moves.

### D. Integrated Light Stack
*   **Unified Light Loop:** A single shader function to evaluate Sun, IBL (HDR), and local lights via ReSTIR.
*   **Area Lights:** Treat emissive model geometry as first-class area lights in the ReSTIR reservoirs.

### E. DLSS-D (Ray Reconstruction) Integration
*   **Streamline Framework:** Use NVIDIA Streamline to inject DLSS 4.0+.
*   **G-Buffer Expansion:** Export Motion Vectors, Depth, Normals, and Albedo to the Streamline constants.
*   **Ray Reconstruction:** Replace traditional denoisers with DLSS-D to handle ReSTIR's noisy output while preserving sharp ArchViz details.

## 3. UI & Render Settings (Render Panel)

The Render Panel will be updated with the following controls:
*   **Max Bounces:** (Slider 1-32) Depth of reflections and GI.
*   **Accumulation Toggle:** Auto-start accumulation when the camera stops moving.
*   **Sample Limit:** (InputInt) Target samples for final export (e.g., 1024, 2048).
*   **ReSTIR Settings:** Toggle Temporal/Spatial resampling and reservoir counts.
*   **DLSS Mode:** Selection between Performance, Balanced, Quality, and Ray Reconstruction.

## 4. File Structure (Modular Approach)

To maintain clean code, the implementation is split into specialized files:

| File | Responsibility |
| :--- | :--- |
| `src/dxr_accumulation.cpp/h` | Manages the accumulation buffer, frame counters, and blending logic. |
| `shaders/path_tracer_core.hlsl` | Main Workhorse: RayGen shader with the path tracing loop. |
| `shaders/restir_lib.hlsl` | Reservoir structures and Resampling logic (DI/GI). |
| `shaders/brdf_lib.hlsl` | Physically correct GGX and Fresnel functions. |
| `shaders/lights_lib.hlsl` | Evaluation logic for Point, Spot, Area, and Sun. |
| `src/streamline_manager.cpp/h` | Streamline SDK initialization and DLSS-D dispatch. |

## 5. Implementation Phases

1.  **Phase 1 (Foundations):** ✅ Implement the Accumulation Buffer and basic Path Tracing loop (Reflections + Shadows).
2.  **Phase 2 (ReSTIR DI):** ✅ Implement Reservoir sampling for local lights to get clean soft shadows.
3.  **Phase 3 (BSDF/GI/Optimization):** ✅ Refine BSDFs (Height-correlated Smith), added refraction, and implemented ReSTIR GI (Temporal/Spatial/Reconnection).
4.  **Phase 4 (Streamline/DLSS-D):** 🟡 Hook up the G-Buffer and Motion Vectors to DLSS Ray Reconstruction.
5.  **Phase 5 (Polishing):** Add the Final Export button (high-spp render to disk).


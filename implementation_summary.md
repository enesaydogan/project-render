# DLSS-D Jitter Fix Implementation Summary

## Changes Applied

1.  **Shader Updates (`shaders/path_tracer_core.hlsl`)**:
    - **Reverted Surface MV Calculation**: Restored the original "reprojection" method for `currScreen`. This produces "Jittered" motion vectors (relative to the sample position), which restores the behavior that worked for DLSS-SR.
    - **Specular Virtual Point Fix**: Retained the physics correction (`primaryPos + rayDir * dist`).

2.  **Renderer Configuration (`src/streamline_manager.cpp`)**:
    - **Set `motionVectorsJittered = eTrue`**: This is the critical fix. It aligns Streamline's expectation with the shader's output (which produces jittered MVs). Previously this was `false`, causing a mismatch.

3.  **Renderer Logic (`src/dxr_renderer.cpp`)**:
    - Fixed history reset bug.

## Verification
- Run in **DLSS-SR** mode -> Should be stable (original behavior restored + config aligned).
- Run in **DLSS-D** mode -> Should be stable (Specular MVs are now generated, physically correct, and config aligned).

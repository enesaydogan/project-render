2. Light Trees / ReGIR for Scalable ReSTIR
In 

path_tracer_core.hlsl
, when creating the first candidate for ReSTIR, your engine uniformly picks a random light: uint lightIdx = next_uint(rng) % numLights;.

The Problem: If you place 10,000 lights in a scene, blindly picking one light means you will almost never randomly pick the light physically closest to the pixel. ReSTIR will eventually find the right light, but it will take many frames and produce boiling noise in the process.
The Magic: Implement a Light Tree (a BVH specifically for lights) or ReGIR (Reservoir-based Grid Importance Resampling). Instead of randomly picking from a flat array, you sample from a lightweight spatial structure so your engine only feeds lights that are physically relevant into the ReSTIR reservoirs. This makes ReSTIR converge almost instantly regardless of light count.
3. Shader Execution Reordering (SER) / Wavefront Path Tracing
Path Tracers suffer from extreme "thread divergence". Ray #1 might hit glass, Ray #2 might hit a mirror, Ray #3 might hit the sky. The GPU is forced to execute all 3 logic paths for all threads in the warp because GPU threads move in lockstep.

The Magic: NVIDIA recently exposed Shader Execution Reordering (SER), which you can implement via the NVAPI extension in DirectX12. It literally pauses your GPU threads, sorts them on-the-fly based on what material they hit, and then resumes them perfectly packed together (all glass threads together, all mirror threads together). It provides a massive framerate boost for multi-bounce path tracing.
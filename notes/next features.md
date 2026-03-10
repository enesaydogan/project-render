2. Light Trees / ReGIR for Scalable ReSTIR
In 

path_tracer_core.hlsl
, when creating the first candidate for ReSTIR, your engine uniformly picks a random light: uint lightIdx = next_uint(rng) % numLights;.

The Problem: If you place 10,000 lights in a scene, blindly picking one light means you will almost never randomly pick the light physically closest to the pixel. ReSTIR will eventually find the right light, but it will take many frames and produce boiling noise in the process.
The Magic: Implement a Light Tree (a BVH specifically for lights) or ReGIR (Reservoir-based Grid Importance Resampling). Instead of randomly picking from a flat array, you sample from a lightweight spatial structure so your engine only feeds lights that are physically relevant into the ReSTIR reservoirs. This makes ReSTIR converge almost instantly regardless of light count.
3. Shader Execution Reordering (SER) / Wavefront Path Tracing
Path Tracers suffer from extreme "thread divergence". Ray #1 might hit glass, Ray #2 might hit a mirror, Ray #3 might hit the sky. The GPU is forced to execute all 3 logic paths for all threads in the warp because GPU threads move in lockstep.

The Magic: NVIDIA recently exposed Shader Execution Reordering (SER), which you can implement via the NVAPI extension in DirectX12. It literally pauses your GPU threads, sorts them on-the-fly based on what material they hit, and then resumes them perfectly packed together (all glass threads together, all mirror threads together). It provides a massive framerate boost for multi-bounce path tracing.

---

### ✨ Additional implementation notes & architectural tips
The roadmap above already hits every major phase. A few extras that will keep the system maintainable and debuggable:

* **CPU/GPU struct layout** – keep the `Light` struct tight (enum+union) and pad to 16/32 bytes. Add a small `uint userData` or `uint id` field for debug/material picking.
* **IES textures** – bake them offline (tool or script) into a Texture2DArray; generate mipmaps to avoid aliasing when sampling at steep angles.
* **Math library** – mirror every `Eval*()` function on the CPU for offline tests. Write simple unit‑test scenes to verify energy conservation before integrating into ReSTIR.
* **ReSTIR reservoir** – store sample point for area lights (64‑bit packed), and precompute any per-light pdf/visibility term so the reservoir update only needs a cheap multiplication.
* **DXR visibility rays** – mark static geometry `OPAQUE` aggressively; avoid AnyHit during the millions of shadow rays that area lights generate.
* **Denoiser handoff** – feed raw noisy results to your TAA/denoiser without pre‑blurring; ensure motion vectors accurately reflect any jitter you introduce.

These extras don’t change the order of the phases but smooth the implementation and future debugging. Geared towards an RTX‑centric renderer, they keep the pipeline efficient even before you add the fancy SER or light‑tree optimizations.
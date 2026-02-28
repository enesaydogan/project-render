The path to a production-ready archviz engine requires a strict, optimized pipeline. Injecting area lights, spots, omnis, and IES profiles into a DXR and OptiX environment—especially one relying on ReSTIR and NVIDIA Streamline—means carefully balancing mathematically pure light evaluation with highly efficient memory management.

Here is the exact architectural roadmap to implement these light types across your codebase.

### **Phase 1: Unifying the Core Light Data (src/light.h & src/light.cpp)**

Your CPU-side structures need to handle all light variants without ballooning in size. A bloated light struct will devastate performance when uploaded to the GPU constant buffers or structured buffers.

* **Create a Unified Enum:** Define LightType (e.g., LIGHT\_DIRECTIONAL, LIGHT\_OMNI, LIGHT\_SPOT, LIGHT\_AREA\_RECT, LIGHT\_AREA\_DISK).  
* **Struct Packing:** Build a single, tightly packed Light struct. Use a union for type-specific properties to save bytes.  
  * *Common:* Position, Emission (in Lumens/Candelas), Type.  
  * *Omni/Spot:* Direction, InnerConeAngle, OuterConeAngle, Radius (for soft shadow omnis).  
  * *Area:* Right, Up, Extents (width/height).  
  * *IES:* IESAtlasIndex (an integer pointing to the specific texture slice).

### **Phase 2: The IES Processing Pipeline (src/assets/ & src/dx12\_context.cpp)**

IES files cannot be mathematically evaluated in real-time; they must be sampled.

* **Parsing:** Implement a parser in asset\_loader.cpp to read .ies photometric web data. Extract the candela multipliers for the vertical and horizontal angles.  
* **Texture Generation:** Bake this parsed data into a floating-point 2D texture map. The X-axis represents the horizontal angle (0-360 degrees), and the Y-axis represents the vertical angle (0-180 degrees).  
* **Descriptor Heap Binding:** Upload all IES textures as a single Texture2DArray in dx12\_context.cpp or bind them to your bindless descriptor heap. This prevents context switching when evaluating different lights in the shaders.

### **Phase 3: The Lighting Mathematics (shaders/lights\_lib.hlsl)**

The math for evaluating these lights must be physically based and decoupled from the ray tracing itself. Create isolated evaluation functions for each light type:

* **EvalOmni():** Implement standard inverse-square falloff. If the radius is $\> 0$, clamp the distance to prevent infinite brightness at the center.  
* **EvalSpot():** Take the EvalOmni result and multiply it by a smoothstep function driven by the InnerConeAngle and OuterConeAngle relative to the light's forward vector.  
* **EvalIES():** Map the vector between the light and the shaded point to spherical coordinates (theta, phi). Use these to sample your IES Texture2DArray. Multiply the fetched value by the light's base emission.  
* **SampleAreaLight():** Pick a random 2D coordinate on the light's surface area. Return the specific sampled position, the normal of the light at that point, and the emission divided by the surface area.

### **Phase 4: ReSTIR Integration (shaders/restir\_lib.hlsl & shaders/restir\_spatial\_cs.hlsl)**

This is where the magic happens. Your ReSTIR compute shaders need to know how to calculate the unshadowed target weight ($p\_{hat}$) for these new complex lights.

* **Candidate Generation:** When a reservoir selects a light, switch on the LightType.  
* **Weight Calculation:** Calculate the luminance of the light source multiplied by the BRDF of the surface, but *incorporate the spotlight cone or IES texture lookup directly into the $p\_{hat}$ calculation*.  
* **Area Light Sampling:** For area lights, ensure the reservoir stores the specific *sampled point* on the light, not just the light ID. If you change the point on the area light during spatial reuse, the variance will explode.

### **Phase 5: DXR Visibility (shaders/raytracing/raygen.hlsl & hit.hlsl)**

Once the compute shader has settled on the best light candidates via ReSTIR, you cast your shadow rays.

* **Ray Setup:** The shadow ray direction is straightforward for spots, omnis, and IES (target point to light position). For area lights, the ray goes from the target point to the exact sampled coordinate on the light's surface saved in the reservoir.  
* **Opaque Flags:** Ensure that D3D12\_RAYTRACING\_GEOMETRY\_FLAG\_OPAQUE is rigorously applied to your architectural meshes. Evaluating AnyHit shaders while casting thousands of area light shadow rays will instantly bottleneck an RTX 3060\. Force the hardware to only check ClosestHit or Miss.

### **Phase 6: Denoising & Streamline Handoff (src/streamline\_manager.cpp)**

Area lights create inherent noise due to spatial sampling, and IES lights create high-frequency contrast patterns.

* **Do Not Pre-blur:** Feed the raw, noisy, physically accurate results of your ReSTIR output directly into the pipeline.  
* **Ray Reconstruction:** Ensure your G-Buffer (normals, depth, albedo, and specifically motion vectors) is perfectly aligned. Ray Reconstruction thrives on the raw noise generated by area lights and will naturally resolve the soft penumbras without you needing to write a custom spatial filter in your compute queue.

Tackle Phase 1 and Phase 3 first. Getting the C++ structs and the HLSL math aligned is the hardest part. Once the lights evaluate correctly on a single pixel, piping them into the ReSTIR reservoirs and DXR visibility checks becomes a matter of routing.

### **Phase 7 Ui Panel Implementation (src/editor_ui.cpp)**

create light panel, this panel wil be used to create a light, set light's position, set light's emission, set light's type, set light's direction, set light's inner/outer cone angle, set light's radius, set light's IES texture
also Imgizmo will be used to move the light, rotate and scale the light. also wil list the lits on the scene.

### ✨ Additional implementation notes & architectural tips
The roadmap above already hits every major phase. A few extras that will keep the system maintainable and debuggable:

* **CPU/GPU struct layout** – keep the `Light` struct tight (enum+union) and pad to 16/32 bytes. Add a small `uint userData` or `uint id` field for debug/material picking.
* **IES textures** – bake them offline (tool or script) into a Texture2DArray; generate mipmaps to avoid aliasing when sampling at steep angles.
* **Math library** – mirror every `Eval*()` function on the CPU for offline tests. Write simple unit‑test scenes to verify energy conservation before integrating into ReSTIR.
* **ReSTIR reservoir** – store sample point for area lights (64‑bit packed), and precompute any per-light pdf/visibility term so the reservoir update only needs a cheap multiplication.
* **DXR visibility rays** – mark static geometry `OPAQUE` aggressively; avoid AnyHit during the millions of shadow rays that area lights generate.
* **Denoiser handoff** – feed raw noisy results to your TAA/denoiser without pre‑blurring; ensure motion vectors accurately reflect any jitter you introduce.

These extras don’t change the order of the phases but smooth the implementation and future debugging. Geared towards an RTX‑centric renderer, they keep the pipeline efficient even before you add the fancy SER or light‑tree optimizations.
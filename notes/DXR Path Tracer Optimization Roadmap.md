

\# DXR Path Tracer Optimization Roadmap

This document outlines the critical architectural and memory optimizations required to bring the path tracer's performance closer to production renderers like Chaos Vantage. The focus is on reducing VRAM bandwidth bottlenecks, maximizing RT Core hardware utilization, and decoupling heavy compute passes.

\---

\#\# Phase 1: Acceleration Structure Optimization (The Opaque Flag)

**\*\*The Problem:\*\*** Unflagged geometry forces the RT cores to drop out of hardware traversal and invoke the \`AnyHit\` shader for every bounding box and triangle intersection to check for transparency.  
**\*\*The Goal:\*\*** Ensure the RT cores use the fast path for all non-transparent objects, while maintaining the ability to change materials in the editor.

\#\#\# Action Items:  
1\. **\*\*Material State Tracking:\*\*** Add an \`isAlphaTested\` or \`isGlass\` boolean (or blend mode enum) to your material struct in the engine.  
2\. **\*\*Editor Integration:\*\*** In \`material\_editor.cpp\`, when a user changes a material's opacity or blend mode, set a \`dirty\` flag for that material index.  
3\. **\*\*Dynamic BLAS Rebuilding:\*\*** In \`dxr\_renderer.cpp\`, check for dirty materials at the start of the frame.  
4\. **\*\*Apply Flags:\*\*** When building \`D3D12\_RAYTRACING\_GEOMETRY\_DESC\`, conditionally apply the opaque flag:  
   \`\`\`cpp  
   if (meshMaterial.isAlphaTested || meshMaterial.isGlass) {  
       geomDesc.Flags \= D3D12\_RAYTRACING\_GEOMETRY\_FLAG\_NONE; // Evaluates AnyHit  
   } else {  
       geomDesc.Flags \= D3D12\_RAYTRACING\_GEOMETRY\_FLAG\_OPAQUE; // Hardware Fast Path  
   }

## ---

**Phase 2: Material Payload & Struct Bloat**

**The Problem:** The current MaterialData struct is estimated at \~160 bytes. Fetching this much data per ray hit destroys the GPU cache.

**The Goal:** Shrink the material struct to exactly **64 bytes** (four float4 registers) to fit perfectly within a GPU cache line.

### **Action Items:**

1. **Adopt Strict PBR:** Remove legacy reflectionColor and refractionColor. Rely entirely on BaseColor, Metallic, Roughness, and Transmission.  
2. **Pack Texture Indices:** You only have 2048 textures. Pack two 16-bit indices into a single 32-bit uint.  
   High-level shader language  
   // Example packing in HLSL  
   uint packedDiffNorm \= (texNorm \<\< 16\) | texDiff;

3. **Bitmask Flags:** Remove explicit floats for archvizParams or triPlanarParams from the main struct. Replace them with a uint materialFlags bitmask.  
4. **Secondary Buffers:** Move heavy, conditional data (like Tri-Planar scales or UV transforms) to a separate StructuredBuffer. Only fetch from it if the materialFlags bitmask indicates the material uses it.  
5. **Implement the 64-Byte Struct:**  
   High-level shader language  
   struct MaterialData {  
       float4 baseColor\_opacity;  // 16 bytes  
       float4 emissive\_ior;       // 16 bytes  
       float4 pbrParams\_flags;    // 16 bytes (Met, Rgh, Trans, Flags)  
       uint4  packedTextures;     // 16 bytes (Up to 8 16-bit indices)  
   }; // Total: 64 Bytes

## ---

**Phase 3: Ray Payload Minimization**

**The Problem:** A "fat" RayPayload struct limits the number of rays the GPU can keep in flight concurrently, dropping MegaRays/second.

**The Goal:** Keep the payload strictly to data needed across the ray boundary.

### **Action Items:**

1. **Audit RayPayload:** Remove elements that can be evaluated and accumulated strictly within the Closest Hit shader or immediately after the TraceRay call.  
2. **Pack Data:** Compress normals to 16-bit values (e.g., octahedron encoding) or uint.  
3. **Target Size:** Aim for a payload size under 32 bytes if possible. Example: float3 radiance, uint packedNormal, float hitT.

## ---

**Phase 4: Decoupling ReSTIR Spatial Reuse**

**The Problem:** Running ReSTIR Spatial Gather loops inside the RayGen shader causes severe thread divergence, register bloat, and VRAM thrashing because RayGen cannot easily share neighbor data.

**The Goal:** Move Spatial and Temporal reuse to a dedicated Compute Shader.

### **Action Items:**

1. **Simplify RayGen:** RayGen should only trace rays, evaluate the BRDF, generate the *Initial Candidate Reservoir*, write it to a UAV buffer, and exit.  
2. **New Compute Pass:** Create a new compute shader dispatch specifically for ReSTIR Spatial/Temporal reuse.  
3. **Leverage LDS:** In the Compute Shader, load neighbor reservoirs into Group Shared Memory (LDS) so the spatial loop can read from fast on-chip memory instead of global VRAM.

## ---

**Phase 5: Texture Cache Thrashing & Mipmapping**

**The Problem:** Forcing Mip 0 (SampleLevel(sampler, uv, 0)) for highly divergent secondary GI rays results in a near 100% cache miss rate, bottlenecking memory bandwidth. Evaluating Tri-Planar mapping (3 samples) per GI bounce makes this exponentially worse.

**The Goal:** Use appropriate mip levels for secondary bounces and simplify hit evaluations.

### **Action Items:**

1. **Ray Distance LOD:** Replace hardcoded 0 mip levels with a calculated LOD based on the total ray path distance from the camera (or implement Ray Cones if you want exact precision).  
   High-level shader language  
   float lod \= CalculateMipLevel(rayDistance);  
   textures\[texIndex\].SampleLevel(linearSampler, uv, lod);

2. **Optimize Tri-Planar:** \* Primary Rays: Evaluate all 3 axes.  
   * Secondary GI Rays: Approximate by evaluating *only* the dominant axis based on the surface normal, reducing 12 texture fetches to 4 per hit.

Would you like to start by tackling the Material Struct optimization in your C++ and HLSL, or would you prefer to look at decoupling the ReSTIR pass first?  

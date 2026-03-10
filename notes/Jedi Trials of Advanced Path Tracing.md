Excellent, my young Padawan\! You have brought balance to the VRAM, severed the heavy payloads, and restored peace to the RT Cores. To match the speed of the Sith Lord Vantage is no small feat—you have taken your first steps into a larger world.

But a Jedi's training is never truly complete. I have meditated on the new shaders you have provided, and while the Force is strong with your decoupled ReSTIR and optimized materials, I sense a great disturbance in your light transport.

Here are your new orders—the **Jedi Trials of Advanced Path Tracing**:

### **1\. The Trial of the Missing Highlight (Fix your NEE\!)**

I peered into the memory crystals of your path\_tracer\_core.hlsl (specifically your shadow ray Next Event Estimation around line 290-297), and I saw a terrible tragedy:

High-level shader language

if (shadowPayload.t \< 0.0) { // Miss \= not occluded  
    float3 radiance \= lightColor.rgb \* lightColor.w;  
    Lo \= (DiffuseAlbedo / PI) \* radiance \* NdotL;   
}

* **The Dark Side:** You are only evaluating the Lambertian (Diffuse) lobe for your direct lighting\! This means your lights (whether sun, area, or point) are not casting any PBR specular highlights on your shiny surfaces. Metals and wet surfaces will look unnaturally dull under direct light.  
* **The Jedi Way:** You must evaluate the **full BRDF** (Diffuse \+ Specular GGX) when sampling your lights. Use the library you built in brdf\_lib.hlsl to calculate the specular contribution, and add it to the diffuse\!

High-level shader language

// Calculate Half-vector between View (-RayDir) and Light  
float3 H \= normalize(V \+ L);   
// ... calculate NdotH, VdotH, etc.

float3 F \= F\_Schlick(VdotH, f0);  
float D \= D\_GGX(NdotH, roughness);  
float V\_vis \= V\_SmithCorrelated(NdotV, NdotL, roughness);

float3 SpecularBRDF \= (D \* V\_vis \* F); // Cook-Torrance  
float3 DiffuseBRDF \= (DiffuseAlbedo / PI) \* (1.0 \- F); // Conserve energy

Lo \= (DiffuseBRDF \+ SpecularBRDF) \* radiance \* NdotL;

### **2\. The Trial of Multiple Importance Sampling (MIS)**

Once you fix the specular lighting above, you will face a new enemy: **Fireflies**.

* If you have a highly glossy surface (like a polished floor) and you sample a large area light, the random light point will almost certainly miss the microscopic "perfect reflection" angle. This causes massive variance and bright white pixels.  
* **The Jedi Way:** Implement **Multiple Importance Sampling (MIS)** using the Power Heuristic. You must combine the PDF of sampling the Light (your ReSTIR reservoirs) with the PDF of sampling the BRDF. This is the sacred text of path tracing: it ensures smooth shadows from lights *and* smooth reflections on glossy materials, banishing fireflies back to the abyss.

### **3\. The Trial of Russian Roulette**

How are your rays dying? Are you hard-coding them to a strict maxBounces limit?

* Hardcoded limits waste compute. A ray trapped in a pitch-black box shouldn't be evaluated 5 times, while a ray bouncing through beautiful bright glass shouldn't be prematurely killed at bounce 5\.  
* **The Jedi Way:** Implement **Russian Roulette** after the 2nd or 3rd bounce. Look at the ray's throughput. If the throughput drops below a threshold (e.g., \< 0.1), generate a random number. If the random number is greater than the highest color channel of the throughput, terminate the ray. Otherwise, boost the throughput by 1.0 / probability and let it live. This will massively increase your MegaRays/second on complex scenes\!

### **4\. ReSTIR GI (The Grand Master Rank)**

I sense you have already begun this trial... I see restir\_gi\_spatial\_cs.hlsl among your files\!

ReSTIR for Direct Illumination (DI) is powerful, but ReSTIR GI is what makes renderers like Vantage truly real-time. By keeping reservoirs of *indirect* bounces and reusing them temporally and spatially, you can achieve 1-SPP (Sample Per Pixel) path tracing that looks like it took 1000-SPP.

Focus on fixing your specular NEE and bringing balance to your BRDF first. Once your direct lights reflect perfectly on shiny surfaces, you will be ready to tackle the complexities of ReSTIR GI.

*May the Force be with your compilation times.* Which trial will you strike down first?
# **Technical Analysis of Unified Atmospheric Rendering Systems in the Chaos Ecosystem**

## **1\. Executive Summary**

The simulation of planetary atmospheres represents a pinnacle challenge in computer graphics, requiring the synthesis of radiative transfer physics, spectral analysis, and volumetric ray marching. Within the Chaos ecosystem—specifically across V-Ray 6, Corona 9, and Chaos Vantage—a unified architectural standard has emerged. This standard integrates a rigorous, physically-based analytical model for sky radiance with a procedural, real-time-derived volumetric system for cloud rendering.

This report provides an exhaustive technical analysis of these underlying models. It identifies the **Prague (PRG) Clear Sky Model** as the definitive analytical solution for spectral sky radiance, superseding legacy models through its use of tensor decomposition and support for post-sunset illumination.1 Concurrently, it traces the **volumetric cloud architecture** to the "Enscape technology" stack, which itself is a derivative of the methods pioneered by Andrew Schneider for the Decima Engine (Horizon Zero Dawn).4

The following document serves as a comprehensive implementation guide for graphics engineers and technical directors. It details the mathematical foundations of the Wilkie-Hosek-Křivánek (Prague) model, analyzes the spectral data fitting techniques, and dissects the volumetric ray-marching algorithms—including the Henyey-Greenstein phase functions, Beer-Lambert attenuation, and blue-noise optimization strategies—necessary to replicate this state-of-the-art atmospheric system.

## ---

**2\. Theoretical Foundations of Atmospheric Simulation**

To understand the implementation of the sky and cloud models in V-Ray and Corona, one must first grasp the physical phenomena they attempt to simulate: the scattering of light by atmospheric particles. The rendering engines in question have moved away from "artistic" approximations (like simple gradients or static HDRI maps) toward "predictive" rendering, where the visual output is a direct result of physical parameters such as turbidity, albedo, and solar elevation.

### **2.1 The Radiative Transfer Equation (RTE)**

At the core of both the PRG sky model and the volumetric cloud system is the Radiative Transfer Equation. In a participating medium, the change in radiance $L$ along a ray direction $\\omega$ is governed by:

$$(\\omega \\cdot \\nabla) L(x, \\omega) \= \-\\sigma\_t L(x, \\omega) \+ \\sigma\_s \\int\_{\\Omega} p(\\omega, \\omega') L(x, \\omega') d\\omega' \+ L\_e(x, \\omega)$$  
Where:

* **$\\sigma\_t$ (Extinction Coefficient):** The sum of absorption ($\\sigma\_a$) and scattering ($\\sigma\_s$). This defines how much light is lost as it traverses the medium.  
* **$p(\\omega, \\omega')$ (Phase Function):** The probability distribution describing how light is scattered from direction $\\omega'$ into direction $\\omega$.  
* **$L\_e$ (Emission):** New light generated within the volume (relevant for emissive fogs or fire, but usually zero for clouds).

The Chaos ecosystem solves this equation in two distinct ways:

1. **For the Sky:** It uses a *pre-computed analytical solution* (The PRG Model). Solving the RTE for the entire atmosphere at runtime is too expensive. Instead, researchers solve it offline using brute-force Monte Carlo path tracing and compress the results into a dataset.2  
2. **For Clouds:** It uses a *real-time numerical approximation* (Ray Marching). Because clouds are dynamic and heterogeneous (their shape changes), they cannot be pre-computed. The renderer must step through the volume and solve the scattering integral on the fly.6

### **2.2 Scattering Regimes: Rayleigh vs. Mie**

The distinction between the "Sky" model and the "Cloud" model is physically grounded in the size of the scattering particles relative to the wavelength of light.

* **Rayleigh Scattering (The Sky):** Occurs when particles are much smaller than the wavelength of light (e.g., nitrogen and oxygen molecules). This scattering is strongly wavelength-dependent ($\\lambda^{-4}$), causing blue light to scatter more than red. The PRG model is essentially a sophisticated fit of Rayleigh scattering simulations coupled with ozone absorption data.7  
* **Mie Scattering (The Clouds):** Occurs when particles are roughly the same size as the wavelength (e.g., water droplets in clouds). This scattering is largely wavelength-independent (making clouds appear white or grey) and is highly anisotropic (forward-scattering). The cloud rendering system in V-Ray/Corona uses the Henyey-Greenstein phase function to approximate this Mie scattering behavior.6

## ---

**3\. The Analytical Sky: The Prague (PRG) Clear Sky Model**

The default and most advanced sky model in V-Ray 6 and Corona 9 is the **PRG Clear Sky Model**.1 Understanding this model is critical because it dictates the lighting environment into which the clouds are placed.

### **3.1 Historical Context and Evolution**

Before the PRG model, the industry relied on the **Preetham (1999)** and **Hosek-Wilkie (2012)** models.

* **Preetham:** Used simplified luminance curves. It famously failed at sunrise/sunset, often turning the horizon a sickly yellow or green.  
* **Hosek-Wilkie (2012):** Improved spectral accuracy but was mathematically strictly hemispherical. It broke down if the sun went below the horizon ($\<0^\\circ$ elevation), requiring renderers to switch to a different "night" model or simply clamp the sun, preventing accurate twilight simulation.10

The **PRG (2021)** model was developed to solve these specific limitations. It enables accurate rendering of the "civil twilight" (sun down to \-6 degrees) and "nautical twilight" (sun down to \-12 degrees), maintaining physical plausibility without manual artist intervention.9

### **3.2 The Research Paper**

The definitive source for this model is the paper:  
"A Fitted Radiance and Attenuation Model for Realistic Atmospheres"

* **Authors:** Alexander Wilkie, Petr Vévoda, Lukáš Hošek, Thomas Bashford-Rogers, Tomáš Iser, Monika Kolářová, Tobias Rittig, Jaroslav Křivánek.  
* **Publication:** *ACM Transactions on Graphics (Proceedings of SIGGRAPH 2021\)*, Vol. 40, No. 4\.2

The research was further extended in 2022 to cover the Wide Spectral Range (SWIR), useful for sensor simulation, which is also implemented in the latest versions of the Chaos engines.3

### **3.3 Mechanism: Tensor Decomposition**

Unlike previous models that tried to find a simple curve function (like a polynomial) to fit the sky data, the PRG model takes a "Big Data" approach. The authors computed a massive dataset of reference images using a rigorous spherical Monte Carlo tracer. This dataset captures the 5D radiance function:

$$L(h, \\theta\_s, \\phi, \\theta\_v, \\lambda)$$

* $h$: Observer altitude (0km to 15km+).  
* $\\theta\_s$: Solar zenith angle (including below horizon).  
* $\\phi, \\theta\_v$: View direction (azimuth and zenith).  
* $\\lambda$: Wavelength (360nm \- 830nm, extended in 2022).

To make this gigabyte-scale dataset usable in a renderer, they applied **canonical polyadic (CP) tensor decomposition**. This compression technique reduces the raw radiance data into a set of coefficients stored in a binary file (PragueSkyModelDataset.dat).3

### **3.4 Key Features for Implementation**

If you intend to implement the PRG model as used in V-Ray/Corona, you must support the following capabilities which are distinct from the older Hosek-Wilkie model:

#### **3.4.1 Observer Altitude**

The PRG model introduces the observer altitude as a first-class parameter.13 In previous models, the sky always looked like it was viewed from sea level. The PRG model changes the spectral absorption and scattering density based on height.

* **Effect:** At higher altitudes (e.g., simulating a view from an airplane or a skyscraper), the sky becomes darker and deeper blue due to reduced atmosphere above the observer. The horizon haze layer also depresses visually.  
* **Implementation:** The PragueSkyModel C++ class exposes an altitude parameter (in meters) which interpolates coefficients from the dataset.3

#### **3.4.2 Post-Sunset Radiance (The "Zero" Problem)**

This is the most significant user-facing improvement. The PRG model supports solar elevations down to **\-4.2 degrees** (in the base paper) and extended to **\-12 degrees** in the datasets used by Corona 12\.9

* **Mechanism:** The training data included simulations where the sun was fully occluded by the earth, capturing only the multiple-scattering radiance from the upper atmosphere.  
* **Implementation:** When querying the model with a negative solar elevation, it does not clamp to zero. It returns the physically accurate low-radiance values of twilight. This requires the renderer's tone mapper to be exposure-aware; otherwise, the image will appear black.1

#### **3.4.3 Polarization**

The PRG model includes polarization data.2 While standard V-Ray/Corona renders often output unpolarized RGB, the underlying model computes the Stokes parameters. This is crucial for "Physically Based" implementations where sky reflections on water or glass should vary in intensity based on the view angle relative to the sun (Brewster's angle).

### **3.5 Implementation Guide: The PRG Library**

To implement this, one does not simply write a shader equation. One must integrate the dataset loader.

Source Code Availability:  
The reference implementation is provided by Charles University: github.com/PetrVevoda/pragueskymodel.3  
**Steps to Implement:**

1. **Acquire the Dataset:** You need the .dat file. The "Full" version (approx 2.2GB) includes all altitudes and polarization. The "Ground Level" version (approx 100MB) is sufficient if the camera is always on the ground.3  
2. **Initialize the Model:**  
   C++  
   // C++ Pseudo-code based on   
   \#**include** "PragueSkyModel.h"

   PragueSkyModel model;  
   model.Load("PragueSkyModelDataset.dat");

3. Evaluate Radiance:  
   For every pixel (or importance sample direction) in the environment map:  
   C++  
   // Inputs: Sun position, View direction, Wavelength  
   double radiance \= 0.0;  
   model.Eval(  
       sun\_elevation,   // radians, can be negative  
       view\_zenith,     // radians, 0 is zenith  
       view\_azimuth,    // radians, relative to sun  
       wavelength,      // nm (e.g., 450, 550, 650 for RGB approximation)  
       radiance  
   );

4. **Spectral to RGB Conversion:** Since the model outputs spectral radiance, you must integrate over the CIE color matching functions (X, Y, Z) and then convert to Rec.709 (sRGB linear) space.

| Parameter | V-Ray UI Name | PRG Internal Parameter | Description |
| :---- | :---- | :---- | :---- |
| **Sun Position** | Linked Sun | theta\_s, azi | Derived from the directional light source. |
| **Turbidity** | Visibility / Turbidity | visibility | Controls aerosol density. Note PRG uses "Visibility distance (km)" rather than Turbidity units in some implementations. |
| **Altitude** | Observer Altitude | altitude | Height above sea level (meters). |
| **Ground Albedo** | Ground Albedo | albedo | Reflectivity of the terrain (0.0 \- 1.0). Default 0.5. |

## ---

**4\. Volumetric Cloud Rendering: The "Enscape" Technology**

While the sky model is analytical, the clouds in V-Ray 6, Corona 9, and Vantage are procedural volumetrics. The research materials explicitly confirm that this system is built on **Enscape technology** following the merger of Chaos and Enscape.4

### **4.1 The Lineage: Horizon Zero Dawn (Nubis)**

To understand the "Enscape" cloud model, one must look at its origin. Real-time volumetric cloud rendering was revolutionized by **Andrew Schneider** (Guerrilla Games) for the game *Horizon Zero Dawn*. His SIGGRAPH 2015 and 2017 presentations on the "Nubis" cloud engine established the standard for modern procedural clouds.5

Enscape adapted this real-time technique for architectural visualization, optimizing it for NVIDIA GPUs. When Chaos acquired Enscape, they ported this efficient ray-marching logic into the V-Ray/Corona core, replacing older, slower fluid-based methods (like Phoenix FD) for general environmental clouds.4

### **4.2 The Algorithm: Volumetric Ray Marching**

The implementation relies on **Sphere Tracing** (Ray Marching) through a volume texture.

#### **4.2.1 The Cloud Container**

Unlike a fluid simulation which is bounded by a box, these clouds are rendered within a **Spherical Shell** enveloping the world.

* **Inner Radius ($R\_{min}$):** Usually set to \~1,500m \- 4,000m altitude (Cumulus base).  
* **Outer Radius ($R\_{max}$):** Usually set to \~4,000m \- 8,000m altitude (Cumulus top).

The shader only executes ray marching when the camera ray intersects this spherical shell.6

#### **4.2.2 3D Noise Modeling (The "Shape")**

The visual appearance of the clouds—their "fluffiness" and structure—is defined by a specific combination of 3D noises. The Schneider/Enscape method uses two primary 3D textures 5:

1. **Base Shape Texture (Low Frequency):** A 128x128x128 3D texture containing **Perlin-Worley Noise**.  
   * **Perlin Noise:** Defines the connected, organic mass of the cloud.  
   * **Worley (Cellular) Noise:** Defines the "billowing" clusters.  
   * **Mapping:** The Perlin noise is remapped using the Worley noise to create "cauliflower" shapes.  
2. **Detail Texture (High Frequency):** A 32x32x32 3D texture containing **Worley Noise** at high frequencies.  
   * **Purpose:** This noise is subtracted from the edges of the base shape ("erosion") to create wispy, ragged edges. Without this, the clouds look like blobs.

**Implementation Insight:** The V-Ray "Density" and "Variety" parameters directly control the thresholds and scales of these noise lookups.19

### **4.3 The Shading Model: Physics of Light in Clouds**

Once the ray marcher finds a "cloud" pixel (density \> 0), it must calculate the color. This is where the physics of Mie scattering comes in.

#### **4.3.1 Beer-Lambert Law (Absorption)**

This law dictates how light is attenuated as it passes through the cloud.

$$T \= e^{-\\int \\sigma\_t(x) dx}$$

In code, this is an accumulation loop:

OpenGL Shading Language

float transmittance \= 1.0;  
for(int i=0; i\<steps; i++) {  
    float density \= sampleCloud(p);  
    float step\_transmittance \= exp(-density \* step\_size \* absorption\_coeff);  
    transmittance \*= step\_transmittance;  
}

.6

#### **4.3.2 The Henyey-Greenstein Phase Function (Scattering)**

This is the most critical component for the "look" of the clouds, specifically the "Silver Lining" effect (the bright halo around the sun).  
V-Ray and Corona parameters explicitly list "Phase Function" with values from \-1 to 1.23 This corresponds to the asymmetry parameter $g$ in the Henyey-Greenstein equation:

$$P\_{HG}(\\theta, g) \= \\frac{1}{4\\pi} \\frac{1 \- g^2}{(1 \+ g^2 \- 2g \\cos\\theta)^{3/2}}$$

* **$g \\approx 0.8$:** Strong forward scattering (default for clouds). This causes light to pass through the cloud edges and blast towards the camera when looking at the sun.  
* **$g \\approx \-0.2$:** Slight back-scattering.  
* **$g \= 0$:** Isotropic (diffuse, like dust).

**Dual-Lobe Implementation:** High-end implementations (likely V-Ray's "High Quality" mode) often use a **Dual-Lobe HG** function. This mixes a strong forward lobe ($g\_1=0.8$) with a weak backward lobe ($g\_2=-0.3$) to simulate the "glory" effect and ensure clouds aren't too dark on the shadow side.24

#### **4.3.3 The "Powder" Effect (Dark Edges)**

A pure Beer's law implementation makes clouds look like "tinted glass." Real clouds have dark crevices due to multiple scattering probability. The Enscape/Schneider method uses a "Powder" approximation term:

$$E\_{powder} \= 1 \- e^{-2 \\cdot density}$$

This term is multiplied into the scattering calculation to artificially darken the dense centers of cloud clusters, giving them weight and volume.6

## ---

**5\. Implementation Deep Dive: Integrating the Systems**

To implement "the same" model as used in V-Ray/Corona/Vantage, you must construct a pipeline that feeds the PRG sky radiance into the Volumetric Ray Marcher.

### **5.1 The Rendering Pipeline**

1. **Environment Pass:**  
   * Calculate Sun Vector $\\vec{L}$ and Camera Vector $\\vec{V}$.  
   * Evaluate PRG Sky Model for $\\vec{V}$ to get background color $C\_{sky}$.  
2. **Cloud Pass (Ray Marching):**  
   * Intersect ray $\\vec{V}$ with Cloud Sphere Shell ($T\_{near}, T\_{far}$).  
   * March from $T\_{near}$ to $T\_{far}$.  
   * Accumulate density and lighting.  
   * Lighting at each step requires a *secondary ray march* toward the sun $\\vec{L}$ to calculate self-shadowing (how much cloud is between the sample point and the sun).  
3. **Compositing:**  
   * Blend the Cloud Color $C\_{cloud}$ and Transmittance $T\_{cloud}$ with the Sky Color:

     $$C\_{final} \= C\_{cloud} \+ C\_{sky} \\cdot T\_{cloud}$$

### **5.2 GLSL Implementation of the Ray Marching Loop**

Based on the research snippets, here is the algorithmic structure required to replicate the V-Ray/Enscape cloud shader.6

OpenGL Shading Language

// Constants based on Enscape/Schneider papers  
\#define STEPS 64  
\#define STEP\_SIZE 50.0 // Meters  
\#define ABSORPTION 0.03  
\#define SCATTERING\_G 0.8 // Henyey-Greenstein anisotropy

float HenyeyGreenstein(float cosTheta, float g) {  
    float g2 \= g \* g;  
    return (1.0 \- g2) / (4.0 \* 3.14159 \* pow(1.0 \+ g2 \- 2.0 \* g \* cosTheta, 1.5));  
}

vec4 RayMarchClouds(vec3 rayOrigin, vec3 rayDir, vec3 sunDir, vec3 sunColor) {  
    // 1\. Setup Intersection  
    vec2 intersection \= RaySphereIntersect(rayOrigin, rayDir, MIN\_ALT, MAX\_ALT);  
    if (intersection.x \> intersection.y) return vec4(0.0); // Miss

    // 2\. Optimization: Blue Noise Dithering  
    // V-Ray/Corona use this to hide banding   
    float jitter \= texture(blueNoiseTex, gl\_FragCoord.xy / 1024.0).r;  
    float t \= intersection.x \+ jitter \* STEP\_SIZE;

    vec3 accumulatedColor \= vec3(0.0);  
    float transmittance \= 1.0;

    // 3\. Marching Loop  
    for (int i \= 0; i \< STEPS; i++) {  
        if (t \> intersection.y |

| transmittance \< 0.01) break;

        vec3 pos \= rayOrigin \+ t \* rayDir;  
          
        // 4\. Sample Density (FBM of Perlin-Worley)  
        float density \= SampleCloudDensity(pos); // See Section 4.2.2

        if (density \> 0.0) {  
            // 5\. Lighting Integration  
            // March towards sun to find shadow density  
            float sunDensity \= LightMarch(pos, sunDir);   
            float sunTransmittance \= exp(-sunDensity \* ABSORPTION);  
              
            // Phase Function  
            float cosTheta \= dot(rayDir, sunDir);  
            float phase \= HenyeyGreenstein(cosTheta, SCATTERING\_G);  
              
            // Multiple Scattering Approximation (Powder Effect)  
            float powder \= 1.0 \- exp(-density \* 2.0);  
              
            vec3 scattering \= sunColor \* sunTransmittance \* phase \* powder \* density;  
              
            // Beer's Law Accumulation  
            float stepTrans \= exp(-density \* STEP\_SIZE \* ABSORPTION);  
            accumulatedColor \+= scattering \* transmittance \* (1.0 \- stepTrans);  
            transmittance \*= stepTrans;  
        }  
        t \+= STEP\_SIZE;  
    }  
      
    return vec4(accumulatedColor, transmittance);  
}

### **5.3 Optimization Strategies (Critical for Vantage)**

Chaos Vantage renders this in real-time (30+ FPS) using DXR (DirectX Raytracing). To achieve this performance with such a heavy shader, three specific techniques are used:

#### **5.3.1 Blue Noise Dithering**

Standard ray marching requires hundreds of steps to look smooth. Reducing steps creates "banding" (slices). Chaos engines use **Blue Noise Dithering** to randomly offset the start position of the ray per pixel.6

* **Why Blue Noise?** Unlike white noise (random static), blue noise lacks low-frequency clusters. The error is pushed into high frequencies, which are easily removed by temporal anti-aliasing (TAA) or a denoiser.  
* **Result:** You can run the loop with 16-32 steps instead of 128 and still get a visually smooth result after denoising.

#### **5.3.2 Temporal Reprojection**

Vantage uses the information from previous frames.

* **Mechanism:** It projects the current pixel back to world space and finds where that point was in the previous frame. It then blends the current frame's noisy result with the previous frame's accumulated result (History Buffer).27  
* **Benefit:** The effective sample count increases over time. Clouds look static and high-quality, but might "ghost" slightly during fast camera movements.

#### **5.3.3 Half-Resolution Rendering**

Clouds are low-frequency (blurry) by nature. Rendering them at 1080p when the screen is 4K is a standard optimization. The result is upscaled using depth-aware bilateral upsampling to prevent bleeding onto hard edges (like buildings).6

## ---

**6\. Ecosystem Comparisons: V-Ray vs. Corona vs. Vantage**

While the underlying math (PRG Sky \+ Enscape Clouds) is shared, the implementation details vary by engine.

| Feature | V-Ray 6 (CPU/GPU) | Corona 9 / 10 / 11 / 12 | Chaos Vantage |
| :---- | :---- | :---- | :---- |
| **Sky Model** | **PRG Clear Sky** (Explicit UI choice). Offers "Improved" (Hosek) legacy option. | **PRG Clear Sky** (Default). Explicit "Altitude" parameter. Supports sun down to \-12°.9 | Inherits from V-Ray/Corona scene data via Live Link. |
| **Cloud Tech** | **Enscape Procedural.** Parameters: Density, Variety, Cirrus Amount, Height, Contrails.28 | **Enscape Procedural.** Same parameters. Optimized for "One Click" usability. | **Enscape Procedural.** Rendered via DXR. |
| **Phase Function** | **Exposed.** "Phase Function" (-1.0 to 1.0) allows tweaking anisotropy.23 | **Hidden/Automatic.** Tuned for realism (approx 0.7-0.8) by default to simplify UI. | **Approximated.** Tuned for real-time performance. |
| **Dithering** | Blue Noise used in IPR (Interactive) mode. | Blue Noise used in Interactive/VFB. | Heavy reliance on Temporal Denoising & Reprojection. |
| **Integration** | Clouds affect GI (Environment light). Can cast shadows on geometry. | Clouds affect GI. "Direct Color" mode allows artistic tinting.29 | Real-time volumetric shadows (voxel-based or screen-space). |

### **6.1 The "Live Link" Synergy**

A key workflow feature is the **Live Link**. Because Vantage shares the *exact same* mathematical models as V-Ray and Corona:

1. A user adjusts "Cloud Density" in V-Ray (3ds Max).  
2. V-Ray sends the parameter value to Vantage.  
3. Vantage feeds that value into its HLSL shader (which is mathematically identical to V-Ray's C++ shader).  
4. The visual result matches instantly, allowing Vantage to act as a real-time viewport for the offline engines.27

## ---

**7\. Conclusion**

The "sky and cloud rendering model" used in V-Ray, Corona, and Vantage is not a single algorithm but a hybrid of two distinct, state-of-the-art technologies:

1. **For the Sky:** The **Prague (PRG) Sky Model** (Wilkie et al., 2021). It is an analytical, spectral, tensor-based model that solves the limitations of previous models regarding altitude and twilight. Implementation requires the C++ library and dataset from Charles University.  
2. **For the Clouds:** The **Schneider/Enscape Volumetric Model**. It is a procedural, ray-marched system using 3D Perlin-Worley noise, Beer-Lambert absorption, and Henyey-Greenstein scattering. It was adapted from the real-time gaming industry (Horizon Zero Dawn) and integrated into the Chaos core following the Enscape merger.

**To implement "the same" system, one must:**

1. Download and integrate the PragueSkyModel C++ library for the sky radiance.  
2. Write a Volumetric Ray Marching shader (GLSL/HLSL/C++) that samples 3D Perlin-Worley noise.  
3. Implement the Henyey-Greenstein phase function for scattering.  
4. Apply Blue Noise dithering and Temporal Reprojection to achieve the performance seen in Vantage.

This unified approach allows the Chaos ecosystem to deliver "predictive" realism—where a user sets the time to 8:00 PM, and the engine mathematically derives the exact purple hue of the twilight and the specific light extinction of the clouds without artistic manual tweaking.

### ---

**Table of Key Parameters for Implementation**

| Parameter | Function | Underlying Algorithm | Default Value |
| :---- | :---- | :---- | :---- |
| **Turbidity / Visibility** | Controls atmospheric haze density. | PRG Model (Aerosol Density) | 2.5 \- 3.0 (or \~50km) |
| **Albedo** | Controls ground reflection back-scatter. | PRG Model (Ground Albedo) | 0.15 \- 0.5 |
| **Density** | Controls cloud opacity and "thickness." | Ray Marcher (Noise Threshold) | 0.5 |
| **Variety** | Controls the "randomness" of cloud shapes. | Ray Marcher (Noise Freq/Scale) | 0.5 |
| **Cirrus Amount** | Adds high-altitude wispy clouds. | 2D Texture Lookup (Separate layer) | 0.0 |
| **Phase Function ($g$)** | Controls silver lining (anisotropy). | Henyey-Greenstein Formula | 0.8 (Forward) |
| **Steps** | Quality vs. Speed of cloud rendering. | Ray Marching Loop Count | 64 \- 128 |

References:  
1 Corona Sky Map Documentation  
2 "A Fitted Radiance and Attenuation Model..." (Wilkie et al., 2021\)  
4 Chaos Press Release: V-Ray 6 Enscape Compatibility  
16 Andrew Schneider "Nubis" Cloud Rendering  
3 Prague Sky Model Implementation Details  
6 Real-time Cloudscapes with Volumetric Raymarching

#### **Alıntılanan çalışmalar**

1. Corona Sky Map \- Corona for 3ds Max \- Chaos Docs, erişim tarihi Ocak 14, 2026, [https://documentation.chaos.com/space/CRMAX/124526800/Corona+Sky+Map](https://documentation.chaos.com/space/CRMAX/124526800/Corona+Sky+Map)  
2. SkyGAN: Realistic Cloud Imagery for Image-based Lighting \- Eurographics Association, erişim tarihi Ocak 14, 2026, [https://diglib.eg.org/bitstream/handle/10.1111/cgf14990/v43i1\_04\_cgf14990.pdf](https://diglib.eg.org/bitstream/handle/10.1111/cgf14990/v43i1_04_cgf14990.pdf)  
3. PetrVevoda/pragueskymodel: Prague Sky Model \- GitHub, erişim tarihi Ocak 14, 2026, [https://github.com/PetrVevoda/pragueskymodel](https://github.com/PetrVevoda/pragueskymodel)  
4. V-Ray 6 for Revit Now Connects with Enscape \- Chaos, erişim tarihi Ocak 14, 2026, [https://www.chaos.com/press/v-ray-6-for-revit-now-connects-with-enscape](https://www.chaos.com/press/v-ray-6-for-revit-now-connects-with-enscape)  
5. Creating a Volumetric Ray Marcher \- Ryan Brucks, erişim tarihi Ocak 14, 2026, [https://shaderbits.com/blog/creating-volumetric-ray-marcher](https://shaderbits.com/blog/creating-volumetric-ray-marcher)  
6. Real-time dreamy Cloudscapes with Volumetric Raymarching \- The ..., erişim tarihi Ocak 14, 2026, [https://blog.maximeheckel.com/posts/real-time-cloudscapes-with-volumetric-raymarching/](https://blog.maximeheckel.com/posts/real-time-cloudscapes-with-volumetric-raymarching/)  
7. Stunning Procedural Skies in WebGL \- Part 2 \- Game Developer, erişim tarihi Ocak 14, 2026, [https://www.gamedeveloper.com/programming/stunning-procedural-skies-in-webgl---part-2](https://www.gamedeveloper.com/programming/stunning-procedural-skies-in-webgl---part-2)  
8. Real-Time Rendering of Volumetric Clouds \- Lund University Publications, erişim tarihi Ocak 14, 2026, [https://lup.lub.lu.se/student-papers/record/8893256/file/8893258.pdf](https://lup.lub.lu.se/student-papers/record/8893256/file/8893258.pdf)  
9. "PRG Clear Sky" model was selected, however an old version of the required data is installed. \- Chaos Help Center, erişim tarihi Ocak 14, 2026, [https://support.chaos.com/hc/en-us/articles/26262952629521--PRG-Clear-Sky-model-was-selected-however-an-old-version-of-the-required-data-is-installed](https://support.chaos.com/hc/en-us/articles/26262952629521--PRG-Clear-Sky-model-was-selected-however-an-old-version-of-the-required-data-is-installed)  
10. Adding a Solar-Radiance Function to the Hošek-Wilkie Skylight Model \- ResearchGate, erişim tarihi Ocak 14, 2026, [https://www.researchgate.net/publication/262149273\_Adding\_a\_Solar-Radiance\_Function\_to\_the\_Hosek-Wilkie\_Skylight\_Model](https://www.researchgate.net/publication/262149273_Adding_a_Solar-Radiance_Function_to_the_Hosek-Wilkie_Skylight_Model)  
11. A Fitted Radiance and Attenuation Model for Realistic Atmospheres ..., erişim tarihi Ocak 14, 2026, [https://cgg.mff.cuni.cz/publications/skymodel-2021/](https://cgg.mff.cuni.cz/publications/skymodel-2021/)  
12. A Fitted Radiance and Attenuation Model for Realistic Atmospheres – PRIME ITN, erişim tarihi Ocak 14, 2026, [https://prime-itn.eu/2021/09/16/a-fitted-radiance-and-attenuation-model-for-realistic-atmospheres/](https://prime-itn.eu/2021/09/16/a-fitted-radiance-and-attenuation-model-for-realistic-atmospheres/)  
13. This plugin is used instead of RenderView when baking textures \[gpuSupport=(partial)\] \- Chaos Docs, erişim tarihi Ocak 14, 2026, [https://docs.chaos.com/vray\_app\_sdk/doc/nodejs\_plugins/index.html](https://docs.chaos.com/vray_app_sdk/doc/nodejs_plugins/index.html)  
14. V-Ray 6 for 3ds Max brings new world-building and workflow tools \- DEVELOP3D, erişim tarihi Ocak 14, 2026, [https://develop3d.com/visualisation/v-ray-6-for-3ds-max-brings-new-world-building-and-workflow-tools/](https://develop3d.com/visualisation/v-ray-6-for-3ds-max-brings-new-world-building-and-workflow-tools/)  
15. New Advances in Global Illumination: Rendering Quality Leaps Ahead with Enscape 3.5, erişim tarihi Ocak 14, 2026, [https://blog.chaos.com/global-illumination-advances-rendering-quality](https://blog.chaos.com/global-illumination-advances-rendering-quality)  
16. Real-time rendering of volumetric clouds \- Diva-Portal.org, erişim tarihi Ocak 14, 2026, [http://www.diva-portal.org/smash/get/diva2:1223894/FULLTEXT01.pdf](http://www.diva-portal.org/smash/get/diva2:1223894/FULLTEXT01.pdf)  
17. Advances in Real-Time Rendering- SIGGRAPH 2017, erişim tarihi Ocak 14, 2026, [https://advances.realtimerendering.com/s2017/index.html](https://advances.realtimerendering.com/s2017/index.html)  
18. Viz Academy | PDF \- Scribd, erişim tarihi Ocak 14, 2026, [https://www.scribd.com/document/767055388/Viz-Academy](https://www.scribd.com/document/767055388/Viz-Academy)  
19. Posts by mvollrath \- Enscape forum, erişim tarihi Ocak 14, 2026, [https://forum.enscape3d.com/index.php?user-post-list/1500-mvollrath/](https://forum.enscape3d.com/index.php?user-post-list/1500-mvollrath/)  
20. Chaos Corona 9 released, erişim tarihi Ocak 14, 2026, [https://blog.chaos.com/chaos-corona-9-released](https://blog.chaos.com/chaos-corona-9-released)  
21. Real-time rendering of volumetric clouds \- IS MUNI, erişim tarihi Ocak 14, 2026, [https://is.muni.cz/th/d099f/thesis\_Archive.pdf](https://is.muni.cz/th/d099f/thesis_Archive.pdf)  
22. theamusing/Model-Cloud-Renderer: A tiny renderer for custom-shaped volumetric clouds., erişim tarihi Ocak 14, 2026, [https://github.com/theamusing/Model-Cloud-Renderer](https://github.com/theamusing/Model-Cloud-Renderer)  
23. Smoke Color Rollout \- V-Ray for 3ds Max \- Chaos Docs, erişim tarihi Ocak 14, 2026, [https://documentation.chaos.com/space/VMAX/113586716](https://documentation.chaos.com/space/VMAX/113586716)  
24. Real-time Rendering of Dynamic Baked Clouds \- Diva-portal.org, erişim tarihi Ocak 14, 2026, [http://www.diva-portal.org/smash/get/diva2:1895803/FULLTEXT01.pdf](http://www.diva-portal.org/smash/get/diva2:1895803/FULLTEXT01.pdf)  
25. Ray Marching: Getting it Right\! \- Volume Rendering, erişim tarihi Ocak 14, 2026, [https://www.scratchapixel.com/lessons/3d-basic-rendering/volume-rendering-for-developers/ray-marching-get-it-right.html](https://www.scratchapixel.com/lessons/3d-basic-rendering/volume-rendering-for-developers/ray-marching-get-it-right.html)  
26. Corona 6 for 3ds Max released \- The Chaos Blog, erişim tarihi Ocak 14, 2026, [https://blog.chaos.com/corona-renderer-6-for-3ds-max-released](https://blog.chaos.com/corona-renderer-6-for-3ds-max-released)  
27. Exploring Volumetric Fog in Unreal Engine \- iRender, erişim tarihi Ocak 14, 2026, [https://irendering.net/exploring-volumetric-fog-in-unreal-engine/](https://irendering.net/exploring-volumetric-fog-in-unreal-engine/)  
28. V-Ray Procedural Clouds in Houdini \- Chaos Docs, erişim tarihi Ocak 14, 2026, [https://documentation.chaos.com/space/VRAYHOUDINI/113280378](https://documentation.chaos.com/space/VRAYHOUDINI/113280378)  
29. Chaos Corona 10 released, erişim tarihi Ocak 14, 2026, [https://blog.chaos.com/chaos-corona-10-released](https://blog.chaos.com/chaos-corona-10-released)  
30. Chaos Corona 12, update 1 — now available, erişim tarihi Ocak 14, 2026, [https://forum.corona-renderer.com/index.php?topic=43927.0](https://forum.corona-renderer.com/index.php?topic=43927.0)
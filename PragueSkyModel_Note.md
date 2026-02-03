# Prague Sky Model Integration Notes

The user referenced the [Prague Sky Model (PetrVevoda/pragueskymodel)](https://github.com/PetrVevoda/pragueskymodel), which corresponds to the "Fitted radiance and attenuation model for analytical atmospheres" (2021).

## Current Status
- The renderer currently uses a simplified **Procedural Gradient + Raymarched Clouds** model.
- The "Black Sky" issue in Raster mode was caused by a **Constant Buffer alignment mismatch** between C++ (`CameraCB`) and HLSL (`skybox.hlsl`), causing the shader to read `skyBoxType = 0` (HDRI mode) instead of `1` (Procedural), and failing to sample the disabled HDRI texture. This has been fixed.

## Integration Path for Prague Sky Model
To fully implement the Prague Sky Model, the following steps are required (cannot be done purely in shader without data):

1.  **Dataset Requirement**: The model is **data-driven** and requires `PragueSkyModelDataset.dat` (approx. 40MB). This file must be loaded by the C++ application.
2.  **CPU Precomputation**: A C++ class must load the dataset and evaluate the coefficients for the current parameters (Sun Elevation, Visibility, Albedo) for the R, G, and B wavelengths.
3.  **Coefficient Transfer**: The resulting coefficients (Tensor composition matrices) should be passed to the GPU in a structured buffer or constant buffer.
4.  **Shader Evaluation**: Permform the tensor reconstruction in `atmosphere_clouds.hlsli` using the passed coefficients.

### Alternative (Recommended for Real-Time)
If the full dataset integration is too heavy, consider upgrading the current gradient model to a **Single-Scattering Rayleigh/Mie (Nishita)** model, which is fully analytic and requires no external data, providing physically plausible results for real-time rendering.

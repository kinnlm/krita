# Painterly PBR Light – Developer Notes

## Layer color-space defaults
- `KisMaterialGroupLayer::colorSpaceForChannel()` wires each material channel to the expected profile: BaseColor uses `KoColorSpaceRegistry::rgb8()` for sRGB painting, Normal uses float16 RGBA, and Height/Roughness/Metallic share float16 GrayA (see `createChannelLayerTemplate()` for instantiation and `validationIssues()` for sanity checks).

## Normal conventions
- Brush routing writes tangent-space normals with a +Y (OpenGL) convention in `encodeNormal()`/`rnmBlend()` so the green channel follows painter stroke direction. The exporter keeps this orientation by default and exposes `_flip_normal_green()` to opt into DirectX-style −Y packing when needed.

## Extending height & normal blending
- The dab routing loop in `KisPainterlyPbrRouter::applyDabs()` provides a single `baseHeight` computation and LERP accumulation per-pixel; extend this block for zen-rake/carve profiles.
- RNM strength currently follows `normalStrength()`; adjust the gradient accumulation and `ensureNormalized` path inside the same routine when introducing richer sculpt responses.

## Future work
- Track TODOs for 3D projection workflows (e.g. camera/lighting assisted previews) and UDIM-aware exports around `_export_material_group()`/`_export_channel_layer()` so they can hook into layer duplication and filename packing.

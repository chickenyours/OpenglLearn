UE 5.7 original blue-noise texture source
========================================

These are decoded copies of the local engine's TextureSource data, not generated
noise. Source package paths, SHA-256 values, compressed-buffer offsets, output
SHA-256 values and decoder details are recorded in provenance.json.

Runtime files:
- FastBlueNoise_scalar_128x128x64.r8: 128x8192, unsigned8 scalar, linear.
- FastBlueNoise_vec2_128x128x64.rgba8: 128x8192, unsigned8 RGBA, linear;
  original TSF_BGRA8 source was losslessly reordered to RGBA.
- PNG files re-encode the same decoded texels for inspection. They are not the
  import-time PNG byte streams and are not used to load runtime textures.

Engine source of truth:
- Config/BaseEngine.ini:162,164: the two original engine texture assets.
- Shaders/Private/BlueNoise.ush:128x128x64 wrapping and atlas layout.
- Source/Runtime/Core/Private/Compression/CompressedBuffer.cpp:49-77:
  big-endian compressed-buffer header; header CRC checked before reading.
- Source/Runtime/ImageCore/Private/ImageCoreDelta.cpp:15-20,42-93,284-355,398-414:
  32768-pixel cuts, 256 rows at width128, first row unchanged, uint8 vertical
  byte delta without bias for G8 and BGRA8. Inverse decoding was verified by
  byte-exact forward re-encoding over the complete source delta buffer.
- The engine's bundled oo2core_9_win64.dll decoded the vec2 Oodle blocks; each
  block's exact decoded byte count was checked. The scalar payload is stored
  uncompressed. Source dimensions are 128x8192, not the 4x256 asset thumbnail.

The extracted vec2 blue and alpha channels were independently found constant
0 and255 respectively across all1048576 texels after source delta decoding.
No GPU work or Unreal editor launch was required. The extraction script is
build/extract_ue57_blue_noise.py.

AO port source parameters
-------------------------
BaseScalability.ini [GlobalIlluminationQuality@2] and
LumenScreenSpaceBentNormal.cpp:104-133 select half-resolution scalar horizon AO,
2 slices,3 steps per slice,both sides,foreground fraction0.3 and High power1.5.
LumenScreenProbeGather.cpp:2620 supplies 64px/viewport radius at High probe
downsample32. LumenScreenSpaceBentNormal.usf:280-419 supplies the R1-jittered
quadratic radius and analytical horizon integral; HZB mode samples furthest
HZB mip0. It is a 2x2 depth reduction, not an additional ray tracer.

Raw AO is half-resolution R8_UNORM. AO history is full-resolution R8_UNORM.
LumenMaterial.ush:87-99 and StochasticLightingTileClassification.usf:278-380
supply the common4-rooks representative and depth/normal/triangle weights.
StochasticLightingUpsample.ush:29-71 selects one weighted corner from the
original scalar blue noise. LumenScreenProbeGatherTemporal.usf:469 uses the
selected corner for both half raw GI and half raw AO. The full AO history uses
GI's actual accepted4tap reprojection and exact current blend alpha. Its own
3x3 current neighborhood rectifies AO history to mean plus/minus1 sigma
(GatherTemporal.usf:297-346,609-630).

AO and receiver albedo are applied only after GI temporal history, using the
AOMultiBounce polynomial in DeferredShadingCommon.ush:1270-1275 and the High
max multibounce albedo0.5 in DiffuseIndirectComposite.usf:187. GI history RGB
therefore stays unmodulated irradiance/pi, GI count stays separate from current
readability, and AO is never baked into GI RGB history.

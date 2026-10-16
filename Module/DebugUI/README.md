# DebugUI

`DebugUIModule` owns Dear ImGui's context, GLFW input backend, font atlas, and an
engine RHI renderer. It links `imgui_core` and `imgui_glfw`; it has no native GPU
draw calls and does not use `imgui_impl_opengl3` or `async_ExecuteCode`.

Call all methods on the application thread, after the RHI starts and before the
GLFW window or RHI is destroyed:

```cpp
DebugUI::DebugUIModule ui(device, window, backend);
if (!ui.Startup()) throw std::runtime_error(ui.LastError());
// Each frame: poll events, start the UI, edit application parameters.
ui.BeginFrame();
ImGui::Begin("Settings");
ImGui::SliderFloat("Exposure", &exposure, 0.1f, 5.0f);
ImGui::End();
ui.EndFrame();
// Record scene and final postprocessing, then overlay before encoder.End().
ui.Record(encoder, framebufferWidth, framebufferHeight);
```

Check the returned booleans and report `LastError()` on failure. Use the noexcept
`CancelFrame()` from a frame guard when minimizing, leaving the loop, or unwinding
an exception: it balances an open ImGui frame and drops prepared data without
uploading any geometry. Cancellation temporarily permits Dear ImGui's upstream
stack recovery without recovery asserts, retaining its debug log so a throwing
panel's unmatched Begin/Push calls can be diagnosed. Normal frame assertions are
restored afterward; Shutdown uses the same cancellation path. `Record()` is a
successful no-op before an UI frame has been prepared and consumes a prepared
snapshot after successful recording. Repeated calls cannot duplicate an overlay.
The framebuffer extent must match the extent from `BeginFrame`; rebuild the UI
after a resize. Minimized windows produce an empty frame. The renderer selects
the window framebuffer, a full viewport, alpha compositing, no culling/depth, and
disables scissor after the overlay. Call it after all scene draws.

`EndFrame()` copies vertices, indices, offsets, clips, and texture bindings into
an independent snapshot, converts positions to RHI NDC, expands packed colors to
float RGBA, and uploads through the RHI resource queue into a pool of at most
three indexed mesh slots. Meshes are created on demand and retain stable RHI
identities, bounding Vulkan's native pipeline cache. A slot can be updated only
when the pool owns its sole reference: prepared/recorded frames and pending
upload callbacks pin their slots. Frame encoder `KeepAlive` pins the independent
CPU snapshot, mesh slot, pipeline/font resources, and every registered texture
owner until the frame executes or is discarded. A later ImGui frame cannot
overwrite queued UI geometry.

Creation and updates must complete before `EndFrame()` publishes a prepared
snapshot. The wait pumps RHI callbacks without polling GLFW, with a ten-second
bound per upload. When all slots are busy it waits up to ten seconds for frame
retirement; callers must submit or discard recorded frames to make progress.
This makes upload and draw ordering explicit despite independent resource/frame
queues. Canceling a prepared frame releases its slot for reuse; Shutdown drops
the pool while recorded frames retain their resources until retirement.

The RHI uses logical bottom-first image coordinates for OpenGL and the positive
Vulkan viewport. ImGui's top-left clips are scaled, clamped, rounded outward, and
converted to bottom-left scissors consistently for both backends. `DisplayPos`,
HiDPI framebuffer scaling, large meshes with `VtxOffset`, and `IdxOffset` are
handled. `ImDrawCallback_ResetRenderState` is recorded as an explicit state
reset. Other callbacks are rejected with an error; arbitrary callbacks cannot
introduce unmanaged GPU work or dangling ImDrawList pointers. Multiple platform
viewports and dynamically rebuilding the font atlas are not implemented; the font
atlas is built once during `Startup()`.

Use `RegisterTexture(rhiHandle, strongOwner)` for `ImGui::Image`, retaining the
RHI resource through the provided owner. The returned `ImTextureID` is an opaque
registry key. Native GL/Vulkan handles and unknown or unregistered IDs are
rejected. `UnregisterTexture()` removes a registration for future frames while
already prepared/recorded frames keep their captured binding. IDs are never
recycled. Font registration is owned by the module.

GLFW callbacks are installed with callback chaining. Initialize application
callbacks before `Startup()`; do not replace them afterward. After `BeginFrame`,
gate application camera/game controls with `WantsMouse()` and `WantsKeyboard()`.
The UI context remains current for panel code; `GetContext()` is available when
coordinating tools that also own ImGui contexts. Startup remembers and Shutdown
restores the previous context. Layout persistence is disabled by default via
`io.IniFilename = nullptr`; applications may opt into an explicit settings path.

`debug_ui_draw_data_test` validates CPU conversion, clipping/DPI and offsets,
snapshot ownership, callback rejection, minimized/invalid input, and texture
registration lifetime without creating a window or GPU device.

`material_lab_debug_ui_gpu_test` renders the actual MaterialLab panel and checks
native-size overlays over a lower-resolution scene, resize, clipping, font and
indexed geometry, separate alpha compositing, repeated frames, cancellation,
mouse capture and slider edits, and recovery from a throwing panel callback.
Build it with the main project and run with `--vulkan --validation` to exercise
the Vulkan RHI, or without arguments for OpenGL. `MATERIAL_LAB_GPU_TESTS=ON`
registers both available backends with CTest; default headless CTest excludes
these window/GPU tests. The independent core/conversion test also builds with
`MODEL_BUILD=OFF` and `RENDER_VULKAN=OFF`.

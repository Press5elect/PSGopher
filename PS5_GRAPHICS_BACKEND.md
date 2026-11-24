# PSGopher: graphics backend

## Decision

parallel-rdp, unchanged, on RADV, presenting through `VK_KHR_display` (PS5_ARCHITECTURE.md D2,
D5). No high-level renderer, no rewrite of the RDP. Labels as in PS5_PORTING_ANALYSIS.md.

## Why parallel-rdp stays

| Question | Answer |
| --- | --- |
| Does Vulkan exist? | Yes: RADV linked into the title, Vulkan 1.4 [console: PS5_Vulkan] |
| Does parallel-rdp run on it? | Yes, in PS5_RetroArch's Mupen64Plus-Next, upscaled 4x by default [console: PS5_Mupen64Plus `7cd4414`] |
| Its fast RDRAM path (`VK_EXT_external_memory_host`)? | Supported: anonymous memory gets GPU access through `sceKernelMprotect` [console: PS5_Vulkan]. Gopher64's RDRAM is 64 KiB aligned [source] |
| Does Gopher64's RDRAM qualify? | **[unknown]** RADV's import (`radv_ps5_buffer_from_ptr`) needs a page-aligned pointer below 2^47 and `sceKernelMprotect` adding GPU access. Gopher64 allocates RDRAM through Rust's allocator, i.e. the platform heap in direct memory at 0x20_0000_0000; the console test imported anonymous `mmap` memory. If the grant fails, parallel-rdp logs `Failed to allocate RDRAM with VK_EXT_external_memory_host` and runs its staging path (correct, slower). The first console run's klog settles it; if it falls back, allocate RDRAM with `mmap` on the PS5 (a one-line `cfg` in `rdram.rs`) |
| Coherence? | Host-visible memory cached and coherent both ways, no flushes needed [console: PS5_Vulkan] |
| Shaders? | SPIR-V precompiled into parallel-rdp (no runtime GLSL); ACO compiles pipelines; RADV's disk cache at a path we set |
| Accuracy | parallel-rdp is the accuracy reference for LLE RDP; any replacement would be a regression by definition |

## The PS5-specific pieces

| Piece | Implementation | Status |
| --- | --- | --- |
| Loader | `SDL_Vulkan_GetVkGetInstanceProcAddr` → `vk_icdGetInstanceProcAddr`; `Context::init_loader` calls `volkInitializeCustom` with it [source: Granite `context.cpp`] | written |
| Instance extensions | `VK_KHR_surface`, `VK_KHR_display` | written |
| Surface | first display, its fastest mode up to 60 Hz (D5), `vkCreateDisplayPlaneSurfaceKHR`, plane 0, identity, opaque | written |
| Window size | the mode's visible region (3840x2160 on the console) | written |
| Present mode | parallel-rdp asks MAILBOX, falls back to FIFO (the console's one mode) | upstream behaviour |
| Queues | one graphics ring for everything [console: CTS_GAPS]; parallel-rdp's async paths fall back to the one queue | upstream behaviour |

## What the GPU does per frame (to be profiled)

1. RDP command batches: parallel-rdp's tile binning, then shading compute dispatches, with
   upscaling multiplying the shaded area (factor² samples).
2. Readbacks/syncs where the CPU reads what the RDP wrote (framebuffer effects, the tracker).
3. VI: `scanout()` (filters, deinterlace, optional SSAA downscale).
4. The blit to the swapchain (`interface.cpp` draws one triangle with the plain or CRT shader),
   OSD text.

## Measured on this console by others

| Fact | Source |
| --- | --- |
| 8x upscale kept the GPU ~10x as long as 4x; the submitting thread waited 2.4 s of every 10 at 8x vs 0.2 s at 4x | PS5_Mupen64Plus `7cd4414` |

## Candidates, each to be measured before it is kept

| Candidate | Expected effect | Risk |
| --- | --- | --- |
| Default upscale from measurement (1x/2x/4x per the GPU's margin at 59.94 Hz) | picture vs headroom | none to accuracy (upstream option) |
| GPU timestamps around RDP batches, scanout and blit (`VK_KHR_calibrated_timestamps` is reported on the port [console: CTS_GAPS]) | profile | none |
| Avoid the intermediate blit: scan out straight to the swapchain image | one pass less | needs parallel-rdp changes; only if the blit shows up |
| Not porting: Crux2's Shared Render Target Memory / Streamed Memory Clearing | — | different renderer; not applicable without evidence (rule 7) |
| Not porting: ROV | made Crux2 slower | rejected |

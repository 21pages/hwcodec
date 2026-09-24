Run from an x64 MSVC developer PowerShell with `VCPKG_ROOT` pointing to an
existing static FFmpeg installation with QSV enabled:

```powershell
./tests/ffmpeg_ram/input_lifetime.ps1
```

The test compiles the production RAM encoder with codec opening and packet I/O
replaced. It uses real FFmpeg frame allocation, reference counting and copying;
no GPU is required and FFmpeg is not rebuilt. The fake encoder retains frame
references across calls to require fresh buffers. Allocation failure is injected
at the allocation/make-writable API boundary to verify that retained frames
survive and the next submission can recover.

For both NV12 and YUV420P, it verifies pixel content after overwriting and freeing
the caller's input, after subsequent inputs, and after encoder cleanup. It checks
alignments 0, 1 and 256, including odd chroma strides at 66x34. Encoder dimensions
are even, as required by the Rust API.
A separate case uses different input and destination strides. Empty, null and
truncated inputs must fail without submitting a frame. Null inputs are rejected
before allocation; other short inputs use the original `fill_frame` check after
making the frame writable. Writable
buffers must be reused when the encoder has released its references.

The Windows workflow runs this suite alongside the VRAM failure-injection tests.

These tests verify buffer ownership and layout, not hardware driver behavior.

`./tests/ffmpeg_ram/compare_revisions.ps1 -Revision <commit>` compiles that
revision's production RAM encoder against the same comparison fixture. Omit
`-Revision` to test the working tree. Old revisions intentionally fail the
checks for bugs they still contain; all observed values are logged under
`target/ram-comparison`. The fixture also checks 96 supported even-dimension
layouts against the planar size formula. It checks that replacing a retained
frame preserves its color-range property and that a failed make-writable call
leaves the wrapper's current frame intact. Old-pixel copies and replacement
strides are reported as observations rather than required optimizations.

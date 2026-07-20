# OpenXR Projection, Menu, and Shader Crash Fixes

This document describes the VR rendering corrections made while testing the
v1.3 beta source on Meta Quest 3. It is intended to help future contributors
understand why the changes exist, how the projection math works, and which
parts must remain separate.

## Contribution Credit

This contribution was made by **J4nky**, using **ChatGPT Codex 5.6 Sol** as a
collaborative development tool to diagnose the rendering problems and to
understand how the OpenXR camera, per-eye frustums, and projection pipeline
work. J4nky performed the headset testing and provided the visual observations
that identified and verified the fixes.

## Status

Confirmed configurations:

- Quest 2 standalone
- Quest 3 standalone
- Quest PCVR through the Meta OpenXR runtime
- Quest PCVR through Virtual Desktop and SteamVR/OpenXR

Confirmed behavior:

- Physical head rotation no longer causes the world to stretch or skew near
  the edges of the lenses.
- In-game menus and HUD elements retain their correct vertical placement.
- Entering the Carrington Institute menu no longer causes the native crash
  encountered during development of the projection fix.

Still requiring validation:

- Headsets with strongly canted or unusually asymmetric displays

The projection values are obtained dynamically from OpenXR. No Quest-specific
FOV, lens centre, IPD, or aspect values are hard-coded, so the correction is
designed to work on other OpenXR headsets.

## Original Symptoms

### World warping during physical head movement

The view had a light fisheye-like stretch at the edges. It was most visible
when the player physically looked left, right, up, or down. Turning with the
controller while facing forward produced much less distortion.

This distinction was important: it pointed to a mismatch between the
projection used to render the scene and the projection that OpenXR used when
presenting the submitted eye images.

### Menus shifted and clipped at the top

The first projection correction removed the world warping but moved 2D menu
content upward. Some menu content was then clipped above its panel.

### Crash entering the Carrington Institute menu

After expanding the generated VR shader, the title screen rendered but the
application crashed with `SIGSEGV` while entering Carrington. The menu used a
larger shader variant than the title screen.

## Projection Root Cause

OpenXR supplies an `XrFovf` for each eye:

- `angleLeft`
- `angleRight`
- `angleUp`
- `angleDown`

These angles normally describe an asymmetric frustum. The original renderer
continued to use Perfect Dark's symmetric projection for the world. It passed
some horizontal per-eye projection data to the multiview shader, but it did
not fully reproduce the OpenXR projection:

- The game projection aspect effectively became `1.0` instead of using the
  optical frustum's aspect.
- Vertical FOV was derived from angular span, which is only equivalent to the
  correct tangent-space calculation for a symmetric frustum.
- Horizontal frustum-centre data was used, but vertical frustum-centre data
  was discarded.
- The tangent half-FOV helper used eye 0 for both eyes.

The result was a rendered image whose projection did not match the projection
OpenXR expected. Runtime lens correction then made the mismatch visible as
world stretching or swimming during physical head rotation.

## Correct Projection Math

For each eye, convert the OpenXR angles to tangent-space extents:

```text
tanLeft   = tan(angleLeft)
tanRight  = tan(angleRight)
tanUp     = tan(angleUp)
tanDown   = tan(angleDown)

tanHalfWidth  = (tanRight - tanLeft) / 2
tanHalfHeight = (tanUp - tanDown) / 2
```

The game still needs one symmetric base projection, so the half-widths and
half-heights are averaged across both eyes. The base values are then:

```text
verticalFov = 2 * atan(averageTanHalfHeight)
aspect      = averageTanHalfWidth / averageTanHalfHeight
```

The asymmetric centre terms remain per-eye and come from the OpenXR
column-major projection matrices:

```text
matrix[0] = horizontal scale
matrix[8] = horizontal frustum centre
matrix[9] = vertical frustum centre
```

The renderer sends four values per eye to the multiview shader:

```text
x = scaled eye/IPD translation
y = horizontal frustum centre
z = HUD parallax offset
w = vertical frustum centre
```

For normal 3D world geometry, the shader applies both asymmetric centre terms:

```glsl
mvPos.x -= eyeOffset.x + (eyeOffset.y * mvPos.w);
mvPos.y -= eyeOffset.w * mvPos.w;
```

## Why Menus Must Be Treated Separately

Menus and HUD elements are authored in screen/clip space rather than being
ordinary world geometry. Applying the optical vertical-centre correction to
every vertex shifted the entire interface vertically.

On the Quest 3 used for diagnosis, OpenXR reported a vertical centre term of
approximately `-0.193`. Applying that globally moved screen-space interface
geometry upward by roughly 19 percent of clip space, matching the observed
clipping.

The vertical correction must therefore remain inside the normal 3D-world
branch:

```glsl
else if (uIsMenu == 0 && !vr_is_Menu_blur) {
    mvPos.x -= eyeOffset.x + (eyeOffset.y * mvPos.w);
    mvPos.y -= eyeOffset.w * mvPos.w;
}
```

Do not move the Y correction above the menu/HUD conditionals. Menus, HUD,
crosshairs, blur geometry, and the legal-title treatment retain their existing
special positioning.

## Carrington Crash Root Cause

The OpenGL backend constructs shader variants in local character buffers using
append functions without bounds checking. The VR shader prelude is already
about 3.2 KiB, while the vertex shader buffer was only 4 KiB. Simpler title
shaders fit, but more complex menu variants could overflow the buffer and
corrupt the native stack.

The vertex and fragment shader construction buffers were increased to 16 KiB.
This fix only prevents memory corruption; it does not alter menu layout or
projection behavior.

Future work should replace the unchecked fixed-buffer construction with a
bounded builder or dynamically sized string. The larger buffers are a safe
practical correction, but they do not make the append functions intrinsically
safe.

## Files Changed

| File | Responsibility |
| --- | --- |
| `port/vr/vr_openxr.cpp` | Derives base FOV/aspect from OpenXR tangent extents, keeps per-eye projection matrices, fixes the per-eye tangent lookup, and logs projection diagnostics. |
| `port/vr/vr_openxr.h` | Exposes the runtime-derived `XrAspect`. |
| `src/game/player.c` | Uses `XrAspect` for the game's world projection instead of the previous value that effectively collapsed to `1.0`. |
| `port/fast3d/gfx_pc.cpp` | Sends horizontal and vertical projection-centre terms for both eyes to the rendering backend. |
| `port/fast3d/gfx_rendering_api.h` | Expands the eye-offset rendering callback from six to eight values. |
| `port/fast3d/gfx_opengl.cpp` | Stores four values per eye, uploads `vec4` uniforms, applies vertical asymmetry only to 3D world geometry, and enlarges generated-shader buffers. |

## Quest 3 Diagnostic Values

These are recorded examples, not constants to copy into the code:

```text
Eye 0 FOV radians: left=-0.942478 right=0.698132 up=0.767945 down=-0.959931
Eye 1 FOV radians: left=-0.698132 right=0.942478 up=0.767945 down=-0.959931
Game projection: vertical_fov=100.2439 aspect=0.925494
```

Approximate angular extents were:

- Left eye: left -54 degrees, right 40 degrees, up 44 degrees, down -55 degrees
- Right eye: left -40 degrees, right 54 degrees, up 44 degrees, down -55 degrees

The left/right difference demonstrates horizontal asymmetry. The unequal
up/down values demonstrate why the missing vertical centre term mattered.

## Building and Testing Android

From the `android` directory on Windows:

```powershell
.\gradlew.bat assembleDebug
```

The APK is produced at:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

Install it while preserving the existing debug app data:

```powershell
adb -s <device-serial> install -r android\app\build\outputs\apk\debug\app-debug.apk
```

Useful projection log messages include:

```text
OpenXR eye <n> FOV radians: ...
OpenXR game projection: vertical_fov=... aspect=...
OpenXR eye <n> projection: scale_x=... center_x=... scale_y=... center_y=...
```

## Regression Checklist

For every headset/runtime combination:

1. Physically rotate the headset left/right and up/down while looking at
   straight architectural edges near the lens periphery. Confirm the world
   does not stretch, skew, or swim.
2. Repeat using controller snap/smooth turning to compare physical and
   artificial rotation.
3. Enter the Carrington Institute main menu and confirm there is no crash.
4. Open several in-game menus, including the player-name keyboard and mission
   completion panels. Confirm their top edges and text are not clipped.
5. Check the HUD and both crosshairs in normal gameplay.
6. Check the legal/title splash and fullscreen menu blur.
7. Capture the projection log values and record the headset model and active
   OpenXR runtime with the test result.

## Important Maintenance Notes

- Do not substitute swapchain texture aspect for optical-frustum aspect. They
  are not guaranteed to be equivalent.
- Do not calculate asymmetric FOV from angle span alone; use tangent extents.
- Do not reuse eye 0 projection data for eye 1.
- Do not apply the vertical optical-centre offset globally to menus and HUD.
- Do not reduce the generated vertex shader buffer to 4 KiB.
- Keep testing headset-specific behavior through reported OpenXR values rather
  than adding hard-coded Quest 2, Quest 3, or SteamVR constants.

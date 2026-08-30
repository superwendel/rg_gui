# Demo font assets

`inter_medium_16.font` and `inter_medium_16.rgba` are the ASCII metrics and
RGBA8 atlas used by all three demos. They were baked at 16 pixels from the
hinted `Inter-Medium.ttf` in the
[official Inter 4.1 release archive](https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip).

## Recorded inputs and outputs

| Item | Recorded value |
| --- | --- |
| Source TTF SHA-256 | `97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872` |
| Baker vcpkg baseline | `91e8cb4be8195112ea3a9c7e5846bd0b3ff74673` |
| Baker triplet | `x64-windows` |
| FreeType | `2.14.3#0` (default features disabled) |
| HarfBuzz | `14.4.0#0` (default features disabled; `freetype` enabled) |
| Codepoint range | U+0020 through U+007E |
| Pixel size | 16 |
| Kerning | enabled |
| Initial atlas width | 256 pixels |
| Output atlas | 256 x 128, RGBA8, 131072 bytes |
| `.font` SHA-256 | `e31e494cee22d7a7b03355d70862e6631993a55dc75f23b3e0aa896bb1369e83` |
| `.rgba` SHA-256 | `9d67b0f6edfe34294fecd4888e8058a9c5e21e1fa2589b8ff873496b6230e626` |

The metrics use a 16-pixel em line box rather than the font's 22-pixel
typographic line spacing. Pair advances include the font's shaping adjustments,
rounded to the nearest whole pixel. Negative fractional adjustments are rounded
symmetrically; they are not shifted or floored to an extra negative pixel. Each
nonempty glyph rectangle also contains the baker's one-pixel dark shadow
footprint.

## Reproduce

From a sibling [`rg_text`](https://github.com/superwendel/rg_text) checkout, use
a vcpkg checkout at the baseline recorded above, install the optional `baker`
feature for the recorded triplet, set `RG_TEXT_TEST_FONT` to the source TTF, and
run:

```bat
vcpkg install --x-feature=baker --triplet x64-windows
build.bat rg_text_bake
rg_text_bake.exe "%RG_TEXT_TEST_FONT%" "..\rg_gui\examples\assets\inter_medium_16" 16 32-126 1 256
```

Then verify the two output hashes:

```powershell
Get-FileHash ..\rg_gui\examples\assets\inter_medium_16.font -Algorithm SHA256
Get-FileHash ..\rg_gui\examples\assets\inter_medium_16.rgba -Algorithm SHA256
```

The recorded Windows baker environment reproduces both hashes above. Raster
output can vary across dependency versions and platforms, so these hashes are a
release check for that environment, not a cross-platform reproducibility claim.
The Linux baker sanitizer job is compatibility coverage and does not produce
the checked-in golden assets.
Before the first public tag, replace `rg_text/main` in the `rg_gui` CI workflow
with the full commit SHA of the released baker source; the dependency table in
the root README tracks that remaining source-identity gate.

The raw atlas keeps FreeType and HarfBuzz out of the runtime dependency graph.
`rg_text` parses the `.font` file, and the shared SDL3 bootstrap uploads the
matching `.rgba` bytes directly. If either asset is missing or malformed, the
examples stop with an error naming the expected files.

Inter font software is licensed under the SIL Open Font License 1.1; see
[`LICENSE-INTER.txt`](LICENSE-INTER.txt).

# Demo font assets

`inter_medium_16.font` and `inter_medium_16.rgba` are the ASCII metrics and
RGBA8 atlas used by all three demos. They were baked at 16 pixels from the
hinted `Inter-Medium.ttf` in the
[official Inter 4.1 release archive](https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip).

## Using the bundled assets

The demos load these files directly; building or running them does not require
FreeType, HarfBuzz, or a font baker. Keep the two files together in
`examples/assets` beside a repository-built executable, or in `assets` beside
a deployed executable. The loader checks those locations beside the executable
first, then relative to the working directory. Missing or malformed assets
produce an error naming the expected files.

The atlas is 256 x 128 pixels in RGBA8 format and covers U+0020 through U+007E.
The metrics use a 16-pixel em line box rather than the font's 22-pixel
typographic line spacing. Pair advances include shaping adjustments rounded
to the nearest whole pixel, including symmetric rounding for negative values.
Nonempty glyph rectangles include a one-pixel dark shadow.

Inter font software is licensed under the SIL Open Font License 1.1; see
[`LICENSE-INTER.txt`](LICENSE-INTER.txt).

## Regenerating the exact bundled assets (optional)

Use the inputs below when reproducing the bundled files byte for byte. These
versions apply to asset generation, not to ordinary `rg_gui` builds. Use a
separate `rg_text` checkout if your application uses its latest branch.

| Item | Recorded value |
| --- | --- |
| Source TTF SHA-256 | `97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872` |
| `rg_text` source revision | `4b98c6d38d4de1398ebb0970fc8e0e3db01bafa5` |
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

From a sibling [`rg_text`](https://github.com/superwendel/rg_text) checkout, use
the source revision and vcpkg baseline recorded above. Set `RG_TEXT_TEST_FONT`
to the source TTF, then install the optional `baker` feature and run:

```bat
git checkout 4b98c6d38d4de1398ebb0970fc8e0e3db01bafa5
vcpkg install --x-feature=baker --triplet x64-windows
build.bat rg_text_bake
rg_text_bake.exe "%RG_TEXT_TEST_FONT%" "..\rg_gui\examples\assets\inter_medium_16" 16 32-126 1 256
```

Then verify the two output hashes:

```powershell
Get-FileHash ..\rg_gui\examples\assets\inter_medium_16.font -Algorithm SHA256
Get-FileHash ..\rg_gui\examples\assets\inter_medium_16.rgba -Algorithm SHA256
```

The hashes identify the bundled files produced with the recorded Windows baker
environment. Raster output can vary across dependency versions and platforms.
Run `build.bat test_assets` from the `rg_gui` directory to check the bundled
assets and their hashes.

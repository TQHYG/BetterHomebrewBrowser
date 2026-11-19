# Better Homebrew Browser — TQHYG fork

A homebrew browser for the PS Vita / PSTV with background downloading support.

This is a fork of [Ibrahim778/BetterHomebrewBrowser](https://github.com/Ibrahim778/BetterHomebrewBrowser)
built with the **original VDSuite 3.57 SDK + SNC toolchain** instead of VitaSDK,
with a set of fixes and an extra data source. See [CHANGELOG.md](CHANGELOG.md)
for the full list of changes.

Releases: <https://github.com/TQHYG/BetterHomebrewBrowser/releases>

> The build in this repository was compiled, packaged and verified statically,
> and additionally exercised on one PS Vita. Firmware specific behaviour (very
> old/new firmwares, standby edge cases, exFAT repairs) is not covered.

# Features

### Multiple sources, so a larger content library to choose from

| id | Source | Index |
| -- | ------ | ----- |
| 0 | CBPS DB | `raw.githubusercontent.com/KuromeSan/cbps-db` |
| 1 | Vita DB | `rinnegatamante.eu/vitadb` |
| 3 | PSP Homebrew Browser | `archive.org/homebrew_45` |
| 4 | NeoVitaDB | `robin994.github.io/NeoVitaDB-Catalog` |

<img src="imgs/vitadb_icon.png" height="100" />
<img src="imgs/cbpsdb_icon.png" height="100" />

### Sleek UI

<img src="imgs/ui1.png" width="350" />
<img src="imgs/ui2.png" width="350" />

### Background downloads with a native looking install flow and notifications

<img src="imgs/download1.png" width="300" />
<img src="imgs/download2.png" width="300" />
<img src="imgs/download3.png" width="300" />
<img src="imgs/download4.png" width="300" />

Downloads go through the system background downloader. When the transfer is done
the app asks whether to install right away or to keep the file; keeping it copies
the file to **`ux0:/downloads/bhbb/`**.

### Optional debug log

Settings → *Debug log* (off by default). When enabled the app appends to
`ux0:data/betterHomebrewBrowser/bhbb_log.txt` (rotated at 256 KiB) and records
every network step with its result, which makes bug reports useful:

```
[AppBrowser] source switch requested -> 0
[Utils] ok, 2 hop(s), status 200: https://release-assets.githubusercontent.com/...
[AppViewer] enqueue -> 0x0
[NeoVitaDB] json capacity 2097152 for 2195633 bytes of index
```

### Twelve languages

English, German, Spanish, Finnish, French, Italian, Japanese, Korean, Polish,
Portuguese (Brazil), Russian, Chinese.

## Requirements

- PS Vita or PSTV
- Firmware 3.60 or newer. TLS 1.2 is required by most sources: either firmware
  3.68+ or a TLS provider such as [vitals](https://github.com/TheOfficialFloW/VitaTLS)
  installed. No specific TLS plugin is mandatory.
- A memory card / SD2VITA with some free space

## Install

1. Download `BetterHomebrewBrowser.vpk` from the [releases page](https://github.com/TQHYG/BetterHomebrewBrowser/releases).
2. Copy it to the console and install it with VitaShell (or any VPK installer).
3. Start *Homebrew Browser* from the LiveArea.

## Build from source

### Toolchain

This fork builds with the **VDSuite 3.57 SDK + SNC**, not with VitaSDK:

- `psp2snc.exe` (SNC 3.57.0.12983) and `psp2ld`
- CMake ≥ 3.20 and Ninja
- Python 3 (used by `genhashheaders.py`, `acdc` helpers)
- SCE Publishing Tools (`vdsuite-pubgen`, `unfself`, ...), unpacked anywhere; the
  build only needs them on `PATH`

The SDK is expected in the layout the toolchain file assumes
(`sdk/host_tools/build/cmake/psp2-snc-toolchain.cmake`, `sdk/target/...`).
Nothing has to be installed system wide.

### Environment

```bash
export PYTHONUTF8=1                 # required: the xml files are utf-8, without
                                    # this genhashheaders.py reads them as gbk
export SCE_PSP2_SDK_DIR=/path/to/sdk
export SCE_ROOT_DIR=/path/to/SCE    # unpacked Publishing Tools
export PATH=$SCE_PSP2_SDK_DIR/host_tools/build/bin:/path/to/cmake:/path/to/ninja:$PATH
```

### Configure and build

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$SCE_PSP2_SDK_DIR/host_tools/build/cmake/psp2-snc-toolchain.cmake \
  -DCMAKE_MAKE_PROGRAM=/path/to/ninja \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --verbose
```

Outputs in `build/`:

```
BetterHomebrewBrowser.vpk      installable package
BetterHomebrewBrowser.self     signed eboot (inside the vpk as eboot.bin)
BetterHomebrewBrowser.sfo      param.sfo (sce_sys/param.sfo)
bhbb_plugin.rco / .rcd         UI resources
bhbb_dl/bhbb_dl.suprx          download helper module (module/bhbb_dl.suprx)
mapfile                        linker map
```

### Build notes

- Header changes need a clean rebuild: `ninja -t clean` first. The SNC depfiles
  in this setup do not track `#include` dependencies, so stale objects otherwise
  link against removed symbols.
- `include/bhbb_plugin.h`, `include/bhbb_locale.h` and `include/bhbb_settings.h`
  are generated during the build (`acdc -x`, `genhashheaders.py`). When you
  change the xml files and only want to recompile, run the generators first.
- `*.rcs` and the `rco` are build products and are ignored by git.

## Adding a new language

(Instructions by [GrapheneCt](https://github.com/GrapheneCt))

- Use [English locale](resource/locale/en.xml) as base. Name your translation
  `XX.xml`, where `XX` is the language code. Supported codes are the `id`
  attributes in [bhbb_plugin.xml](resource/bhbb_plugin.xml).
- Submit your translation as a pull request, it will be added in the next
  release.

## Credits

### Upstream author and contributors

- **@Ibrahim778** — BetterHomebrewBrowser
- **@GrapheneCt** — ScePaf reversing, EMPVA (reference), NetStream (reference),
  CXML compiler, SceIPMI & SceDownload reversing
- **@CreepNT** — SceDownload reversing
- **@Princess-of-sleeping** — help with debugging and development, SceLsdb
  notification reversing (SceShell notice PoC)
- **@SKGleba & @TheFlow** — bgvpk & Download Enabler (basis for `bhbb_dl`)
- **CBPS** — original SceDownload PoC

### This fork

- **@robin994** — NeoVitaDB and its public catalog
- **@BenMitnicK** — NeoVitaDB integration reference in their BetterHomebrewBrowser fork

### Beta testers

@SomeOnPC, @ShadowKnight1620, @Koutsie, @SparklingPheonix, @gl33ntwine

### Translators

@isage (RU), @S1ngyy (DE), @CreepNT (FR), @Princess-of-sleeping (JA),
@maniek6666 (PL), @michael-moon-lee (ZH), @Sfresneda (ES), @gilgilgan (KO),
@Koutsie (FI), @ganona (IT), @iveurne (PT-BR)

LiveArea assets by SomeonPC.

## Donations

This fork does not take donations. The upstream author's link is kept here for
reference: [PayPal](https://www.paypal.com/paypalme/GloveTekLtd).

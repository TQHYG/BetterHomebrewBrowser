# Changelog

All notable changes to this fork. Upstream history lives in
[Ibrahim778/BetterHomebrewBrowser](https://github.com/Ibrahim778/BetterHomebrewBrowser).

## 1.46-tqhyg.1

First release of this fork. Based on upstream `InDev` @ `de251fb`, built with the
original VDSuite 3.57 SDK + SNC toolchain instead of VitaSDK.

### Added

- **NeoVitaDB** data source (source id 4). Reads the static catalog published at
  `https://robin994.github.io/NeoVitaDB-Catalog/vita.json` (currently 1314
  entries, categories: games / ports / emulators / utilities).
- **Debug log switch** in the settings page, next to the data source entry.
  Off by default; when enabled it appends to
  `ux0:data/betterHomebrewBrowser/bhbb_log.txt` (256 KiB, rotated on start).
  Entry points and failures of every network step are logged with error codes.
- Localized labels for the install/save dialog (`msg_install*`, `msg_save*`) in
  all twelve languages.

### Fixed

- **CBPS DB and VHB downloads** failed with a generic error. GitHub release
  assets now redirect to short lived https-only signed URLs, so the old
  "downgrade everything to http" step always failed. Downloads now resolve the
  hop chain with libcurl and hand bgdl the direct link.
- **Switching data source** froze the console, then crashed it. The swap used to
  run on a job thread (template reopen + transitions race with the shell UI) and
  the old icon jobs still referenced the list being destroyed. The swap is now
  done on the main thread after the texture pool stopped accepting work, and the
  stored category is reset so the new list is not rendered empty.
- Corrupted download URLs (`  z   z `) that made the first download attempt of a
  session fail. `ResolveFinalURL` copied `CURLINFO_EFFECTIVE_URL` *after*
  `curl_easy_cleanup()`, i.e. from freed memory.
- `common::FormatString` was called from several worker threads at once (it is
  not reentrant); the download and icon paths now use plain `sce_paf_snprintf`.
- Install notification for Vita packages showed neither icon nor app name: the
  App branch cleared `title` and `icon_path` before sending it.
- Tapping a completion notification answered "application not found" because the
  notifications targeted `VITASHELL` / the freshly installed title with an open
  or highlight action. Notifications are now sent without an action, so a tap
  only opens the notification centre entry.
- CBPS entries without a description tried to resolve the host `None`
  (`CURLE_COULDNT_RESOLVE_HOST`) because the csv uses the literal string `None`
  for empty fields.

### Changed

- "Save File" copies downloads to `ux0:/downloads/bhbb/` (was
  `ux0:/bhbb_downloads/`). The directory is created on demand.
- The install/save dialog is fully localized; only the "download complete" line
  came from the system indicator plugin before, the rest was hardcoded English.
- No iTLS-Enso requirement: any TLS provider (firmware 3.68+, vitals,
  tls_fixer) is accepted.
- Safer download write path (memory buffer, 32 MiB cap, temp file + rename).
- Defensive null/dedup handling for list entries, texture pool and source
  lookups.

### Build

- Compiles with the VDSuite 3.57 SDK and SNC 3.57 (`psp2snc` + `psp2ld`) via
  `psp2-snc-toolchain.cmake`; no VitaSDK involved.
- `paf_file_ext::CurlFile` is no longer used anywhere: the prebuilt
  `libPafFileExt.a` shipped with the SDK was compiled against the 2025 PAF API
  (`int64_t` based `File::WaitAsync` / `Seek` / `GetFileSize`) while the
  firmware 3.57 headers declare 32-bit variants, so reads, seeks and sizes
  through it were wrong. That also removed `libPafFileExt.a` from the link.
- Internal sysmodule ids (PAF, bXCe, IniFileProcessor, CommonGuiDialog,
  PromoterUtil) are declared in `libsysmodule.h` instead of being scattered
  through the build files.
- `bhbb_plugin.h` is regenerated with `genhashheaders.py`, because the current
  `acdc -x` no longer emits the `id:hash` constants.

### Notes / not verified

- Only compiled, packaged and statically verified here; testing was done on one
  PS Vita. Power/standby edge cases, exFAT repair paths and every firmware are
  untested.
- Resolved GitHub asset URLs are signed and expire after about an hour. If a
  download sits in the queue for longer than that it may fail; report it and the
  raw release URL can be handed over instead.

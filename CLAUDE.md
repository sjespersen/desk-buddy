# Working in this repo

- Commit straight to `main` and push; no feature branches or pull requests.
- The repo is public: no WiFi names, home location or serial port IDs in code, examples or
  commit messages (the device keeps WiFi and location in its own flash).
- `vendor/` are submodules; Seeed_GFX2 carries `vendor-patches/seeed_gfx2-perf.patch` as local
  changes. After moving its pin, regenerate the patch with `git -C vendor/Seeed_GFX2 diff`.

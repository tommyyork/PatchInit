# Rack dependency for VCVHARNESS test

The **`make test`** target runs the VCV Harness plugin inside a dummy Rack environment for 10+ seconds (same validation as when the VST is used in a DAW). To do that, the test needs the full VCV Rack **source** (not just the SDK).

## Option A: Git submodule (recommended)

From the **DaisyExamples** repo root:

```bash
cd VCVHARNESS
git submodule add -b v2 https://github.com/VCVRack/Rack.git dep/Rack
```

Then run:

```bash
make -f Makefile.test test
```

## Option B: Clone manually

If you prefer not to use a submodule:

```bash
cd VCVHARNESS/dep
git clone -b v2 https://github.com/VCVRack/Rack.git Rack
cd ../..
make -f Makefile.test test
```

## What the test does

1. Copies **`dep/standalone-run-seconds.cpp`** over `dep/Rack/adapters/standalone.cpp` so Rack supports **`--run-seconds=N`** in headless mode.
2. Builds Rack from `dep/Rack` (dep + libRack + standalone binary).
3. Builds the VCVHARNESS plugin with `RACK_DIR=dep/Rack`.
4. Creates a minimal `.vcv` patch containing one **Patch SM Harness** module.
5. Runs Rack headless with that patch for **10 seconds** (plugin load, engine run, `process()` called every block).
6. Exits with code 0 if no crash; non-zero on failure.

This matches the validation a DAW performs when loading the VCV Rack VST and running audio: load plugin → create module → run engine for a sustained time.

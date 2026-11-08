# EBOV-TIP engine (C++20) + Android app

```
engine/            code.cpp (whole engine + C API), input.txt, screen.txt, test_api.cpp
app/               Android app: Kotlin UI -> JNI -> engine/code.cpp (built by CMake/NDK)
.github/workflows/ build-apk.yml (desktop engine test, then debug APK)
```

## Get the APK
1. Push this folder to a GitHub repository (branch `main`).
2. Open the **Actions** tab, run **Build APK** (it also runs on every push).
3. Download the `ebov-tip-debug-apk` artifact, unzip, and install `app-debug.apk`
   (allow "install unknown apps" on the phone).

## Desktop use
```
cd engine
g++ -std=c++20 -O2 -pthread code.cpp -o engine
./engine            # single run -> output.txt, output.svg
./engine --screen   # screening  -> screen_report.txt, screen_pareto.svg, screen_best.svg
```

## In the app
Edit the quick parameters or add any name from `input.txt` in the free-text box, then **Run** (trajectory plot and
summary) or **Screen** (ranked profiles, best-profile plot; **Pareto plot** shows the trade-off chart).

Parameters are illustrative until the Phase-1 provenance register is complete; results are simulated, not predictions.

## Model v2 (primed cells)
The engine now has ten dynamic states: TIP-only "primed" cells `P` are tracked and can be superinfected by WT
(`primed = 1`, default; `primed = 0` reproduces the earlier abortive-entry model). `rho_T` adds TIP-driven cytokine
stimulation (default 0). The `c_T` search range is 0.1–15 d⁻¹. With primed cells tracked, WT extinction needs
`c_T` below roughly 0.5 d⁻¹ (illustrative parameters, not predictions).

# Commands

Quick reference for `test-faults`. Every script also supports `--help`.
Run `cmake -S . -B build -DCMAKE_C_COMPILER=<compiler> -DP101_BUILD_LEVEL=1` once before building.

| Command | What it does |
| --- | --- |
| `cmake -S . -B build -DCMAKE_C_COMPILER=<cc> -DP101_BUILD_LEVEL=1` | Configure the build with a compiler (also `set CMAKE_C_COMPILER=<cc>`). `--help` lists detected compilers. |
| `cmake -S . -B build -DCMAKE_C_COMPILER=<cc> -DP101_BUILD_LEVEL=1 -s address,undefined` | Configure with specific sanitizers |
| `cmake -S . -B build -DCMAKE_C_COMPILER=<cc> -DP101_BUILD_LEVEL=1 --coverage` | Configure an instrumented build for coverage (gcov) |
| `cmake --build build` | Strict analysis build: format-check, clang-tidy, cppcheck, static analyzer, `-Werror`, sanitizers. `-q` = quiet |
| `cmake --build build --target format` | Auto-fix in place: clang-tidy `--fix` + clang-format |
| `clang-format --dry-run --Werror -style=file <sources>` | Format check only, no build (hook-friendly); non-zero if unclean |
| `cmake -S . -B build -DP101_BUILD_LEVEL=3 && cmake --build build` | **The gate:** format + strict build + tests + fuzz smoke -> one PASS/FAIL. `--cov <pct>` adds a coverage gate |
| `cmake -S . -B build -DP101_BUILD_LEVEL=2 && cmake --build build` | Build & run the Unity test suite (ctest) |
| `../../scripts/update-all.sh --level 2` | Run the tests across every supported compiler |
| `configure and run the fuzz/ CMake project` | Run the libFuzzer target (coverage-guided + sanitizers); PASS/FAIL. `-t <secs>` sets the time budget |
| `configure with -DP101_COVERAGE_MODE=ON and run gcovr` | HTML coverage report. `--report-only` skips the run; `--min <pct>` fails under a threshold |
| `configure with -DP101_COVERAGE_MODE=ON and run gcovr` \| `profile` | One entry point for the coverage / profiling reports |
| `cmake -S . -B build` | Report what actually works on this machine for this project |
| `cmake --build build --target clean` | Remove `build-` / `coverage-` / `profile-` output (`-n` previews) |
| `./copy-template.sh <dir>` | Start a new project from this template |

Program examples:

| Command | What it does |
| --- | --- |
| `test-faults -- ./prog` | Run `./prog` normally, then walk fault injections until no fault fires |
| `test-faults -n 0 -- ./prog` | Baseline only |
| `test-faults -n 20 -l /tmp/run -- ./prog config.txt` | Run baseline plus fault calls up to 20 using `/tmp/run-*` capture/analysis directories |
| `test-faults -F p101_open -- ./prog config.txt` | Walk only the exact wrapper API identity |
| `test-faults -E 24 -- ./prog config.txt` | Inject errno `24` instead of the default `EIO` |
| `test-faults -O ../p101-inspect/inspect-capture -- ./prog` | Use the in-tree capture launcher |
| `test-faults -U ../p101-inspect/build-clang/p101-inspect -- ./prog` | Use the in-tree native inspection driver |
| `test-faults -B ../../libraries/lib_tool_event/build-clang/p101-event-model -- ./prog` | Use an in-tree shared event-model build |
| `./test-corpus --strict --keep-going -o /tmp/p101-corpus` | Execute and verify every playground lesson fixture and write `/tmp/p101-corpus/receipt.json` |

The final summary groups injected runs by faulted wrapper name and resource
finding count.

Less common: `../../scripts/update-all.sh --level 1` (build with every compiler), `cmake -S . -B build`
(detect installed compilers), `cmake -S . -B build` (verify required tools).

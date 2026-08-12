# Commands

Quick reference for `test-faults`. Every script also supports `--help`.
Run `./change-compiler.sh -c <compiler>` once before building.

| Command | What it does |
| --- | --- |
| `./change-compiler.sh -c <cc>` | Configure the build with a compiler (also `./change-compiler.sh <cc>`). `--help` lists detected compilers. |
| `./change-compiler.sh -c <cc> -s address,undefined` | Configure with specific sanitizers |
| `./change-compiler.sh -c <cc> --coverage` | Configure an instrumented build for coverage (gcov) |
| `./build.sh` | Strict analysis build: format-check, clang-tidy, cppcheck, static analyzer, `-Werror`, sanitizers. `-q` = quiet |
| `./build.sh -f` | Auto-fix in place: clang-tidy `--fix` + clang-format |
| `./build.sh -C` | Format check only, no build (hook-friendly); non-zero if unclean |
| `./check.sh` | **The gate:** format + strict build + tests + fuzz smoke -> one PASS/FAIL. `--cov <pct>` adds a coverage gate |
| `./test.sh` | Build & run the Unity test suite (ctest) |
| `./test-all.sh` | Run the tests across every supported compiler |
| `./fuzz.sh` | Run the libFuzzer target (coverage-guided + sanitizers); PASS/FAIL. `-t <secs>` sets the time budget |
| `./coverage-report.sh` | HTML coverage report. `--report-only` skips the run; `--min <pct>` fails under a threshold |
| `./report.sh coverage` \| `profile` | One entry point for the coverage / profiling reports |
| `./doctor.sh` | Report what actually works on this machine for this project |
| `./clean.sh` | Remove `build-` / `coverage-` / `profile-` output (`-n` previews) |
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

The final summary groups injected runs by faulted wrapper name and resource
finding count.

Less common: `./build-all.sh` (build with every compiler), `./check-compilers.sh`
(detect installed compilers), `./check-env.sh` (verify required tools).

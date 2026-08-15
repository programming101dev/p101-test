# p101-test

`p101-test` owns executable campaigns that go beyond one repository's CTest and
fuzz targets.

## Internal engines

- `test-faults`: run a baseline and bounded wrapper-failure cases through the
  shared capture/model policy pipeline.
- `test-mutation`: generate and execute bounded source mutations, reporting
  killed and surviving candidates.

Detailed contracts live in `components/mutation/README.md` and the fault
runner's source-level CLI help.

## Boundaries

Inputs are an executable command, explicit bounds, wrapper fault policy, and
mutation candidates. Outputs are case evidence and exit status. A clean result
only covers admitted and executed cases; it does not prove unexecuted paths,
third-party internals, or unbounded schedules.

`test-mutation` accepts `-d:human`, `-d:json`, or `-d:human,json` and uses the
shared `p101-tool-report-v1` lifecycle. Candidate inventory remains a distinct
machine document because it is an input set, not a finding run. `test-faults`
continues to produce the runtime campaign evidence owned by its capture/model
pipeline rather than pretending that evidence is a source-diagnostic report.

## Evidence

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DP101_BUILD_LEVEL=1
cmake --build build
cmake -S . -B build -DP101_BUILD_LEVEL=2 && cmake --build build
```

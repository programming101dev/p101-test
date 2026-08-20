# p101-test

`p101-test` owns executable campaigns that go beyond one repository's CTest and
fuzz targets.

## Internal engines

- `test-faults`: run a baseline and bounded wrapper-failure cases through the
  shared capture/model policy pipeline.
- `test-mutation`: generate and execute bounded source mutations, reporting
  killed and surviving candidates.
- `test-corpus`: execute the lesson fixtures owned by `playgrounds`, verify
  exit status, diagnostic IDs, error-path evidence, and observable output, and
  write `p101-corpus-receipt-v1`.

Detailed contracts live in `components/mutation/README.md` and the fault
runner's source-level CLI help.

## Boundaries

Inputs are executable commands, explicit bounds, wrapper fault policy, mutation
candidates, and the `expected.json` lesson fixtures owned by `playgrounds`.
Outputs are case evidence, versioned receipts, and exit status. `p101-test`
owns execution and diagnostic verification; it does not own lesson prose or
fixture policy. A clean result only covers admitted and executed cases; it does
not prove unexecuted paths, third-party internals, or unbounded schedules.

`test-mutation` accepts `-d:human`, `-d:json`, or `-d:human,json` and uses the
shared `p101-tool-report-v1` lifecycle. Candidate inventory remains a distinct
machine document because it is an input set, not a finding run. `test-faults`
continues to produce the runtime campaign evidence owned by its capture/model
pipeline rather than pretending that evidence is a source-diagnostic report.
The complete corpus receipt is the sole handoff to the student-facing
playground lab renderer. A changed oracle is recorded as evidence so students
can see progress, while CI requires `passed: true`; `lab.sh` never reruns or
re-judges the regression campaign.

## Evidence

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DP101_BUILD_LEVEL=1
cmake --build build
cmake -S . -B build -DP101_BUILD_LEVEL=2 && cmake --build build
```

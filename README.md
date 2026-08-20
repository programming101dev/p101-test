# p101-test

`p101-test` owns executable campaigns that go beyond one repository's CTest and
fuzz targets.

## Internal engines

- `test-faults`: run a baseline and bounded wrapper-failure cases through the
  shared capture/model policy pipeline.
- `test-mutation`: generate and execute bounded source mutations, reporting
  killed and surviving candidates.
- `test-command-contract`: execute an explicitly admitted command and check
  repeatability, sensitive-probe redaction, output and monotonic elapsed-time
  budgets, caller recovery status, or retained-schema acceptance.

Detailed contracts live in `components/mutation/README.md` and the fault
runner's source-level CLI help.

## Boundaries

Inputs are executable commands, explicit bounds, wrapper fault policy, and
mutation candidates. Outputs are case evidence, versioned receipts, and exit
status. `p101-test` owns execution and diagnostic verification. A clean result
only covers admitted and executed cases; it does not prove unexecuted paths,
third-party internals, or unbounded schedules.

`test-mutation` accepts `-d:human`, `-d:json`, or `-d:human,json` and uses the
shared `p101-tool-report-v1` lifecycle. Candidate inventory remains a distinct
machine document because it is an input set, not a finding run. `test-faults`
continues to produce the runtime campaign evidence owned by its capture/model
pipeline rather than pretending that evidence is a source-diagnostic report.

`test-command-contract` accepts the same three diagnostic-output selections.
It observes only the selected command's wait status and captured output, plus
the monotonic interval around that execution. The output limit is an
acceptance check after capture, not a memory or CPU sandbox; recovery and
schema compatibility cover only the cases supplied by the owning test.
Compatibility fixture commands remain responsible for asserting normalized
semantic fields before returning their declared status.
Redaction checks take a synthetic probe in `argv`; callers must not substitute
a real credential.

## Evidence

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DP101_BUILD_LEVEL=1
cmake --build build
cmake -S . -B build -DP101_BUILD_LEVEL=2 && cmake --build build
```

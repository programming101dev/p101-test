#!/usr/bin/env bash
set -euo pipefail

engine="${1:?test-corpus launcher required}"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/p101-test-corpus.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT

fixtures="$temporary/playgrounds"
mkdir -p "$fixtures/corpus/cases/clean" "$fixtures/corpus/cases/changed" "$fixtures/src"
cat > "$fixtures/corpus/cases/clean/expected.json" <<'JSON'
{
  "name": "clean",
  "lab_order": 0,
  "issue_id": "P101-LAB-000",
  "title": "Clean",
  "category": "baseline",
  "tracks": ["c"],
  "scenario": "tour",
  "expected_exit": 0,
  "expected_status": "PASS",
  "expected_findings": [],
  "fault_count": 0,
  "fix_goal": "Stay clean.",
  "fix_steps": [],
  "lesson": "Clean fixture."
}
JSON
cat > "$fixtures/corpus/cases/changed/expected.json" <<'JSON'
{
  "name": "changed",
  "lab_order": 1,
  "issue_id": "P101-LAB-001",
  "title": "Changed",
  "category": "diagnostic",
  "tracks": ["c"],
  "scenario": "changed",
  "expected_exit": 0,
  "expected_status": "FINDINGS",
  "expected_findings": ["P101-EXPECTED-001"],
  "fault_count": 0,
  "fix_goal": "Exercise a changed oracle.",
  "fix_steps": [],
  "lesson": "Changed fixture."
}
JSON

playground="$temporary/p101-tool-playground"
cat > "$playground" <<'SH'
#!/usr/bin/env bash
if [[ "${1:-}" == "-S" ]]; then
  printf 'P101SCENARIOS\t1\n'
  printf 'tour\texecutable-clean\tClean fixture\n'
  printf 'changed\texecutable-defect\tChanged fixture\n'
  exit 0
fi
exit 0
SH
chmod +x "$playground"

workflow="$temporary/student-workflow.sh"
cat > "$workflow" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
out=""
while (($#)); do
  case "$1" in
    -o) out="$2"; shift 2 ;;
    --) shift; break ;;
    *) shift ;;
  esac
done
mkdir -p "$out/runtime/analysis" "$out/doctor"
printf '{"findings":[],"summary":{"findings":0}}\n' > "$out/runtime/analysis/correlated-report.json"
printf '{"statuses":{}}\n' > "$out/doctor/doctor.json"
while (($#)); do
  if [[ "$1" == "-o" ]]; then
    mkdir -p "$(dirname -- "$2")"
    : > "$2"
    break
  fi
  shift
done
exit 0
SH
chmod +x "$workflow"

output="$temporary/output"
"$engine" \
  --fixtures "$fixtures" \
  --workflow "$workflow" \
  --playground "$playground" \
  --case clean \
  --strict \
  -o "$output"

grep -q '"schema": "p101-corpus-receipt-v1"' "$output/receipt.json"
grep -q '"passed": true' "$output/receipt.json"
grep -q '"completed_cases": 1' "$output/receipt.json"

changed_output="$temporary/changed-output"
set +e
"$engine" \
  --fixtures "$fixtures" \
  --workflow "$workflow" \
  --playground "$playground" \
  --case changed \
  --strict \
  -o "$changed_output"
changed_status=$?
set -e
[[ "$changed_status" -eq 1 ]]
grep -q '"passed": false' "$changed_output/receipt.json"
grep -q 'missing finding IDs: P101-EXPECTED-001' "$changed_output/receipt.json"

echo "p101 corpus engine tests: PASS"

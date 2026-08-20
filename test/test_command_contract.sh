#!/usr/bin/env bash
set -euo pipefail

tool=$1
work=$(mktemp -d "${TMPDIR:-/tmp}/p101-command-contract-test.XXXXXX")
trap 'rm -rf "$work"' EXIT

"$tool" determinism -- /bin/sh -c 'printf fixed'

status=0
"$tool" determinism -- /bin/sh -c 'printf "%s" "$$"' >"$work/nondeterministic.out" 2>"$work/nondeterministic.err" || status=$?
test "$status" -eq 1
grep -Fq '[P101-TEST-DETERMINISM-001]' "$work/nondeterministic.err"

status=0
"$tool" redaction p101-secret-probe -- /bin/sh -c 'printf p101-secret-probe' >"$work/redaction.out" 2>"$work/redaction.err" || status=$?
test "$status" -eq 1
grep -Fq '[P101-DATA-001]' "$work/redaction.err"

status=0
"$tool" redaction p101-secret-probe -- /bin/sh -c 'printf "prefix\000p101-secret-probe"' >"$work/binary-redaction.out" 2>"$work/binary-redaction.err" || status=$?
test "$status" -eq 1
grep -Fq '[P101-DATA-001]' "$work/binary-redaction.err"

status=0
"$tool" output-limit 4 -- /bin/sh -c 'printf 12345' >"$work/resource.out" 2>"$work/resource.err" || status=$?
test "$status" -eq 1
grep -Fq '[P101-RESOURCE-006]' "$work/resource.err"

"$tool" time-limit 5000 -- /bin/sh -c 'exit 0'
status=0
"$tool" time-limit 10 -- /bin/sh -c 'sleep 1' >"$work/time.out" 2>"$work/time.err" || status=$?
test "$status" -eq 1
grep -Fq '[P101-TIME-001]' "$work/time.err"

"$tool" -d:json output-limit 5 -- /bin/sh -c 'printf 12345' >"$work/clean.json"
test ! -s "$work/clean.json"

"$tool" recovery 7 -- /bin/sh -c 'exit 7'
status=0
"$tool" recovery 0 -- /bin/sh -c 'exit 7' >"$work/recovery.out" 2>"$work/recovery.err" || status=$?
test "$status" -eq 1
grep -Fq '[P101-TEST-RECOVERY-001]' "$work/recovery.err"

"$tool" schema-compatibility 0 -- /bin/sh -c 'exit 0'
status=0
"$tool" schema-compatibility 0 -- /bin/sh -c 'exit 2' >"$work/schema.out" 2>"$work/schema.err" || status=$?
test "$status" -eq 1
grep -Fq '[P101-SCHEMA-001]' "$work/schema.err"

"$tool" side-effect-determinism "$work/effects-clean" -- /bin/sh -c 'printf fixed > "$1"' sh "$work/effects-clean"
status=0
"$tool" side-effect-determinism "$work/effects-changing" -- /bin/sh -c 'printf "%s" "$$" > "$1"' sh "$work/effects-changing" >"$work/effects.out" 2>"$work/effects.err" || status=$?
test "$status" -eq 1
grep -Fq '[P101-TEST-SIDE-EFFECT-001]' "$work/effects.err"

printf 'command contract tests: PASS\n'

#!/usr/bin/env bash
set -euo pipefail

tool=$1
compiler=$2
sysroot=${3:-}
compiler_arg1=${4:-}
if [[ -z "$sysroot" && "$(uname -s)" == "Darwin" ]]; then
    sysroot=$(xcrun --sdk macosx --show-sdk-path)
fi
printf -v quoted_compiler '%q' "$compiler"
printf -v quoted_sysroot '%q' "$sysroot"
printf -v quoted_compiler_arg1 '%q' "$compiler_arg1"
work=$(mktemp -d "${TMPDIR:-/tmp}/test-mutation-test.XXXXXX")
trap 'rm -rf "$work"' EXIT

"$tool" --help >/dev/null
set +e
"$tool" -j >/dev/null 2>&1
status=$?
set -e
[ "$status" -eq 2 ]

cat >"$work/boundary.c" <<'SOURCE'
struct p101_env;
static void p101_close(const struct p101_env *env, void *err, int fd)
{
    (void)env;
    (void)err;
    (void)fd;
}

int below_limit(int value)
{
    return value < 7;
}

int combine(int left, int right, int enabled)
{
    int total = left + right;
    return enabled && total > 0;
}

void cleanup(const struct p101_env *env, void *err, int fd)
{
    p101_close(env, err, fd);
}
SOURCE

cat >"$work/driver.c" <<'SOURCE'
int below_limit(int value);
int main(void)
{
    return below_limit(7) == 0 ? 0 : 1;
}
SOURCE

cat >"$work/run-test.sh" <<SCRIPT
#!/usr/bin/env bash
set -euo pipefail
root=\$(CDPATH='' cd -- "\$(dirname -- "\${BASH_SOURCE[0]}")" && pwd)
compiler=$quoted_compiler
sysroot=$quoted_sysroot
compiler_arg1=$quoted_compiler_arg1
if [[ -n "\$compiler_arg1" ]]; then
    set -- "\$compiler_arg1"
fi
if [[ -n "\$sysroot" ]]; then
    set -- "\$@" -isysroot "\$sysroot"
fi
"\$compiler" "\$@" "\$root/boundary.c" "\$root/driver.c" -o "\$root/boundary-test"
"\$root/boundary-test"
SCRIPT
chmod +x "$work/run-test.sh"

escaped_work=${work//\\/\\\\}
escaped_work=${escaped_work//\"/\\\"}
compile_driver_arguments=""
if [[ -n "$compiler_arg1" ]]; then
    escaped_compiler_arg1=${compiler_arg1//\\/\\\\}
    escaped_compiler_arg1=${escaped_compiler_arg1//\"/\\\"}
    compile_driver_arguments=", \"$escaped_compiler_arg1\""
fi
if [[ -n "$sysroot" ]]; then
    escaped_sysroot=${sysroot//\\/\\\\}
    escaped_sysroot=${escaped_sysroot//\"/\\\"}
    compile_driver_arguments="$compile_driver_arguments, \"-isysroot\", \"$escaped_sysroot\""
fi
cat >"$work/compile_commands.json" <<JSON
[
  {
    "directory": "$escaped_work",
    "file": "$escaped_work/boundary.c",
    "arguments": ["$compiler"$compile_driver_arguments, "-c", "$escaped_work/boundary.c"]
  }
]
JSON

"$tool" --compile-db "$work/compile_commands.json" \
    --operator comparison-boundary --list "$work" |
    grep -q 'comparison-boundary'
"$tool" --compile-db "$work/compile_commands.json" \
    --operator logical-connective --list "$work" |
    grep -q 'logical-connective'
"$tool" --compile-db "$work/compile_commands.json" \
    --operator arithmetic-operator --list "$work" |
    grep -q 'arithmetic-operator'
"$tool" --compile-db "$work/compile_commands.json" \
    --operator skip-call --list "$work" |
    grep -q 'skip-call'
"$tool" --compile-db "$work/compile_commands.json" \
    --operator comparison-boundary --list -d:json "$work" >"$work/list.json"
grep -q '"schema":"p101-mutation-candidates-v2"' "$work/list.json"
"$tool" --compile-db "$work/compile_commands.json" \
    --operator comparison-boundary --list -d:human,json "$work" >"$work/list-both.json" 2>"$work/list-both.txt"
grep -q '"schema":"p101-mutation-candidates-v2"' "$work/list-both.json"
grep -q 'comparison-boundary' "$work/list-both.txt"

output=$("$tool" --compile-db "$work/compile_commands.json" \
    --operator comparison-boundary --max-mutants 1 "$work" -- bash "$work/run-test.sh")
grep -q 'selected=1 killed=1 survived=0' <<<"$output"

"$tool" --compile-db "$work/compile_commands.json" \
    --operator comparison-boundary --max-mutants 1 -d:json "$work" -- bash "$work/run-test.sh" \
    >"$work/result.json"
grep -q '"schema":"p101-tool-report-v1"' "$work/result.json"
grep -q '"tool":"test-mutation"' "$work/result.json"
grep -q '"summary":{"findings":0,"selected":1,"killed":1,"survived":0,"inconclusive":0}' "$work/result.json"
grep -q '"outcome":"clean","exit_status":0' "$work/result.json"

set +e
"$tool" --compile-db "$work/compile_commands.json" \
    --operator error-predicate "$work" -- bash "$work/run-test.sh" \
    >"$work/no-mutants.out" 2>"$work/no-mutants.err"
status=$?
set -e
[ "$status" -eq 2 ]
grep -q 'no mutants' "$work/no-mutants.err"

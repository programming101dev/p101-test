#!/usr/bin/env bash
# Additional component suite for the consolidated p101-test repository.
# This file is sourced after test.sh establishes the shared test variables.
# shellcheck disable=SC2154

if [ -n "$test_cache_root" ]; then
  component_test_bd="$test_cache_root/components/mutation/build-$sfx"
  mkdir -p "$(dirname "$component_test_bd")"
else
  component_test_bd="components/mutation/test/build-$sfx"
fi
if [ "$coverage" -eq 1 ]; then
  rm -rf "$component_test_bd"
fi
echo ">> configuring mutation tests ($component_test_bd)"
cmake -S components/mutation/test -B "$component_test_bd" \
  -U 'P101_*_LIBRARY' "$compflag" \
  ${compiler_driver_args[@]+"${compiler_driver_args[@]}"} \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON "$compile_flag_arg" \
  ${sanitizer_args[@]+"${sanitizer_args[@]}"} \
  ${p101_path_args[@]+"${p101_path_args[@]}"} "$cov_arg" >/dev/null
if [ "$coverage" -eq 1 ]; then
  find "$component_test_bd" -type f -name '*.gcda' -exec rm -f {} +
fi
echo ">> building mutation tests"
cmake --build "$component_test_bd"
echo ">> ctest: mutation"
( cd "$component_test_bd" && ctest --output-on-failure ${ctest_args[@]+"${ctest_args[@]}"} )

set(PROJECT_NAME "p101-test")
set(PROJECT_VERSION "2.0.0")
set(PROJECT_DESCRIPTION "Programming 101 executable fault and mutation test engines")
set(PROJECT_LANGUAGE "C")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Common compiler flags
set(STANDARD_FLAGS
        -D_POSIX_C_SOURCE=200809L
        -D_XOPEN_SOURCE=700
        -Werror
)

set(DARWIN_STANDARD_FLAGS
        -D_DARWIN_C_SOURCE
)

set(LINUX_STANDARD_FLAGS
)

set(BSD_STANDARD_FLAGS
)

# Define targets
set(EXECUTABLE_TARGETS test_faults test_mutation test_repository_receipt test_wrapper_conformance)
set(LIBRARY_TARGETS "")
set(test_faults_OUTPUT_NAME test-faults)
set(test_mutation_OUTPUT_NAME test-mutation)
set(test_repository_receipt_OUTPUT_NAME test-repository-receipt)
set(test_wrapper_conformance_OUTPUT_NAME test-wrapper-conformance)

set(test_faults_SOURCES
        src/cli.c
        src/main.c
        src/paths.c
        src/printer.c
        src/resource.c
        src/runner.c
)

set(test_mutation_SOURCES
        components/mutation/src/candidates.c
        components/mutation/src/cli.c
        components/mutation/src/execution.c
        components/mutation/src/files.c
        components/mutation/src/main.c
        components/mutation/src/output.c
)

set(test_repository_receipt_SOURCES
        components/repository-receipt/src/main.c
)

set(test_wrapper_conformance_SOURCES
        components/conformance/src/main.c
)

set(test_faults_HEADERS
        include/arguments.h
        include/cli.h
        include/constants.h
        include/errors.h
        include/paths.h
        include/printer.h
        include/resource.h
        include/result.h
        include/runner.h
)

set(test_mutation_HEADERS
        components/mutation/include/mutation_check.h
)

set(test_repository_receipt_HEADERS)
set(test_wrapper_conformance_HEADERS)

set(test_faults_LINK_LIBRARIES
        p101_error
        p101_env
        p101_record
        p101_tool_event
        p101_tool_support
        p101_c
        p101_cli
        p101_filesystem
        p101_io
        p101_process
        p101_convert
        p101_subprocess
        m
)

set(test_mutation_LINK_LIBRARIES
        p101_error
        p101_env
        p101_json
        p101_record
        p101_tool_support
        p101_c
        p101_c_facts
        p101_filesystem
        p101_io
        p101_process
        p101_time
        m
)

set(test_repository_receipt_LINK_LIBRARIES
        p101_error
        p101_env
        p101_json
        p101_record
        p101_tool_support
        p101_c
        p101_filesystem
)

set(test_wrapper_conformance_LINK_LIBRARIES
        p101_error
        p101_env
        p101_record
        p101_json
        p101_tool_support
        p101_c
        p101_io
)

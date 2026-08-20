#include <errno.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_convert/integer.h>
#include <p101_env/env.h>
#include <p101_error/error.h>
#include <p101_filesystem/p101_unistd.h>
#include <p101_process/p101_unistd.h>
#include <p101_subprocess/tool_run.h>
#include <p101_time/p101_time.h>
#include <p101_tool_support/diagnostic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>

enum
{
    EXIT_FINDING = 1,
    EXIT_TROUBLE = 2,
    PATH_SIZE    = 4096
};

enum contract_mode
{
    CONTRACT_DETERMINISM = 0,
    CONTRACT_REDACTION,
    CONTRACT_OUTPUT_LIMIT,
    CONTRACT_TIME_LIMIT,
    CONTRACT_RECOVERY,
    CONTRACT_SCHEMA_COMPATIBILITY,
    CONTRACT_SIDE_EFFECT_DETERMINISM
};

struct captured_file
{
    char  *data;
    size_t size;
};

struct run_capture
{
    struct captured_file standard_output;
    struct captured_file standard_error;
    int                  status;
};

static void usage(const struct p101_env *env, struct p101_error *err, const char *program);
static bool parse_mode(const struct p101_env *env, const char *text, enum contract_mode *mode);
static bool read_file(const struct p101_env *env, struct p101_error *err, const char *path, struct captured_file *file);
static bool run_command(const struct p101_env *env, struct p101_error *err, char *const command[], const char *prefix, unsigned int run_index, struct run_capture *capture);
static void destroy_capture(const struct p101_env *env, struct run_capture *capture);
static void remove_capture_files(const struct p101_env *env, const char *prefix, unsigned int run_index);
static bool captured_equal(const struct p101_env *env, const struct captured_file *left, const struct captured_file *right);
static bool captured_contains(const struct p101_env *env, const struct captured_file *file, const char *text);
static int  emit_finding(const struct p101_env *env, struct p101_error *err, unsigned int outputs, p101_tool_finding finding, const char *message);

int main(int argc, char **argv)
{
    struct p101_error   *err;
    struct p101_env     *env;
    struct run_capture   first;
    struct run_capture   second;
    struct captured_file first_effects;
    struct captured_file second_effects;
    enum contract_mode   mode;
    unsigned int         outputs;
    const char          *value;
    const char          *temporary_directory;
    char *const         *command;
    char                 prefix[PATH_SIZE];
    pid_t                pid;
    int                  argument_index;
    int                  comparison;
    int                  operation_status;
    int                  status;
    bool                 parsed;
    bool                 first_run;
    bool                 second_run;
    bool                 finding;
    struct timespec      started_at;
    struct timespec      finished_at;

    err   = p101_error_create(false);
    env   = p101_env_create(err, NULL);
    first = (struct run_capture){
        .standard_output = {.data = NULL, .size = 0U},
        .standard_error  = {.data = NULL, .size = 0U},
        .status          = 0
    };
    second              = first;
    first_effects       = (struct captured_file){.data = NULL, .size = 0U};
    second_effects      = first_effects;
    outputs             = P101_TOOL_DIAGNOSTIC_OUTPUT_HUMAN;
    value               = NULL;
    temporary_directory = NULL;
    command             = NULL;
    status              = EXIT_TROUBLE;
    parsed              = false;
    finding             = false;
    started_at          = (struct timespec){.tv_sec = 0, .tv_nsec = 0};
    finished_at         = started_at;
    prefix[0]           = '\0';
    argument_index      = 1;
    while(argument_index < argc)
    {
        comparison = p101_strncmp(env, argv[argument_index], "-d:", 3U);
        if(comparison != 0)
        {
            break;
        }
        operation_status = p101_tool_diagnostic_parse_outputs(argv[argument_index] + 3, &outputs);
        if(operation_status != 0)
        {
            P101_ERROR_RAISE_USER(err, "invalid diagnostic output selection", EINVAL);
            goto done;
        }
        argument_index++;
    }
    if(argument_index < argc)
    {
        parsed = parse_mode(env, argv[argument_index], &mode);
        argument_index++;
    }
    if(!parsed)
    {
        usage(env, P101_ERROR_OPTIONAL, argv[0]);
        goto done;
    }
    if(mode != CONTRACT_DETERMINISM)
    {
        if(argument_index >= argc)
        {
            usage(env, P101_ERROR_OPTIONAL, argv[0]);
            goto done;
        }
        value = argv[argument_index];
        argument_index++;
    }
    if(argument_index >= argc)
    {
        usage(env, P101_ERROR_OPTIONAL, argv[0]);
        goto done;
    }
    comparison = p101_strcmp(env, argv[argument_index], "--");
    if(comparison != 0 || argument_index + 1 >= argc)
    {
        usage(env, P101_ERROR_OPTIONAL, argv[0]);
        goto done;
    }
    command             = &argv[argument_index + 1];
    pid                 = p101_getpid(env);
    temporary_directory = p101_getenv(env, P101_ERROR_OPTIONAL, "TMPDIR");
    if(temporary_directory == NULL || temporary_directory[0] == '\0')
    {
        temporary_directory = P_tmpdir;
    }
    operation_status = p101_snprintf(env, err, prefix, sizeof(prefix), "%s/p101-command-contract-%ld", temporary_directory, (long)pid);
    if(operation_status < 0 || (size_t)operation_status >= sizeof(prefix))
    {
        goto done;
    }
    if(mode == CONTRACT_TIME_LIMIT)
    {
        operation_status = p101_clock_gettime(env, err, CLOCK_MONOTONIC, &started_at);
        if(operation_status != 0)
        {
            goto done;
        }
    }
    first_run = run_command(env, err, command, prefix, 1U, &first);
    if(!first_run)
    {
        goto done;
    }
    if(mode == CONTRACT_TIME_LIMIT)
    {
        operation_status = p101_clock_gettime(env, err, CLOCK_MONOTONIC, &finished_at);
        if(operation_status != 0)
        {
            goto done;
        }
    }
    if(mode == CONTRACT_DETERMINISM || mode == CONTRACT_SIDE_EFFECT_DETERMINISM)
    {
        bool output_equal;
        bool error_equal;

        if(mode == CONTRACT_SIDE_EFFECT_DETERMINISM)
        {
            bool effect_read;

            effect_read = read_file(env, err, value, &first_effects);
            if(!effect_read)
            {
                goto done;
            }
        }
        second_run = run_command(env, err, command, prefix, 2U, &second);
        if(!second_run)
        {
            goto done;
        }
        output_equal = captured_equal(env, &first.standard_output, &second.standard_output);
        error_equal  = captured_equal(env, &first.standard_error, &second.standard_error);
        if(mode == CONTRACT_SIDE_EFFECT_DETERMINISM)
        {
            bool effect_read;
            bool effects_equal;

            effect_read = read_file(env, err, value, &second_effects);
            if(!effect_read)
            {
                goto done;
            }
            effects_equal = captured_equal(env, &first_effects, &second_effects);
            if(first.status != second.status || !output_equal || !error_equal || !effects_equal)
            {
                operation_status = emit_finding(env, err, outputs, P101_TOOL_FINDING_TEST_SIDE_EFFECT_001, "identical admitted inputs produced different exit status, output bytes, or declared filesystem-effect manifest bytes");
                if(operation_status != 0)
                {
                    goto done;
                }
                finding = true;
            }
        }
        else if(first.status != second.status || !output_equal || !error_equal)
        {
            operation_status = emit_finding(env, err, outputs, P101_TOOL_FINDING_TEST_DETERMINISM_001, "identical admitted inputs produced different exit status or output bytes");
            if(operation_status != 0)
            {
                goto done;
            }
            finding = true;
        }
    }
    else if(mode == CONTRACT_REDACTION)
    {
        bool output_contains;
        bool error_contains;

        if(value[0] == '\0')
        {
            P101_ERROR_RAISE_USER(err, "the sensitive probe must not be empty", EINVAL);
            goto done;
        }
        output_contains = captured_contains(env, &first.standard_output, value);
        error_contains  = captured_contains(env, &first.standard_error, value);
        if(output_contains || error_contains)
        {
            operation_status = emit_finding(env, err, outputs, P101_TOOL_FINDING_DATA_001, "command output disclosed the admitted sensitive probe");
            if(operation_status != 0)
            {
                goto done;
            }
            finding = true;
        }
    }
    else if(mode == CONTRACT_OUTPUT_LIMIT)
    {
        unsigned long long maximum;
        size_t             total;
        size_t             maximum_size;
        bool               has_error;

        maximum      = p101_parse_unsigned_long_long(env, err, value, 0ULL);
        has_error    = p101_error_has_error(err);
        maximum_size = (size_t)maximum;
        if(has_error || (unsigned long long)maximum_size != maximum)
        {
            P101_ERROR_RAISE_USER(err, "the output limit must be a representable byte count", EINVAL);
            goto done;
        }
        total = first.standard_output.size + first.standard_error.size;
        if(total < first.standard_output.size || total > maximum_size)
        {
            operation_status = emit_finding(env, err, outputs, P101_TOOL_FINDING_RESOURCE_006, "command output exceeded its admitted byte budget");
            if(operation_status != 0)
            {
                goto done;
            }
            finding = true;
        }
    }
    else if(mode == CONTRACT_TIME_LIMIT)
    {
        unsigned long long maximum_milliseconds;
        time_t             elapsed_seconds;
        long               elapsed_nanoseconds;
        unsigned long long maximum_seconds;
        unsigned long long maximum_nanoseconds;
        bool               has_error;
        bool               exceeded;

        maximum_milliseconds = p101_parse_unsigned_long_long(env, err, value, 0ULL);
        has_error            = p101_error_has_error(err);
        if(has_error)
        {
            P101_ERROR_RAISE_USER(err, "the time limit must be a representable millisecond count", EINVAL);
            goto done;
        }
        elapsed_seconds     = finished_at.tv_sec - started_at.tv_sec;
        elapsed_nanoseconds = finished_at.tv_nsec - started_at.tv_nsec;
        if(elapsed_nanoseconds < 0L)
        {
            elapsed_seconds--;
            elapsed_nanoseconds += 1000000000L;
        }
        maximum_seconds     = maximum_milliseconds / 1000ULL;
        maximum_nanoseconds = (maximum_milliseconds % 1000ULL) * 1000000ULL;
        exceeded            = elapsed_seconds < 0;
        if(!exceeded)
        {
            exceeded = (unsigned long long)elapsed_seconds > maximum_seconds;
        }
        if(!exceeded && (unsigned long long)elapsed_seconds == maximum_seconds)
        {
            exceeded = (unsigned long long)elapsed_nanoseconds > maximum_nanoseconds;
        }
        if(exceeded)
        {
            operation_status = emit_finding(env, err, outputs, P101_TOOL_FINDING_TIME_001, "command exceeded its admitted monotonic elapsed-time budget");
            if(operation_status != 0)
            {
                goto done;
            }
            finding = true;
        }
    }
    else
    {
        unsigned long long expected;
        int                expected_status;
        bool               has_error;
        p101_tool_finding  contract_finding;
        const char        *contract_message;

        expected        = p101_parse_unsigned_long_long(env, err, value, 0ULL);
        has_error       = p101_error_has_error(err);
        expected_status = (int)expected;
        if(has_error || expected > 255ULL || expected_status < 0)
        {
            P101_ERROR_RAISE_USER(err, "the expected exit status must be between 0 and 255", EINVAL);
            goto done;
        }
        if(mode == CONTRACT_RECOVERY)
        {
            contract_finding = P101_TOOL_FINDING_TEST_RECOVERY_001;
            contract_message = "the admitted fault scenario did not produce the caller's expected recovery status";
        }
        else
        {
            contract_finding = P101_TOOL_FINDING_SCHEMA_001;
            contract_message = "the admitted compatibility fixture did not produce its expected status";
        }
        if(!WIFEXITED(first.status) || WEXITSTATUS(first.status) != expected_status)
        {
            operation_status = emit_finding(env, err, outputs, contract_finding, contract_message);
            if(operation_status != 0)
            {
                goto done;
            }
            finding = true;
        }
    }
    status = finding ? EXIT_FINDING : EXIT_SUCCESS;

done:
    remove_capture_files(env, prefix, 2U);
    remove_capture_files(env, prefix, 1U);
    destroy_capture(env, &second);
    destroy_capture(env, &first);
    p101_free(env, second_effects.data);
    p101_free(env, first_effects.data);
    {
        bool has_error;

        has_error = p101_error_has_error(err);
        if(has_error)
        {
            const char *message;

            message = p101_error_get_message(err);
            p101_fprintf(env, P101_ERROR_OPTIONAL, stderr, "%s:1:1: error: %s [P101-TEST-TROUBLE]\n", argv[0], message);
            status = EXIT_TROUBLE;
        }
    }
    p101_env_destroy(env);
    p101_error_destroy(err);
    return status;
}

static void usage(const struct p101_env *env, struct p101_error *err, const char *program)
{
    p101_fprintf(env,
                 err,
                 stderr,
                 "Usage: %s [-d:human|json|human,json] determinism -- COMMAND [ARG...]\n"
                 "       %s [-d:human|json|human,json] redaction SENSITIVE_PROBE -- COMMAND [ARG...]\n"
                 "       %s [-d:human|json|human,json] output-limit MAXIMUM_BYTES -- COMMAND [ARG...]\n"
                 "       %s [-d:human|json|human,json] time-limit MAXIMUM_MILLISECONDS -- COMMAND [ARG...]\n"
                 "       %s [-d:human|json|human,json] recovery EXPECTED_STATUS -- COMMAND [ARG...]\n"
                 "       %s [-d:human|json|human,json] schema-compatibility EXPECTED_STATUS -- COMMAND [ARG...]\n"
                 "       %s [-d:human|json|human,json] side-effect-determinism EFFECT_MANIFEST -- COMMAND [ARG...]\n",
                 program,
                 program,
                 program,
                 program,
                 program,
                 program,
                 program);
}

static bool parse_mode(const struct p101_env *env, const char *text, enum contract_mode *mode)
{
    bool parsed;
    int  comparison;

    parsed     = true;
    comparison = p101_strcmp(env, text, "determinism");
    if(comparison == 0)
    {
        *mode = CONTRACT_DETERMINISM;
    }
    else
    {
        comparison = p101_strcmp(env, text, "redaction");
        if(comparison == 0)
        {
            *mode = CONTRACT_REDACTION;
        }
        else
        {
            comparison = p101_strcmp(env, text, "output-limit");
            if(comparison == 0)
            {
                *mode = CONTRACT_OUTPUT_LIMIT;
            }
            else
            {
                comparison = p101_strcmp(env, text, "time-limit");
                if(comparison == 0)
                {
                    *mode = CONTRACT_TIME_LIMIT;
                }
                else
                {
                    comparison = p101_strcmp(env, text, "recovery");
                    if(comparison == 0)
                    {
                        *mode = CONTRACT_RECOVERY;
                    }
                    else
                    {
                        comparison = p101_strcmp(env, text, "schema-compatibility");
                        if(comparison == 0)
                        {
                            *mode = CONTRACT_SCHEMA_COMPATIBILITY;
                        }
                        else
                        {
                            comparison = p101_strcmp(env, text, "side-effect-determinism");
                            if(comparison == 0)
                            {
                                *mode = CONTRACT_SIDE_EFFECT_DETERMINISM;
                            }
                            else
                            {
                                parsed = false;
                            }
                        }
                    }
                }
            }
        }
    }
    return parsed;
}

static bool read_file(const struct p101_env *env, struct p101_error *err, const char *path, struct captured_file *file)
{
    FILE  *stream;
    long   length;
    int    operation_status;
    size_t amount;
    bool   read_ok;

    read_ok = false;
    stream  = p101_fopen(env, err, path, "rb");
    if(stream == NULL)
    {
        goto done;
    }
    operation_status = p101_fseek(env, err, stream, 0L, SEEK_END);
    if(operation_status != 0)
    {
        goto close_stream;
    }
    length = p101_ftell(env, err, stream);
    if(length < 0 || (unsigned long)length > SIZE_MAX - 1U)
    {
        P101_ERROR_RAISE_ERRNO(err, EOVERFLOW);
        goto close_stream;
    }
    operation_status = p101_fseek(env, err, stream, 0L, SEEK_SET);
    if(operation_status != 0)
    {
        goto close_stream;
    }
    file->data = (char *)p101_malloc(env, err, (size_t)length + 1U);
    if(file->data == NULL)
    {
        goto close_stream;
    }
    amount = p101_fread(env, err, file->data, 1U, (size_t)length, stream);
    if(amount != (size_t)length)
    {
        goto close_stream;
    }
    file->data[amount] = '\0';
    file->size         = amount;
    read_ok            = true;

close_stream:
    operation_status = p101_fclose(env, P101_ERROR_OPTIONAL, stream);
    (void)operation_status;
done:
    return read_ok;
}

static bool run_command(const struct p101_env *env, struct p101_error *err, char *const command[], const char *prefix, unsigned int run_index, struct run_capture *capture)
{
    struct p101_tool_run_options options;
    char                         stdout_path[PATH_SIZE];
    char                         stderr_path[PATH_SIZE];
    int                          written;
    bool                         ran;
    bool                         has_error;
    bool                         read_ok;

    ran     = false;
    written = p101_snprintf(env, err, stdout_path, sizeof(stdout_path), "%s-%u.stdout", prefix, run_index);
    if(written < 0 || (size_t)written >= sizeof(stdout_path))
    {
        goto done;
    }
    written = p101_snprintf(env, err, stderr_path, sizeof(stderr_path), "%s-%u.stderr", prefix, run_index);
    if(written < 0 || (size_t)written >= sizeof(stderr_path))
    {
        goto done;
    }
    options.stdout_path         = stdout_path;
    options.stderr_path         = stderr_path;
    options.diagnostic_name     = "test-command-contract";
    options.output_mode         = 0600;
    options.child_setup         = NULL;
    options.child_setup_context = NULL;
    capture->status             = p101_tool_run_capture(env, err, command, &options);
    has_error                   = p101_error_has_error(err);
    if(has_error)
    {
        goto done;
    }
    read_ok = read_file(env, err, stdout_path, &capture->standard_output);
    if(!read_ok)
    {
        goto done;
    }
    read_ok = read_file(env, err, stderr_path, &capture->standard_error);
    if(!read_ok)
    {
        goto done;
    }
    ran = true;

done:
    return ran;
}

static void destroy_capture(const struct p101_env *env, struct run_capture *capture)
{
    p101_free(env, capture->standard_error.data);
    p101_free(env, capture->standard_output.data);
    *capture = (struct run_capture){
        .standard_output = {.data = NULL, .size = 0U},
        .standard_error  = {.data = NULL, .size = 0U},
        .status          = 0
    };
}

static void remove_capture_files(const struct p101_env *env, const char *prefix, unsigned int run_index)
{
    char path[PATH_SIZE];
    int  written;
    int  operation_status;

    if(prefix[0] == '\0')
    {
        goto done;
    }
    written = p101_snprintf(env, P101_ERROR_OPTIONAL, path, sizeof(path), "%s-%u.stdout", prefix, run_index);
    if(written >= 0 && (size_t)written < sizeof(path))
    {
        operation_status = p101_unlink(env, P101_ERROR_OPTIONAL, path);
        (void)operation_status;
    }
    written = p101_snprintf(env, P101_ERROR_OPTIONAL, path, sizeof(path), "%s-%u.stderr", prefix, run_index);
    if(written >= 0 && (size_t)written < sizeof(path))
    {
        operation_status = p101_unlink(env, P101_ERROR_OPTIONAL, path);
        (void)operation_status;
    }

done:
    return;
}

static bool captured_equal(const struct p101_env *env, const struct captured_file *left, const struct captured_file *right)
{
    bool equal;
    int  comparison;

    equal = left->size == right->size;
    if(equal && left->size > 0U)
    {
        comparison = p101_memcmp(env, left->data, right->data, left->size);
        equal      = comparison == 0;
    }
    return equal;
}

static bool captured_contains(const struct p101_env *env, const struct captured_file *file, const char *text)
{
    size_t text_size;
    bool   found;

    text_size = p101_strlen(env, text);
    found     = false;
    if(text_size <= file->size)
    {
        for(size_t offset = 0U; offset <= file->size - text_size; offset++)
        {
            int comparison;

            comparison = p101_memcmp(env, file->data + offset, text, text_size);
            if(comparison == 0)
            {
                found = true;
                break;
            }
        }
    }
    return found;
}

static int emit_finding(const struct p101_env *env, struct p101_error *err, unsigned int outputs, p101_tool_finding finding, const char *message)
{
    struct p101_tool_diagnostic diagnostic;
    FILE                       *human_stream;
    FILE                       *json_stream;
    int                         operation_status;

    human_stream     = (outputs & P101_TOOL_DIAGNOSTIC_OUTPUT_HUMAN) != 0U ? stderr : NULL;
    json_stream      = (outputs & P101_TOOL_DIAGNOSTIC_OUTPUT_JSON) != 0U ? stdout : NULL;
    operation_status = p101_tool_diagnostic_initialize(&diagnostic, finding, P101_TOOL_DIAGNOSTIC_ERROR, "<command>", 1U, 1U, NULL, message);
    if(operation_status == 0)
    {
        operation_status = p101_tool_diagnostic_write_outputs(human_stream, json_stream, &diagnostic);
    }
    if(operation_status != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno == 0 ? EIO : errno);
    }
    else if(json_stream != NULL)
    {
        p101_fputc(env, err, '\n', json_stream);
    }
    return operation_status;
}

#include "../include/mutation_check.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_c_facts/compile_command.h>
#include <p101_filesystem/p101_dirent.h>
#include <p101_filesystem/p101_fnmatch.h>
#include <p101_filesystem/p101_ftw.h>
#include <p101_filesystem/p101_glob.h>
#include <p101_filesystem/p101_libgen.h>
#include <p101_filesystem/p101_stdio.h>
#include <p101_filesystem/p101_stdlib.h>
#include <p101_filesystem/p101_unistd.h>
#include <p101_filesystem/sys/p101_stat.h>
#include <p101_filesystem/sys/p101_statvfs.h>
#include <p101_io/p101_aio.h>
#include <p101_io/p101_fcntl.h>
#include <p101_io/p101_poll.h>
#include <p101_io/p101_stdio.h>
#include <p101_io/p101_unistd.h>
#include <p101_io/sys/p101_select.h>
#include <p101_io/sys/p101_uio.h>
#include <p101_json/json.h>
#include <p101_process/p101_sched.h>
#include <p101_process/p101_setjmp.h>
#include <p101_process/p101_signal.h>
#include <p101_process/p101_spawn.h>
#include <p101_process/p101_stdio.h>
#include <p101_process/p101_stdlib.h>
#include <p101_process/p101_unistd.h>
#include <p101_process/sys/p101_resource.h>
#include <p101_process/sys/p101_times.h>
#include <p101_process/sys/p101_wait.h>
#include <p101_time/p101_time.h>
#include <p101_tool_support/report.h>
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum
{
    MUTATION_DIAGNOSTIC_MESSAGE_SIZE = 1024U
};

static void json_string(const struct p101_env *env, struct p101_error *err, FILE *stream, const char *text)
{
    int p101_call_result_1;
    P101_TRACE_SCOPE(env);
    p101_call_result_1 = p101_json_write_string(stream, text == NULL ? "" : text);
    if(p101_call_result_1 != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno == 0 ? EIO : errno);
    }
}

static void report_check(struct p101_error *err, int status)
{
    if(status != 0)
    {
        P101_ERROR_RAISE_ERRNO(err, errno == 0 ? EIO : errno);
    }
}

static void mutation_message(const struct p101_env *env, struct p101_error *err, const struct p101_mutation_candidate *candidate, char *message, size_t message_size)
{
    const char *mutation_kind;
    int         written;

    P101_TRACE_SCOPE(env);
    mutation_kind = p101_c_mutation_kind_name(candidate->kind);
    written       = p101_snprintf(env, err, message, message_size, "surviving %s mutation: %s -> %s", mutation_kind, candidate->original, candidate->replacement);
    if(written < 0 || (size_t)written >= message_size)
    {
        P101_ERROR_RAISE_ERRNO(err, EOVERFLOW);
    }
}

void p101_mutation_report_results(const struct p101_env *env, struct p101_error *err, const struct p101_mutation_arguments *arguments, const struct p101_mutation_result results[], size_t result_count)
{
    struct p101_tool_report_options report_options = {"test-mutation",
                                                      "Clang mutation candidates selected from the requested sources and outcomes from the configured test command.",
                                                      "This report does not prove correctness, and it cannot assess unselected mutations, unexecuted paths, flaky tests, or equivalent mutants.",
                                                      0U,
                                                      true};
    struct p101_tool_report_counter counters[]     = {
        {"selected",     result_count},
        {"killed",       0U          },
        {"survived",     0U          },
        {"inconclusive", 0U          }
    };
    struct p101_tool_report report;
    p101_tool_outcome       outcome;
    size_t                  index;
    int                     exit_status;
    int                     report_status;

    P101_TRACE_SCOPE(env);
    if(arguments->human)
    {
        report_options.outputs |= P101_TOOL_DIAGNOSTIC_OUTPUT_HUMAN;
    }
    if(arguments->json)
    {
        report_options.outputs |= P101_TOOL_DIAGNOSTIC_OUTPUT_JSON;
    }
    if(report_options.outputs == 0U)
    {
        report_options.outputs = P101_TOOL_DIAGNOSTIC_OUTPUT_HUMAN;
    }
    report_status = p101_tool_report_begin(&report, stdout, stderr, &report_options);
    report_check(err, report_status);
    if(report_status != 0)
    {
        goto done;
    }
    for(index = 0U; index < result_count; index++)
    {
        if(results[index].outcome == P101_MUTATION_OUTCOME_KILLED)
        {
            counters[1].value++;
        }
        else if(results[index].outcome == P101_MUTATION_OUTCOME_SURVIVED)
        {
            counters[2].value++;
        }
        else
        {
            counters[3].value++;
        }
    }
    for(index = 0U; index < result_count; index++)
    {
        const struct p101_mutation_candidate *candidate;
        struct p101_tool_diagnostic           diagnostic;
        char                                  message[MUTATION_DIAGNOSTIC_MESSAGE_SIZE];

        if(results[index].outcome != P101_MUTATION_OUTCOME_SURVIVED)
        {
            continue;
        }
        candidate = results[index].candidate;
        mutation_message(env, err, candidate, message, sizeof(message));
        report_status = p101_tool_diagnostic_initialize(&diagnostic, P101_TOOL_FINDING_MUTATION_001, P101_TOOL_DIAGNOSTIC_WARNING, candidate->path, candidate->line, 0U, "", message);
        report_check(err, report_status);
        if(report_status != 0)
        {
            goto done;
        }
        report_status = p101_tool_report_emit(&report, &diagnostic);
        report_check(err, report_status);
    }
    if(counters[3].value > 0U)
    {
        outcome = P101_TOOL_OUTCOME_TOOL_ERROR;
    }
    else if(counters[2].value > 0U)
    {
        outcome = P101_TOOL_OUTCOME_FINDINGS;
    }
    else
    {
        outcome = P101_TOOL_OUTCOME_CLEAN;
    }
    exit_status   = p101_tool_outcome_exit_status(outcome);
    report_status = p101_tool_report_end(&report, outcome, exit_status, counters, sizeof(counters) / sizeof(counters[0]));
    report_check(err, report_status);

done:
    return;
}

void p101_mutation_list_candidates(const struct p101_env *env, struct p101_error *err, const struct p101_mutation_arguments *arguments, const struct p101_mutation_candidates *candidates)
{
    const char *p101_call_result_7;
    const char *p101_call_result_8;
    size_t      index;
    FILE       *human_stream;

    P101_TRACE_SCOPE(env);
    if(arguments->json)
    {
        human_stream = stderr;
    }
    else
    {
        human_stream = stdout;
    }
    if(arguments->json)
    {
        p101_fputs(env, err, "{\"schema\":\"p101-mutation-candidates-v2\",\"candidates\":[", stdout);
    }
    for(index = 0U; index < candidates->count; index++)
    {
        const struct p101_mutation_candidate *candidate;

        candidate = &candidates->items[index];
        if(arguments->json)
        {
            if(index > 0U)
            {
                p101_fputc(env, err, ',', stdout);
            }
            p101_fputs(env, err, "{\"path\":", stdout);
            json_string(env, err, stdout, candidate->path);
            p101_fprintf(env, err, stdout, ",\"line\":%zu,\"operator\":", candidate->line);
            p101_call_result_7 = p101_c_mutation_kind_name(candidate->kind);
            json_string(env, err, stdout, p101_call_result_7);
            p101_fputs(env, err, ",\"original\":", stdout);
            json_string(env, err, stdout, candidate->original);
            p101_fputs(env, err, ",\"replacement\":", stdout);
            json_string(env, err, stdout, candidate->replacement);
            p101_fputc(env, err, '}', stdout);
        }
        if(arguments->human)
        {
            p101_call_result_8 = p101_c_mutation_kind_name(candidate->kind);
            p101_fprintf(env, err, human_stream, "%s:%zu: %s: %s -> %s\n", candidate->path, candidate->line, p101_call_result_8, candidate->original, candidate->replacement);
        }
    }
    if(arguments->json)
    {
        p101_fprintf(env, err, stdout, "],\"summary\":{\"selected\":%zu}}\n", candidates->count);
    }
    if(arguments->human)
    {
        p101_fprintf(env, err, human_stream, "test-mutation: %zu candidate(s)\n", candidates->count);
    }
}

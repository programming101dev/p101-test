#include <errno.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_filesystem/p101_dirent.h>
#include <p101_json/json.h>
#include <p101_record/record.h>
#include <p101_tool_support/diagnostic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    MAX_RECORDS      = 256,
    MAX_PATH         = 4096,
    MAX_LINE         = 16384,
    RESULT_FIELDS    = 6,
    INTEGER_BASE     = 10,
    FIELD_REPOSITORY = 0,
    FIELD_UNIT       = 1,
    FIELD_FUZZ       = 2,
    FIELD_TEST_LOG   = 3,
    FIELD_FUZZ_LOG   = 4,
    FIELD_DURATION   = 5
};

struct repository_result
{
    char   repository[MAX_PATH];
    char   unit[MAX_PATH];
    char   fuzz[MAX_PATH];
    char   test_log[MAX_PATH];
    char   fuzz_log[MAX_PATH];
    size_t duration_seconds;
};

static void usage(const struct p101_env *env, struct p101_error *err, const char *program);
static bool parse_arguments(const struct p101_env *env, struct p101_error *err, int argc, char **argv, const char **results_path, const char **output_path, bool *help);
static bool load_results(const struct p101_env *env, struct p101_error *err, const char *directory, struct repository_result records[MAX_RECORDS], size_t *record_count);
static bool load_result(const struct p101_env *env, struct p101_error *err, const char *path, struct repository_result *record);
static bool write_receipt(const struct p101_env *env, struct p101_error *err, const char *path, const struct repository_result records[MAX_RECORDS], size_t record_count, bool passed);
static bool write_json_string(struct p101_error *err, FILE *stream, const char *text);
static bool has_suffix(const struct p101_env *env, const char *text, const char *suffix);
static void sort_paths(const struct p101_env *env, char paths[MAX_RECORDS][MAX_PATH], size_t count);
static bool copy_field(const struct p101_env *env, struct p101_error *err, char *output, size_t output_size, const char *input);

int main(int argc, char **argv)
{
    struct repository_result    records[MAX_RECORDS];
    struct p101_error          *err;
    struct p101_env            *env;
    struct p101_tool_diagnostic diagnostic;
    const char                 *results_path;
    const char                 *output_path;
    const char                 *message;
    size_t                      record_count;
    size_t                      index;
    bool                        help;
    bool                        parsed;
    bool                        loaded;
    bool                        passed;
    bool                        written;
    bool                        has_error;
    int                         unit_comparison;
    int                         fuzz_comparison;
    int                         status;

    err          = p101_error_create(false);
    env          = p101_env_create(err, NULL);
    results_path = NULL;
    output_path  = NULL;
    record_count = 0U;
    status       = 2;
    parsed       = parse_arguments(env, err, argc, argv, &results_path, &output_path, &help);
    if(!parsed)
    {
        usage(env, P101_ERROR_OPTIONAL, argv[0]);
        if(help)
        {
            status = EXIT_SUCCESS;
        }
        goto done;
    }
    loaded = load_results(env, err, results_path, records, &record_count);
    if(!loaded)
    {
        goto done;
    }
    passed = record_count > 0U;
    for(index = 0U; index < record_count; index++)
    {
        unit_comparison = p101_strcmp(env, records[index].unit, "FAIL");
        if(unit_comparison != 0)
        {
            unit_comparison = p101_strcmp(env, records[index].unit, "MISSING");
        }
        fuzz_comparison = p101_strcmp(env, records[index].fuzz, "FAIL");
        if(unit_comparison == 0 || fuzz_comparison == 0)
        {
            passed = false;
        }
    }
    written = write_receipt(env, err, output_path, records, record_count, passed);
    if(written)
    {
        if(passed)
        {
            status = EXIT_SUCCESS;
        }
        else
        {
            status = EXIT_FAILURE;
        }
    }

done:
    has_error = p101_error_has_error(err);
    if(has_error)
    {
        message = p101_error_get_message(err);
        status  = p101_tool_diagnostic_initialize_id(&diagnostic, "P101-TEST-RECEIPT-001", P101_TOOL_DIAGNOSTIC_ERROR, argv[0], 1U, 1U, "main", message);
        if(status == 0)
        {
            status = p101_tool_diagnostic_write(stderr, P101_TOOL_DIAGNOSTIC_TEXT, &diagnostic);
        }
        status = 2;
    }
    p101_env_destroy(env);
    p101_error_destroy(err);
    return status;
}

static void usage(const struct p101_env *env, struct p101_error *err, const char *program)
{
    p101_fprintf(env, err, stderr, "Usage: %s --results DIRECTORY --output FILE\n", program);
}

static bool parse_arguments(const struct p101_env *env, struct p101_error *err, int argc, char **argv, const char **results_path, const char **output_path, bool *help)
{
    int  index;
    bool parsed;

    *help  = false;
    parsed = true;
    for(index = 1; index < argc && parsed; index++)
    {
        int comparison;

        comparison = p101_strcmp(env, argv[index], "-h");
        if(comparison != 0)
        {
            comparison = p101_strcmp(env, argv[index], "--help");
        }
        if(comparison == 0)
        {
            *help  = true;
            parsed = false;
            continue;
        }
        comparison = p101_strcmp(env, argv[index], "--results");
        if(comparison == 0 && index + 1 < argc)
        {
            index++;
            *results_path = argv[index];
            continue;
        }
        comparison = p101_strcmp(env, argv[index], "--output");
        if(comparison == 0 && index + 1 < argc)
        {
            index++;
            *output_path = argv[index];
            continue;
        }
        P101_ERROR_RAISE_USER(err, "unknown or incomplete repository-receipt option", EINVAL);
        parsed = false;
    }
    if(parsed && (*results_path == NULL || *output_path == NULL))
    {
        P101_ERROR_RAISE_USER(err, "--results and --output are required", EINVAL);
        parsed = false;
    }
    return parsed;
}

static bool load_results(const struct p101_env *env, struct p101_error *err, const char *directory, struct repository_result records[MAX_RECORDS], size_t *record_count)
{
    char           paths[MAX_RECORDS][MAX_PATH];
    char           full_path[MAX_PATH];
    DIR           *stream;
    struct dirent *entry;
    size_t         path_count;
    size_t         index;
    bool           suffix_matches;
    bool           loaded;
    int            close_status;

    loaded     = false;
    path_count = 0U;
    stream     = p101_opendir(env, err, directory);
    if(stream == NULL)
    {
        goto done;
    }
    for(;;)
    {
        entry = p101_readdir(env, err, stream);
        if(entry == NULL)
        {
            break;
        }
        suffix_matches = has_suffix(env, entry->d_name, ".result");
        if(!suffix_matches)
        {
            continue;
        }
        if(path_count >= MAX_RECORDS)
        {
            P101_ERROR_RAISE_USER(err, "too many repository result records", EOVERFLOW);
            break;
        }
        p101_snprintf(env, err, paths[path_count], sizeof(paths[path_count]), "%s", entry->d_name);
        path_count++;
    }
    close_status = p101_closedir(env, err, stream);
    if(close_status != 0)
    {
        goto done;
    }
    sort_paths(env, paths, path_count);
    for(index = 0U; index < path_count; index++)
    {
        p101_snprintf(env, err, full_path, sizeof(full_path), "%s/%s", directory, paths[index]);
        loaded = load_result(env, err, full_path, &records[index]);
        if(!loaded)
        {
            goto done;
        }
    }
    *record_count = path_count;
    loaded        = p101_error_has_no_error(err);

done:
    return loaded;
}

static bool load_result(const struct p101_env *env, struct p101_error *err, const char *path, struct repository_result *record)
{
    char   line[MAX_LINE];
    char   extra[2];
    char  *fields[RESULT_FIELDS];
    char  *cursor;
    char  *end;
    FILE  *stream;
    size_t field_count;
    long   duration;
    bool   loaded;
    bool   copied;
    int    close_status;

    loaded = false;
    stream = p101_fopen(env, err, path, "r");
    if(stream == NULL)
    {
        goto done;
    }
    if(p101_fgets(env, err, line, sizeof(line), stream) == NULL)
    {
        P101_ERROR_RAISE_USER(err, "repository result is empty", EINVAL);
        goto close;
    }
    if(p101_fgets(env, err, extra, sizeof(extra), stream) != NULL)
    {
        P101_ERROR_RAISE_USER(err, "repository result has multiple lines", EINVAL);
        goto close;
    }
    cursor = line;
    for(field_count = 0U; field_count < RESULT_FIELDS; field_count++)
    {
        char *separator;

        fields[field_count] = cursor;
        if(cursor == NULL)
        {
            break;
        }
        separator = cursor;
        while(*separator != '\0' && *separator != '|')
        {
            separator++;
        }
        if(*separator == '\0')
        {
            separator = NULL;
        }
        if(separator == NULL)
        {
            cursor = NULL;
        }
        else
        {
            *separator = '\0';
            cursor     = separator + 1;
        }
    }
    if(field_count != RESULT_FIELDS || cursor != NULL)
    {
        P101_ERROR_RAISE_USER(err, "repository result must contain six fields", EINVAL);
        goto close;
    }
    fields[FIELD_DURATION][p101_strcspn(env, fields[FIELD_DURATION], "\r\n")] = '\0';
    duration                                                                  = p101_strtol(env, err, fields[FIELD_DURATION], &end, INTEGER_BASE);
    if(end == fields[FIELD_DURATION] || *end != '\0' || duration < 0)
    {
        P101_ERROR_RAISE_USER(err, "repository result has an invalid duration", EINVAL);
        goto close;
    }
    loaded                   = copy_field(env, err, record->repository, sizeof(record->repository), fields[FIELD_REPOSITORY]);
    copied                   = copy_field(env, err, record->unit, sizeof(record->unit), fields[FIELD_UNIT]);
    loaded                   = (bool)(copied && loaded);
    copied                   = copy_field(env, err, record->fuzz, sizeof(record->fuzz), fields[FIELD_FUZZ]);
    loaded                   = (bool)(copied && loaded);
    copied                   = copy_field(env, err, record->test_log, sizeof(record->test_log), fields[FIELD_TEST_LOG]);
    loaded                   = (bool)(copied && loaded);
    copied                   = copy_field(env, err, record->fuzz_log, sizeof(record->fuzz_log), fields[FIELD_FUZZ_LOG]);
    loaded                   = (bool)(copied && loaded);
    record->duration_seconds = (size_t)duration;

close:
    close_status = p101_fclose(env, err, stream);
    if(close_status != 0)
    {
        loaded = false;
    }

done:
    return loaded;
}

static bool write_receipt(const struct p101_env *env, struct p101_error *err, const char *path, const struct repository_result records[MAX_RECORDS], size_t record_count, bool passed)
{
    FILE       *stream;
    size_t      index;
    const char *passed_text;
    bool        written;
    int         write_status;
    int         close_status;

    written = false;
    stream  = p101_fopen(env, err, path, "w");
    if(stream == NULL)
    {
        goto done;
    }
    if(passed)
    {
        passed_text = "true";
    }
    else
    {
        passed_text = "false";
    }
    write_status = p101_fprintf(env, err, stream, "{\n  \"schema\": \"p101-repository-test-receipt-v1\",\n  \"passed\": %s,\n  \"repositories\": [", passed_text);
    if(write_status < 0)
    {
        goto close;
    }
    for(index = 0U; index < record_count; index++)
    {
        if(index > 0U)
        {
            p101_fputc(env, err, ',', stream);
        }
        p101_fputs(env, err, "\n    {\"repository\": ", stream);
        write_json_string(err, stream, records[index].repository);
        p101_fputs(env, err, ", \"unit\": ", stream);
        write_json_string(err, stream, records[index].unit);
        p101_fputs(env, err, ", \"fuzz\": ", stream);
        write_json_string(err, stream, records[index].fuzz);
        p101_fputs(env, err, ", \"test_log\": ", stream);
        write_json_string(err, stream, records[index].test_log);
        p101_fputs(env, err, ", \"fuzz_log\": ", stream);
        write_json_string(err, stream, records[index].fuzz_log);
        p101_fprintf(env, err, stream, ", \"duration_seconds\": %zu}", records[index].duration_seconds);
    }
    p101_fputs(
        env,
        err,
        "\n  ],\n  \"does_not_prove\": \"A clean receipt proves only that each repository-owned test launcher admitted by repos.txt either completed in this campaign or reused stricter unit evidence admitted by the runner. It does not prove that an unregistered test, platform, branch, or third-party dependency ran.\"\n}\n",
        stream);
    written = p101_error_has_no_error(err);

close:
    close_status = p101_fclose(env, err, stream);
    if(close_status != 0)
    {
        written = false;
    }

done:
    return written;
}

static bool write_json_string(struct p101_error *err, FILE *stream, const char *text)
{
    int  status;
    bool written;

    status  = p101_json_write_string(stream, text);
    written = status == 0;
    if(!written)
    {
        P101_ERROR_RAISE_ERRNO(err, errno);
    }
    return written;
}

static bool has_suffix(const struct p101_env *env, const char *text, const char *suffix)
{
    size_t text_length;
    size_t suffix_length;
    bool   matches;

    text_length   = p101_strlen(env, text);
    suffix_length = p101_strlen(env, suffix);
    matches       = text_length >= suffix_length;
    if(matches)
    {
        int comparison;

        comparison = p101_strcmp(env, text + text_length - suffix_length, suffix);
        matches    = comparison == 0;
    }
    return matches;
}

static void sort_paths(const struct p101_env *env, char paths[MAX_RECORDS][MAX_PATH], size_t count)
{
    char   temporary[MAX_PATH];
    size_t index;

    for(index = 1U; index < count; index++)
    {
        size_t cursor;

        p101_memcpy(env, temporary, paths[index], sizeof(temporary));
        cursor = index;
        while(cursor > 0U)
        {
            int comparison;

            comparison = p101_strcmp(env, paths[cursor - 1U], temporary);
            if(comparison <= 0)
            {
                break;
            }
            p101_memcpy(env, paths[cursor], paths[cursor - 1U], sizeof(paths[cursor]));
            cursor--;
        }
        p101_memcpy(env, paths[cursor], temporary, sizeof(paths[cursor]));
    }
}

static bool copy_field(const struct p101_env *env, struct p101_error *err, char *output, size_t output_size, const char *input)
{
    size_t length;
    bool   copied;

    length = p101_strlen(env, input);
    copied = length < output_size;
    if(copied)
    {
        p101_memcpy(env, output, input, length + 1U);
    }
    else
    {
        P101_ERROR_RAISE_USER(err, "repository result field is too long", EOVERFLOW);
    }
    return copied;
}

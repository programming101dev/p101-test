#include <errno.h>
#include <p101_c/p101_stdio.h>
#include <p101_c/p101_stdlib.h>
#include <p101_c/p101_string.h>
#include <p101_io/p101_stdio.h>
#include <p101_json/json.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    MAX_APIS                         = 2048,
    MAX_CALLS                        = 4096,
    MAX_LINE                         = 32768,
    MAX_NAME                         = 256,
    MAX_USR                          = 1024,
    MAX_FAULTS                       = 8192,
    MAX_JSON                         = 128 * 1024 * 1024,
    PATH_SIZE                        = 4096,
    MACRO_EXTRA                      = 16,
    API_FIELDS                       = 5,
    TEST_FIELDS                      = 4,
    CONTRACT_FIELDS                  = 4,
    FAULT_FIELDS                     = 11,
    OUT_FIELDS                       = 10,
    FAULT_LINUX_PLATFORM_COLUMN      = 4,
    FAULT_MACOS_PLATFORM_COLUMN      = 5,
    FAULT_FREEBSD_PLATFORM_COLUMN    = 6,
    FAULT_LINUX_CONDITIONAL_COLUMN   = 8,
    FAULT_MACOS_CONDITIONAL_COLUMN   = 9,
    FAULT_FREEBSD_CONDITIONAL_COLUMN = 10,
    OUTCOME_WRAPPER_COLUMN           = 5,
    OUTCOME_DOMAIN_COLUMN            = 6,
    OUTCOME_SYMBOL_COLUMN            = 7,
    OUTCOME_STATUS_COLUMN            = 9
};

struct api_record
{
    char   name[MAX_NAME];
    char   usr[MAX_USR];
    bool   tested;
    bool   fault_test;
    bool   conformance_seen;
    bool   instrumentation_seen;
    bool   trace_applicable;
    bool   require_arguments;
    bool   require_result;
    size_t enters;
    size_t exits;
    bool   arguments;
    bool   result;
};

struct expected_fault
{
    char wrapper[MAX_NAME];
    char domain[MAX_NAME];
    char symbol[MAX_NAME];
    bool observed;
};

struct conformance
{
    struct api_record     apis[MAX_APIS];
    struct expected_fault faults[MAX_FAULTS];
    size_t                api_count;
    size_t                fault_count;
    size_t                fault_outcomes_observed;
    size_t                findings;
};

struct arguments
{
    const char *library;
    const char *repo;
    const char *instrumentation;
    const char *model;
    const char *outcomes;
    const char *platform;
    const char *macros;
    const char *receipt;
    bool        help;
};

static bool                   parse_arguments(const struct p101_env *env, struct p101_error *err, int argc, char **argv, struct arguments *arguments);
static void                   usage(const struct p101_env *env, const char *program);
static bool                   read_json_file(const struct p101_env *env, struct p101_error *err, const char *path, struct p101_json *document);
static bool                   load_api_manifest(const struct p101_env *env, struct p101_error *err, const char *repo, struct conformance *state);
static bool                   load_test_manifest(const struct p101_env *env, struct p101_error *err, const char *repo, struct conformance *state);
static bool                   load_conformance_manifest(const struct p101_env *env, struct p101_error *err, const char *repo, struct conformance *state);
static bool                   load_fault_manifest(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, struct conformance *state);
static bool                   load_instrumentation(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, struct conformance *state);
static bool                   load_model(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, struct conformance *state);
static bool                   load_outcomes(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, struct conformance *state);
static bool                   validate_conformance(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, struct conformance *state);
static bool                   write_receipt(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, const struct conformance *state);
static size_t                 split_fields(char *line, char *fields[], size_t maximum);
static struct api_record     *find_api(const struct p101_env *env, struct conformance *state, const char *usr);
static struct api_record     *find_api_name(const struct p101_env *env, struct conformance *state, const char *name);
static struct expected_fault *find_fault(const struct p101_env *env, struct conformance *state, const char *wrapper, const char *domain, const char *symbol);
static bool                   token_text(const struct p101_env *env, struct p101_error *err, const struct p101_json *document, size_t object, const char *key, char *output, size_t output_size);
static bool                   token_boolean(const struct p101_env *env, const struct p101_json *document, size_t object, const char *key, bool *value);
static bool                   macro_present(const struct p101_env *env, struct p101_error *err, const char *path, const char *symbol);
static bool                   list_contains(const struct p101_env *env, const char *list, const char *value);
static void                   finding(const struct p101_env *env, const char *path, const char *library, const char *message, const char *identity, struct conformance *state);
static bool                   copy_text(const struct p101_env *env, struct p101_error *err, char *output, size_t output_size, const char *input);

int main(int argc, char **argv)
{
    struct arguments    arguments;
    struct conformance *state;
    struct p101_error  *err;
    struct p101_env    *env;
    const char         *message;
    bool                parsed;
    bool                loaded;
    bool                valid;
    bool                written;
    bool                has_error;
    int                 status;

    state     = NULL;
    arguments = (struct arguments){.library = NULL, .repo = NULL, .instrumentation = NULL, .model = NULL, .outcomes = NULL, .platform = NULL, .macros = NULL, .receipt = NULL, .help = false};
    err       = p101_error_create(false);
    env       = p101_env_create(err, NULL);
    state     = (struct conformance *)p101_calloc(env, err, 1U, sizeof(*state));
    status    = 2;
    parsed    = parse_arguments(env, err, argc, argv, &arguments);
    if(!parsed)
    {
        usage(env, argv[0]);
        if(arguments.help)
        {
            status = EXIT_SUCCESS;
        }
        goto done;
    }
    loaded = state != NULL;
    if(loaded)
    {
        loaded = load_api_manifest(env, err, arguments.repo, state);
    }
    if(loaded)
    {
        loaded = load_test_manifest(env, err, arguments.repo, state);
    }
    if(loaded)
    {
        loaded = load_conformance_manifest(env, err, arguments.repo, state);
    }
    if(loaded)
    {
        loaded = load_fault_manifest(env, err, &arguments, state);
    }
    if(loaded)
    {
        loaded = load_instrumentation(env, err, &arguments, state);
    }
    if(loaded)
    {
        loaded = load_model(env, err, &arguments, state);
    }
    if(loaded)
    {
        loaded = load_outcomes(env, err, &arguments, state);
    }
    valid = false;
    if(loaded)
    {
        valid = validate_conformance(env, err, &arguments, state);
    }
    written = false;
    if(loaded)
    {
        written = write_receipt(env, err, &arguments, state);
    }
    if(written)
    {
        if(valid)
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
        p101_fprintf(env, P101_ERROR_OPTIONAL, stderr, "%s:1:1: error: %s [P101-TEST-CONFORMANCE-001]\n", argv[0], message);
        status = 2;
    }
    p101_free(env, state);
    p101_env_destroy(env);
    p101_error_destroy(err);
    return status;
}

static bool parse_arguments(const struct p101_env *env, struct p101_error *err, int argc, char **argv, struct arguments *arguments)
{
    const char **destination;
    int          comparison;
    int          index;
    bool         parsed;

    parsed = true;
    for(index = 1; index < argc && parsed; index++)
    {
        comparison = p101_strcmp(env, argv[index], "--help");
        if(comparison == 0)
        {
            arguments->help = true;
            parsed          = false;
            continue;
        }
        destination = NULL;
        comparison  = p101_strcmp(env, argv[index], "--library");
        if(comparison == 0)
        {
            destination = &arguments->library;
        }
        comparison = p101_strcmp(env, argv[index], "--repo");
        if(comparison == 0)
        {
            destination = &arguments->repo;
        }
        comparison = p101_strcmp(env, argv[index], "--instrumentation");
        if(comparison == 0)
        {
            destination = &arguments->instrumentation;
        }
        comparison = p101_strcmp(env, argv[index], "--model");
        if(comparison == 0)
        {
            destination = &arguments->model;
        }
        comparison = p101_strcmp(env, argv[index], "--outcomes");
        if(comparison == 0)
        {
            destination = &arguments->outcomes;
        }
        comparison = p101_strcmp(env, argv[index], "--platform");
        if(comparison == 0)
        {
            destination = &arguments->platform;
        }
        comparison = p101_strcmp(env, argv[index], "--macros");
        if(comparison == 0)
        {
            destination = &arguments->macros;
        }
        comparison = p101_strcmp(env, argv[index], "--receipt");
        if(comparison == 0)
        {
            destination = &arguments->receipt;
        }
        if(destination == NULL || index + 1 >= argc)
        {
            P101_ERROR_RAISE_USER(err, "unknown or incomplete conformance option", EINVAL);
            parsed = false;
            continue;
        }
        index++;
        *destination = argv[index];
    }
    if(parsed && (arguments->library == NULL || arguments->repo == NULL || arguments->instrumentation == NULL || arguments->model == NULL || arguments->outcomes == NULL || arguments->platform == NULL || arguments->macros == NULL || arguments->receipt == NULL))
    {
        P101_ERROR_RAISE_USER(err, "all conformance inputs are required", EINVAL);
        parsed = false;
    }
    return parsed;
}

static void usage(const struct p101_env *env, const char *program)
{
    p101_fprintf(env, P101_ERROR_OPTIONAL, stderr, "Usage: %s --library NAME --repo DIR --instrumentation FILE --model FILE --outcomes FILE --platform NAME --macros FILE --receipt FILE\n", program);
}

static bool read_json_file(const struct p101_env *env, struct p101_error *err, const char *path, struct p101_json *document)
{
    FILE  *stream;
    char  *text;
    long   length;
    size_t bytes;
    int    seek_status;
    int    close_status;
    bool   parsed;

    text   = NULL;
    parsed = false;
    stream = p101_fopen(env, err, path, "rb");
    if(stream == NULL)
    {
        goto done;
    }
    seek_status = p101_fseek(env, err, stream, 0L, SEEK_END);
    if(seek_status != 0)
    {
        goto close_stream;
    }
    length = p101_ftell(env, err, stream);
    if(length < 0 || length > MAX_JSON)
    {
        P101_ERROR_RAISE_USER(err, "JSON input is too large", EOVERFLOW);
        goto close_stream;
    }
    seek_status = p101_fseek(env, err, stream, 0L, SEEK_SET);
    if(seek_status != 0)
    {
        goto close_stream;
    }
    text = (char *)p101_malloc(env, err, (size_t)length + 1U);
    if(text == NULL)
    {
        goto close_stream;
    }
    bytes = p101_fread(env, err, text, 1U, (size_t)length, stream);
    if(bytes != (size_t)length)
    {
        goto close_stream;
    }
    text[bytes] = '\0';
    parsed      = p101_json_parse(err, text, bytes, document);

close_stream:
    close_status = p101_fclose(env, err, stream);
    if(close_status != 0)
    {
        parsed = false;
    }
done:
    p101_free(env, text);
    return parsed;
}

static size_t split_fields(char *line, char *fields[], size_t maximum)
{
    char  *cursor;
    size_t count;

    count  = 0U;
    cursor = line;
    while(count < maximum)
    {
        fields[count] = cursor;
        count++;
        while(*cursor != '\0' && *cursor != '\t' && *cursor != '\n')
        {
            cursor++;
        }
        if(*cursor == '\0' || *cursor == '\n')
        {
            *cursor = '\0';
            break;
        }
        *cursor = '\0';
        cursor++;
    }
    return count;
}

static bool copy_text(const struct p101_env *env, struct p101_error *err, char *output, size_t output_size, const char *input)
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
        P101_ERROR_RAISE_ERRNO(err, EOVERFLOW);
    }
    return copied;
}

static bool load_api_manifest(const struct p101_env *env, struct p101_error *err, const char *repo, struct conformance *state)
{
    char   path[PATH_SIZE];
    char   line[MAX_LINE];
    char  *fields[API_FIELDS];
    FILE  *stream;
    size_t field_count;
    bool   first;
    bool   loaded;
    int    written;
    int    close_status;

    written = p101_snprintf(env, err, path, sizeof(path), "%s/api-manifest.tsv", repo);
    if(written < 0 || (size_t)written >= sizeof(path))
    {
        return false;
    }
    stream = p101_fopen(env, err, path, "r");
    if(stream == NULL)
    {
        return false;
    }
    first  = true;
    loaded = true;
    while(p101_fgets(env, err, line, sizeof(line), stream) != NULL)
    {
        if(first)
        {
            first = false;
            continue;
        }
        field_count = split_fields(line, fields, API_FIELDS);
        if(field_count != API_FIELDS || state->api_count >= MAX_APIS)
        {
            P101_ERROR_RAISE_USER(err, "malformed API manifest", EINVAL);
            loaded = false;
            break;
        }
        loaded = copy_text(env, err, state->apis[state->api_count].name, sizeof(state->apis[state->api_count].name), fields[0]);
        if(loaded)
        {
            loaded = copy_text(env, err, state->apis[state->api_count].usr, sizeof(state->apis[state->api_count].usr), fields[1]);
        }
        if(!loaded)
        {
            break;
        }
        state->api_count++;
    }
    close_status = p101_fclose(env, err, stream);
    if(close_status != 0)
    {
        loaded = false;
    }
    return loaded;
}

static struct api_record *find_api(const struct p101_env *env, struct conformance *state, const char *usr)
{
    struct api_record *record;
    size_t             index;
    int                comparison;

    record = NULL;
    for(index = 0U; index < state->api_count; index++)
    {
        comparison = p101_strcmp(env, state->apis[index].usr, usr);
        if(comparison == 0)
        {
            record = &state->apis[index];
            break;
        }
    }
    return record;
}

static struct api_record *find_api_name(const struct p101_env *env, struct conformance *state, const char *name)
{
    struct api_record *record;
    size_t             index;
    int                comparison;

    record = NULL;
    for(index = 0U; index < state->api_count; index++)
    {
        comparison = p101_strcmp(env, state->apis[index].name, name);
        if(comparison == 0)
        {
            record = &state->apis[index];
            break;
        }
    }
    return record;
}

static bool load_test_manifest(const struct p101_env *env, struct p101_error *err, const char *repo, struct conformance *state)
{
    char               path[PATH_SIZE];
    char               line[MAX_LINE];
    char              *fields[TEST_FIELDS];
    struct api_record *api;
    FILE              *stream;
    size_t             field_count;
    bool               first;
    bool               loaded;
    int                comparison;
    int                written;
    int                close_status;

    written = p101_snprintf(env, err, path, sizeof(path), "%s/test/unit-test-manifest.tsv", repo);
    if(written < 0 || (size_t)written >= sizeof(path))
    {
        return false;
    }
    stream = p101_fopen(env, err, path, "r");
    if(stream == NULL)
    {
        return false;
    }
    first  = true;
    loaded = true;
    while(p101_fgets(env, err, line, sizeof(line), stream) != NULL)
    {
        if(first)
        {
            first = false;
            continue;
        }
        field_count = split_fields(line, fields, TEST_FIELDS);
        if(field_count != TEST_FIELDS)
        {
            P101_ERROR_RAISE_USER(err, "malformed unit-test manifest", EINVAL);
            loaded = false;
            break;
        }
        api = find_api(env, state, fields[1]);
        if(api == NULL || api->tested)
        {
            P101_ERROR_RAISE_USER(err, "unit-test manifest identity mismatch", EINVAL);
            loaded = false;
            break;
        }
        api->tested = true;
        comparison  = p101_strcmp(env, fields[2], "fault");
        if(comparison == 0)
        {
            api->fault_test = true;
        }
    }
    close_status = p101_fclose(env, err, stream);
    if(close_status != 0)
    {
        loaded = false;
    }
    return loaded;
}

static bool load_conformance_manifest(const struct p101_env *env, struct p101_error *err, const char *repo, struct conformance *state)
{
    char               path[PATH_SIZE];
    char               line[MAX_LINE];
    char              *fields[CONTRACT_FIELDS];
    struct api_record *api;
    FILE              *stream;
    size_t             field_count;
    bool               first;
    bool               loaded;
    int                written;
    int                comparison;
    int                close_status;

    written = p101_snprintf(env, err, path, sizeof(path), "%s/test/conformance-manifest.tsv", repo);
    loaded  = (bool)(written >= 0 && (size_t)written < sizeof(path));
    stream  = NULL;
    if(loaded)
    {
        stream = p101_fopen(env, err, path, "r");
        loaded = stream != NULL;
    }
    first = true;
    while(loaded && p101_fgets(env, err, line, sizeof(line), stream) != NULL)
    {
        if(first)
        {
            first = false;
            continue;
        }
        field_count = split_fields(line, fields, CONTRACT_FIELDS);
        loaded      = field_count == CONTRACT_FIELDS;
        if(!loaded)
        {
            P101_ERROR_RAISE_USER(err, "malformed conformance manifest", EINVAL);
            break;
        }
        api    = find_api(env, state, fields[1]);
        loaded = (bool)(api != NULL && !api->conformance_seen);
        if(!loaded)
        {
            P101_ERROR_RAISE_USER(err, "conformance manifest identity mismatch", EINVAL);
            break;
        }
        api->conformance_seen  = true;
        comparison             = p101_strcmp(env, fields[2], "true");
        api->require_arguments = comparison == 0;
        comparison             = p101_strcmp(env, fields[3], "true");
        api->require_result    = comparison == 0;
    }
    if(stream != NULL)
    {
        close_status = p101_fclose(env, err, stream);
        if(close_status != 0)
        {
            loaded = false;
        }
    }
    return loaded;
}

static bool list_contains(const struct p101_env *env, const char *list, const char *value)
{
    const char *start;
    const char *end;
    size_t      length;
    size_t      value_length;
    int         comparison;
    bool        found;

    value_length = p101_strlen(env, value);
    start        = list;
    found        = false;
    while(*start != '\0')
    {
        end = start;
        while(*end != '\0' && *end != ',')
        {
            end++;
        }
        length = (size_t)(end - start);
        if(length == value_length)
        {
            comparison = p101_strncmp(env, start, value, length);
            if(comparison == 0)
            {
                found = true;
                break;
            }
        }
        start = *end == ',' ? end + 1 : end;
    }
    return found;
}

static bool macro_present(const struct p101_env *env, struct p101_error *err, const char *path, const char *symbol)
{
    char   line[MAX_LINE];
    char   expected[MAX_NAME + MACRO_EXTRA];
    FILE  *stream;
    size_t expected_length;
    bool   found;
    int    written;
    int    comparison;
    int    close_status;

    written = p101_snprintf(env, err, expected, sizeof(expected), "#define %s", symbol);
    if(written < 0 || (size_t)written >= sizeof(expected))
    {
        return false;
    }
    expected_length = (size_t)written;
    stream          = p101_fopen(env, err, path, "r");
    if(stream == NULL)
    {
        return false;
    }
    found = false;
    while(p101_fgets(env, err, line, sizeof(line), stream) != NULL)
    {
        comparison = p101_strncmp(env, line, expected, expected_length);
        if(comparison == 0 && (line[expected_length] == ' ' || line[expected_length] == '\t' || line[expected_length] == '\n'))
        {
            found = true;
            break;
        }
    }
    close_status = p101_fclose(env, err, stream);
    if(close_status != 0)
    {
        found = false;
    }
    return found;
}

static bool load_fault_manifest(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, struct conformance *state)
{
    char               path[PATH_SIZE];
    char               line[MAX_LINE];
    char               faults[MAX_LINE];
    char              *fields[FAULT_FIELDS];
    char              *cursor;
    char              *end;
    const char        *symbol;
    struct api_record *api;
    FILE              *stream;
    size_t             field_count;
    size_t             platform_column;
    size_t             conditional_column;
    bool               conditional;
    bool               available;
    bool               loaded;
    bool               first;
    int                comparison;
    int                written;
    int                close_status;

    comparison         = p101_strcmp(env, arguments->platform, "linux");
    platform_column    = FAULT_LINUX_PLATFORM_COLUMN;
    conditional_column = FAULT_LINUX_CONDITIONAL_COLUMN;
    if(comparison != 0)
    {
        comparison         = p101_strcmp(env, arguments->platform, "macos");
        platform_column    = FAULT_MACOS_PLATFORM_COLUMN;
        conditional_column = FAULT_MACOS_CONDITIONAL_COLUMN;
    }
    if(comparison != 0)
    {
        comparison         = p101_strcmp(env, arguments->platform, "freebsd");
        platform_column    = FAULT_FREEBSD_PLATFORM_COLUMN;
        conditional_column = FAULT_FREEBSD_CONDITIONAL_COLUMN;
    }
    if(comparison != 0)
    {
        P101_ERROR_RAISE_USER(err, "unsupported conformance platform", EINVAL);
        return false;
    }
    written = p101_snprintf(env, err, path, sizeof(path), "%s/test/fault-outcome-manifest.tsv", arguments->repo);
    if(written < 0 || (size_t)written >= sizeof(path))
    {
        return false;
    }
    stream = p101_fopen(env, err, path, "r");
    if(stream == NULL)
    {
        return false;
    }
    loaded = true;
    first  = true;
    while(p101_fgets(env, err, line, sizeof(line), stream) != NULL)
    {
        if(first)
        {
            first = false;
            continue;
        }
        field_count = split_fields(line, fields, FAULT_FIELDS);
        if(field_count != FAULT_FIELDS)
        {
            P101_ERROR_RAISE_USER(err, "malformed fault-outcome manifest", EINVAL);
            loaded = false;
            break;
        }
        api = find_api(env, state, fields[1]);
        if(api == NULL || !api->fault_test)
        {
            P101_ERROR_RAISE_USER(err, "fault-outcome manifest identity mismatch", EINVAL);
            loaded = false;
            break;
        }
        loaded = copy_text(env, err, faults, sizeof(faults), fields[platform_column]);
        if(!loaded)
        {
            break;
        }
        cursor = faults;
        while(*cursor != '\0')
        {
            symbol = cursor;
            end    = cursor;
            while(*end != '\0' && *end != ',')
            {
                end++;
            }
            if(*end == ',')
            {
                *end   = '\0';
                cursor = end + 1;
            }
            else
            {
                cursor = end;
            }
            conditional = list_contains(env, fields[conditional_column], symbol);
            available   = true;
            if(conditional)
            {
                available = macro_present(env, err, arguments->macros, symbol);
            }
            if(available)
            {
                if(state->fault_count >= MAX_FAULTS)
                {
                    P101_ERROR_RAISE_USER(err, "too many expected fault outcomes", EOVERFLOW);
                    loaded = false;
                    break;
                }
                loaded = copy_text(env, err, state->faults[state->fault_count].wrapper, sizeof(state->faults[state->fault_count].wrapper), fields[0]);
                if(loaded)
                {
                    loaded = copy_text(env, err, state->faults[state->fault_count].domain, sizeof(state->faults[state->fault_count].domain), fields[2]);
                }
                if(loaded)
                {
                    loaded = copy_text(env, err, state->faults[state->fault_count].symbol, sizeof(state->faults[state->fault_count].symbol), symbol);
                }
                if(!loaded)
                {
                    break;
                }
                state->fault_count++;
            }
        }
        if(!loaded)
        {
            break;
        }
    }
    close_status = p101_fclose(env, err, stream);
    if(close_status != 0)
    {
        loaded = false;
    }
    return loaded;
}

static bool token_text(const struct p101_env *env, struct p101_error *err, const struct p101_json *document, size_t object, const char *key, char *output, size_t output_size)
{
    size_t token;
    bool   found;
    bool   copied;

    found  = p101_json_object_get(document, object, key, &token);
    copied = false;
    if(found)
    {
        copied = p101_json_token_copy(err, document, token, output, output_size);
    }
    (void)env;
    return copied;
}

static bool token_boolean(const struct p101_env *env, const struct p101_json *document, size_t object, const char *key, bool *value)
{
    size_t token;
    bool   found;
    bool   equal;

    found = p101_json_object_get(document, object, key, &token);
    if(found)
    {
        equal  = p101_json_token_equals(document, token, "true");
        *value = equal;
    }
    (void)env;
    return found;
}

static bool load_instrumentation(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, struct conformance *state)
{
    struct p101_json   document;
    struct api_record *api;
    char               schema[MAX_NAME];
    char               library[MAX_NAME];
    char               usr[MAX_USR];
    size_t             capabilities;
    size_t             row;
    size_t             index;
    bool               parsed;
    bool               found;
    bool               passed;
    bool               has_env;
    int                comparison;

    p101_json_init(&document);
    parsed = read_json_file(env, err, arguments->instrumentation, &document);
    if(!parsed)
    {
        goto done;
    }
    found      = token_text(env, err, &document, 0U, "schema", schema, sizeof(schema));
    comparison = p101_strcmp(env, schema, "p101-instrumentation-platform-receipt-v1");
    if(!found || comparison != 0)
    {
        P101_ERROR_RAISE_USER(err, "unsupported instrumentation receipt", EINVAL);
        parsed = false;
        goto done;
    }
    found = token_boolean(env, &document, 0U, "passed", &passed);
    if(!found || !passed)
    {
        P101_ERROR_RAISE_USER(err, "instrumentation audit did not pass", EINVAL);
        parsed = false;
        goto done;
    }
    found = p101_json_object_get(&document, 0U, "function_capabilities", &capabilities);
    if(!found || document.tokens[capabilities].kind != P101_JSON_ARRAY)
    {
        P101_ERROR_RAISE_USER(err, "instrumentation receipt lacks capabilities", EINVAL);
        parsed = false;
        goto done;
    }
    for(index = 0U; index < document.tokens[capabilities].child_count; index++)
    {
        found = p101_json_array_get(&document, capabilities, index, &row);
        if(!found)
        {
            parsed = false;
            break;
        }
        found = token_text(env, err, &document, row, "library", library, sizeof(library));
        if(!found)
        {
            parsed = false;
            break;
        }
        comparison = p101_strcmp(env, library, arguments->library);
        if(comparison != 0)
        {
            continue;
        }
        found = token_text(env, err, &document, row, "usr", usr, sizeof(usr));
        if(!found)
        {
            parsed = false;
            break;
        }
        api = find_api(env, state, usr);
        if(api == NULL)
        {
            P101_ERROR_RAISE_USER(err, "instrumentation receipt has unknown API identity", EINVAL);
            parsed = false;
            break;
        }
        api->instrumentation_seen = true;
        found                     = token_boolean(env, &document, row, "has_env", &has_env);
        if(!found)
        {
            parsed = false;
            break;
        }
        api->trace_applicable = has_env;
    }

done:
    p101_json_destroy(&document);
    return parsed;
}

static bool load_model(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, struct conformance *state)
{
    struct p101_json   document;
    struct api_record *api;
    char               schema[MAX_NAME];
    char               domain[MAX_NAME];
    char               name[MAX_NAME];
    char               kind[MAX_NAME];
    char               value[MAX_LINE];
    size_t             nodes;
    size_t             row;
    size_t             index;
    bool               parsed;
    bool               found;
    int                comparison;

    p101_json_init(&document);
    parsed = read_json_file(env, err, arguments->model, &document);
    if(!parsed)
    {
        goto done;
    }
    found      = token_text(env, err, &document, 0U, "schema", schema, sizeof(schema));
    comparison = p101_strcmp(env, schema, "p101-run-model-v1");
    if(!found || comparison != 0)
    {
        P101_ERROR_RAISE_USER(err, "unsupported run model", EINVAL);
        parsed = false;
        goto done;
    }
    found = p101_json_object_get(&document, 0U, "nodes", &nodes);
    if(!found || document.tokens[nodes].kind != P101_JSON_ARRAY)
    {
        P101_ERROR_RAISE_USER(err, "run model lacks nodes", EINVAL);
        parsed = false;
        goto done;
    }
    for(index = 0U; index < document.tokens[nodes].child_count; index++)
    {
        found = p101_json_array_get(&document, nodes, index, &row);
        if(!found)
        {
            parsed = false;
            break;
        }
        found = token_text(env, err, &document, row, "domain", domain, sizeof(domain));
        if(!found)
        {
            parsed = false;
            break;
        }
        comparison = p101_strcmp(env, domain, "call");
        if(comparison != 0)
        {
            continue;
        }
        found = token_text(env, err, &document, row, "name", name, sizeof(name));
        if(found)
        {
            found = token_text(env, err, &document, row, "kind", kind, sizeof(kind));
        }
        if(!found)
        {
            parsed = false;
            break;
        }
        api = find_api_name(env, state, name);
        if(api == NULL)
        {
            continue;
        }
        comparison = p101_strcmp(env, kind, "call-enter");
        if(comparison == 0)
        {
            api->enters++;
            found = token_text(env, P101_ERROR_OPTIONAL, &document, row, "arguments", value, sizeof(value));
            if(found)
            {
                comparison     = p101_strcmp(env, value, "-");
                api->arguments = (bool)(value[0] != '\0' && comparison != 0);
            }
        }
        comparison = p101_strcmp(env, kind, "call-exit");
        if(comparison == 0)
        {
            api->exits++;
            found = token_text(env, P101_ERROR_OPTIONAL, &document, row, "result", value, sizeof(value));
            if(found)
            {
                comparison  = p101_strcmp(env, value, "-");
                api->result = (bool)(value[0] != '\0' && comparison != 0);
            }
        }
    }

done:
    p101_json_destroy(&document);
    return parsed;
}

static struct expected_fault *find_fault(const struct p101_env *env, struct conformance *state, const char *wrapper, const char *domain, const char *symbol)
{
    struct expected_fault *fault;
    size_t                 index;
    int                    wrapper_comparison;
    int                    domain_comparison;
    int                    symbol_comparison;

    fault = NULL;
    for(index = 0U; index < state->fault_count; index++)
    {
        wrapper_comparison = p101_strcmp(env, state->faults[index].wrapper, wrapper);
        domain_comparison  = p101_strcmp(env, state->faults[index].domain, domain);
        symbol_comparison  = p101_strcmp(env, state->faults[index].symbol, symbol);
        if(wrapper_comparison == 0 && domain_comparison == 0 && symbol_comparison == 0)
        {
            fault = &state->faults[index];
            break;
        }
    }
    return fault;
}

static bool load_outcomes(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, struct conformance *state)
{
    char                   line[MAX_LINE];
    char                  *fields[OUT_FIELDS];
    struct expected_fault *fault;
    FILE                  *stream;
    size_t                 field_count;
    bool                   loaded;
    int                    comparison;
    int                    close_status;

    stream = p101_fopen(env, err, arguments->outcomes, "r");
    if(stream == NULL)
    {
        return false;
    }
    loaded = true;
    while(p101_fgets(env, err, line, sizeof(line), stream) != NULL)
    {
        field_count = split_fields(line, fields, OUT_FIELDS);
        if(field_count != OUT_FIELDS)
        {
            P101_ERROR_RAISE_USER(err, "malformed wrapper outcome record", EINVAL);
            loaded = false;
            break;
        }
        comparison = p101_strcmp(env, fields[0], "P101WRAPPER");
        if(comparison != 0)
        {
            P101_ERROR_RAISE_USER(err, "unsupported wrapper outcome record", EINVAL);
            loaded = false;
            break;
        }
        fault = find_fault(env, state, fields[OUTCOME_WRAPPER_COLUMN], fields[OUTCOME_DOMAIN_COLUMN], fields[OUTCOME_SYMBOL_COLUMN]);
        if(fault == NULL || fault->observed)
        {
            finding(env, arguments->outcomes, arguments->library, "unexpected or duplicate fault outcome", fields[OUTCOME_WRAPPER_COLUMN], state);
            continue;
        }
        comparison = p101_strcmp(env, fields[OUTCOME_STATUS_COLUMN], "PASS");
        if(comparison != 0)
        {
            finding(env, arguments->outcomes, arguments->library, "fault outcome failed", fields[OUTCOME_WRAPPER_COLUMN], state);
        }
        fault->observed = true;
        state->fault_outcomes_observed++;
    }
    close_status = p101_fclose(env, err, stream);
    if(close_status != 0)
    {
        loaded = false;
    }
    return loaded;
}

static void finding(const struct p101_env *env, const char *path, const char *library, const char *message, const char *identity, struct conformance *state)
{
    p101_fprintf(env, P101_ERROR_OPTIONAL, stderr, "%s:1:1: error: %s: %s: %s [P101-TEST-CONFORMANCE-002]\n", path, library, identity, message);
    state->findings++;
}

static bool validate_conformance(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, struct conformance *state)
{
    size_t index;

    for(index = 0U; index < state->api_count; index++)
    {
        if(!state->apis[index].tested)
        {
            finding(env, arguments->repo, arguments->library, "public API has no unit-test identity", state->apis[index].usr, state);
        }
        if(!state->apis[index].conformance_seen)
        {
            finding(env, arguments->repo, arguments->library, "public API has no generated conformance contract", state->apis[index].usr, state);
        }
        if(!state->apis[index].instrumentation_seen)
        {
            finding(env, arguments->instrumentation, arguments->library, "public API has no instrumentation identity", state->apis[index].usr, state);
        }
        if(state->apis[index].trace_applicable && state->apis[index].enters == 0U)
        {
            finding(env, arguments->model, arguments->library, "traced API has no runtime invocation", state->apis[index].name, state);
        }
        if(state->apis[index].trace_applicable && state->apis[index].enters != state->apis[index].exits)
        {
            finding(env, arguments->model, arguments->library, "runtime ENTER/EXIT counts are unbalanced", state->apis[index].name, state);
        }
        if(state->apis[index].fault_test && state->apis[index].enters == 0U)
        {
            finding(env, arguments->model, arguments->library, "fault test emitted no call", state->apis[index].name, state);
        }
        if(state->apis[index].require_arguments && !state->apis[index].arguments)
        {
            finding(env, arguments->model, arguments->library, "required arguments were not logged", state->apis[index].name, state);
        }
        if(state->apis[index].require_result && !state->apis[index].result)
        {
            finding(env, arguments->model, arguments->library, "required result was not logged", state->apis[index].name, state);
        }
    }
    for(index = 0U; index < state->fault_count; index++)
    {
        if(!state->faults[index].observed)
        {
            finding(env, arguments->outcomes, arguments->library, "missing direct platform fault outcome", state->faults[index].wrapper, state);
        }
    }
    (void)err;
    return state->findings == 0U;
}

static bool write_receipt(const struct p101_env *env, struct p101_error *err, const struct arguments *arguments, const struct conformance *state)
{
    FILE  *stream;
    size_t traced;
    size_t invoked;
    size_t index;
    int    written;
    int    close_status;
    bool   success;

    traced  = 0U;
    invoked = 0U;
    for(index = 0U; index < state->api_count; index++)
    {
        if(state->apis[index].trace_applicable)
        {
            traced++;
            if(state->apis[index].enters > 0U)
            {
                invoked++;
            }
        }
    }
    stream = p101_fopen(env, err, arguments->receipt, "w");
    if(stream == NULL)
    {
        return false;
    }
    written      = p101_fprintf(env,
                                err,
                                stream,
                                "{\"schema\":\"p101-wrapper-conformance-library-v1\",\"library\":\"%s\",\"apis\":%zu,\"trace_applicable\":%zu,\"invoked\":%zu,\"fault_cases\":%zu,\"fault_outcomes_observed\":%zu,\"findings\":%zu,\"passed\":%s}\n",
                                arguments->library,
                                state->api_count,
                                traced,
                                invoked,
                                state->fault_outcomes_observed,
                                state->fault_count,
                                state->findings,
                                state->findings == 0U ? "true" : "false");
    success      = written >= 0;
    close_status = p101_fclose(env, err, stream);
    if(close_status != 0)
    {
        success = false;
    }
    return success;
}

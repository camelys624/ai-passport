// Host tests for the bounded overview / pairing JSON parser and request bodies.
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "codeloom_protocol.h"
#include "codeloom_text.h"

static char s_buf[CL_HTTP_RESPONSE_CAP + 64];
static cl_overview_t s_ov;

static cl_parse_result_t parse(const char *json, cl_parse_stats_t *stats)
{
    return cl_parse_overview(json, strlen(json), &s_ov, stats);
}

static const char VALID[] =
    "{\"serverTime\":\"2026-09-29T10:00:00.000Z\",\"workspaceName\":\"团队 A\","
    "\"approvalsTotal\":3,\"approvals\":["
    "{\"id\":\"apr_1\",\"kind\":\"shell\",\"title\":\"运行 npm test\",\"detail\":\"npm test\\n--ci\","
    "\"taskTitle\":\"修复登录\",\"runId\":\"run_1\",\"createdAt\":\"2026-09-29T09:55:00.000Z\","
    "\"expiresAt\":\"2026-09-29T10:10:00.000Z\"},"
    "{\"id\":\"apr_2\",\"kind\":\"file_write\",\"title\":\"\\u5199 src/a.ts\",\"detail\":\"\","
    "\"taskTitle\":\"\",\"runId\":\"run_2\",\"createdAt\":\"bad\",\"expiresAt\":\"x\"}],"
    "\"tasksTotal\":12,\"tasks\":["
    "{\"id\":\"t1\",\"title\":\"修复登录\",\"status\":\"in_progress\",\"runStatus\":\"waiting_approval\"},"
    "{\"id\":\"t2\",\"title\":\"文档\",\"status\":\"todo\",\"runStatus\":null}]}";

static void test_valid(void)
{
    cl_parse_stats_t stats;

    assert(parse(VALID, &stats) == CL_PARSE_OK);
    assert(stats.skipped_approvals == 0 && stats.skipped_tasks == 0 && !stats.truncated_strings);
    assert(s_ov.server_time_valid && s_ov.server_time_s == 1790676000);
    assert(strcmp(s_ov.workspace_name, "团队 A") == 0);
    assert(s_ov.approvals_total == 3 && s_ov.approval_count == 2);
    assert(strcmp(s_ov.approvals[0].id, "apr_1") == 0);
    assert(s_ov.approvals[0].kind == CL_KIND_SHELL);
    assert(strcmp(s_ov.approvals[0].detail, "npm test --ci") == 0); // newline collapsed
    assert(strcmp(s_ov.approvals[0].task_title, "修复登录") == 0);
    assert(s_ov.approvals[0].created_valid && s_ov.approvals[0].created_at_s == 1790676000 - 300);
    assert(s_ov.approvals[1].kind == CL_KIND_FILE_WRITE);
    assert(strcmp(s_ov.approvals[1].title, "写 src/a.ts") == 0); // \u escape decoded
    assert(!s_ov.approvals[1].created_valid);
    assert(s_ov.tasks_total == 12 && s_ov.task_count == 2);
    assert(s_ov.tasks[0].status == CL_TASK_IN_PROGRESS);
    assert(s_ov.tasks[0].run_status == CL_RUN_WAITING_APPROVAL);
    assert(s_ov.tasks[1].status == CL_TASK_TODO && s_ov.tasks[1].run_status == CL_RUN_NONE);
}

static void test_size_and_syntax_limits(void)
{
    const char *core = "{\"approvalsTotal\":0,\"approvals\":[],\"tasksTotal\":0,\"tasks\":[]}";
    size_t core_len = strlen(core);

    // Padding up to the cap (8192 + slack) is still accepted.
    memset(s_buf, ' ', sizeof(s_buf));
    memcpy(s_buf, core, core_len);
    assert(cl_parse_overview(s_buf, CL_HTTP_RESPONSE_CAP, &s_ov, NULL) == CL_PARSE_OK);
    // One byte beyond the cap is rejected before parsing.
    assert(cl_parse_overview(s_buf, CL_HTTP_RESPONSE_CAP + 1, &s_ov, NULL) == CL_PARSE_TOO_LARGE);

    assert(parse("{\"approvals\":[[[[[[[[[1]]]]]]]]],\"tasks\":[],\"approvalsTotal\":0,"
                 "\"tasksTotal\":0}", NULL) == CL_PARSE_TOO_DEEP);
    // Brackets inside strings do not count toward depth.
    assert(parse("{\"workspaceName\":\"[[[[[[[[[[[[\",\"approvalsTotal\":0,\"approvals\":[],"
                 "\"tasksTotal\":0,\"tasks\":[]}", NULL) == CL_PARSE_OK);
    assert(parse("{\"approvals\":[", NULL) == CL_PARSE_SYNTAX);
    assert(parse("", NULL) == CL_PARSE_SYNTAX);
    assert(parse("[]", NULL) == CL_PARSE_SCHEMA);
    snprintf(s_buf, sizeof(s_buf), "%s{}", core);
    assert(parse(s_buf, NULL) == CL_PARSE_SYNTAX); // concatenated document
}

static void test_missing_and_mistyped_keys(void)
{
    static const char *const bad[] = {
        "{\"approvalsTotal\":0,\"tasksTotal\":0,\"tasks\":[]}",
        "{\"approvalsTotal\":0,\"approvals\":[],\"tasksTotal\":0}",
        "{\"approvals\":[],\"tasksTotal\":0,\"tasks\":[]}",
        "{\"approvalsTotal\":0,\"approvals\":[],\"tasks\":[]}",
        "{\"approvalsTotal\":-1,\"approvals\":[],\"tasksTotal\":0,\"tasks\":[]}",
        "{\"approvalsTotal\":1.5,\"approvals\":[],\"tasksTotal\":0,\"tasks\":[]}",
        "{\"approvalsTotal\":\"1\",\"approvals\":[],\"tasksTotal\":0,\"tasks\":[]}",
        "{\"approvalsTotal\":0,\"approvals\":{},\"tasksTotal\":0,\"tasks\":[]}",
        "{\"approvalsTotal\":0,\"approvals\":[],\"tasksTotal\":0,\"tasks\":[],\"workspaceName\":5}",
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(parse(bad[i], NULL) == CL_PARSE_SCHEMA);
        assert(s_ov.approval_count == 0 && s_ov.task_count == 0);
    }
    // Optional top-level keys may be absent.
    assert(parse("{\"approvalsTotal\":0,\"approvals\":[],\"tasksTotal\":0,\"tasks\":[]}", NULL) ==
           CL_PARSE_OK);
    assert(!s_ov.server_time_valid && s_ov.workspace_name[0] == '\0');
}

static void test_items_and_enums(void)
{
    cl_parse_stats_t stats;
    const char *json =
        "{\"approvalsTotal\":0,\"tasksTotal\":0,\"approvals\":["
        "{\"id\":\"apr_ok\",\"kind\":\"banana\",\"title\":\"t\"},"   // unknown kind → other
        "{\"kind\":\"tool\",\"title\":\"no id\"},"                  // missing id
        "{\"id\":\"../x\",\"kind\":\"tool\",\"title\":\"unsafe\"},"  // unsafe id
        "{\"id\":\"apr_3\",\"kind\":7,\"title\":\"t\"},"             // kind wrong type
        "{\"id\":\"apr_4\",\"kind\":\"tool\"},"                      // missing title
        "{\"id\":\"apr_5\",\"kind\":\"tool\",\"title\":\"t\",\"detail\":3},"
        "\"not an object\"],"
        "\"tasks\":["
        "{\"id\":\"t1\",\"title\":\"a\",\"status\":\"weird\",\"runStatus\":\"zzz\"},"
        "{\"id\":\"t2\",\"title\":\"b\",\"status\":\"done\",\"runStatus\":5},"
        "{\"id\":\"t3\",\"title\":\"c\"},"
        "{\"id\":\"t4\",\"title\":\"d\",\"status\":\"canceled\"}]}";

    assert(parse(json, &stats) == CL_PARSE_OK);
    assert(s_ov.approval_count == 1 && stats.skipped_approvals == 6);
    assert(s_ov.approvals[0].kind == CL_KIND_OTHER);
    assert(s_ov.approvals_total == 1); // clamped up to the visible count
    assert(s_ov.task_count == 2 && stats.skipped_tasks == 2);
    assert(s_ov.tasks[0].status == CL_TASK_UNKNOWN && s_ov.tasks[0].run_status == CL_RUN_UNKNOWN);
    assert(s_ov.tasks[1].status == CL_TASK_CANCELED && s_ov.tasks[1].run_status == CL_RUN_NONE);
}

static void test_item_caps(void)
{
    size_t n = 0;

    n += (size_t)snprintf(s_buf + n, sizeof(s_buf) - n,
                          "{\"approvalsTotal\":20,\"tasksTotal\":30,\"approvals\":[");
    for (int i = 0; i < 10; ++i) {
        n += (size_t)snprintf(s_buf + n, sizeof(s_buf) - n,
                              "%s{\"id\":\"apr_%d\",\"kind\":\"tool\",\"title\":\"t\"}",
                              i ? "," : "", i);
    }
    n += (size_t)snprintf(s_buf + n, sizeof(s_buf) - n, "],\"tasks\":[");
    for (int i = 0; i < 15; ++i) {
        n += (size_t)snprintf(s_buf + n, sizeof(s_buf) - n,
                              "%s{\"id\":\"t%d\",\"title\":\"x\",\"status\":\"todo\"}",
                              i ? "," : "", i);
    }
    snprintf(s_buf + n, sizeof(s_buf) - n, "]}");
    assert(parse(s_buf, NULL) == CL_PARSE_OK);
    assert(s_ov.approval_count == CL_MAX_APPROVALS && s_ov.approvals_total == 20);
    assert(strcmp(s_ov.approvals[7].id, "apr_7") == 0);
    assert(s_ov.task_count == CL_MAX_TASKS && s_ov.tasks_total == 30);
}

static void test_string_truncation(void)
{
    cl_parse_stats_t stats;
    size_t n = 0;
    size_t len;

    n += (size_t)snprintf(s_buf + n, sizeof(s_buf) - n,
                          "{\"approvalsTotal\":1,\"tasksTotal\":0,\"tasks\":[],\"approvals\":["
                          "{\"id\":\"apr_1\",\"kind\":\"tool\",\"title\":\"");
    for (int i = 0; i < 120; ++i) { // over the 96-character contract cap
        n += (size_t)snprintf(s_buf + n, sizeof(s_buf) - n, "审");
    }
    snprintf(s_buf + n, sizeof(s_buf) - n, "\"}]}");
    assert(parse(s_buf, &stats) == CL_PARSE_OK);
    assert(stats.truncated_strings);
    len = strlen(s_ov.approvals[0].title);
    assert(len < CL_APPROVAL_TITLE_MAX && len >= CL_APPROVAL_TITLE_MAX - 4);
    assert(cl_utf8_valid(s_ov.approvals[0].title, len));
    assert(strcmp(s_ov.approvals[0].title + len - 3, "\xE2\x80\xA6") == 0);

    // A 96-character title fits exactly without truncation.
    n = 0;
    n += (size_t)snprintf(s_buf + n, sizeof(s_buf) - n,
                          "{\"approvalsTotal\":1,\"tasksTotal\":0,\"tasks\":[],\"approvals\":["
                          "{\"id\":\"apr_1\",\"kind\":\"tool\",\"title\":\"");
    for (int i = 0; i < 96; ++i) {
        n += (size_t)snprintf(s_buf + n, sizeof(s_buf) - n, "审");
    }
    snprintf(s_buf + n, sizeof(s_buf) - n, "\"}]}");
    assert(parse(s_buf, &stats) == CL_PARSE_OK);
    assert(!stats.truncated_strings && strlen(s_ov.approvals[0].title) == 96 * 3);
}

static void test_pairing(void)
{
    cl_pair_response_t pair;
    char code[32];
    char body[256];
    const char *ok = "{\"deviceId\":\"dev_abc\",\"workspaceId\":\"ws_1\","
                     "\"workspaceName\":\"工作区\",\"deviceToken\":\"awd_s3cr3t-_x\"}";

    assert(cl_parse_pair_response(ok, strlen(ok), &pair) == CL_PARSE_OK);
    assert(strcmp(pair.device_id, "dev_abc") == 0);
    assert(strcmp(pair.device_token, "awd_s3cr3t-_x") == 0);
    assert(strcmp(pair.workspace_name, "工作区") == 0);
    {
        const char *s = "{\"deviceId\":\"dev_abc\",\"deviceToken\":\"awr_runner\"}";
        assert(cl_parse_pair_response(s, strlen(s), &pair) == CL_PARSE_SCHEMA);
        assert(pair.device_token[0] == '\0');
    }
    {
        const char *s = "{\"deviceId\":\"abc\",\"deviceToken\":\"awd_x\"}";
        assert(cl_parse_pair_response(s, strlen(s), &pair) == CL_PARSE_SCHEMA);
    }
    {
        const char *s = "{\"deviceId\":\"dev_a\",\"deviceToken\":\"awd_x y\"}";
        assert(cl_parse_pair_response(s, strlen(s), &pair) == CL_PARSE_SCHEMA);
    }
    {
        const char *s = "{\"error\":{\"code\":\"invalid_pairing_code\",\"message\":\"x\"}}";
        assert(cl_parse_error_code(s, strlen(s), code, sizeof(code)));
        assert(strcmp(code, "invalid_pairing_code") == 0);
        assert(!cl_parse_error_code("{\"error\":\"x\"}", 13, code, sizeof(code)));
        assert(!cl_parse_error_code("<html>", 6, code, sizeof(code)));
    }

    assert(cl_build_pair_body(body, sizeof(body), "pair_x", "Passport-\"A1\"", "v1.0") > 0);
    assert(strcmp(body, "{\"pairingCode\":\"pair_x\",\"name\":\"Passport-\\\"A1\\\"\","
                        "\"firmwareVersion\":\"v1.0\"}") == 0);
    assert(cl_build_pair_body(body, 20, "pair_x", "n", "v") == -1);

    assert(cl_build_resolve_body(body, sizeof(body), CL_DECISION_ALLOW_ALWAYS) > 0);
    assert(strcmp(body, "{\"decision\":\"allow_always\"}") == 0);
    assert(cl_build_resolve_body(body, sizeof(body), CL_DECISION_DENY) > 0);
    assert(strcmp(body, "{\"decision\":\"deny\"}") == 0);
    assert(cl_build_resolve_body(body, sizeof(body), CL_DECISION_COUNT) == -1);
    assert(cl_build_resolve_body(body, 10, CL_DECISION_ALLOW) == -1);
}

int main(void)
{
    test_valid();
    test_size_and_syntax_limits();
    test_missing_and_mistyped_keys();
    test_items_and_enums();
    test_item_caps();
    test_string_truncation();
    test_pairing();
    return 0;
}

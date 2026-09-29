// Host tests for cl_config_valid and the versioned settings persistence.
#undef NDEBUG
#include <assert.h>
#include <string.h>

#include "codeloom_settings.h"

#define MEM_KEYS 12

typedef struct {
    char keys[MEM_KEYS][16];
    char values[MEM_KEYS][256];
    bool is_u8[MEM_KEYS];
    uint8_t u8[MEM_KEYS];
    int count;
    int fail_set_after; // -1 = never; otherwise fail the Nth set call
    int sets;
    int commits;
} mem_store_t;

static int find(mem_store_t *m, const char *key)
{
    for (int i = 0; i < m->count; ++i) {
        if (strcmp(m->keys[i], key) == 0) {
            return i;
        }
    }
    return -1;
}

static int slot(mem_store_t *m, const char *key)
{
    int i = find(m, key);
    if (i < 0) {
        i = m->count++;
        strcpy(m->keys[i], key);
    }
    return i;
}

static cl_backend_status_t mem_get_str(void *ctx, const char *key, char *value, size_t *length)
{
    mem_store_t *m = ctx;
    int i = find(m, key);
    size_t n;
    if (i < 0 || m->is_u8[i]) {
        return CL_BACKEND_NOT_FOUND;
    }
    n = strlen(m->values[i]) + 1;
    if (n > *length) {
        return CL_BACKEND_ERROR;
    }
    memcpy(value, m->values[i], n);
    *length = n;
    return CL_BACKEND_OK;
}

static bool should_fail(mem_store_t *m)
{
    return m->fail_set_after >= 0 && m->sets++ >= m->fail_set_after;
}

static cl_backend_status_t mem_set_str(void *ctx, const char *key, const char *value)
{
    mem_store_t *m = ctx;
    int i;
    if (should_fail(m)) {
        return CL_BACKEND_ERROR;
    }
    i = slot(m, key);
    m->is_u8[i] = false;
    strcpy(m->values[i], value);
    return CL_BACKEND_OK;
}

static cl_backend_status_t mem_get_u8(void *ctx, const char *key, uint8_t *value)
{
    mem_store_t *m = ctx;
    int i = find(m, key);
    if (i < 0 || !m->is_u8[i]) {
        return CL_BACKEND_NOT_FOUND;
    }
    *value = m->u8[i];
    return CL_BACKEND_OK;
}

static cl_backend_status_t mem_set_u8(void *ctx, const char *key, uint8_t value)
{
    mem_store_t *m = ctx;
    int i;
    if (should_fail(m)) {
        return CL_BACKEND_ERROR;
    }
    i = slot(m, key);
    m->is_u8[i] = true;
    m->u8[i] = value;
    return CL_BACKEND_OK;
}

static cl_backend_status_t mem_erase_all(void *ctx)
{
    mem_store_t *m = ctx;
    m->count = 0;
    return CL_BACKEND_OK;
}

static cl_backend_status_t mem_commit(void *ctx)
{
    ((mem_store_t *)ctx)->commits++;
    return CL_BACKEND_OK;
}

static const cl_settings_backend_t MEM = {
    mem_get_str, mem_set_str, mem_get_u8, mem_set_u8, mem_erase_all, mem_commit,
};

static cl_config_t valid_config(void)
{
    cl_config_t c;
    memset(&c, 0, sizeof(c));
    strcpy(c.ssid, "Home Net");
    strcpy(c.password, "secret123");
    strcpy(c.server_url, "http://192.168.1.10:5181");
    strcpy(c.device_token, "awd_abcDEF123-_");
    strcpy(c.device_id, "dev_1");
    strcpy(c.workspace_name, "工作区");
    return c;
}

static void test_validation(void)
{
    cl_config_t c = valid_config();
    assert(cl_config_valid(&c));

    c = valid_config();
    strcpy(c.server_url, "http://192.168.1.10:5181/"); // not normalized
    assert(!cl_config_valid(&c));
    c = valid_config();
    strcpy(c.server_url, "ftp://x");
    assert(!cl_config_valid(&c));
    c = valid_config();
    strcpy(c.device_token, "awr_runner");
    assert(!cl_config_valid(&c));
    c = valid_config();
    strcpy(c.device_id, "abc");
    assert(!cl_config_valid(&c));
    c = valid_config();
    c.ssid[0] = '\0';
    assert(!cl_config_valid(&c));
    c = valid_config();
    strcpy(c.password, "short");
    assert(!cl_config_valid(&c));
    c = valid_config();
    strcpy(c.workspace_name, "\xFF");
    assert(!cl_config_valid(&c));
    assert(!cl_config_valid(NULL));
}

static void test_round_trip_and_erase(void)
{
    mem_store_t m = {.fail_set_after = -1};
    cl_config_t in = valid_config();
    cl_config_t out;

    assert(cl_settings_load(&MEM, &m, &out) == CL_SETTINGS_NOT_FOUND);
    assert(cl_settings_save(&MEM, &m, &in) == CL_SETTINGS_OK);
    assert(cl_settings_load(&MEM, &m, &out) == CL_SETTINGS_OK);
    assert(memcmp(&in, &out, sizeof(in)) == 0);

    assert(cl_settings_erase(&MEM, &m) == CL_SETTINGS_OK);
    assert(cl_settings_load(&MEM, &m, &out) == CL_SETTINGS_NOT_FOUND);
    assert(out.device_token[0] == '\0');
}

static void test_invalid_save_is_rejected(void)
{
    mem_store_t m = {.fail_set_after = -1};
    cl_config_t in = valid_config();

    strcpy(in.device_token, "bad");
    assert(cl_settings_save(&MEM, &m, &in) == CL_SETTINGS_INVALID);
    assert(m.count == 0 && m.commits == 0);
}

static void test_version_and_corruption(void)
{
    mem_store_t m = {.fail_set_after = -1};
    cl_config_t in = valid_config();
    cl_config_t out;

    assert(cl_settings_save(&MEM, &m, &in) == CL_SETTINGS_OK);
    m.u8[find(&m, "ver")] = CL_SETTINGS_VERSION + 1;
    assert(cl_settings_load(&MEM, &m, &out) == CL_SETTINGS_NOT_FOUND);

    assert(cl_settings_save(&MEM, &m, &in) == CL_SETTINGS_OK);
    strcpy(m.values[find(&m, "token")], "awd_bad token");
    assert(cl_settings_load(&MEM, &m, &out) == CL_SETTINGS_NOT_FOUND);

    assert(cl_settings_save(&MEM, &m, &in) == CL_SETTINGS_OK);
    // Stored value longer than the field buffer must not be accepted.
    memset(m.values[find(&m, "ssid")], 'a', 40);
    m.values[find(&m, "ssid")][40] = '\0';
    assert(cl_settings_load(&MEM, &m, &out) == CL_SETTINGS_IO_ERROR);
}

static void test_interrupted_save_leaves_no_config(void)
{
    // Fail at every set position: the version marker is written last, so a partial
    // write must never load as a valid configuration.
    for (int fail_at = 0; fail_at < 7; ++fail_at) {
        mem_store_t m = {.fail_set_after = -1};
        cl_config_t in = valid_config();
        cl_config_t out;

        assert(cl_settings_save(&MEM, &m, &in) == CL_SETTINGS_OK);
        m.fail_set_after = fail_at;
        m.sets = 0;
        assert(cl_settings_save(&MEM, &m, &in) == CL_SETTINGS_IO_ERROR);
        assert(cl_settings_load(&MEM, &m, &out) == CL_SETTINGS_NOT_FOUND);
    }
}

int main(void)
{
    test_validation();
    test_round_trip_and_erase();
    test_invalid_save_is_rejected();
    test_version_and_corruption();
    test_interrupted_save_leaves_no_config();
    return 0;
}

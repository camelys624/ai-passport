// Host tests for server URL validation/normalization and setup form decoding.
#undef NDEBUG
#include <assert.h>
#include <string.h>

#include "codeloom_setup_form.h"
#include "codeloom_url.h"

static void test_url_accepts_and_normalizes(void)
{
    cl_url_t url;
    char joined[CL_URL_MAX + 32];
    char host[CL_HOST_MAX + 8];

    assert(cl_url_parse("http://192.168.1.10:5181", &url));
    assert(!url.https && url.port == 5181 && url.explicit_port);
    assert(strcmp(url.normalized, "http://192.168.1.10:5181") == 0);

    assert(cl_url_parse("  HTTPS://Codeloom.Example.COM/base/// \n", &url));
    assert(url.https && url.port == 443 && !url.explicit_port);
    assert(strcmp(url.host, "codeloom.example.com") == 0);
    assert(strcmp(url.normalized, "https://codeloom.example.com/base") == 0);

    assert(cl_url_parse("http://host.local/", &url));
    assert(strcmp(url.normalized, "http://host.local") == 0 && url.port == 80);

    assert(cl_url_parse("http://[fe80::1]:8080", &url));
    assert(strcmp(url.host, "[fe80::1]") == 0 && url.port == 8080);

    assert(cl_url_join(joined, sizeof(joined), "http://h:1", "/api/v1/device/overview"));
    assert(strcmp(joined, "http://h:1/api/v1/device/overview") == 0);
    assert(!cl_url_join(joined, 12, "http://h:1", "/api/v1/device/overview"));
    assert(!cl_url_join(joined, sizeof(joined), "http://h:1", "api"));

    assert(cl_url_parse("http://10.0.0.2:5181", &url));
    cl_url_display_host(&url, host, sizeof(host));
    assert(strcmp(host, "10.0.0.2:5181") == 0);
}

static void test_url_rejects(void)
{
    cl_url_t url;
    static const char *const bad[] = {
        "",
        "ftp://host",
        "http://",
        "http://:80",
        "http://host:0",
        "http://host:65536",
        "http://host:12a",
        "http://user:pw@host",
        "http://host?x=1",
        "http://host/path?x=1",
        "http://host/#frag",
        "http://ho st",
        "http://-host",
        "http://host-/x",
        "http://host..com",
        "http://[fe80::1",
        "http://host/a%20b",
        "http://host:",
        "http://" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com",
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!cl_url_parse(bad[i], &url));
    }
}

static void test_ids(void)
{
    char long_id[CL_ID_MAX + 1];

    assert(cl_id_is_safe("apr_01HZX-abc"));
    assert(!cl_id_is_safe(""));
    assert(!cl_id_is_safe(".."));
    assert(!cl_id_is_safe("a/b"));
    assert(!cl_id_is_safe("a%2F"));
    assert(!cl_id_is_safe("a b"));
    memset(long_id, 'a', CL_ID_MAX);
    long_id[CL_ID_MAX] = '\0';
    assert(!cl_id_is_safe(long_id));
    long_id[CL_ID_MAX - 1] = '\0';
    assert(cl_id_is_safe(long_id));
}

static void test_form_get(void)
{
    char out[16];
    const char *body = "a=1&ssid=My+Wi%2DFi%21&empty=&x=%E4%B8%AD";

    assert(cl_form_get(body, strlen(body), "ssid", out, sizeof(out)) == CL_FORM_OK);
    assert(strcmp(out, "My Wi-Fi!") == 0);
    assert(cl_form_get(body, strlen(body), "empty", out, sizeof(out)) == CL_FORM_OK);
    assert(strcmp(out, "") == 0);
    assert(cl_form_get(body, strlen(body), "x", out, sizeof(out)) == CL_FORM_OK);
    assert(strcmp(out, "中") == 0);
    assert(cl_form_get(body, strlen(body), "missing", out, sizeof(out)) == CL_FORM_MISSING);
    assert(cl_form_get(body, strlen(body), "ss", out, sizeof(out)) == CL_FORM_MISSING);
    assert(cl_form_get("k=%4", 4, "k", out, sizeof(out)) == CL_FORM_MALFORMED);
    assert(cl_form_get("k=%", 3, "k", out, sizeof(out)) == CL_FORM_MALFORMED);
    assert(cl_form_get("k=%zz", 5, "k", out, sizeof(out)) == CL_FORM_MALFORMED);
    assert(cl_form_get("k=a%00b", 7, "k", out, sizeof(out)) == CL_FORM_MALFORMED);
    assert(cl_form_get("k=%FF", 5, "k", out, sizeof(out)) == CL_FORM_MALFORMED);
    assert(cl_form_get("k=0123456789abcdef", 18, "k", out, sizeof(out)) == CL_FORM_TOO_LONG);
    // Length bound is honored even when the buffer is not NUL-terminated there.
    assert(cl_form_get("k=abcdef", 5, "k", out, sizeof(out)) == CL_FORM_OK);
    assert(strcmp(out, "abc") == 0);
}

static void test_setup_form(void)
{
    cl_setup_request_t req;
    const char *ok =
        "ssid=Home+Net&password=secret123&server=+http%3A%2F%2F192.168.1.10%3A5181%2F+"
        "&code=+pair_AbC-123_x+";
    const char *open = "ssid=Cafe&password=&server=https%3A%2F%2Fcl.example&code=pair_x";
    char big[CL_SETUP_FORM_BODY_MAX + 2];

    assert(cl_setup_form_parse(ok, strlen(ok), &req) == CL_SETUP_OK);
    assert(strcmp(req.ssid, "Home Net") == 0);
    assert(strcmp(req.password, "secret123") == 0);
    assert(strcmp(req.server_url, "http://192.168.1.10:5181") == 0);
    assert(strcmp(req.pairing_code, "pair_AbC-123_x") == 0);

    assert(cl_setup_form_parse(open, strlen(open), &req) == CL_SETUP_OK);
    assert(req.password[0] == '\0');

    assert(cl_setup_form_parse("password=x&server=http%3A%2F%2Fh&code=pair_x", 44, &req) ==
           CL_SETUP_BAD_FORM);
    {
        const char *s = "ssid=&password=&server=http%3A%2F%2Fh&code=pair_x";
        assert(cl_setup_form_parse(s, strlen(s), &req) == CL_SETUP_BAD_SSID);
    }
    {
        const char *s =
            "ssid=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa&password=&server=http%3A%2F%2Fh&code=pair_x";
        assert(cl_setup_form_parse(s, strlen(s), &req) == CL_SETUP_BAD_SSID);
    }
    {
        const char *s = "ssid=a&password=short&server=http%3A%2F%2Fh&code=pair_x";
        assert(cl_setup_form_parse(s, strlen(s), &req) == CL_SETUP_BAD_PASSWORD);
    }
    {
        const char *s = "ssid=a&password=&server=ftp%3A%2F%2Fh&code=pair_x";
        assert(cl_setup_form_parse(s, strlen(s), &req) == CL_SETUP_BAD_URL);
    }
    {
        const char *s = "ssid=a&password=&server=http%3A%2F%2Fh&code=code_x";
        assert(cl_setup_form_parse(s, strlen(s), &req) == CL_SETUP_BAD_CODE);
    }
    {
        const char *s = "ssid=a&password=&server=http%3A%2F%2Fh&code=pair_";
        assert(cl_setup_form_parse(s, strlen(s), &req) == CL_SETUP_BAD_CODE);
    }
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    assert(cl_setup_form_parse(big, strlen(big), &req) == CL_SETUP_BAD_FORM);

    assert(cl_wifi_password_valid("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    assert(!cl_wifi_password_valid("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg"));
    assert(!cl_wifi_password_valid("tab\there!"));
}

int main(void)
{
    test_url_accepts_and_normalizes();
    test_url_rejects();
    test_ids();
    test_form_get();
    test_setup_form();
    return 0;
}

/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * \file
 * \brief Unit tests for the URL destination checks
 */
#include <CUnit/CUnit.h>
#include <netdb.h>
#include <string.h>
#include "wget_agent.h"

extern char *GlobalProxy[6];

static int isPublic(const char *address)
{
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  int result;

  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = AI_NUMERICHOST;
  if (getaddrinfo(address, NULL, &hints, &res) != 0)
  {
    return -1;
  }
  result = UrlGuardIsPublic(res->ai_addr);
  freeaddrinfo(res);
  return result;
}

/**
 * \test Hosts are taken from URLs in every form the agent receives
 */
void testUrlGuardHost()
{
  const char *cases[][2] = {
    {"http://Example.ORG/a.tgz", "example.org"},
    {"https://user:p%40ss@example.org:8443/r.git", "example.org"},
    {"http://a@b@10.0.0.5/x", "10.0.0.5"},
    {"http://[::1]:8080/x", "::1"},
    {"ftp://example.org.", "example.org"},
    {"git@github.com:fossology/fossology.git", "github.com"},
    {"svn://svn.example.org/repo", "svn.example.org"},
    {"https://example.org?x=1", "example.org"},
  };
  char host[256];
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
  {
    CU_ASSERT_EQUAL(UrlGuardHost(cases[i][0], host, sizeof(host)), 0);
    CU_ASSERT_STRING_EQUAL(host, cases[i][1]);
  }
  CU_ASSERT_EQUAL(UrlGuardHost("/local/path", host, sizeof(host)), -1);
  CU_ASSERT_EQUAL(UrlGuardHost("http:///x", host, sizeof(host)), -1);
  CU_ASSERT_EQUAL(UrlGuardHost("http://[::1/x", host, sizeof(host)), -1);
}

/**
 * \test Internal address ranges, including IPv4 written as IPv6
 */
void testUrlGuardIsPublic()
{
  const char *internal[] = {"127.0.0.1", "10.1.2.3", "172.16.0.1", "172.31.255.255",
    "192.168.1.1", "169.254.169.254", "100.64.0.1", "0.0.0.0", "224.0.0.1",
    "255.255.255.255", "198.18.0.1", "::1", "::", "fd00::1", "fe80::1", "ff02::1",
    "::ffff:127.0.0.1", "::ffff:169.254.169.254", "64:ff9b::10.0.0.1",
    "2002:7f00:1::", "2001:db8::1"};
  const char *public[] = {"8.8.8.8", "93.184.215.14", "172.32.0.1", "100.128.0.1",
    "2606:4700::1111", "::ffff:8.8.8.8", "2002:808:808::"};
  size_t i;

  for (i = 0; i < sizeof(internal) / sizeof(internal[0]); i++)
  {
    CU_ASSERT_EQUAL(isPublic(internal[i]), 0);
  }
  for (i = 0; i < sizeof(public) / sizeof(public[0]); i++)
  {
    CU_ASSERT_EQUAL(isPublic(public[i]), 1);
  }
}

/**
 * \test Refusals, and what allow_internal lets through
 */
void testUrlGuardCheck()
{
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://127.0.0.1/a", NULL, 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://127.1/a", NULL, 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://2130706433/a", NULL, 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://localhost/a", NULL, 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://169.254.169.254/latest/", "", 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("https://u@10.0.0.5:8443/r.git", NULL, 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://[::ffff:10.0.0.1]/a", NULL, 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://nowhere.invalid/a", NULL, 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("not a url", NULL, 0));
  CU_ASSERT_PTR_NULL(UrlGuardCheckVia("http://8.8.8.8/a", NULL, 0));

  CU_ASSERT_PTR_NULL(UrlGuardCheckVia("http://10.20.1.5/a", " 10.20.0.0/16 , x", 0));
  CU_ASSERT_PTR_NULL(UrlGuardCheckVia("http://10.0.0.9/a", "10.0.0.8/29", 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://10.0.0.16/a", "10.0.0.8/29", 0));
  CU_ASSERT_PTR_NULL(UrlGuardCheckVia("http://LOCALHOST/a", "localhost", 0));
  CU_ASSERT_PTR_NULL(UrlGuardCheckVia("http://nowhere.invalid/a", "nowhere.invalid", 0));
  CU_ASSERT_PTR_NULL(UrlGuardCheckVia("http://[fd12:3456::1]/a", "fd12:3400::/22", 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://[fd12:3c56::1]/a", "fd12:3400::/22", 0));
  CU_ASSERT_PTR_NULL(UrlGuardCheckVia("http://[::ffff:10.1.1.1]/a", "10.1.0.0/16", 0));
  /* malformed entries are ignored, never read as /0 */
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://10.0.0.9/a", "0.0.0.0/0", 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://10.0.0.9/a", "10.0.0.0/x", 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://10.0.0.9/a", "10.0.0.0/", 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://10.0.0.9/a", "10.0.0.0/33", 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://10.0.0.9/a", "10.0.0.0/8/8", 0));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://10.0.0.9/a", "::/0", 0));
}

/**
 * \test Location headers are resolved against the URL they came from
 */
void testUrlGuardJoin()
{
  const char *cases[][3] = {
    {"http://a.org/dir/x", "https://b.org/y", "https://b.org/y"},
    {"https://a.org/dir/x", "//c.org/z", "https://c.org/z"},
    {"http://a.org:8080/dir/x?q=1", "/top", "http://a.org:8080/top"},
    {"http://a.org/dir/x?q=/1", "y.txt", "http://a.org/dir/y.txt"},
    {"http://a.org", "y.txt", "http://a.org/y.txt"},
    {"http://a.org/x", "http://169.254.169.254/#frag", "http://169.254.169.254/"},
    {"http://a.org/x", "y?u=http://z", "http://a.org/y?u=http://z"},
    {"http://a.org/dir/file.cgi?old=1", "?id=5", "http://a.org/dir/file.cgi?id=5"},
    {"http://a.org", "?id=5", "http://a.org?id=5"},
  };
  char out[256];
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
  {
    CU_ASSERT_EQUAL(UrlGuardJoin(cases[i][0], cases[i][1], out, sizeof(out)), 0);
    CU_ASSERT_STRING_EQUAL(out, cases[i][2]);
  }
  CU_ASSERT_EQUAL(UrlGuardJoin("http://a.org/x", "/y", out, 10), -1);
}

/**
 * \test The redirect walk refuses an internal start without contacting it
 */
void testResolveRedirectsRefusesInternal()
{
  char url[URLMAX] = "http://169.254.169.254/latest/meta-data/";
  CU_ASSERT_EQUAL(ResolveRedirects(url, sizeof(url), "", "", NULL), 1);
  strcpy(url, "http://127.0.0.1:9/nothing");
  CU_ASSERT_EQUAL(ResolveRedirects(url, sizeof(url), "", "", "127.0.0.1"), 0);
  CU_ASSERT_STRING_EQUAL(url, "http://127.0.0.1:9/nothing");
}

/**
 * \test Same scheme, host and port, with default ports filled in
 */
void testUrlGuardSameOrigin()
{
  CU_ASSERT_TRUE(UrlGuardSameOrigin("http://a.org/x", "http://A.org:80/y/z"));
  CU_ASSERT_TRUE(UrlGuardSameOrigin("https://u@a.org/x", "https://a.org:443/"));
  CU_ASSERT_TRUE(UrlGuardSameOrigin("http://[::1]:8080/x", "http://[::1]:8080/y"));
  CU_ASSERT_FALSE(UrlGuardSameOrigin("http://a.org/x", "https://a.org/x"));
  CU_ASSERT_FALSE(UrlGuardSameOrigin("http://a.org/x", "http://a.org:8080/x"));
  CU_ASSERT_FALSE(UrlGuardSameOrigin("http://a.org/x", "http://b.a.org/x"));
  CU_ASSERT_FALSE(UrlGuardSameOrigin("http://127.0.0.1/x", "http://localhost/x"));
  CU_ASSERT_FALSE(UrlGuardSameOrigin("http://a.org/x", "/relative"));
}

struct seenRedirects { int count; char from[4][256]; char location[4][256]; };

static void collect(const char *from, const char *location, void *ctx)
{
  struct seenRedirects *seen = ctx;
  if (seen->count < 4)
  {
    snprintf(seen->from[seen->count], 256, "%s", from);
    snprintf(seen->location[seen->count], 256, "%s", location);
  }
  seen->count++;
}

/**
 * \test Redirects in a wget -S log belong to the request before them; the
 * unindented "[following]" echo is not a second redirect; ERROR lines count
 */
void testUrlGuardWgetLog()
{
  char log[] =
    "--2026-11-08 09:16:17--  http://h:1/pub/\n"
    "Connecting to h:1... connected.\n"
    "  HTTP/1.0 200 OK\n"
    "--2026-11-08 09:16:17--  http://h:1/pub/sub\n"
    "  HTTP/1.0 301 Moved Permanently\n"
    "  Location: /pub/sub/\r\n"
    "Location: /pub/sub/ [following]\n"
    "0 redirections exceeded.\n"
    "--2026-11-08 09:16:18--  http://h:1/pub/away\n"
    "  HTTP/1.0 302 Found\n"
    "  location: http://other:2/secret\n"
    "--2026-11-08 09:16:18--  http://h:1/pub/gone\n"
    "  HTTP/1.0 404 Not Found\n"
    "2026-11-08 09:16:18 ERROR 404: Not Found.\n";
  struct seenRedirects seen = {0};
  FILE *file = fmemopen(log, strlen(log), "r");
  int errors = UrlGuardWgetLog(file, collect, &seen);

  fclose(file);
  CU_ASSERT_EQUAL(errors, 1);
  CU_ASSERT_EQUAL(seen.count, 2);
  CU_ASSERT_STRING_EQUAL(seen.from[0], "http://h:1/pub/sub");
  CU_ASSERT_STRING_EQUAL(seen.location[0], "/pub/sub/");
  CU_ASSERT_STRING_EQUAL(seen.from[1], "http://h:1/pub/away");
  CU_ASSERT_STRING_EQUAL(seen.location[1], "http://other:2/secret");
}

/**
 * \test no_proxy entries match host names and domain suffixes
 */
void testUrlGuardNoProxy()
{
  const char *list = "localhost, .corp.example,internal.example ,10.0.0.1";
  CU_ASSERT_TRUE(UrlGuardNoProxy("http://localhost/x", list));
  CU_ASSERT_TRUE(UrlGuardNoProxy("http://git.corp.example/x", list));
  CU_ASSERT_TRUE(UrlGuardNoProxy("https://a.internal.example:8443/x", list));
  CU_ASSERT_TRUE(UrlGuardNoProxy("http://INTERNAL.example/x", list));
  CU_ASSERT_TRUE(UrlGuardNoProxy("http://10.0.0.1/x", list));
  CU_ASSERT_FALSE(UrlGuardNoProxy("http://github.com/x", list));
  CU_ASSERT_FALSE(UrlGuardNoProxy("http://evilcorp.example/x", list));
  CU_ASSERT_FALSE(UrlGuardNoProxy("http://notinternal.example/x", list));
  CU_ASSERT_FALSE(UrlGuardNoProxy("http://github.com/x", NULL));
  CU_ASSERT_TRUE(UrlGuardNoProxy("http://github.com/x", "*"));
}

/**
 * \test Through a proxy an unresolvable host is left to the proxy; IP
 * literals and resolvable names are still checked
 */
void testUrlGuardCheckVia()
{
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://nowhere.invalid/a", NULL, 0));
  CU_ASSERT_PTR_NULL(UrlGuardCheckVia("http://nowhere.invalid/a", NULL, 1));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://169.254.169.254/latest/", NULL, 1));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://[::ffff:10.0.0.1]/a", NULL, 1));
  CU_ASSERT_PTR_NOT_NULL(UrlGuardCheckVia("http://localhost/a", NULL, 1));
  CU_ASSERT_PTR_NULL(UrlGuardCheckVia("http://8.8.8.8/a", NULL, 1));
}

/**
 * \test The proxy wget or the VCS command would use, and no_proxy
 */
void testProxyApplies()
{
  char *saved[4] = {GlobalProxy[0], GlobalProxy[1], GlobalProxy[2], GlobalProxy[3]};

  GlobalProxy[0] = "http://proxy:3128";
  GlobalProxy[1] = NULL;
  GlobalProxy[2] = "";
  GlobalProxy[3] = ".corp.example";
  CU_ASSERT_TRUE(ProxyApplies("http://github.com/x", 0));
  CU_ASSERT_FALSE(ProxyApplies("https://github.com/x", 0));
  CU_ASSERT_TRUE(ProxyApplies("https://github.com/x.git", 1));
  CU_ASSERT_FALSE(ProxyApplies("ftp://ftp.example/x", 0));
  CU_ASSERT_FALSE(ProxyApplies("http://git.corp.example/x", 0));
  CU_ASSERT_FALSE(ProxyApplies("https://git.corp.example/x.git", 1));
  GlobalProxy[0] = NULL;
  CU_ASSERT_FALSE(ProxyApplies("http://github.com/x", 0));
  GlobalProxy[0] = saved[0]; GlobalProxy[1] = saved[1];
  GlobalProxy[2] = saved[2]; GlobalProxy[3] = saved[3];
}

/**
 * \test Classify the address list shared with the PHP guard's tests
 */
void testUrlGuardSharedAddresses()
{
  char path[4096];
  char line[256];
  const char *src = strstr(__FILE__, "wget_agent/agent_tests/");
  FILE *file;
  int cases = 0;

  CU_ASSERT_PTR_NOT_NULL_FATAL(src);
  snprintf(path, sizeof(path), "%.*stesting/dataFiles/url-guard-addresses.txt",
    (int) (src - __FILE__), __FILE__);
  file = fopen(path, "r");
  CU_ASSERT_PTR_NOT_NULL_FATAL(file);
  while (fgets(line, sizeof(line), file))
  {
    char *tab = strchr(line, '\t');
    if (line[0] == '#' || !tab)
    {
      continue;
    }
    *tab = '\0';
    int expectPublic = strncmp(tab + 1, "public", 6) == 0;
    int actual = isPublic(line);
    if (actual != expectPublic)
    {
      printf("url-guard-addresses.txt: %s classified %d, expected %d\n", line, actual, expectPublic);
    }
    CU_ASSERT_EQUAL(actual, expectPublic);
    cases++;
  }
  fclose(file);
  CU_ASSERT_TRUE(cases > 30);
}

/**
 * \test Redirect targets under the start URL's directory
 */
void testUrlGuardInStartPath()
{
  CU_ASSERT_TRUE(UrlGuardInStartPath("http://h/pub/", "http://h/pub/sub/"));
  CU_ASSERT_TRUE(UrlGuardInStartPath("http://h/pub/index.html", "http://h/pub/a/b"));
  CU_ASSERT_TRUE(UrlGuardInStartPath("http://h", "http://h/anything"));
  CU_ASSERT_FALSE(UrlGuardInStartPath("http://h/pub/", "http://h/other/x"));
  CU_ASSERT_FALSE(UrlGuardInStartPath("http://h/pub/", "http://h/public/x"));
  CU_ASSERT_FALSE(UrlGuardInStartPath("http://h/pub/x?d=/a/", "http://h/a/y"));
}

/**
 * \test Local paths for redirected links stay inside the download directory
 */
void testUrlGuardLocalPath()
{
  char out[256];
  CU_ASSERT_EQUAL(UrlGuardLocalPath("http://u:p@h:81/pub/away?x=1", "/d", out, sizeof(out)), 0);
  CU_ASSERT_STRING_EQUAL(out, "/d/h:81/pub/away");
  CU_ASSERT_EQUAL(UrlGuardLocalPath("https://h/a/b.tgz", NULL, out, sizeof(out)), 0);
  CU_ASSERT_STRING_EQUAL(out, "./h/a/b.tgz");
  CU_ASSERT_EQUAL(UrlGuardLocalPath("http://h/a/../../etc/x", "/d", out, sizeof(out)), -1);
  CU_ASSERT_EQUAL(UrlGuardLocalPath("http://h/a/./x", "/d", out, sizeof(out)), -1);
  CU_ASSERT_EQUAL(UrlGuardLocalPath("http://h/it's", "/d", out, sizeof(out)), -1);
  CU_ASSERT_EQUAL(UrlGuardLocalPath("http://h/dir/", "/d", out, sizeof(out)), -1);
  CU_ASSERT_EQUAL(UrlGuardLocalPath("http://h", "/d", out, sizeof(out)), -1);
  CU_ASSERT_EQUAL(UrlGuardLocalPath("http://h/x", "/d", out, 6), -1);
  CU_ASSERT_EQUAL(UrlGuardLocalPath("http://h/x", "/d", out, 7), 0);
}

CU_TestInfo testcases_UrlGuard[] =
{
  {"UrlGuard:host", testUrlGuardHost},
  {"UrlGuard:is_public", testUrlGuardIsPublic},
  {"UrlGuard:check", testUrlGuardCheck},
  {"UrlGuard:join", testUrlGuardJoin},
  {"UrlGuard:redirects_refuse_internal", testResolveRedirectsRefusesInternal},
  {"UrlGuard:same_origin", testUrlGuardSameOrigin},
  {"UrlGuard:wget_log", testUrlGuardWgetLog},
  {"UrlGuard:no_proxy", testUrlGuardNoProxy},
  {"UrlGuard:check_via_proxy", testUrlGuardCheckVia},
  {"UrlGuard:proxy_applies", testProxyApplies},
  {"UrlGuard:shared_addresses", testUrlGuardSharedAddresses},
  {"UrlGuard:in_start_path", testUrlGuardInStartPath},
  {"UrlGuard:local_path", testUrlGuardLocalPath},
  CU_TEST_INFO_NULL
};

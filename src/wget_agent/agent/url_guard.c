/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * \file
 * \brief Destination checks for URL and VCS downloads
 */
#include "url_guard.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/** IPv4 networks that are not public: address (host order) and prefix */
static const struct { uint32_t net; int bits; } blocked4[] = {
  {0x00000000, 8},  /* 0.0.0.0/8 */
  {0x0A000000, 8},  /* 10.0.0.0/8 */
  {0x64400000, 10}, /* 100.64.0.0/10 */
  {0x7F000000, 8},  /* 127.0.0.0/8 */
  {0xA9FE0000, 16}, /* 169.254.0.0/16 */
  {0xAC100000, 12}, /* 172.16.0.0/12 */
  {0xC0000000, 24}, /* 192.0.0.0/24 */
  {0xC0000200, 24}, /* 192.0.2.0/24 */
  {0xC0A80000, 16}, /* 192.168.0.0/16 */
  {0xC6120000, 15}, /* 198.18.0.0/15 */
  {0xC6336400, 24}, /* 198.51.100.0/24 */
  {0xCB007100, 24}, /* 203.0.113.0/24 */
  {0xE0000000, 3},  /* 224.0.0.0/3: multicast, reserved, broadcast */
};

static int ipv4Blocked(const unsigned char *b)
{
  uint32_t a = ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) |
    ((uint32_t) b[2] << 8) | b[3];
  size_t i;
  for (i = 0; i < sizeof(blocked4) / sizeof(blocked4[0]); i++)
  {
    if (((a ^ blocked4[i].net) >> (32 - blocked4[i].bits)) == 0)
    {
      return 1;
    }
  }
  return 0;
}

/**
 * IPv4 written inside IPv6 (::a.b.c.d, ::ffff:a.b.c.d, 64:ff9b::a.b.c.d, 6to4)
 * \return pointer to the four IPv4 bytes, or NULL
 */
static const unsigned char *embeddedIpv4(const unsigned char *b)
{
  static const unsigned char zero[12] = {0};
  if (memcmp(b, zero, 10) == 0 &&
      ((b[10] == 0xff && b[11] == 0xff) || (b[10] == 0 && b[11] == 0)))
  {
    return b + 12;
  }
  if (b[0] == 0x00 && b[1] == 0x64 && b[2] == 0xff && b[3] == 0x9b &&
      memcmp(b + 4, zero, 8) == 0)
  {
    return b + 12;
  }
  if (b[0] == 0x20 && b[1] == 0x02)
  {
    return b + 2;
  }
  return NULL;
}

/**
 * \param sa An AF_INET or AF_INET6 address
 * \return 1 if the address is public, 0 otherwise
 */
int UrlGuardIsPublic(const struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET)
  {
    const struct sockaddr_in *in = (const struct sockaddr_in *) sa;
    return !ipv4Blocked((const unsigned char *) &in->sin_addr.s_addr);
  }
  if (sa->sa_family == AF_INET6)
  {
    const unsigned char *b = ((const struct sockaddr_in6 *) sa)->sin6_addr.s6_addr;
    const unsigned char *v4 = embeddedIpv4(b);
    if (v4)
    {
      return !ipv4Blocked(v4);
    }
    if ((b[0] & 0xfe) == 0xfc ||                     /* fc00::/7 unique local */
        (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) ||   /* fe80::/10 link-local */
        (b[0] == 0xfe && (b[1] & 0xc0) == 0xc0) ||   /* fec0::/10 site-local */
        b[0] == 0xff ||                              /* ff00::/8 multicast */
        (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x0d && b[3] == 0xb8) || /* 2001:db8::/32 */
        (b[0] == 0x01 && b[1] == 0 && b[2] == 0 && b[3] == 0 &&
         b[4] == 0 && b[5] == 0 && b[6] == 0 && b[7] == 0))  /* 100::/64 discard */
    {
      return 0;
    }
    return 1;
  }
  return 0;
}

/**
 * \brief Authority of scheme://[user@]host[:port]/..., without user information
 * \return 0 on success, -1 if the URL has no "://"
 */
static int authorityOf(const char *url, const char **start, const char **end)
{
  const char *sep = strstr(url, "://");

  if (!sep)
  {
    return -1;
  }
  *start = sep + 3;
  *end = *start + strcspn(*start, "/?#");
  for (const char *at = *start; at < *end; at++)
  {
    if (*at == '@')
    {
      *start = at + 1;
    }
  }
  return 0;
}

/**
 * \brief Host part of a URL, lower case, without brackets or a trailing dot
 *
 * Accepts scheme://[user@]host[:port]/... and user@host:path (scp style).
 * \return 0 on success, -1 if there is no host
 */
int UrlGuardHost(const char *url, char *host, size_t size)
{
  const char *start;
  const char *end;
  size_t len;
  size_t i;

  if (authorityOf(url, &start, &end) != 0)
  {
    /* scp style: user@host:path, no slash before the colon */
    end = strchr(url, ':');
    if (!end || memchr(url, '/', end - url))
    {
      return -1;
    }
    start = url;
    for (const char *at = start; at < end; at++)
    {
      if (*at == '@')
      {
        start = at + 1;
      }
    }
  }
  if (start < end && *start == '[')
  {
    const char *close = memchr(start, ']', end - start);
    if (!close)
    {
      return -1;
    }
    start++;
    end = close;
  }
  else
  {
    const char *colon = memchr(start, ':', end - start);
    if (colon)
    {
      end = colon;
    }
  }
  len = end - start;
  while (len > 0 && start[len - 1] == '.')
  {
    len--;
  }
  if (len == 0 || len >= size)
  {
    return -1;
  }
  for (i = 0; i < len; i++)
  {
    host[i] = tolower((unsigned char) start[i]);
  }
  host[len] = '\0';
  return 0;
}

/**
 * \brief Whether an address lies in a network written as address[/prefix]
 */
static int inNetwork(const struct sockaddr *sa, const char *entry)
{
  char base[64];
  unsigned char want[16];
  const unsigned char *have;
  const char *slash = strchr(entry, '/');
  size_t baseLen = slash ? (size_t) (slash - entry) : strlen(entry);
  int family;
  int maxBits;
  int bits;
  int full;
  int rest;

  if (baseLen == 0 || baseLen >= sizeof(base))
  {
    return 0;
  }
  memcpy(base, entry, baseLen);
  base[baseLen] = '\0';

  if (sa->sa_family == AF_INET)
  {
    have = (const unsigned char *) &((const struct sockaddr_in *) sa)->sin_addr.s_addr;
    family = AF_INET;
    maxBits = 32;
  }
  else if (sa->sa_family == AF_INET6)
  {
    const unsigned char *b = ((const struct sockaddr_in6 *) sa)->sin6_addr.s6_addr;
    const unsigned char *v4 = embeddedIpv4(b);
    have = v4 ? v4 : b;
    family = v4 ? AF_INET : AF_INET6;
    maxBits = v4 ? 32 : 128;
  }
  else
  {
    return 0;
  }
  if (inet_pton(family, base, want) != 1)
  {
    return 0;
  }
  bits = maxBits;
  if (slash)
  {
    const char *p = slash + 1;
    if (*p == '\0')
    {
      return 0;
    }
    for (; *p; p++)
    {
      if (!isdigit((unsigned char) *p))
      {
        return 0;
      }
    }
    bits = atoi(slash + 1);
    if (bits < 1 || bits > maxBits)
    {
      return 0;
    }
  }
  full = bits / 8;
  rest = bits % 8;
  if (memcmp(have, want, full) != 0)
  {
    return 0;
  }
  return rest == 0 ||
    ((have[full] ^ want[full]) & (0xff << (8 - rest)) & 0xff) == 0;
}

/**
 * \brief Next comma separated entry of a list, trimmed
 * \return pointer after the entry, or NULL at the end
 */
static const char *nextEntry(const char *list, char *entry, size_t size)
{
  const char *end;
  size_t len;
  while (*list == ',' || isspace((unsigned char) *list))
  {
    list++;
  }
  if (*list == '\0')
  {
    return NULL;
  }
  end = list + strcspn(list, ",");
  len = end - list;
  while (len > 0 && isspace((unsigned char) list[len - 1]))
  {
    len--;
  }
  if (len >= size)
  {
    len = 0;
  }
  memcpy(entry, list, len);
  entry[len] = '\0';
  return end;
}

/**
 * \brief Whether a host is listed in a no_proxy value
 *
 * Entries are host names or domain suffixes, comma separated; "*" matches all.
 * \return 1 if the URL's host is listed, 0 otherwise
 */
int UrlGuardNoProxy(const char *url, const char *noProxy)
{
  char host[256];
  char entry[256];
  const char *list;

  if (!noProxy || UrlGuardHost(url, host, sizeof(host)) != 0)
  {
    return 0;
  }
  for (list = noProxy; (list = nextEntry(list, entry, sizeof(entry))) != NULL;)
  {
    const char *suffix = entry[0] == '.' ? entry + 1 : entry;
    size_t hostLen = strlen(host);
    size_t suffixLen = strlen(suffix);
    if (strcmp(entry, "*") == 0 || strcasecmp(host, suffix) == 0 ||
        (suffixLen > 0 && hostLen > suffixLen && host[hostLen - suffixLen - 1] == '.' &&
         strcasecmp(host + hostLen - suffixLen, suffix) == 0))
    {
      return 1;
    }
  }
  return 0;
}

/**
 * \brief Check that the agent may fetch the URL
 *
 * With a proxy the server may have no DNS for outside names; a host it can
 * not resolve is then left to the proxy. IP literals and names that do
 * resolve are still checked.
 * \param proxied 1 if a proxy will fetch the URL
 */
const char *UrlGuardCheckVia(const char *url, const char *allowInternal, int proxied)
{
  static char reason[512];
  char host[256];
  char entry[256];
  char text[INET6_ADDRSTRLEN];
  const char *list;
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  struct addrinfo *ai;

  if (UrlGuardHost(url, host, sizeof(host)) != 0)
  {
    return "the URL has no host";
  }
  for (list = allowInternal ? allowInternal : "";
       (list = nextEntry(list, entry, sizeof(entry))) != NULL;)
  {
    if (strcasecmp(entry, host) == 0)
    {
      return NULL;
    }
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL)
  {
    if (proxied)
    {
      return NULL;
    }
    snprintf(reason, sizeof(reason), "can not resolve host %s", host);
    return reason;
  }
  for (ai = res; ai; ai = ai->ai_next)
  {
    int allowed = UrlGuardIsPublic(ai->ai_addr);
    for (list = allowInternal ? allowInternal : "";
         !allowed && (list = nextEntry(list, entry, sizeof(entry))) != NULL;)
    {
      allowed = inNetwork(ai->ai_addr, entry);
    }
    if (!allowed)
    {
      const void *addr = ai->ai_family == AF_INET ?
        (const void *) &((struct sockaddr_in *) ai->ai_addr)->sin_addr :
        (const void *) &((struct sockaddr_in6 *) ai->ai_addr)->sin6_addr;
      inet_ntop(ai->ai_family, addr, text, sizeof(text));
      snprintf(reason, sizeof(reason),
        "host %s resolves to the internal address %s; allow it with "
        "allow_internal in the [UPLOAD] section of fossology.conf", host, text);
      freeaddrinfo(res);
      return reason;
    }
  }
  freeaddrinfo(res);
  return NULL;
}

/**
 * \brief Resolve a Location header against the URL it came from
 * \return 0 on success, -1 if the result does not fit
 */
int UrlGuardJoin(const char *base, const char *location, char *out, size_t size)
{
  const char *scheme = strstr(base, "://");
  const char *path;
  size_t keep;
  size_t locLen = strcspn(location, "#");
  int n;

  if (strstr(location, "://") && strstr(location, "://") < location + strcspn(location, "/?#"))
  {
    n = snprintf(out, size, "%.*s", (int) locLen, location);
  }
  else if (!scheme)
  {
    return -1;
  }
  else if (location[0] == '/' && location[1] == '/')
  {
    n = snprintf(out, size, "%.*s:%.*s", (int) (scheme - base), base,
      (int) locLen, location);
  }
  else
  {
    path = scheme + 3 + strcspn(scheme + 3, "/?#");
    if (location[0] == '?')
    {
      keep = (path - base) + strcspn(path, "?#");
    }
    else if (location[0] == '/')
    {
      keep = path - base;
    }
    else
    {
      const char *slash = NULL;
      const char *p;
      const char *query = path + strcspn(path, "?#");
      for (p = path; p < query; p++)
      {
        if (*p == '/')
        {
          slash = p;
        }
      }
      keep = slash ? (size_t) (slash + 1 - base) : (size_t) (path - base);
      if (!slash)
      {
        n = snprintf(out, size, "%.*s/%.*s", (int) keep, base, (int) locLen, location);
        return (n < 0 || (size_t) n >= size) ? -1 : 0;
      }
    }
    n = snprintf(out, size, "%.*s%.*s", (int) keep, base, (int) locLen, location);
  }
  return (n < 0 || (size_t) n >= size) ? -1 : 0;
}

/**
 * \brief Scheme, host and port of a URL; the port defaults by scheme
 * \return 0 on success, -1 if the URL has no scheme or host
 */
static int urlOrigin(const char *url, char *scheme, size_t schemeSize,
  char *host, size_t hostSize, long *port)
{
  const char *sep = strstr(url, "://");
  const char *authority;
  const char *end;
  const char *colon;
  size_t len;

  if (!sep || (size_t) (sep - url) >= schemeSize ||
      UrlGuardHost(url, host, hostSize) != 0)
  {
    return -1;
  }
  len = sep - url;
  for (size_t i = 0; i < len; i++)
  {
    scheme[i] = tolower((unsigned char) url[i]);
  }
  scheme[len] = '\0';

  authorityOf(url, &authority, &end);
  if (*authority == '[')
  {
    const char *close = memchr(authority, ']', end - authority);
    colon = (close && close + 1 < end && close[1] == ':') ? close + 1 : NULL;
  }
  else
  {
    colon = memchr(authority, ':', end - authority);
  }
  if (colon && colon + 1 < end)
  {
    *port = strtol(colon + 1, NULL, 10);
  }
  else if (strcmp(scheme, "https") == 0)
  {
    *port = 443;
  }
  else if (strcmp(scheme, "ftp") == 0)
  {
    *port = 21;
  }
  else
  {
    *port = 80;
  }
  return 0;
}

/**
 * \brief Whether two URLs share scheme, host and port
 * \return 1 if they do, 0 otherwise
 */
int UrlGuardSameOrigin(const char *a, const char *b)
{
  char schemeA[16], schemeB[16], hostA[256], hostB[256];
  long portA, portB;

  if (urlOrigin(a, schemeA, sizeof(schemeA), hostA, sizeof(hostA), &portA) != 0 ||
      urlOrigin(b, schemeB, sizeof(schemeB), hostB, sizeof(hostB), &portB) != 0)
  {
    return 0;
  }
  return strcmp(schemeA, schemeB) == 0 && strcmp(hostA, hostB) == 0 && portA == portB;
}

/**
 * \brief Read a wget log written with -S and redirects off
 *
 * Request lines look like "--2026-01-01 10:00:00--  http://host/path"; the
 * response headers that follow are indented, so an indented "Location:" is
 * the redirect of the last request.
 * \param log        Open wget log
 * \param onRedirect Called with the request URL and the raw Location value
 * \param ctx        Passed to onRedirect
 * \return number of error responses other than redirects
 */
int UrlGuardWgetLog(FILE *log, UrlGuardRedirectFn onRedirect, void *ctx)
{
  char line[4096];
  char current[4096] = "";
  int errors = 0;

  while (fgets(line, sizeof(line), log))
  {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
    {
      line[--len] = '\0';
    }
    if (strncmp(line, "--", 2) == 0)
    {
      const char *url = strstr(line + 2, "--  ");
      if (url)
      {
        snprintf(current, sizeof(current), "%s", url + 4);
      }
    }
    else if (isspace((unsigned char) line[0]))
    {
      const char *p = line;
      while (isspace((unsigned char) *p))
      {
        p++;
      }
      if (strncasecmp(p, "Location:", 9) == 0 && current[0])
      {
        p += 9;
        while (isspace((unsigned char) *p))
        {
          p++;
        }
        if (*p)
        {
          onRedirect(current, p, ctx);
        }
      }
    }
    else if (strstr(line, " ERROR ") || strncmp(line, "ERROR ", 6) == 0)
    {
      errors++;
    }
  }
  return errors;
}

/**
 * \brief Whether a URL lies under the directory of the start URL
 */
int UrlGuardInStartPath(const char *startUrl, const char *url)
{
  const char *startPath;
  const char *path;
  size_t dirLen;
  const char *slash;
  const char *sep = strstr(startUrl, "://");
  const char *sep2 = strstr(url, "://");

  if (!sep || !sep2)
  {
    return 0;
  }
  startPath = sep + 3 + strcspn(sep + 3, "/?#");
  path = sep2 + 3 + strcspn(sep2 + 3, "/?#");
  slash = startPath;
  for (const char *p = startPath; *p && *p != '?' && *p != '#'; p++)
  {
    if (*p == '/')
    {
      slash = p;
    }
  }
  dirLen = (*slash == '/') ? (size_t) (slash - startPath + 1) : 0;
  return dirLen == 0 || strncmp(path, startPath, dirLen) == 0;
}

/**
 * \brief Where wget saves a URL under dest: dest/host[:port]/path
 * \return 0 on success, -1 for a URL that can not be mapped safely
 */
int UrlGuardLocalPath(const char *url, const char *dest, char *out, size_t size)
{
  const char *authority;
  const char *end;
  const char *pathEnd;
  int n;

  if (authorityOf(url, &authority, &end) != 0)
  {
    return -1;
  }
  pathEnd = end + strcspn(end, "?#");
  if (*end != '/' || pathEnd[-1] == '/' || memchr(end, '\'', pathEnd - end) ||
      memchr(authority, '\'', end - authority) || strstr(end, "/../") ||
      strstr(end, "/./"))
  {
    return -1;
  }
  n = snprintf(out, size, "%s/%.*s%.*s", dest ? dest : ".",
    (int) (end - authority), authority, (int) (pathEnd - end), end);
  return (n < 0 || (size_t) n >= size) ? -1 : 0;
}

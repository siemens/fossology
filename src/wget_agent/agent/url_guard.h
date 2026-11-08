/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * \file
 * \brief Decide whether the agent may fetch a URL: refuse hosts that resolve
 * to private, loopback, link-local or reserved addresses unless allowed by
 * `[UPLOAD] allow_internal` in fossology.conf.
 */
#ifndef URL_GUARD_H
#define URL_GUARD_H

#include <stddef.h>
#include <stdio.h>
#include <sys/socket.h>

int UrlGuardHost(const char *url, char *host, size_t size);
int UrlGuardIsPublic(const struct sockaddr *sa);
const char *UrlGuardCheckVia(const char *url, const char *allowInternal, int proxied);
int UrlGuardNoProxy(const char *url, const char *noProxy);
int UrlGuardJoin(const char *base, const char *location, char *out, size_t size);
int UrlGuardSameOrigin(const char *a, const char *b);
int UrlGuardInStartPath(const char *startUrl, const char *url);
int UrlGuardLocalPath(const char *url, const char *dest, char *out, size_t size);

/** Called for each redirect found in a wget log */
typedef void (*UrlGuardRedirectFn)(const char *from, const char *location, void *ctx);
int UrlGuardWgetLog(FILE *log, UrlGuardRedirectFn onRedirect, void *ctx);

#endif

<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

namespace Fossology\Lib\Util;

/**
 * @class UrlDestination
 * @brief Decides whether the server may fetch a user-supplied URL
 *
 * A host is refused when any address it resolves to is private, loopback,
 * link-local or otherwise not public, unless the host name or a network
 * holding the address is listed in `[UPLOAD] allow_internal`.
 */
class UrlDestination
{
  /** Not public, and not covered by FILTER_FLAG_NO_PRIV_RANGE|NO_RES_RANGE;
   * the same list as wget_agent/agent/url_guard.c */
  const EXTRA_BLOCKED = ['100.64.0.0/10', '192.0.0.0/24', '192.0.2.0/24',
    '198.18.0.0/15', '198.51.100.0/24', '203.0.113.0/24', '224.0.0.0/3',
    'fc00::/7', 'fe80::/10', 'fec0::/10', 'ff00::/8', '2001:db8::/32', '100::/64'];

  /**
   * @param string $url
   * @param string $allowInternal Comma separated host names and networks
   * @param callable|null $resolve host => list of addresses
   * @param bool $proxied A proxy fetches the URL; a host this server can not
   *        resolve is then left to the proxy
   * @return string|null Why the URL is refused, or null when allowed
   */
  public static function check($url, $allowInternal = '', $resolve = null,
    $proxied = false)
  {
    $parts = parse_url($url);
    $host = strtolower(rtrim(trim($parts['host'] ?? '', '[]'), '.'));
    if ($host === '') {
      return _("The URL has no host.");
    }
    $allowed = array_filter(array_map(function ($entry) {
      return strtolower(trim($entry));
    }, explode(',', (string) $allowInternal)));
    if (in_array($host, $allowed, true)) {
      return null;
    }

    if (filter_var($host, FILTER_VALIDATE_IP) !== false) {
      $addresses = [$host];
    } else {
      $addresses = ($resolve ?? [self::class, 'resolve'])($host);
    }
    if (empty($addresses)) {
      return $proxied ? null : sprintf(_("Can not resolve host %s."), $host);
    }
    foreach ($addresses as $address) {
      if (!self::isPublic($address) && !self::inAnyNetwork($address, $allowed)) {
        return sprintf(_("Host %s resolves to the internal address %s. " .
          "An administrator can allow it with allow_internal in the [UPLOAD] " .
          "section of fossology.conf."), $host, $address);
      }
    }
    return null;
  }

  /**
   * Whether a proxy from the [FOSSOLOGY] section of fossology.conf fetches
   * the URL, as wget_agent decides it
   * @param string $url
   * @param array $conf The [FOSSOLOGY] section
   * @param bool $vcs Git and svn use http_proxy for every scheme
   * @return bool
   */
  public static function isProxied($url, $conf, $vcs = false)
  {
    $scheme = strtolower((string) parse_url($url, PHP_URL_SCHEME));
    $key = 'http_proxy';
    if (!$vcs && $scheme === 'https') {
      $key = 'https_proxy';
    } elseif (!$vcs && $scheme === 'ftp') {
      $key = 'ftp_proxy';
    } elseif (!$vcs && $scheme !== 'http') {
      return false;
    }
    if (empty(trim($conf[$key] ?? ''))) {
      return false;
    }
    $host = strtolower(rtrim(trim((string) parse_url($url, PHP_URL_HOST), '[]'), '.'));
    foreach (explode(',', $conf['no_proxy'] ?? '') as $entry) {
      $entry = strtolower(trim($entry));
      $suffix = ltrim($entry, '.');
      if ($entry === '*' || ($suffix !== '' && ($host === $suffix ||
          substr($host, -strlen($suffix) - 1) === '.' . $suffix))) {
        return false;
      }
    }
    return true;
  }

  /**
   * @param string $host
   * @return string[]
   */
  public static function resolve($host)
  {
    $addresses = gethostbynamel($host) ?: [];
    foreach (@dns_get_record($host, DNS_AAAA) ?: [] as $record) {
      if (!empty($record['ipv6'])) {
        $addresses[] = $record['ipv6'];
      }
    }
    return $addresses;
  }

  /**
   * @param string $address
   * @return bool
   */
  public static function isPublic($address)
  {
    $address = self::unwrapIpv4($address);
    if (filter_var($address, FILTER_VALIDATE_IP,
        FILTER_FLAG_NO_PRIV_RANGE | FILTER_FLAG_NO_RES_RANGE) === false) {
      return false;
    }
    return !self::inAnyNetwork($address, self::EXTRA_BLOCKED);
  }

  /**
   * IPv4 addresses written as IPv6 (::a.b.c.d, ::ffff:a.b.c.d,
   * 64:ff9b::a.b.c.d, 6to4 2002:aabb:ccdd::)
   * @param string $address
   * @return string
   */
  private static function unwrapIpv4($address)
  {
    $packed = @inet_pton($address);
    if ($packed === false || strlen($packed) !== 16) {
      return $address;
    }
    $prefix = substr($packed, 0, 12);
    if ($prefix === str_repeat("\0", 10) . "\xff\xff" || $prefix === str_repeat("\0", 12) ||
        $prefix === "\x00\x64\xff\x9b" . str_repeat("\0", 8)) {
      return inet_ntop(substr($packed, 12));
    }
    if (substr($packed, 0, 2) === "\x20\x02") {
      return inet_ntop(substr($packed, 2, 4));
    }
    return $address;
  }

  /**
   * @param string $address
   * @param string[] $networks Entries that are not networks are skipped
   * @return bool
   */
  private static function inAnyNetwork($address, $networks)
  {
    $packed = @inet_pton(self::unwrapIpv4($address));
    if ($packed === false) {
      return false;
    }
    foreach ($networks as $network) {
      $parts = explode('/', $network);
      $base = @inet_pton($parts[0]);
      if (count($parts) > 2 || $base === false || strlen($base) !== strlen($packed)) {
        continue;
      }
      $bits = 8 * strlen($base);
      if (isset($parts[1])) {
        if (!ctype_digit($parts[1]) || intval($parts[1]) < 1 || intval($parts[1]) > $bits) {
          continue;
        }
        $bits = intval($parts[1]);
      }
      $bytes = intdiv($bits, 8);
      if (substr($packed, 0, $bytes) !== substr($base, 0, $bytes)) {
        continue;
      }
      $rest = $bits % 8;
      if ($rest === 0 ||
          ((ord($packed[$bytes]) ^ ord($base[$bytes])) & (0xff << (8 - $rest)) & 0xff) === 0) {
        return true;
      }
    }
    return false;
  }
}

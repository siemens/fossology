<?php
/*
 SPDX-FileCopyrightText: © 2014-2015 Siemens AG

 SPDX-License-Identifier: GPL-2.0-only
*/

/**
 * @namespace Fossology::Lib::Auth
 * Contains the constants and helpers for authentication of user.
 */
namespace Fossology\Lib\Auth;

/**
 * @file
 * @brief Contains the constants and helpers for authentication of user.
 * @class Auth
 * @brief Contains the constants and helpers for authentication of user.
 *
 * Permissions
 * See https://github.com/fossology/fossology/wiki/Access-Control
 */
class Auth
{
  /** @var string USER_NAME
   * Session variable name containing user name */
  const USER_NAME = 'User';
  /** @var string USER_ID
   * Session variable name containing user id */
  const USER_ID = 'UserId';
  /** @var string GROUP_ID
   * Session variable name containing group id */
  const GROUP_ID = 'GroupId';
  /** @var string USER_LEVEL
   * Session variable name containing user permission level */
  const USER_LEVEL = 'UserLevel';

  /** @var int PERM_NONE
   * No permissions */
  const PERM_NONE = 0;
  /** @var int PERM_READ
   * Read only permission */
  const PERM_READ = 1;
  /** @var int PERM_WRITE
   * DB writes permitted */
  const PERM_WRITE= 3;
  /** @var int PERM_WRITE
   * DB writes permitted, with additional clearing permissions. */
  const PERM_CADMIN=5;
  /** @var int PERM_ADMIN
   * Add/delete users and groups. This is the 'superuser' permission. */
  const PERM_ADMIN=10;

  /** @var int TOKEN_OAUTH
   * Allow OAuth tokens for REST */
  const TOKEN_OAUTH = 0x1;
  /** @var int TOKEN_TOKEN
   * Allow FOSSology JWT tokens for REST */
  const TOKEN_TOKEN = 0x2;
  /** @var int TOKEN_BOTH
   * Allow both token formats for REST */
  const TOKEN_BOTH  = 0x3;

  /**
   * @brief Get the current user's id
   * @return int User id
   */
  public static function getUserId()
  {
    if (array_key_exists('auth', $GLOBALS['SysConf'])) {
      return $GLOBALS['SysConf']['auth'][self::USER_ID];
    }
    return 0;
  }

  /**
   * @brief Get the current user's group id
   * @return int Group id
   */
  public static function getGroupId()
  {
    if (array_key_exists('auth', $GLOBALS['SysConf'])) {
      return $GLOBALS['SysConf']['auth'][self::GROUP_ID];
    }
    return 0;
  }

  /**
   * @brief Check if user is admin
   * @return boolean True if user is an admin, false otherwise.
   */
  public static function isAdmin()
  {
    return isset($_SESSION[self::USER_LEVEL]) && $_SESSION[self::USER_LEVEL] == self::PERM_ADMIN;
  }

  /**
   * @brief Check if user is clearing admin
   * @return boolean True if user is an clearing admin or more, false otherwise.
   */
  public static function isClearingAdmin()
  {
    return $_SESSION[self::USER_LEVEL]>=self::PERM_CADMIN;
  }

  /**
   * Whether the browser reached this site over HTTPS, directly or through a
   * proxy that sets X-Forwarded-Proto
   *
   * @param array $server Request headers as in $_SERVER
   * @return boolean
   */
  public static function isHttpsRequest($server)
  {
    $https = strtolower((string) ($server['HTTPS'] ?? ''));
    $forwarded = strtolower(trim(explode(',', (string) ($server['HTTP_X_FORWARDED_PROTO'] ?? ''))[0]));
    return ($https !== '' && $https !== 'off') || $forwarded === 'https';
  }

  /**
   * Whether a URL may be used as a redirect target: a local path, or an
   * http(s) URL on this host. Backslashes, whitespace and control characters
   * are refused because browsers read a backslash as a slash.
   *
   * @param string $url
   * @param array $server Request headers as in $_SERVER
   * @return boolean
   */
  public static function isLocalRedirect($url, $server)
  {
    if (preg_match('/[\\\\\s\x00-\x1f]/', $url)) {
      return false;
    }
    $parts = parse_url($url);
    if ($parts === false) {
      return false;
    }
    if (empty($parts['scheme']) && empty($parts['host'])) {
      return preg_match('#^/(?!/)#', $url) === 1;
    }
    $scheme = strtolower($parts['scheme'] ?? '');
    $own = parse_url('http://' . ($server['HTTP_HOST'] ?? ''));
    $ownPort = $own['port'] ?? (self::isHttpsRequest($server) ? 443 : 80);
    $port = $parts['port'] ?? ($scheme === 'https' ? 443 : 80);
    return in_array($scheme, ['http', 'https'], true) && !empty($parts['host']) &&
      strcasecmp($parts['host'], (string) ($own['host'] ?? '')) === 0 && $port == $ownPort;
  }

  /**
   * Check that a state-changing browser request comes from this site.
   * Origin is preferred, Referer is the fallback, and a request with
   * neither is allowed so non-browser clients keep working.
   *
   * @param array $server Request headers as in $_SERVER
   * @return boolean False for a cross-site POST, PUT, PATCH or DELETE
   */
  public static function isSameOriginRequest($server)
  {
    global $SysConf;
    if (!filter_var($SysConf['AUTHENTICATION']['origin_check'] ?? true,
        FILTER_VALIDATE_BOOLEAN)) {
      return true;
    }
    $method = strtoupper($server['REQUEST_METHOD'] ?? 'GET');
    if (in_array($method, ['GET', 'HEAD', 'OPTIONS'], true)) {
      return true;
    }
    $source = $server['HTTP_ORIGIN'] ?? ($server['HTTP_REFERER'] ?? '');
    if ($source === '') {
      return true;
    }
    $parts = parse_url($source);
    if (empty($parts['host'])) {
      return false;
    }
    $sourceHost = strtolower($parts['host'] .
      (isset($parts['port']) ? ':' . $parts['port'] : ''));
    $hosts = [$server['HTTP_HOST'] ?? ''];
    if (!empty($server['HTTP_X_FORWARDED_HOST'])) {
      $hosts[] = trim(explode(',', $server['HTTP_X_FORWARDED_HOST'])[0]);
    }
    /* behind a proxy that rewrites Host, the configured public address */
    $siteUrl = trim($SysConf['SYSCONFIG']['FOSSologyURL'] ?? '');
    if ($siteUrl !== '') {
      $site = parse_url(preg_match('#^[a-z][a-z0-9+.-]*://#i', $siteUrl) ?
        $siteUrl : "http://$siteUrl");
      if (!empty($site['host'])) {
        $hosts[] = $site['host'] . (isset($site['port']) ? ':' . $site['port'] : '');
      }
    }
    foreach ($hosts as $host) {
      if ($host !== '' && strtolower($host) === $sourceHost) {
        return true;
      }
    }
    return false;
  }

  /**
   * Get REST Token format from conf
   * @return int Auth::TOKEN_TOKEN | Auth::TOKEN_OAUTH | Auth::TOKEN_BOTH
   */
  public static function getRestTokenType()
  {
    global $SysConf;
    $restToken = "token";
    if (array_key_exists('AUTHENTICATION', $SysConf) &&
        array_key_exists('resttoken', $SysConf['AUTHENTICATION'])) {
      $restToken = $SysConf['AUTHENTICATION']['resttoken'];
    }
    switch ($restToken) {
      case 'oauth':
        return self::TOKEN_OAUTH;
        break;
      case 'both':
        return self::TOKEN_BOTH;
        break;
      default:
        return self::TOKEN_TOKEN;
    }
  }
}

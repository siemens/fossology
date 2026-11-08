<?php
/*
 SPDX-FileCopyrightText: © 2026 Siemens AG

 SPDX-License-Identifier: GPL-2.0-only
*/

namespace Fossology\Lib\Auth;

/**
 * @class AuthTest
 * @brief Test for class Auth
 */
class AuthTest extends \PHPUnit\Framework\TestCase
{
  protected function setUp(): void
  {
    $_SESSION = array();
  }

  protected function tearDown(): void
  {
    $_SESSION = array();
  }

  /**
   * @test
   * -# isAdmin() must return false, not raise a warning, when nothing has
   *    populated the session yet (e.g. an anonymous request to a page with
   *    REQUIRES_LOGIN => false).
   */
  public function testIsAdminFalseWhenSessionEmpty(): void
  {
    $this->assertFalse(Auth::isAdmin());
  }

  /**
   * @test
   * -# isAdmin() must return false for a logged-in, non-admin user level.
   */
  public function testIsAdminFalseWhenUserLevelBelowAdmin(): void
  {
    $_SESSION[Auth::USER_LEVEL] = Auth::PERM_ADMIN - 1;
    $this->assertFalse(Auth::isAdmin());
  }

  /**
   * @test
   * -# isAdmin() must return true when the session user level is exactly
   *    PERM_ADMIN.
   */
  public function testIsAdminTrueWhenUserLevelIsAdmin(): void
  {
    $_SESSION[Auth::USER_LEVEL] = Auth::PERM_ADMIN;
    $this->assertTrue(Auth::isAdmin());
  }

  public function testSameOriginAllowsSafeMethods()
  {
    $this->assertTrue(Auth::isSameOriginRequest(['REQUEST_METHOD' => 'GET',
      'HTTP_HOST' => 'fossy.example', 'HTTP_ORIGIN' => 'https://evil.example']));
  }

  public function testSameOriginAcceptsOwnSite()
  {
    foreach ([['HTTP_ORIGIN' => 'https://Fossy.example:8081'],
        ['HTTP_REFERER' => 'https://fossy.example:8081/repo/?mod=browse'],
        []] as $source) {
      $this->assertTrue(Auth::isSameOriginRequest($source + [
        'REQUEST_METHOD' => 'POST', 'HTTP_HOST' => 'fossy.example:8081']));
    }
  }

  public function testSameOriginAcceptsForwardedHost()
  {
    $this->assertTrue(Auth::isSameOriginRequest(['REQUEST_METHOD' => 'POST',
      'HTTP_HOST' => 'web:80', 'HTTP_X_FORWARDED_HOST' => 'fossy.example, proxy',
      'HTTP_ORIGIN' => 'https://fossy.example']));
  }

  public function testSameOriginRefusesOtherSites()
  {
    foreach ([['HTTP_ORIGIN' => 'https://evil.example'],
        ['HTTP_ORIGIN' => 'https://fossy.example.evil.example'],
        ['HTTP_ORIGIN' => 'https://fossy.example:9999'],
        ['HTTP_ORIGIN' => 'null'],
        ['HTTP_REFERER' => 'https://evil.example/?fossy.example'],
        ['HTTP_ORIGIN' => 'https://evil.example', 'HTTP_REFERER' => 'https://fossy.example/']]
        as $source) {
      $this->assertFalse(Auth::isSameOriginRequest($source + [
        'REQUEST_METHOD' => 'POST', 'HTTP_HOST' => 'fossy.example']), json_encode($source));
    }
  }

  public function testIsLocalRedirect()
  {
    $server = ['HTTP_HOST' => 'fossology.example:8080'];
    foreach (['/repo/?mod=browse&upload=3', 'http://FOSSOLOGY.example:8080/repo/',
        'https://fossology.example:8080/repo/'] as $url) {
      $this->assertTrue(Auth::isLocalRedirect($url, $server), $url);
    }
    foreach (['http://fossology.example:8888/evil', 'https://fossology.example/repo/',
        'http://fossology.example/repo/'] as $url) {
      $this->assertFalse(Auth::isLocalRedirect($url, $server), $url);
    }
    $this->assertTrue(Auth::isLocalRedirect('https://f.example/repo/',
      ['HTTP_HOST' => 'f.example', 'HTTPS' => 'on']));
    $this->assertFalse(Auth::isLocalRedirect('https://f.example:8443/',
      ['HTTP_HOST' => 'f.example', 'HTTPS' => 'on']));
    $this->assertFalse(Auth::isLocalRedirect('http://f.example/repo/',
      ['HTTP_HOST' => 'f.example', 'HTTPS' => 'on']));
    foreach (['https://evil.example/', '//evil.example/x', "/\\evil.example",
        "http://evil.example\\@fossology.example/", 'javascript:alert(1)',
        'http://fossology.example.evil.example/', 'relative/path', "/repo/\nx",
        'ftp://fossology.example/x', "/repo/ x"] as $url) {
      $this->assertFalse(Auth::isLocalRedirect($url, $server), $url);
    }
  }

  public function testSameOriginBehindProxyUsesConfiguredUrl()
  {
    $server = ['REQUEST_METHOD' => 'POST', 'HTTP_HOST' => 'fossology:80',
      'HTTP_ORIGIN' => 'https://fossology.corp'];
    $saved = $GLOBALS['SysConf'] ?? null;
    try {
      $GLOBALS['SysConf'] = ['SYSCONFIG' => []];
      $this->assertFalse(Auth::isSameOriginRequest($server));
      foreach (['fossology.corp/repo/', 'https://fossology.corp/repo/', 'FOSSOLOGY.corp'] as $url) {
        $GLOBALS['SysConf'] = ['SYSCONFIG' => ['FOSSologyURL' => $url]];
        $this->assertTrue(Auth::isSameOriginRequest($server), $url);
      }
      $GLOBALS['SysConf'] = ['SYSCONFIG' => ['FOSSologyURL' => 'fossology.corp:8443/repo/']];
      $this->assertFalse(Auth::isSameOriginRequest($server));
      $this->assertTrue(Auth::isSameOriginRequest(
        ['HTTP_ORIGIN' => 'https://fossology.corp:8443'] + $server));
      $this->assertFalse(Auth::isSameOriginRequest(
        ['HTTP_ORIGIN' => 'https://evil.example'] + $server));
      $GLOBALS['SysConf'] = ['AUTHENTICATION' => ['origin_check' => 'false']];
      $this->assertTrue(Auth::isSameOriginRequest(
        ['HTTP_ORIGIN' => 'https://evil.example'] + $server));
      $GLOBALS['SysConf'] = ['AUTHENTICATION' => ['origin_check' => 'true']];
      $this->assertFalse(Auth::isSameOriginRequest(
        ['HTTP_ORIGIN' => 'https://evil.example'] + $server));
    } finally {
      $GLOBALS['SysConf'] = $saved;
    }
  }

  public function testIsHttpsRequest()
  {
    foreach ([['HTTPS' => 'on'], ['HTTPS' => '1'], ['HTTP_X_FORWARDED_PROTO' => 'https'],
        ['HTTP_X_FORWARDED_PROTO' => 'HTTPS, http']] as $server) {
      $this->assertTrue(Auth::isHttpsRequest($server), json_encode($server));
    }
    foreach ([[], ['HTTPS' => 'off'], ['HTTPS' => ''], ['HTTP_X_FORWARDED_PROTO' => 'http'],
        ['HTTP_X_FORWARDED_PROTO' => 'http, https']] as $server) {
      $this->assertFalse(Auth::isHttpsRequest($server), json_encode($server));
    }
  }
}


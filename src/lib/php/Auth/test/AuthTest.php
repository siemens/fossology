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


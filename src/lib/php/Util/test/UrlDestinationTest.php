<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

namespace Fossology\Lib\Util;

class UrlDestinationTest extends \PHPUnit\Framework\TestCase
{
  private function resolver($map)
  {
    return function ($host) use ($map) {
      return $map[$host] ?? [];
    };
  }

  public function provideInternal()
  {
    return [
      ['http://127.0.0.1/a.tgz'], ['http://127.1/a.tgz'], ['http://[::1]/a.tgz'],
      ['http://[::ffff:127.0.0.1]/a.tgz'], ['http://[::ffff:a9fe:a9fe]/x'],
      ['http://[64:ff9b::10.0.0.1]/x'], ['http://169.254.169.254/latest/meta-data/'],
      ['http://10.1.2.3/a'], ['http://172.16.0.1/a'], ['http://192.168.1.1/a'],
      ['http://100.64.0.1/a'], ['http://0.0.0.0/a'], ['http://[fd00::1]/a'],
      ['http://[fe80::1]/a'], ['https://user:pw@10.0.0.5:8443/repo.git'],
      ['http://public.example@10.0.0.5/a'], ['ftp://192.168.0.9/pub/a.tgz'],
      ['http://LOCALHOST./a'],
    ];
  }

  /** @dataProvider provideInternal */
  public function testInternalRefused($url)
  {
    $resolve = $this->resolver(['localhost' => ['127.0.0.1', '::1']]);
    $this->assertNotNull(UrlDestination::check($url, '', $resolve), $url);
  }

  public function testPublicAllowed()
  {
    $resolve = $this->resolver(['example.org' => ['93.184.215.14', '2606:2800:21f:cb07:6820:80da:af6b:8b2c']]);
    $this->assertNull(UrlDestination::check('https://example.org/a.tgz', '', $resolve));
    $this->assertNull(UrlDestination::check('http://8.8.8.8/a', '', $resolve));
    $this->assertNull(UrlDestination::check('http://[2606:4700::1111]/a', '', $resolve));
  }

  public function testAnyInternalAddressRefusesTheHost()
  {
    $resolve = $this->resolver(['mixed.example' => ['93.184.215.14', '10.0.0.7']]);
    $this->assertStringContainsString('10.0.0.7',
      UrlDestination::check('http://mixed.example/a', '', $resolve));
  }

  public function testUnresolvableAndHostlessRefused()
  {
    $resolve = $this->resolver([]);
    $this->assertStringContainsString('resolve',
      UrlDestination::check('http://nowhere.invalid/a', '', $resolve));
    $this->assertNotNull(UrlDestination::check('not a url', '', $resolve));
    $this->assertNotNull(UrlDestination::check('http:///a', '', $resolve));
  }

  public function testAllowInternal()
  {
    $resolve = $this->resolver(['git.corp.example' => ['10.20.1.5'],
      'other.corp.example' => ['10.30.1.5'], 'off.example' => []]);
    $allow = ' Git.Corp.Example , 10.20.0.0/16, 192.168.7.7, off.example ';
    $this->assertNull(UrlDestination::check('https://git.corp.example/r.git', $allow, $resolve));
    $this->assertNull(UrlDestination::check('http://10.20.200.1/a', $allow, $resolve));
    $this->assertNull(UrlDestination::check('http://192.168.7.7/a', $allow, $resolve));
    $this->assertNull(UrlDestination::check('http://off.example/a', $allow, $resolve));
    $this->assertNotNull(UrlDestination::check('https://other.corp.example/r', $allow, $resolve));
    $this->assertNotNull(UrlDestination::check('http://10.21.0.1/a', $allow, $resolve));
    $this->assertNotNull(UrlDestination::check('http://192.168.7.8/a', $allow, $resolve));
  }

  public function testNetworkPrefixesOffByteBoundary()
  {
    $resolve = $this->resolver([]);
    $this->assertNull(UrlDestination::check('http://10.0.0.9/a', '10.0.0.8/29', $resolve));
    $this->assertNotNull(UrlDestination::check('http://10.0.0.16/a', '10.0.0.8/29', $resolve));
    $this->assertNull(UrlDestination::check('http://[fd12:3456::1]/a', 'fd12:3400::/22', $resolve));
    $this->assertNotNull(UrlDestination::check('http://[fd12:3c56::1]/a', 'fd12:3400::/22', $resolve));
    $this->assertNotNull(UrlDestination::check('http://10.0.0.9/a', 'garbage, 10.0.0.0/x', $resolve));
    foreach (['0.0.0.0/0', '10.0.0.0/', '10.0.0.0/-1', '10.0.0.0/33', '::/0', '10.0.0.0/8/8'] as $entry) {
      $this->assertNotNull(UrlDestination::check('http://10.0.0.9/a', $entry, $resolve), $entry);
    }
  }

  public function testExtraRanges()
  {
    $this->assertFalse(UrlDestination::isPublic('100.127.255.255'));
    $this->assertTrue(UrlDestination::isPublic('100.128.0.0'));
    $this->assertFalse(UrlDestination::isPublic('198.19.0.1'));
    $this->assertTrue(UrlDestination::isPublic('198.20.0.1'));
  }

  public function testProxyLeavesUnresolvableHostsToTheProxy()
  {
    $resolve = $this->resolver(['inside.example' => ['10.1.1.1']]);
    $this->assertNotNull(UrlDestination::check('http://outside.example/a', '', $resolve, false));
    $this->assertNull(UrlDestination::check('http://outside.example/a', '', $resolve, true));
    $this->assertNotNull(UrlDestination::check('http://inside.example/a', '', $resolve, true));
    $this->assertNotNull(UrlDestination::check('http://169.254.169.254/a', '', $resolve, true));
    $this->assertNotNull(UrlDestination::check('http://[::ffff:10.0.0.1]/a', '', $resolve, true));
  }

  public function testIsProxied()
  {
    $conf = ['http_proxy' => 'http://proxy:3128', 'https_proxy' => '',
      'no_proxy' => 'localhost, .corp.example,internal.example'];
    $this->assertTrue(UrlDestination::isProxied('http://github.com/x', $conf));
    $this->assertFalse(UrlDestination::isProxied('https://github.com/x', $conf));
    $this->assertTrue(UrlDestination::isProxied('https://github.com/x.git', $conf, true));
    $this->assertFalse(UrlDestination::isProxied('ftp://ftp.example/x', $conf));
    $this->assertFalse(UrlDestination::isProxied('http://git.corp.example/x', $conf));
    $this->assertFalse(UrlDestination::isProxied('http://a.internal.example/x', $conf));
    $this->assertTrue(UrlDestination::isProxied('http://evilcorp.example/x', $conf));
    $this->assertFalse(UrlDestination::isProxied('http://github.com/x',
      array_merge($conf, ['no_proxy' => '*'])));
    $this->assertFalse(UrlDestination::isProxied('http://github.com/x', []));
  }

  /** The address list shared with the wget_agent guard's tests */
  public static function sharedAddresses()
  {
    $file = dirname(__DIR__, 4) . '/testing/dataFiles/url-guard-addresses.txt';
    $cases = [];
    foreach (file($file, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) as $line) {
      if ($line[0] !== '#') {
        list($address, $class) = explode("\t", $line);
        $cases[$address] = [$address, $class === 'public'];
      }
    }
    return $cases;
  }

  /** @dataProvider sharedAddresses */
  public function testSharedAddressList($address, $public)
  {
    $this->assertSame($public, UrlDestination::isPublic($address));
  }
}


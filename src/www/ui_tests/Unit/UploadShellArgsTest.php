<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

use Fossology\Lib\Test\Reflectory;
use Symfony\Component\HttpFoundation\Request;

$lib = dirname(dirname(dirname(__DIR__))) . '/lib/php';
require_once("$lib/Test/Reflectory.php");
require_once("$lib/Plugin/FO_Plugin.php");
require_once("$lib/common-plugin.php");
require_once("$lib/common-menu.php");

/**
 * @class UploadShellArgsTest
 * @brief Values handed to wget_agent stay single shell words
 * @runTestsInSeparateProcesses
 * @preserveGlobalState disabled
 */
class UploadShellArgsTest extends \PHPUnit\Framework\TestCase
{
  private function page($class = 'UploadVcsPage')
  {
    $GLOBALS['SysConf'] = [];
    $GLOBALS['MenuList'] = [];
    $GLOBALS['Plugins'] = [];
    $GLOBALS['container'] = new class {
      public function get($name)
      {
        return new \stdClass();
      }
    };
    require_once(__DIR__ . "/../../ui/page/$class.php");
    return (new \ReflectionClass("Fossology\\UI\\Page\\$class"))
      ->newInstanceWithoutConstructor();
  }

  /** @test Every value survives the shell unchanged, as one word */
  public function testShQuoteIsOneWord()
  {
    $page = $this->page();
    foreach (["plain", "two words", "it's", "p\xc3\xa4ss", "*.tgz", "a;b", "a|b",
        "a&b", "\$HOME", "`x`", "back\\slash", '"q"'] as $value) {
      $quoted = Reflectory::invokeObjectsMethodnameWith($page, 'shQuote', [$value]);
      $output = [];
      exec('printf "%s\n" ' . $quoted, $output);
      $this->assertSame([$value], $output);
    }
  }

  /** @test VCS URLs are refused when the shell would interpret them */
  public function testVcsUrlWithShellCharactersRefused()
  {
    $page = $this->page();
    foreach (['a;b', 'a|b', 'a&b', 'a(b', 'a<b', "a'b"] as $path) {
      $request = new Request([], ['geturl' => "https://example.org/$path"]);
      $result = Reflectory::invokeObjectsMethodnameWith($page, 'handleUpload',
        [$request]);
      $this->assertFalse($result[0], $path);
      $this->assertStringContainsString('Invalid URL', $result[1]);
    }
  }

  /** @test URL and VCS uploads refuse internal hosts before creating an upload */
  public function testInternalDestinationRefused()
  {
    foreach (['UploadUrlPage', 'UploadVcsPage'] as $class) {
      $page = $this->page($class);
      foreach (['http://127.0.0.1/a.tgz', 'http://169.254.169.254/latest/',
          'https://10.0.0.1:8443/r.git'] as $url) {
        $request = new Request([], ['geturl' => $url, 'vcstype' => 'Git']);
        $result = Reflectory::invokeObjectsMethodnameWith($page, 'handleUpload',
          [$request]);
        $this->assertFalse($result[0], "$class $url");
        $this->assertStringContainsString('internal address', $result[1], "$class $url");
      }
    }
  }

  public static function shellPaths()
  {
    return [['a;b', 'a;b'], ['a&b', 'a&b'], ['a(b)', 'a(b)'], ['a;b', 'a;b/*.tar']];
  }

  /**
   * @test Server paths with shell characters are refused, patterns or not
   * @dataProvider shellPaths
   */
  public function testServerPathWithShellCharactersRefused($dirName, $relPath)
  {
    $base = sys_get_temp_dir() . '/fo_srv' . getmypid();
    @mkdir("$base/$dirName", 0777, true);
    $GLOBALS['SysConf'] = ['SYSCONFIG' => ['UploadFromServerWhitelist' => $base]];
    $page = $this->page('UploadSrvPage');
    $GLOBALS['SysConf'] = ['SYSCONFIG' => ['UploadFromServerWhitelist' => $base]];
    try {
      $request = new Request([], ['sourceFiles' => "$base/$relPath", 'folder' => 1,
        'uploadformbuild' => 'b', 'name' => 'n']);
      $request->setSession(new \Symfony\Component\HttpFoundation\Session\Session(
        new \Symfony\Component\HttpFoundation\Session\Storage\MockArraySessionStorage()));
      $request->getSession()->set('uploadformbuild', 'b');
      $result = Reflectory::invokeObjectsMethodnameWith($page, 'handleUpload', [$request]);
      $this->assertFalse($result[0], $relPath);
      $this->assertStringContainsString('not allowed', $result[1], $relPath);
    } finally {
      @rmdir("$base/$dirName");
      @rmdir($base);
    }
  }
}


<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

namespace Fossology\Lib\UI\TwigExtensions;

use Twig\Environment;
use Twig\Loader\ArrayLoader;
use Twig\Loader\FilesystemLoader;

class AssetVersionTest extends \PHPUnit\Framework\TestCase
{
  /** Scripts changed to send writes by POST; a cached old copy sends GET */
  const VERSIONED = ['scripts/change-license-common.js', 'scripts/license.js'];

  private $root;

  protected function setUp(): void
  {
    $this->root = sys_get_temp_dir() . '/assetversion' . getmypid();
    @mkdir($this->root . '/www/ui/scripts', 0777, true);
    file_put_contents($this->root . '/www/ui/scripts/a.js', '');
    touch($this->root . '/www/ui/scripts/a.js', 1700000000);
  }

  protected function tearDown(): void
  {
    @unlink($this->root . '/www/ui/scripts/a.js');
    @rmdir($this->root . '/www/ui/scripts');
    @rmdir($this->root . '/www/ui');
    @rmdir($this->root . '/www');
    @rmdir($this->root);
  }

  public function testAddsModificationTime()
  {
    $twig = new Environment(new ArrayLoader(['t' =>
      '<script src="{{ asset_version(\'scripts/a.js\') }}"></script>']));
    $twig->addExtension(new AssetVersion($this->root . '/'));
    $this->assertSame('<script src="scripts/a.js?v=1700000000"></script>',
      $twig->render('t'));
  }

  public function testUnknownOrOutsidePathIsUnchanged()
  {
    $extension = new AssetVersion($this->root);
    $this->assertSame('scripts/missing.js', $extension->assetVersion('scripts/missing.js'));
    $this->assertSame('../../etc/hostname', $extension->assetVersion('../../etc/hostname'));
  }

  /** Every page that loads a versioned script compiles and loads it with a version */
  public function testTemplatesLoadVersionedScripts()
  {
    $src = dirname(__DIR__, 5);
    $templates = 0;
    foreach (new \RecursiveIteratorIterator(new \RecursiveDirectoryIterator($src)) as $file) {
      $path = $file->getPathname();
      if (substr($path, -5) !== '.twig' || strpos($path, '/vendor/') !== false) {
        continue;
      }
      $content = file_get_contents($path);
      foreach (self::VERSIONED as $script) {
        if (strpos($content, $script) === false) {
          continue;
        }
        $templates++;
        $this->assertStringNotContainsString("src=\"$script\"", $content,
          "$path loads $script without asset_version()");
        $twig = new Environment(new FilesystemLoader([dirname($path),
          "$src/lib/php/UI/template"]));
        $twig->addExtension(new AssetVersion($src));
        $twig->addExtension(new TranslateFilter());
        $twig->parse($twig->tokenize($twig->getLoader()->getSourceContext(basename($path))));
      }
    }
    $this->assertGreaterThan(0, $templates);
  }
}

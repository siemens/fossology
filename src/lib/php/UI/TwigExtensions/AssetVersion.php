<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

namespace Fossology\Lib\UI\TwigExtensions;

use Twig\Extension\AbstractExtension;
use Twig\TwigFunction;

/**
 * @class AssetVersion
 * @brief `asset_version('scripts/x.js')` adds the file's modification time,
 * so browsers fetch a script again after an upgrade
 */
class AssetVersion extends AbstractExtension
{
  /** @var string */
  private $webRoot;

  public function __construct($applicationRoot)
  {
    $this->webRoot = rtrim($applicationRoot, '/') . '/www/ui/';
  }

  public function getFunctions()
  {
    return [new TwigFunction('asset_version', [$this, 'assetVersion'])];
  }

  /**
   * @param string $path Path relative to the web root
   * @return string
   */
  public function assetVersion($path)
  {
    $file = $this->webRoot . $path;
    if (strpos($path, '..') !== false || !is_file($file)) {
      return $path;
    }
    return $path . '?v=' . filemtime($file);
  }
}

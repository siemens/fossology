<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

/**
 * @class AddUserCallsTest
 * @brief Every add_user() call passes the arguments the function requires
 */
class AddUserCallsTest extends \PHPUnit\Framework\TestCase
{
  /** @test */
  public function testCallsMatchSignature()
  {
    $root = dirname(dirname(dirname(__DIR__)));
    require_once("$root/lib/php/common-users.php");
    $function = new \ReflectionFunction('add_user');
    $required = $function->getNumberOfRequiredParameters();
    $max = $function->getNumberOfParameters();

    $files = new \RegexIterator(new \RecursiveIteratorIterator(
      new \RecursiveDirectoryIterator($root)), '/\.php$/');
    $calls = 0;
    foreach ($files as $file) {
      $path = $file->getPathname();
      if (preg_match('#/(vendor|testing|\w*tests?)/#', $path)) {
        continue;
      }
      $tokens = token_get_all(file_get_contents($path));
      foreach ($tokens as $i => $token) {
        if (!is_array($token) || $token[0] !== T_STRING || $token[1] !== 'add_user') {
          continue;
        }
        $prev = $tokens[$i - 1][0] ?? null;
        $prev2 = $tokens[$i - 2][0] ?? null;
        $before = $prev === T_WHITESPACE ? $prev2 : $prev;
        if (in_array($before, [T_FUNCTION, T_OBJECT_OPERATOR, T_DOUBLE_COLON,
            T_NULLSAFE_OBJECT_OPERATOR], true)) {
          continue;
        }
        $depth = 0;
        $args = 0;
        $seen = false;
        for ($j = $i + 1; $j < count($tokens); $j++) {
          $t = $tokens[$j];
          if ($t === '(' || $t === '[') {
            $depth++;
          } elseif ($t === ')' || $t === ']') {
            $depth--;
            if ($depth === 0) {
              break;
            }
          } elseif ($t === ',' && $depth === 1) {
            $args++;
          } elseif ($depth === 1 && !(is_array($t) && $t[0] === T_WHITESPACE)) {
            $seen = true;
          }
        }
        $args += $seen ? 1 : 0;
        $calls++;
        $this->assertTrue($args >= $required && $args <= $max,
          "$path line {$token[2]}: add_user() called with $args arguments, needs $required to $max");
      }
    }
    $this->assertGreaterThan(0, $calls);
  }
}

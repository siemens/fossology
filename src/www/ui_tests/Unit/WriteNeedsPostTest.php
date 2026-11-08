<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

require_once(dirname(dirname(dirname(__DIR__))) . '/lib/php/common-parm.php');

/**
 * @class WriteNeedsPostTest
 * @brief Pages that change data must not act on a plain link
 */
class WriteNeedsPostTest extends \PHPUnit\Framework\TestCase
{
  protected function tearDown(): void
  {
    unset($_SERVER['REQUEST_METHOD']);
  }

  /** @test */
  public function testIsPostRequest()
  {
    foreach (['POST' => true, 'post' => true, 'GET' => false, 'HEAD' => false] as $method => $isPost) {
      $_SERVER['REQUEST_METHOD'] = $method;
      $this->assertSame($isPost, IsPostRequest(), $method);
    }
    unset($_SERVER['REQUEST_METHOD']);
    $this->assertFalse(IsPostRequest());
  }

  /**
   * @return array File, expression that must guard its change
   */
  public static function gates()
  {
    $fo = 'IsPostRequest()';
    $sf = "\$request->isMethod('POST')";
    return [
      ['www/ui/user-del.php', $fo], ['www/ui/group-add.php', $fo],
      ['www/ui/admin-config.php', $fo], ['www/ui/admin-scheduler.php', $fo],
      ['www/ui/admin-folder-create.php', $fo], ['www/ui/admin-folder-edit.php', $fo],
      ['delagent/ui/admin-folder-delete.php', $fo], ['maintagent/ui/maintagent.php', $fo],
      ['www/ui/user-add.php', $sf], ['www/ui/page/AdminGroupDelete.php', $sf],
      ['www/ui/page/AdminGroupEdit.php', $sf], ['www/ui/page/AdminContentDelete.php', $sf],
      ['www/ui/page/AdminContentMove.php', $sf], ['delagent/ui/Page/AdminUploadDelete.php', $sf],
      ['www/ui/ui-tags.php', $fo], ['www/ui/admin-tag-manage.php', $fo],
      ['buckets/ui/admin-bucket-pool.php', $fo], ['www/ui/ajax-acknowledgement-conf.php', $fo],
      ['www/ui/page/AdminLicenseAcknowledgements.php', $sf],
      ['www/ui/page/AdminLicenseStdComments.php', $sf],
      ['www/ui/async/AjaxLicenseStdComments.php', $sf],
      ['www/ui/async/AjaxLicenseAcknowledgements.php', $sf],
      ['www/ui/page/AdminLicenseCompatibilityRules.php', $sf],
      ['www/ui/page/AdviceLicense.php', $sf], ['www/ui/page/AdminLicenseCandidate.php', $sf],
      ['www/ui/agent-add.php', $sf], ['www/ui/showjobs.php', $fo],
    ];
  }

  /**
   * @test
   * @dataProvider gates
   * Source check only: it keeps the gate from being removed, it does not run the page.
   */
  public function testPageChecksMethod($file, $gate)
  {
    $source = file_get_contents(dirname(dirname(dirname(__DIR__))) . '/' . $file);
    $this->assertStringContainsString($gate, $source, "$file changes data on GET");
  }

  /**
   * @test Pages do not link to actions that now need POST; the link would
   * silently do nothing
   */
  public function testNoGetLinksToPostActions()
  {
    $root = dirname(dirname(dirname(__DIR__)));
    $links = [
      'www/ui/ui-tags.php' => "/href='[^']*action=delete/",
      'www/ui/template/ui-showjobs.js.twig' => '/href="\' \+ fullUri \+ \'&action=/',
    ];
    foreach ($links as $file => $pattern) {
      $this->assertSame(0, preg_match($pattern, file_get_contents("$root/$file")), $file);
    }
  }
}


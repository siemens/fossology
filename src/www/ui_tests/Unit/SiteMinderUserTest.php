<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

use Fossology\Lib\Auth\Auth;
use Fossology\Lib\Test\TestPgDb;

/**
 * @class SiteMinderUserTest
 * @brief First SiteMinder login creates a usable account
 * @runTestsInSeparateProcesses
 * @preserveGlobalState disabled
 */
class SiteMinderUserTest extends \PHPUnit\Framework\TestCase
{
  const EMAIL = "o'brien@example.org";

  /** @var TestPgDb */
  private $testDb;

  protected function setUp(): void
  {
    $lib = dirname(dirname(dirname(__DIR__))) . '/lib/php';
    foreach (['Plugin/FO_Plugin.php', 'common-plugin.php', 'common-menu.php',
        'common-db.php', 'common-parm.php', 'common-users.php'] as $file) {
      require_once("$lib/$file");
    }
    $this->testDb = new TestPgDb("smauth");
    $tables = ['users', 'groups', 'group_user_member', 'folder', 'foldercontents'];
    $this->testDb->createPlainTables(array_merge($tables, ['upload', 'uploadtree']));
    $this->testDb->createSequences(['users_user_pk_seq', 'group_group_pk_seq',
      'group_user_member_group_user_member_pk_seq', 'folder_folder_pk_seq',
      'foldercontents_foldercontents_pk_seq']);
    $this->testDb->alterTables($tables);
    $this->testDb->createViews(['folderlist']);
    $GLOBALS['PG_CONN'] = (new \ReflectionProperty($this->testDb, 'connection'))
      ->getValue($this->testDb);
    $GLOBALS['SysConf']['SYSCONFIG']['UploadVisibility'] = 'protected';
    $_SESSION = [];
    if (!class_exists('core_smauth', false)) {
      require_once(__DIR__ . '/../../ui/core-smauth.php');
    }
  }

  /** @test */
  public function testFirstLoginCreatesUser()
  {
    $auth = (new \ReflectionClass('core_smauth'))->newInstanceWithoutConstructor();
    $auth->CheckUser(self::EMAIL);

    $db = $this->testDb->getDbManager();
    $user = $db->getSingleRow("SELECT * FROM users WHERE user_email = $1",
      [self::EMAIL], __METHOD__ . '.user');
    $this->assertNotEmpty($user, "user stored with the email as given");
    $this->assertSame(self::EMAIL, $user['user_name']);
    $this->assertEquals(Auth::PERM_WRITE, $user['user_perm']);
    $this->assertNotEmpty($user['user_pass']);
    $this->assertFalse(password_verify('', $user['user_pass']));
    $this->assertEquals($user['user_pk'], $_SESSION['UserId']);
    $this->assertEquals(Auth::PERM_WRITE, $_SESSION[Auth::USER_LEVEL]);
    $folder = $db->getSingleRow("SELECT folder_pk FROM folder WHERE folder_name = $1",
      ["o'brien"], __METHOD__ . '.folder');
    $this->assertEquals($folder['folder_pk'], $user['root_folder_fk']);
  }

  /** @test */
  public function testSecondLoginReusesUser()
  {
    $auth = (new \ReflectionClass('core_smauth'))->newInstanceWithoutConstructor();
    $auth->CheckUser(self::EMAIL);
    $_SESSION = [];
    $auth->CheckUser(self::EMAIL);
    $count = $this->testDb->getDbManager()->getSingleRow(
      "SELECT count(*) cnt FROM users WHERE user_email = $1", [self::EMAIL], __METHOD__);
    $this->assertEquals(1, $count['cnt']);
    $this->assertNotEmpty($_SESSION['UserId']);
  }
}

<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

use Fossology\Lib\Auth\Auth;
use Fossology\Lib\Test\Reflectory;
use Mockery as M;
use Symfony\Component\HttpFoundation\Request;

$lib = dirname(dirname(dirname(__DIR__))) . '/lib/php';
require_once("$lib/Test/Reflectory.php");
require_once("$lib/Plugin/FO_Plugin.php");
require_once("$lib/common-plugin.php");
require_once("$lib/common-menu.php");

/**
 * @class AccessGuardsTest
 * @brief Guards in the legacy UI code shared with the REST API
 * @runTestsInSeparateProcesses
 * @preserveGlobalState disabled
 */
class AccessGuardsTest extends \PHPUnit\Framework\TestCase
{
  protected function setUp(): void
  {
    $GLOBALS['SysConf'] = ['auth' => [Auth::USER_ID => 2, Auth::GROUP_ID => 3]];
    $GLOBALS['MenuList'] = [];
    $GLOBALS['Plugins'] = [];
    $GLOBALS['container'] = new class {
      public function get($name)
      {
        return new \stdClass();
      }
    };
    if (!function_exists('passwordPolicyEnabled')) {
      function passwordPolicyEnabled()
      {
        return false;
      }
    }
    if (!function_exists('auth_external_check')) {
      function auth_external_check()
      {
        return false;
      }
    }
    if (!function_exists('FolderListUploads_perm')) {
      function FolderListUploads_perm($folder, $perm)
      {
        return [];
      }
    }
  }

  protected function tearDown(): void
  {
    $this->addToAssertionCount(M::getContainer()->mockery_getExpectationCount());
    M::close();
  }

  private function bare($class, $file)
  {
    if (!class_exists($class, false)) {
      require_once(__DIR__ . "/../../ui/$file");
    }
    return (new \ReflectionClass($class))->newInstanceWithoutConstructor();
  }

  private function userRec($name)
  {
    return ['user_pk' => 2, 'user_name' => $name, '_pass1' => 'a', '_pass2' => '',
      '_blank_pass' => '', 'user_email' => 'a@b.c'];
  }

  private function permissionPage($editable, $groups = [])
  {
    $page = $this->bare('UploadPermissionPage', 'page/UploadPermissionPage.php');
    $folderDao = M::mock();
    $folderDao->shouldReceive('getRootFolder->getId')->andReturn(1);
    $dao = M::mock(\Fossology\Lib\Dao\UploadPermissionDao::class);
    $dao->shouldReceive('isEditable')->with(11, 3)->andReturn($editable);
    $dao->shouldReceive('getPermissionGroups')->with(11)->andReturn($groups);
    Reflectory::setObjectsProperty($page, 'folderDao', $folderDao);
    Reflectory::setObjectsProperty($page, 'uploadPermDao', $dao);
    return [$page, $dao];
  }

  /** @test Username lookup binds the name as data */
  public function testUpdateUserBindsUserName()
  {
    $name = "o'brien";
    $_SESSION = ['User' => $name];
    $page = $this->bare('UserEditPage', 'user-edit.php');
    $db = M::mock(\Fossology\Lib\Db\DbManager::class);
    $db->shouldReceive('getSingleRow')->once()->with(
      M::on(fn ($sql) => strpos($sql, $name) === false), [$name], M::any())
      ->andReturn(false);
    $db->shouldReceive('getSingleRow')->andReturn(['user_email' => 'a@b.c']);
    Reflectory::setObjectsProperty($page, 'dbManager', $db);
    $this->assertStringContainsString('Passwords do not match',
      $page->UpdateUser($this->userRec($name), false));
  }

  /** @test A name held by another user is still refused */
  public function testUpdateUserDuplicateName()
  {
    $_SESSION = ['User' => 'bob'];
    $page = $this->bare('UserEditPage', 'user-edit.php');
    $db = M::mock(\Fossology\Lib\Db\DbManager::class);
    $db->shouldReceive('getSingleRow')->andReturn(
      ['user_pk' => 9, 'user_email' => 'a@b.c']);
    Reflectory::setObjectsProperty($page, 'dbManager', $db);
    $this->assertStringContainsString('Username is not unique',
      $page->UpdateUser($this->userRec('bob'), false));
  }

  /** @test Tag lookups bind the name as data */
  public function testFindTagBindsName()
  {
    $name = "o'brien";
    $this->bare('ui_tag', 'ui-tags.php');
    $db = M::mock(\Fossology\Lib\Db\DbManager::class);
    $db->shouldReceive('getSingleRow')->once()->with(
      M::on(fn ($sql) => strpos($sql, $name) === false), [$name], M::any())
      ->andReturn(['tag_pk' => 4]);
    $GLOBALS['container'] = new class($db) {
      private $db;
      public function __construct($db)
      {
        $this->db = $db;
      }
      public function get($name)
      {
        return $this->db;
      }
    };
    $page = (new \ReflectionClass('ui_tag'))->newInstanceWithoutConstructor();
    $this->assertEquals(['tag_pk' => 4],
      Reflectory::invokeObjectsMethodnameWith($page, 'findTag', [$name]));
  }

}

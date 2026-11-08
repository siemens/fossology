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

  /** @test Switching to a group of the user works */
  public function testSelectMemberGroupAllowed()
  {
    $_SESSION = [Auth::USER_ID => 2, Auth::GROUP_ID => 3];
    $auth = $this->bare('core_auth', 'core-auth.php');
    $userDao = M::mock();
    $userDao->shouldReceive('getUserGroupMap')->with(2)->andReturn([3 => 'a', 4 => 'b']);
    $userDao->shouldReceive('setDefaultGroupMembership')->with(2, 4)->once();
    $session = M::mock();
    $session->shouldReceive('set')->with(Auth::GROUP_ID, 4)->once();
    Reflectory::setObjectsProperty($auth, 'userDao', $userDao);
    Reflectory::setObjectsProperty($auth, 'session', $session);
    Reflectory::invokeObjectsMethodnameWith($auth, 'selectMemberGroup', [4]);
    $this->assertEquals(4, $_SESSION[Auth::GROUP_ID]);
  }

  /** @test Switching to a foreign group changes nothing */
  public function testSelectMemberGroupRefused()
  {
    $_SESSION = [Auth::USER_ID => 2, Auth::GROUP_ID => 3];
    $auth = $this->bare('core_auth', 'core-auth.php');
    $userDao = M::mock();
    $userDao->shouldReceive('getUserGroupMap')->with(2)->andReturn([3 => 'a']);
    $userDao->shouldNotReceive('setDefaultGroupMembership');
    $session = M::mock();
    $session->shouldNotReceive('set');
    Reflectory::setObjectsProperty($auth, 'userDao', $userDao);
    Reflectory::setObjectsProperty($auth, 'session', $session);
    Reflectory::invokeObjectsMethodnameWith($auth, 'selectMemberGroup', [99]);
    $this->assertEquals(3, $_SESSION[Auth::GROUP_ID]);
  }

  /** @test A reader can not change public access */
  public function testPublicPermissionNeedsWriteAccess()
  {
    list($page, $dao) = $this->permissionPage(false);
    $dao->shouldNotReceive('setPublicPermission');
    $this->expectException(\Exception::class);
    $page->editPermissionsForUpload(true, 5, 11, 0, 0, 0, 0, 0, 1);
  }

  /** @test A permission row of another upload is refused */
  public function testForeignPermissionRowRefused()
  {
    list($page, $dao) = $this->permissionPage(true,
      [3 => ['perm_upload_pk' => 70]]);
    $dao->shouldNotReceive('updatePermissionId');
    $this->expectException(\Exception::class);
    $page->editPermissionsForUpload(true, 5, 11, 0, 71, 10, 0, 0, -1);
  }

  /** @test An editor can change a row of the upload */
  public function testOwnPermissionRowUpdated()
  {
    list($page, $dao) = $this->permissionPage(true,
      [3 => ['perm_upload_pk' => 70]]);
    $dao->shouldReceive('updatePermissionId')->with(70, 10)->once();
    $page->editPermissionsForUpload(true, 5, 11, 0, 70, 10, 0, 0, -1);
  }

  /** @test Viewing the page without changes needs no write access */
  public function testViewNeedsNoWriteAccess()
  {
    list($page) = $this->permissionPage(false);
    $res = $page->editPermissionsForUpload(true, 5, 11, 0, 0, 0, 0, 0, -1);
    $this->assertEquals(11, $res['upload_pk']);
  }

  /** @test A wrong password fails on an account without a seed */
  public function testWrongPasswordRefusedWithoutSeed()
  {
    $_SESSION = [];
    $auth = $this->bare('core_auth', 'core-auth.php');
    $userDao = M::mock();
    $userDao->shouldReceive('getUserAndDefaultGroupByUserName')->andReturn([
      'user_name' => 'bob', 'user_pk' => 2, 'user_seed' => '',
      'user_pass' => password_hash('right', PASSWORD_DEFAULT)]);
    $userDao->shouldNotReceive('isUserActive');
    Reflectory::setObjectsProperty($auth, 'userDao', $userDao);
    Reflectory::setObjectsProperty($auth, 'authExternal', false);
    $this->assertFalse($auth->checkUsernameAndPassword('bob', 'wrong'));
  }

  /** @test Clearing writes follow the upload that holds the item */
  public function testClearingItemNeedsWriteAccess()
  {
    $view = $this->bare('AjaxClearingView', 'ajax-clearing-view.php');
    $dao = M::mock(\Fossology\Lib\Dao\UploadDao::class);
    $dao->shouldReceive('getUploadEntry')->with(7)->andReturn(['upload_fk' => 11]);
    $dao->shouldReceive('getUploadEntry')->with(8)->andReturn(false);
    $dao->shouldReceive('isEditable')->with(11, 3)->andReturn(true);
    $dao->shouldReceive('isEditable')->with(11, 4)->andReturn(false);
    Reflectory::setObjectsProperty($view, 'uploadDao', $dao);
    $call = fn ($item, $group) => Reflectory::invokeObjectsMethodnameWith($view,
      'isItemEditable', [$item, $group]);
    $this->assertTrue($call(7, 3));
    $this->assertFalse($call(7, 4));
    $this->assertFalse($call(8, 3));
  }

  /**
   * @param int|null $group Group that owns the membership row
   * @param int $members Members of that group
   * @param bool $changed Whether the permission may change
   * @return string Text returned by the page
   */
  private function updateMember($group, $members, $changed)
  {
    $page = $this->bare('Fossology\UI\Page\AdminGroupUsers', 'page/AdminGroupUsers.php');
    $userDao = M::mock();
    $userDao->shouldReceive('getGroupOfMembership')->with(70)->andReturn($group);
    $userDao->shouldReceive('getGroupMembers')->with($group)
      ->andReturn(array_fill(0, $members, ['user_pk' => 1, 'group_perm' => 0]));
    if ($changed) {
      $userDao->shouldReceive('updateGUMPermission')->with(70, -1)->once();
    } else {
      $userDao->shouldNotReceive('updateGUMPermission');
    }
    return Reflectory::invokeObjectsMethodnameWith($page, 'updateMember',
      [$userDao, 70, -1, [3 => 'managed']]);
  }

  /** @test A member of a managed group can be changed */
  public function testGroupMemberChangedInManagedGroup()
  {
    $this->assertSame('', $this->updateMember(3, 2, true));
  }

  /** @test A membership in another group is refused */
  public function testGroupMemberRefusedInForeignGroup()
  {
    $this->assertStringContainsString('no permission',
      $this->updateMember(9, 2, false));
  }

  /** @test An unknown membership row is refused */
  public function testGroupMemberRefusedWhenUnknown()
  {
    $this->assertStringContainsString('no permission',
      $this->updateMember(null, 0, false));
  }

  /** @test The last member of a group stays */
  public function testLastGroupMemberKept()
  {
    $this->assertStringContainsString('atleast one user',
      $this->updateMember(3, 1, false));
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

  /** @test Content for the job page is set by code, not by the URL */
  public function testShowJobsIgnoresRequestContent()
  {
    $source = file_get_contents(__DIR__ . '/../../ui/showjobs.php');
    $this->assertStringNotContainsString('GetParm("injectedMessage"', $source);
    $this->assertStringNotContainsString('GetParm("injectedFoot"', $source);
  }

  /**
   * @param string $method
   * @return int Status returned for an addLicense request
   */
  private function clearingWriteStatus($method)
  {
    require_once(dirname(dirname(dirname(__DIR__))) . '/lib/php/common-parm.php');
    $view = $this->bare('AjaxClearingView', 'ajax-clearing-view.php');
    $dao = M::mock(\Fossology\Lib\Dao\UploadDao::class);
    $dao->shouldReceive('getUploadEntry')->andReturn(['upload_fk' => 11]);
    $dao->shouldReceive('isEditable')->andReturn(false);
    Reflectory::setObjectsProperty($view, 'uploadDao', $dao);
    $_SERVER['REQUEST_METHOD'] = $method;
    $_GET = ['do' => 'addLicense', 'upload' => 11, 'item' => 7, 'licenseId' => 3];
    return $view->Output()->getStatusCode();
  }

  /** @test Clearing changes are refused over GET */
  public function testClearingWriteNeedsPost()
  {
    $this->assertEquals(405, $this->clearingWriteStatus('GET'));
  }

  /** @test A POST reaches the permission check */
  public function testClearingWritePostReachesPermissionCheck()
  {
    $this->assertEquals(403, $this->clearingWriteStatus('POST'));
  }

  /** @test Permission changes are read only from a POST */
  public function testPermissionChangesNeedPost()
  {
    $page = $this->bare('UploadPermissionPage', 'page/UploadPermissionPage.php');
    $query = ['permupk' => 70, 'perm' => 10, 'newgroup' => 4, 'newperm' => 3,
      'public' => 1];
    $read = fn ($method) => Reflectory::invokeObjectsMethodnameWith($page,
      'readChanges', [Request::create('/?mod=upload_permissions&' .
        http_build_query($query), $method)]);
    $this->assertEquals([0, 0, 0, 0, -1], $read('GET'));
    $this->assertEquals([70, 10, 4, 3, 1], $read('POST'));
  }

  /** @test Membership changes are read only from a POST */
  public function testGroupMemberChangesNeedPost()
  {
    $page = $this->bare('Fossology\UI\Page\AdminGroupUsers', 'page/AdminGroupUsers.php');
    $read = fn ($method) => Reflectory::invokeObjectsMethodnameWith($page,
      'readChanges', [Request::create(
        '/?mod=group_manage_users&gum_pk=70&perm=1&newuser=5&newperm=2', $method)]);
    $this->assertEquals([0, 0], $read('GET'));
    $this->assertEquals([70, 5], $read('POST'));
  }
}

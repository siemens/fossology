<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/

namespace Fossology\Lib\Dao;

use Fossology\Lib\Test\TestPgDb;
use Monolog\Logger;

/**
 * @class UserDaoGroupMemberTest
 * @brief UserDao::updateGUMPermission() needs RETURNING, so it runs on PostgreSQL
 */
class UserDaoGroupMemberTest extends \PHPUnit\Framework\TestCase
{
  const GUM_PK = 900;
  const GROUP_ID = 2;
  const OTHER_GROUP_ID = 1;
  const USER_ID = 1;

  /** @var TestPgDb */
  private $testDb;
  /** @var UserDao */
  private $userDao;
  /** @var int */
  private $assertCountBefore;

  protected function setUp() : void
  {
    $this->testDb = new TestPgDb();
    $this->testDb->createPlainTables(array('users','groups','group_user_member'));
    $this->testDb->insertData(array('users','groups','group_user_member'));
    $dbManager = $this->testDb->getDbManager();
    $dbManager->getSingleRow("DELETE FROM group_user_member WHERE user_fk=$1",
      array(self::USER_ID));
    $dbManager->insertTableRow('group_user_member', array(
      'group_user_member_pk' => self::GUM_PK, 'group_fk' => self::GROUP_ID,
      'user_fk' => self::USER_ID, 'group_perm' => UserDao::USER));
    $this->userDao = new UserDao($dbManager, new Logger("test"));
    $this->assertCountBefore = \Hamcrest\MatcherAssert::getCount();
  }

  protected function tearDown() : void
  {
    $this->addToAssertionCount(\Hamcrest\MatcherAssert::getCount() - $this->assertCountBefore);
    $this->testDb = null;
  }

  private function memberPerm()
  {
    $members = $this->userDao->getGroupMembers(self::GROUP_ID);
    return intval(array_column($members, 'group_perm', 'user_pk')[self::USER_ID]);
  }

  public function testChangesPermission()
  {
    $this->userDao->updateGUMPermission(self::GUM_PK, UserDao::ADVISOR);
    assertThat($this->memberPerm(), is(UserDao::ADVISOR));
  }

  public function testIgnoresUnknownPermission()
  {
    $this->userDao->updateGUMPermission(self::GUM_PK, 7);
    assertThat($this->memberPerm(), is(UserDao::USER));
  }

  public function testOtherMembersKeepTheirPermission()
  {
    $this->testDb->getDbManager()->insertTableRow('group_user_member', array(
      'group_user_member_pk' => 901, 'group_fk' => self::OTHER_GROUP_ID,
      'user_fk' => self::USER_ID, 'group_perm' => UserDao::USER));
    $this->userDao->updateGUMPermission(self::GUM_PK, UserDao::ADVISOR);
    $members = $this->userDao->getGroupMembers(self::OTHER_GROUP_ID);
    assertThat(intval(array_column($members, 'group_perm', 'user_pk')[self::USER_ID]),
      is(UserDao::USER));
  }

  public function testRemovesMember()
  {
    $this->userDao->updateGUMPermission(self::GUM_PK, -1);
    assertThat($this->userDao->getGroupMembershipId(self::GROUP_ID, self::USER_ID),
      is(nullValue()));
  }

  public function testRemovalMovesActiveGroup()
  {
    $this->userDao->addGroupMembership(self::OTHER_GROUP_ID, self::USER_ID, UserDao::USER);
    $this->userDao->setDefaultGroupMembership(self::USER_ID, self::GROUP_ID);
    $this->userDao->updateGUMPermission(self::GUM_PK, -1);
    $user = $this->userDao->getUserByPk(self::USER_ID);
    assertThat(intval($user['group_fk']), is(self::OTHER_GROUP_ID));
  }

  public function testRemovalKeepsOtherActiveGroup()
  {
    $this->userDao->addGroupMembership(self::OTHER_GROUP_ID, self::USER_ID, UserDao::USER);
    $this->userDao->setDefaultGroupMembership(self::USER_ID, self::OTHER_GROUP_ID);
    $this->userDao->updateGUMPermission(self::GUM_PK, -1);
    $user = $this->userDao->getUserByPk(self::USER_ID);
    assertThat(intval($user['group_fk']), is(self::OTHER_GROUP_ID));
  }

  public function testGetGroupOfMembership()
  {
    assertThat($this->userDao->getGroupOfMembership(self::GUM_PK), is(self::GROUP_ID));
    assertThat($this->userDao->getGroupOfMembership(99999), is(nullValue()));
  }
}

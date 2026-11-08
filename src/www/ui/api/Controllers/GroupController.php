<?php
/*
 SPDX-FileCopyrightText: © 2021 Orange
 SPDX-FileCopyrightText: © 2022 Samuel Dushimimana <dushsam100@gmail.com>
 Author: Piotr Pszczola <piotr.pszczola@orange.com>

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * @file
 * @brief Controller for user queries
 */

namespace Fossology\UI\Api\Controllers;

use Fossology\Lib\Auth\Auth;
use Fossology\Lib\Dao\UserDao;
use Fossology\UI\Api\Exceptions\HttpBadRequestException;
use Fossology\UI\Api\Exceptions\HttpConflictException;
use Fossology\UI\Api\Exceptions\HttpErrorException;
use Fossology\UI\Api\Exceptions\HttpForbiddenException;
use Fossology\UI\Api\Exceptions\HttpNotFoundException;
use Fossology\UI\Api\Helper\ResponseHelper;
use Fossology\UI\Api\Models\Group;
use Fossology\UI\Api\Models\Info;
use Fossology\UI\Api\Models\InfoType;
use Fossology\UI\Api\Models\ApiVersion;
use Fossology\UI\Api\Models\UserGroupMember;
use Psr\Http\Message\ServerRequestInterface;

/**
 * @class GroupController
 * @brief Controller for Group model
 */
class GroupController extends RestController
{

   /**
   * Get list of Groups
   *
   * @param ServerRequestInterface $request
   * @param ResponseHelper $response
   * @param array $args
   * @return ResponseHelper
   */
  public function getGroups($request, $response, $args)
  {
    $userDao = $this->restHelper->getUserDao();
    $groups = array();
    if (Auth::isAdmin()) {
      $groups = $userDao->getAdminGroupMap($this->restHelper->getUserId(),Auth::PERM_ADMIN);
    } else {
      $groups = $userDao->getUserGroupMap($this->restHelper->getUserId());
    }
    $groupList = array();
    foreach ($groups as $key => $value) {
      $groupObject = new Group($key,$value);
      $groupList[] = $groupObject->getArray();
    }
    return $response->withJson($groupList, 200);
  }

  /**
   * Create a given group
   *
   * @param ServerRequestInterface $request
   * @param ResponseHelper $response
   * @param array $args
   * @return ResponseHelper
   * @throws HttpErrorException
   */
  public function createGroup($request, $response, $args)
  {
    $apiVersion = ApiVersion::getVersion($request);
    $groupName = '';
    if ($apiVersion == ApiVersion::V2) {
      $queryParams = $request->getQueryParams();
      $groupName = $queryParams['name'] ?? '';
    } else {
      $groupName = $request->getHeaderLine('name') ?: '';
    }
    if (empty($groupName)) {
      throw new HttpBadRequestException("ERROR - no group name provided");
    }
    $userDao = $this->restHelper->getUserDao();
    $groupId = $userDao->addGroup($groupName);
    $userDao->addGroupMembership($groupId, $this->restHelper->getUserId());
    $statusCode = $apiVersion == ApiVersion::V2 ? 201 : 200;
    $returnVal = new Info($statusCode, "Group $groupName added.", InfoType::INFO);
    return $response->withJson($returnVal->getArray(), $returnVal->getCode());
  }

  /**
   * Delete a given group
   *
   * @param ServerRequestInterface $request
   * @param ResponseHelper $response
   * @param array $args
   * @return ResponseHelper
   * @throws HttpErrorException
   */
  public function deleteGroup($request, $response, $args)
  {
    $apiVersion = ApiVersion::getVersion($request);
    if (empty($args['pathParam'])) {
      throw new HttpBadRequestException("ERROR - No group name or id provided");
    }
    $userId = $this->restHelper->getUserId();

    /** @var UserDao $userDao */
    $userDao = $this->restHelper->getUserDao();
    $groupMap = $userDao->getDeletableAdminGroupMap($userId,
      $_SESSION[Auth::USER_LEVEL]);
    $groupId = null;
    if ($apiVersion == ApiVersion::V2) {
      $groupName = $args['pathParam'];
      $groupId = intval($userDao->getGroupIdByName($groupName));
    } else {
      $groupId = intval($args['pathParam']);
    }

    if (!$this->dbHelper->doesIdExist("groups", "group_pk", $groupId)) {
      throw new HttpNotFoundException("Group id not found!");
    }
    try {
      $userDao->deleteGroup($groupId);
      $returnVal = new Info(202, "User Group will be deleted", InfoType::INFO);
      unset($groupMap[$groupId]);
    } catch (\Exception $e) {
      throw new HttpBadRequestException($e->getMessage(), $e);
    }
    return $response->withJson($returnVal->getArray(), $returnVal->getCode());
  }

  /**
   * Rename a given group
   *
   * @param ServerRequestInterface $request
   * @param ResponseHelper $response
   * @param array $args
   * @return ResponseHelper
   * @throws HttpErrorException
   */
  public function updateGroup($request, $response, $args)
  {
    $apiVersion = ApiVersion::getVersion($request);
    if (empty($args['pathParam'])) {
      throw new HttpBadRequestException("ERROR - No group name or id provided");
    }
    $newGroupName = trim($this->getParsedBody($request)['name'] ?? '');
    if (empty($newGroupName)) {
      throw new HttpBadRequestException("ERROR - no group name provided");
    }
    /** @var \Fossology\UI\Page\AdminGroupEdit $adminGroupEdit */
    $adminGroupEdit = $this->restHelper->getPlugin('group_edit');
    $validationError = $adminGroupEdit->validateGroupName($newGroupName);
    if (!empty($validationError)) {
      throw new HttpBadRequestException($validationError);
    }

    /** @var UserDao $userDao */
    $userDao = $this->restHelper->getUserDao();
    $groupId = null;
    if ($apiVersion == ApiVersion::V2) {
      $groupId = intval($userDao->getGroupIdByName($args['pathParam']));
    } else {
      $groupId = intval($args['pathParam']);
    }

    if (!$this->dbHelper->doesIdExist("groups", "group_pk", $groupId)) {
      throw new HttpNotFoundException("Group id not found!");
    }
    $groupMap = $userDao->getDeletableAdminGroupMap($this->restHelper->getUserId(),
      $_SESSION[Auth::USER_LEVEL]);
    if (!array_key_exists($groupId, $groupMap)) {
      throw new HttpForbiddenException("Not admin of the group. " .
        "Can not process request.");
    }
    try {
      $userDao->editGroup($groupId, $newGroupName);
    } catch (\Exception $e) {
      throw new HttpConflictException($e->getMessage(), $e);
    }
    $returnVal = new Info(200, "Group $newGroupName updated.", InfoType::INFO);
    return $response->withJson($returnVal->getArray(), $returnVal->getCode());
  }

  /**
   * Delete a given group member
   *
   * @param ServerRequestInterface $request
   * @param ResponseHelper $response
   * @param array $args
   * @return ResponseHelper
   * @throws HttpErrorException
   */
  public function deleteGroupMember($request, $response, $args)
  {
    $apiVersion = ApiVersion::getVersion($request);

    $user_pk = null;
    $group_pk = null;
    if ($apiVersion == ApiVersion::V2) {
      $user = $this->restHelper->getUserDao()->getUserByName($args['userPathParam']);
      if ($user === null) {
        throw new HttpNotFoundException("User not found");
      }
      $user_pk = intval($user['user_pk']);
      $group_pk = intval($this->restHelper->getUserDao()->getGroupIdByName($args['pathParam']));
    } else {
      $user_pk = intval($args['userPathParam']);
      $group_pk = intval($args['pathParam']);
    }

    if (!$this->dbHelper->doesIdExist("groups", "group_pk", $group_pk)) {
      throw new HttpNotFoundException("Group id not found!");
    }
    if (!$this->dbHelper->doesIdExist("users", "user_pk", $user_pk)) {
      throw new HttpNotFoundException("User id not found!");
    }
    $this->throwNotGroupManager($group_pk);
    $group_user_member_pk = $this->restHelper->getUserDao()
      ->getGroupMembershipId($group_pk, $user_pk);
    if ($group_user_member_pk === null) {
      throw new HttpNotFoundException("Not a member!");
    }
    $this->restHelper->getUserDao()->updateGUMPermission($group_user_member_pk, -1);
    $returnVal = new Info(202, "User will be removed from group.", InfoType::INFO);
    return $response->withJson($returnVal->getArray(), $returnVal->getCode());
  }

  /**
   * Get a list of groups that can be deleted
   *
   * @param ServerRequestInterface $request
   * @param ResponseHelper $response
   * @param array $args
   * @return ResponseHelper
   */
  public function getDeletableGroups($request, $response, $args)
  {
    $userId = $this->restHelper->getUserId();
    /* @var $userDao UserDao */
    $userDao = $this->restHelper->getUserDao();
    $groupMap = $userDao->getDeletableAdminGroupMap($userId,
      $_SESSION[Auth::USER_LEVEL]);

    $groupList = array();
    foreach ($groupMap as $key => $value) {
      $groupObject = new Group($key, $value);
      $groupList[] = $groupObject->getArray();
    }
    return $response->withJson($groupList, 200);
  }

  /**
   * Get users with their roles from a given group
   *
   * @param ServerRequestInterface $request
   * @param ResponseHelper $response
   * @param array $args
   * @return ResponseHelper
   * @throws HttpErrorException
   */
  public function getGroupMembers($request, $response, $args)
  {
    $apiVersion = ApiVersion::getVersion($request);
    $userDao = $this->restHelper->getUserDao();

    // Get the group name/id form the params and then the group Id
    $groupId = $apiVersion == ApiVersion::V2 ? intval($userDao->getGroupIdByName($args['pathParam'])) : intval($args['pathParam']);

    if (!$this->dbHelper->doesIdExist("groups", "group_pk", $groupId)) {
      throw new HttpNotFoundException("Group id not found!");
    }
    $this->throwNotGroupManager($groupId);

    $memberList = array();
    foreach ($userDao->getGroupMembers($groupId) as $record) {
      $user = $this->dbHelper->getUsers($record['user_pk']);
      $userGroupMember = new UserGroupMember($user[0],$record["group_perm"]);
      $memberList[] = $userGroupMember->getArray(ApiVersion::getVersion($request));
    }

    return $response->withJson($memberList, 200);
  }


  /**
   * Add a user to a group
   *
   * @param ServerRequestInterface $request
   * @param ResponseHelper $response
   * @param array $args
   * @return ResponseHelper
   * @throws HttpErrorException
   */
  public function addMember($request, $response, $args)
  {
    $apiVersion = ApiVersion::getVersion($request);

    $body = $this->getParsedBody($request);
    $newuser = null;
    $group_pk = null;
    if ($apiVersion == ApiVersion::V2) {
      $user = $this->restHelper->getUserDao()->getUserByName($args['userPathParam']);
      if ($user === null) {
        throw new HttpNotFoundException("User not found");
      }
      $newuser = intval($user['user_pk']);
      $group_pk = intval($this->restHelper->getUserDao()->getGroupIdByName($args['pathParam']));
    } else {
      $group_pk = intval($args['pathParam']);
      $newuser = intval($args['userPathParam']);
    }
    $newperm = intval($body['perm']);

    if (!isset($newperm)) {
      throw new HttpBadRequestException("ERROR - no default permission provided");
    }
    if (!$this->dbHelper->doesIdExist("groups", "group_pk", $group_pk)) {
      throw new HttpNotFoundException("Group id not found!");
    }
    if (!$this->dbHelper->doesIdExist("users", "user_pk", $newuser)) {
      throw new HttpNotFoundException("User id not found!");
    }
    if ($newperm < 0 || $newperm > 2) {
      throw new HttpBadRequestException("ERROR - Permission should be in range [0-2]");
    }
    $this->throwNotGroupManager($group_pk);
    $userDao = $this->restHelper->getUserDao();
    // Do not produce duplicate
    if ($userDao->getGroupMembershipId($group_pk, $newuser) !== null) {
      throw new HttpBadRequestException("Already a member!");
    }
    $userDao->addGroupMembership($group_pk, $newuser, $newperm);

    $returnVal = new Info(201, "User added to group.", InfoType::INFO);
    return $response->withJson($returnVal->getArray(), $returnVal->getCode());
  }

  /**
   * Change user permission
   *
   * @param ServerRequestInterface $request
   * @param ResponseHelper $response
   * @param array $args
   * @return ResponseHelper
   * @throws HttpErrorException
   */
  public function changeUserPermission($request, $response, $args)
  {
    // Extract all  prerequisites (user_pk , new_permission , group_pk ) for this functionality
    $apiVersion = ApiVersion::getVersion($request);

    $user_pk = null;
    $group_pk = null;
    if ($apiVersion == ApiVersion::V2) {
      $user = $this->restHelper->getUserDao()->getUserByName($args['userPathParam']);
      if ($user === null) {
        throw new HttpNotFoundException("User not found");
      }
      $user_pk = intval($user['user_pk']);
      $group_pk = intval($this->restHelper->getUserDao()->getGroupIdByName($args['pathParam']));
    } else {
      $user_pk = intval($args['userPathParam']);
      $group_pk = intval($args['pathParam']);
    }

    $newperm = intval($this->getParsedBody($request)['perm']);
    // Validate arguments

    if (!isset($newperm)) {
      throw new HttpBadRequestException("Permission should be provided");
    }
    if (!$this->dbHelper->doesIdExist("groups", "group_pk", $group_pk)) {
      throw new HttpNotFoundException("Group id not found!");
    }
    if (!$this->dbHelper->doesIdExist("users", "user_pk", $user_pk)) {
      throw new HttpNotFoundException("User id not found!");
    }
    if ($newperm < 0) {
      throw new HttpBadRequestException("Permission can not be negative");
    }
    if ($newperm > 2) {
      throw new HttpBadRequestException("Permission can not be greater than 2");
    }
    $this->throwNotGroupManager($group_pk);

    // Check if the relation already exists, retrieve the PK.
    // IF not, return 404 error
    $group_user_member_pk = $this->restHelper->getUserDao()
      ->getGroupMembershipId($group_pk, $user_pk);
    if ($group_user_member_pk === null) {
      throw new HttpNotFoundException("User not part of the group");
    }
    $this->restHelper->getUserDao()->updateGUMPermission($group_user_member_pk, $newperm);
    $info = new Info(202, "Permission updated successfully.", InfoType::INFO);
    return $response->withJson($info->getArray(), $info->getCode());
  }

  /**
   * Refuse callers who neither administer the system nor manage this group.
   *
   * @param int $groupId
   * @throws HttpForbiddenException
   */
  private function throwNotGroupManager($groupId): void
  {
    if (! Auth::isAdmin() && ! $this->restHelper->getUserDao()->isAdvisorOrAdmin(
        $this->restHelper->getUserId(), $groupId)) {
      throw new HttpForbiddenException("Not advisor or admin of the group. " .
        "Can not process request.");
    }
  }
}

<?php
/*
 SPDX-FileCopyrightText: © 2014-2015, 2018 Siemens AG
 Author: Steffen Weber
 SPDX-FileCopyrightText: © 2021-2022 Orange
 Contributors: Piotr Pszczola, Bartlomiej Drozdz

 SPDX-License-Identifier: GPL-2.0-only
*/

namespace Fossology\UI\Page;

use Fossology\Lib\Auth\Auth;
use Fossology\Lib\Dao\UserDao;
use Fossology\Lib\Plugin\DefaultPlugin;
use Symfony\Component\HttpFoundation\Request;
use Symfony\Component\HttpFoundation\Response;

/**
 * \class group_manage extends FO_Plugin
 * \brief edit group user permissions
 */
class AdminGroupUsers extends DefaultPlugin
{
  var $groupPermissions = array(-1 => "None", UserDao::USER => "User",
    UserDao::ADMIN => "Admin", UserDao::ADVISOR => "Advisor");
  const NAME = 'group_manage_users';

  function __construct()
  {
    parent::__construct(self::NAME, array(
        self::TITLE => _("Manage Group Users"),
        self::MENU_LIST => "Admin::Groups::Manage Group Users",
        self::PERMISSION => Auth::PERM_WRITE,
        self::REQUIRES_LOGIN => TRUE
    ));
  }

  /**
   * @param Request $request
   * @return Response
   */
  protected function handle(Request $request)
  {
    $userId = Auth::getUserId();
    /** @var UserDao */
    $userDao = $this->getObject('dao.user');
    $groupMap = $userDao->getAdminGroupMap($userId, $_SESSION[Auth::USER_LEVEL]);
    if (empty($groupMap)) {
      $text = _("You have no permission to manage any group.");
      return $this->render('include/base.html.twig', $this->mergeWithDefault(array('message' => $text)));
    }
    /** @var DbManager */
    $dbManager = $this->getObject('db.manager');
    $group_pk = intval($request->get('group'));
    if (empty($group_pk) || !array_key_exists($group_pk, $groupMap)) {
      $group_pk = key($groupMap);
    }

    list($gum_pk, $newuser) = $this->readChanges($request);
    $text = "";
    if ($gum_pk) {
      $text = $this->updateMember($userDao, $gum_pk,
        intval($request->get('perm')), $groupMap);
      $groupMap = $userDao->getAdminGroupMap($userId,
        $_SESSION[Auth::USER_LEVEL]);
    }

    $newperm = intval($request->get('newperm'));

    if ($newuser && $group_pk) {
      // do not produce duplicate
      $dbManager->prepare($stmt = __METHOD__ . ".delByGroupAndUser",
        "delete from group_user_member where group_fk=$1 and user_fk=$2");
      $dbManager->freeResult(
        $dbManager->execute($stmt, array($group_pk, $newuser)));
      if ($newperm >= 0) {
        $dbManager->prepare($stmt = __METHOD__ . ".insertGUP",
          "insert into group_user_member (group_fk, user_fk, group_perm) values ($1,$2,$3)");
        $dbManager->freeResult(
          $dbManager->execute($stmt, array($group_pk, $newuser, $newperm)));
      }
      if ($newuser == $userId) {
        $groupMap = $userDao->getAdminGroupMap($userId, $_SESSION[Auth::USER_LEVEL]);
      }
      $newperm = $newuser = 0;
    }

    natcasesort($groupMap);
    $baseUrl = Traceback_uri() . "?mod=" . $this->getName() . '&group=';
    $onchange = "onchange=\"js_url(this.value, '$baseUrl')\"";
    $baseUrl .= $group_pk;
    $vars = array('groupMap' => $groupMap,
        'groupId' => $group_pk,
        'permissionMap' => $this->groupPermissions,
        'baseUrl' => $baseUrl,
        'groupMapAction' => $onchange);

    $stmt = __METHOD__ . "getUsersWithGroup";
    $dbManager->prepare($stmt, "select user_pk, user_name, user_status, user_desc, group_user_member_pk, group_perm
         FROM users LEFT JOIN group_user_member gum ON gum.user_fk=users.user_pk AND gum.group_fk=$1 
         ORDER BY user_name");
    $result = $dbManager->execute($stmt, array($group_pk));
    $vars['usersWithGroup'] = $dbManager->fetchAll($result);
    $dbManager->freeResult($result);

    $otherUsers = array('0' => '');
    foreach ($vars['usersWithGroup'] as $row) {
      if ($row['group_user_member_pk'] || $row['user_status']!='active') {
        continue;
      }
      $otherUsers[$row['user_pk']] = !empty($row['user_desc']) ? $row['user_desc']. ' ('. $row['user_name'] .')' : $row['user_name'];
    }

    $vars['existsOtherUsers'] = count($otherUsers) - 1;
    if ($vars['existsOtherUsers']) {
      $vars['newPermissionMap'] = $this->groupPermissions;
      unset($vars['newPermissionMap'][-1]);
      $script = "var newpermurl;
      function setNewPermUrl(newperm){
         newpermurl='" . $baseUrl . "&newperm='+newperm+'&newuser=';
      }
      setNewPermUrl($newperm);";
      $scripts = js_url() . $this->postUrlScript() .
        '<script type="text/javascript"> ' . $script . '</script>';
      $vars['otherUsers'] = $otherUsers;
    } else {
      $scripts = js_url() . $this->postUrlScript();
    }

    $vars['scripts'] = $scripts;
    if (!empty($text)) {
      $vars['message'] .= $text;
    }
    return $this->render('admin_group_users.html.twig', $this->mergeWithDefault($vars));
  }

  /**
   * Membership changes requested by the page. A GET only views, so a link can
   * not change memberships.
   *
   * @param Request $request
   * @return int[] group_user_member_pk to change and user to add, 0 if none
   */
  private function readChanges(Request $request)
  {
    if (!$request->isMethod('POST')) {
      return [0, 0];
    }
    return [intval($request->get('gum_pk')), intval($request->get('newuser'))];
  }

  /**
   * @return string Script defining post_url(val, url), which submits url+val by POST
   */
  private function postUrlScript()
  {
    return '<script type="text/javascript">
      function post_url(val, url) {
        $(\'<form method="post"></form>\').attr("action", url + val).appendTo("body").submit();
      }
    </script>';
  }

  /**
   * Change a membership in one of the groups the caller manages.
   *
   * @param UserDao $userDao
   * @param int $gumPk group_user_member_pk
   * @param int $perm New permission, -1 to remove the member
   * @param array $groupMap Groups the caller manages
   * @return string Error text, empty on success
   */
  private function updateMember($userDao, $gumPk, $perm, $groupMap)
  {
    $groupId = $userDao->getGroupOfMembership($gumPk);
    if ($groupId === null || !array_key_exists($groupId, $groupMap)) {
      return _("You have no permission to manage this group.");
    }
    if (count($userDao->getGroupMembers($groupId)) <= 1) {
      return _("Error: atleast one user should be part of a group.");
    }
    $userDao->updateGUMPermission($gumPk, $perm);
    return "";
  }
}

register_plugin(new AdminGroupUsers());

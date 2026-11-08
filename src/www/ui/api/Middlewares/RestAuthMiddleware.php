<?php
/*
 SPDX-FileCopyrightText: © 2018-2019 Siemens AG
 Author: Gaurav Mishra <mishra.gaurav@siemens.com>

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * @dir
 * @brief Middlewares for the Slim framework
 * @file
 * @brief Auth middleware for Slim
 */

namespace Fossology\UI\Api\Middlewares;

use Fossology\Lib\Auth\Auth;
use Fossology\UI\Api\Exceptions\HttpBadRequestException;
use Fossology\UI\Api\Exceptions\HttpForbiddenException;
use Fossology\UI\Api\Helper\AuthHelper;
use Fossology\UI\Api\Helper\CorsHelper;
use Fossology\UI\Api\Models\ApiVersion;
use Psr\Http\Message\ResponseInterface;
use Psr\Http\Message\ServerRequestInterface as Request;
use Psr\Http\Server\RequestHandlerInterface as RequestHandler;

/**
 * @class RestAuthMiddleware
 * @brief Authentication middleware for Slim framework
 */
class RestAuthMiddleware
{
  /**
   * The route after /api/vN, without a trailing slash; works under any base
   * path such as /repo/api/v2/tokens or /fossology/repo/api/v1/tokens/
   * @param string $path Request path, lower case
   * @return string
   */
  public static function endpointOf($path)
  {
    if (preg_match('#/api/v\d+(/.*)?$#', $path, $match)) {
      $endpoint = rtrim($match[1] ?? '', '/');
      return $endpoint === '' ? '/' : $endpoint;
    }
    $parts = explode("/", $path, 5);
    return "/" . end($parts);
  }

  /**
   * Check authentication for all calls, except for /auth, /tokens
   *
   * @param Request $request PSR7 request
   * @param RequestHandler $handler PSR-15 request handler
   *
   * @return ResponseInterface
   * @throws HttpForbiddenException If the token does not have required scope
   * @throws HttpBadRequestException If the token is not valid
   */
  public function __invoke(Request $request, RequestHandler $handler) : ResponseInterface
  {
    $requestUri = $request->getUri();
    $requestPath = strtolower($requestUri->getPath());
    $authFreePaths = ["/info", "/openapi", "/health"];

    $isPassThroughPath = false;
    $endpoint = self::endpointOf($requestPath);
    foreach ($authFreePaths as $authFreePath) {
      if ( $endpoint === $authFreePath ) {
        $isPassThroughPath = true;
        break;
      }
    }

    if (stristr($request->getMethod(), "options") !== false) {
      $response = $handler->handle($request);
    } elseif ($isPassThroughPath) {
      $response = $handler->handle($request);
    } elseif ($endpoint === "/tokens" &&
        strcasecmp($request->getMethod(), "post") === 0) {
      $response = $handler->handle($request);
    } else {
      /** @var AuthHelper $authHelper */
      $authHelper = $GLOBALS['container']->get('helper.authHelper');
      $authHeaders = $request->getHeader('Authorization');
      if (!empty($authHeaders)) {
        $jwtToken = $authHeaders[0];
      } else {
        $jwtToken = "";
      }
      $userId = -1;
      $tokenScope = false;
      $authHelper->verifyAuthToken($jwtToken, $userId, $tokenScope);
      $isRead = in_array(strtoupper($request->getMethod()), ['GET', 'HEAD'], true);
      if (!$isRead && stristr($tokenScope, "write") === false) {
        /*
         * If the request method is not GET and token scope is not write,
         * do not allow the request to pass through.
         */
        throw new HttpForbiddenException("Do not have required scope.");
      }
      if (ApiVersion::getVersion($request) == ApiVersion::V2) {
        $queryParameters = $request->getQueryParams();
        $groupName = $queryParameters['groupName'] ?? "";
      } else {
        $groupName = $request->getHeaderLine('groupName');
      }
      if (!empty($groupName)) { // if request contains groupName
        $authHelper->userHasGroupAccess($userId, $groupName);
        $authHelper->updateUserSession($userId, $tokenScope, $groupName);
      } else { // no groupName passed, use default groupId saved in DB
        $authHelper->updateUserSession($userId, $tokenScope);
      }
      if (!$isRead && $_SESSION[Auth::USER_LEVEL] < Auth::PERM_WRITE) {
        throw new HttpForbiddenException("Do not have required access level.");
      }
      $response = $handler->handle($request);
    }
    return CorsHelper::addCorsHeaders($response);
  }
}

<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * @file
 * @brief Unit tests for RestAuthMiddleware
 */

namespace Fossology\UI\Api\Test\Middlewares;

use Fossology\Lib\Auth\Auth;
use Fossology\UI\Api\Exceptions\HttpBadRequestException;
use Fossology\UI\Api\Exceptions\HttpForbiddenException;
use Fossology\UI\Api\Helper\AuthHelper;
use Fossology\UI\Api\Middlewares\RestAuthMiddleware;
use Mockery as M;
use Psr\Http\Server\RequestHandlerInterface;
use Slim\Psr7\Factory\ResponseFactory;
use Slim\Psr7\Factory\StreamFactory;
use Slim\Psr7\Headers;
use Slim\Psr7\Request;
use Slim\Psr7\Uri;

class RestAuthMiddlewareTest extends \PHPUnit\Framework\TestCase
{
  /** @var M\MockInterface $authHelper */
  private $authHelper;

  protected function setUp() : void
  {
    global $container, $SysConf;
    $SysConf['SYSCONFIG']['CorsOrigins'] = '*';
    $this->authHelper = M::mock(AuthHelper::class);
    $container = M::mock('ContainerBuilder');
    $container->shouldReceive('get')->with('helper.authHelper')
      ->andReturn($this->authHelper);
  }

  protected function tearDown() : void
  {
    $this->addToAssertionCount(M::getContainer()->mockery_getExpectationCount());
    unset($_SESSION[Auth::USER_LEVEL]);
    M::close();
  }

  private function request($method, $path, $base = "/repo/api/v1")
  {
    $headers = new Headers();
    $headers->setHeader('Authorization', 'bearer a.b.c');
    return new Request($method, new Uri("HTTP", "localhost", 80,
      "$base$path"), $headers, [], [],
      (new StreamFactory())->createStream());
  }

  private function handler($expectCall)
  {
    $handler = M::mock(RequestHandlerInterface::class);
    $expectation = $handler->shouldReceive('handle');
    if ($expectCall) {
      $expectation->once()->andReturn((new ResponseFactory())->createResponse());
    } else {
      $expectation->never();
    }
    return $handler;
  }

  private function authenticate($scope, $level)
  {
    $this->authHelper->shouldReceive('verifyAuthToken')->andReturnUsing(
      function ($header, &$userId, &$tokenScope) use ($scope) {
        $userId = 2;
        $tokenScope = $scope;
      });
    $this->authHelper->shouldReceive('updateUserSession')->andReturnUsing(
      function () use ($level) {
        $_SESSION[Auth::USER_LEVEL] = $level;
      });
  }

  /**
   * @test
   * -# POST to /tokens is the only unauthenticated write
   */
  public function testTokensIsPublic()
  {
    $this->authHelper->shouldNotReceive('verifyAuthToken');
    (new RestAuthMiddleware())($this->request('POST', '/tokens'),
      $this->handler(true));
  }

  /**
   * @test
   * -# Token creation and the open routes work under any base path and with
   *    a trailing slash
   */
  public function testPublicRoutesUnderOtherBasePaths()
  {
    $this->authHelper->shouldNotReceive('verifyAuthToken');
    foreach ([['POST', '/tokens', '/fossology/repo/api/v2'], ['POST', '/tokens/', '/repo/api/v1'],
        ['GET', '/info', '/fossology/repo/api/v1'], ['GET', '/health/', '/repo/api/v2']] as $case) {
      (new RestAuthMiddleware())($this->request($case[0], $case[1], $case[2]),
        $this->handler(true));
    }
    $this->assertSame('/tokens', RestAuthMiddleware::endpointOf('/a/b/c/repo/api/v12/tokens/'));
    $this->assertSame('/users/tokens', RestAuthMiddleware::endpointOf('/repo/api/v1/users/tokens'));
    $this->assertSame('/', RestAuthMiddleware::endpointOf('/repo/api/v1'));
  }

  /**
   * @test
   * -# Paths that merely contain /tokens must still authenticate
   */
  public function testPathContainingTokensStillAuthenticates()
  {
    foreach (['/groups/tokens/user/foo', '/groups/1/user/tokens', '/users/tokens',
        '/tokens/x'] as $path) {
      $this->authHelper->shouldReceive('verifyAuthToken')->once()
        ->andThrow(new HttpBadRequestException("bad"));
      try {
        (new RestAuthMiddleware())($this->request('POST', $path),
          $this->handler(false));
        $this->fail("$path skipped authentication");
      } catch (HttpBadRequestException $e) {
        $this->assertEquals(400, $e->getCode());
      }
    }
  }

  /**
   * @test
   * -# A read token can not write
   */
  public function testReadTokenCanNotWrite()
  {
    $this->authenticate('read', Auth::PERM_WRITE);
    $this->expectException(HttpForbiddenException::class);
    (new RestAuthMiddleware())($this->request('POST', '/uploads'),
      $this->handler(false));
  }

  /**
   * @test
   * -# HEAD is a read: a read token of a read-only user passes
   */
  public function testHeadIsARead()
  {
    $this->authenticate('read', Auth::PERM_READ);
    $this->authHelper->shouldReceive('userHasGroupAccess');
    (new RestAuthMiddleware())($this->request('HEAD', '/uploads'),
      $this->handler(true));
  }

  /**
   * @test
   * -# A write token held by a read-only user can not write
   */
  public function testReadOnlyUserCanNotWrite()
  {
    $this->authenticate('write', Auth::PERM_READ);
    $this->expectException(HttpForbiddenException::class);
    (new RestAuthMiddleware())($this->request('POST', '/groups'),
      $this->handler(false));
  }

  /**
   * @test
   * -# A write token of a write-level user passes, and reads stay open
   */
  public function testAllowedRequestsPass()
  {
    $this->authenticate('write', Auth::PERM_WRITE);
    (new RestAuthMiddleware())($this->request('POST', '/uploads'),
      $this->handler(true));
    $this->authenticate('read', Auth::PERM_READ);
    (new RestAuthMiddleware())($this->request('GET', '/uploads'),
      $this->handler(true));
  }
}

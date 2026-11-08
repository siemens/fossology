<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * @file
 * @brief Unit tests for SearchController and the upload filter of SearchHelperDao
 */

namespace Fossology\UI\Api\Test\Controllers;

use Fossology\Lib\Dao\SearchHelperDao;
use Fossology\Lib\Dao\UploadDao;
use Fossology\Lib\Db\DbManager;
use Fossology\UI\Api\Controllers\SearchController;
use Fossology\UI\Api\Exceptions\HttpBadRequestException;
use Fossology\UI\Api\Helper\DbHelper;
use Fossology\UI\Api\Helper\ResponseHelper;
use Fossology\UI\Api\Helper\RestHelper;
use Fossology\UI\Api\Models\ApiVersion;
use Mockery as M;
use Slim\Psr7\Factory\StreamFactory;
use Slim\Psr7\Headers;
use Slim\Psr7\Request;
use Slim\Psr7\Uri;

require_once dirname(__DIR__, 4) . '/lib/php/common-parm.php';

class SearchControllerTest extends \PHPUnit\Framework\TestCase
{
  /** @var M\MockInterface $searchHelperDao */
  private $searchHelperDao;
  /** @var SearchController $controller */
  private $controller;

  protected function setUp() : void
  {
    global $container;
    $this->searchHelperDao = M::mock(SearchHelperDao::class);
    $restHelper = M::mock(RestHelper::class);
    $restHelper->shouldReceive('getDbHelper')->andReturn(M::mock(DbHelper::class));
    $restHelper->shouldReceive('getUploadDao')->andReturn(M::mock(UploadDao::class));
    $restHelper->shouldReceive('getGroupId')->andReturn(2);
    $container = M::mock('ContainerBuilder');
    $container->shouldReceive('get')->with('helper.restHelper')->andReturn($restHelper);
    $container->shouldReceive('get')->with('dao.searchhelperdao')
      ->andReturn($this->searchHelperDao);
    $this->controller = new SearchController($container);
  }

  protected function tearDown() : void
  {
    $this->addToAssertionCount(M::getContainer()->mockery_getExpectationCount());
    M::close();
  }

  private function search($uploadId)
  {
    $request = new Request("GET", new Uri("HTTP", "localhost", 80, "/search",
      http_build_query(['filename' => 'a.c', 'uploadId' => $uploadId])),
      new Headers(), [], [], (new StreamFactory())->createStream());
    return $this->controller->performSearch(
      $request->withAttribute(ApiVersion::ATTRIBUTE_NAME, ApiVersion::V2),
      new ResponseHelper(), []);
  }

  /**
   * @test
   * -# Values that are not positive integers
   * -# The request is refused before any query
   */
  public function testInvalidUploadIdRefused()
  {
    $this->searchHelperDao->shouldNotReceive('GetResults');
    foreach (['abc', '1 x', '1.5', '-1', '1e3'] as $uploadId) {
      try {
        $this->search($uploadId);
        $this->fail("uploadId '$uploadId' accepted");
      } catch (HttpBadRequestException $e) {
        $this->assertEquals(400, $e->getCode());
      }
    }
  }

  /**
   * @test
   * -# A valid upload id reaches the search
   */
  public function testValidUploadIdSearches()
  {
    $this->searchHelperDao->shouldReceive('GetResults')->once()
      ->with(M::any(), 'a.c', '7', M::any(), M::any(), M::any(), M::any(),
        M::any(), M::any(), M::any(), M::any(), M::any(), 2)
      ->andReturn([[], 0]);
    $this->assertEquals(200, $this->search('7')->getStatusCode());
  }

  /**
   * @test
   * -# The DAO puts only an integer in the query
   */
  public function testDaoCastsUploadId()
  {
    $db = M::mock(DbManager::class);
    $db->shouldReceive('getSingleRow')->once()->with(
      M::on(fn ($sql) => preg_match('/upload_fk = 7$/', $sql) === 1),
      M::any(), M::any())->andReturn(['count' => 0]);
    $db->shouldReceive('getRows')->once()->with(
      M::on(fn ($sql) => preg_match('/upload_fk = 7 ORDER BY/', $sql) === 1),
      M::any(), M::any())->andReturn([]);
    (new SearchHelperDao($db))->GetResults(0, 'a.c', '7 x', '', 0, 10, '', '',
      'allfiles', '', '', M::mock(UploadDao::class), 2);
  }
}

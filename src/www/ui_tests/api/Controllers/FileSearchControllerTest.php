<?php
/*
 SPDX-FileCopyrightText: © 2026 Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * @file
 * @brief Unit tests for FileSearchController
 */

namespace Fossology\UI\Api\Test\Controllers;

use Fossology\Lib\Dao\UploadDao;
use Fossology\UI\Api\Controllers\FileSearchController;
use Fossology\UI\Api\Helper\DbHelper;
use Fossology\UI\Api\Helper\FileHelper;
use Fossology\UI\Api\Helper\ResponseHelper;
use Fossology\UI\Api\Helper\RestHelper;
use Mockery as M;
use Slim\Psr7\Factory\StreamFactory;
use Slim\Psr7\Headers;
use Slim\Psr7\Request;
use Slim\Psr7\Uri;

class FileSearchControllerTest extends \PHPUnit\Framework\TestCase
{
  const SHA1 = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';

  /** @var M\MockInterface */
  private $fileHelper;
  /** @var M\MockInterface */
  private $uploadDao;
  /** @var FileSearchController */
  private $controller;

  protected function setUp() : void
  {
    $this->fileHelper = M::mock(FileHelper::class);
    $this->uploadDao = M::mock(UploadDao::class);
    $restHelper = M::mock(RestHelper::class);
    $restHelper->shouldReceive('getDbHelper')->andReturn(M::mock(DbHelper::class));
    $restHelper->shouldReceive('getUploadDao')->andReturn($this->uploadDao);
    $restHelper->shouldReceive('getGroupId')->andReturn(2);
    $container = M::mock('ContainerBuilder');
    $container->shouldReceive('get')->with('helper.restHelper')->andReturn($restHelper);
    $container->shouldReceive('get')->with('helper.fileHelper')->andReturn($this->fileHelper);
    $container->shouldReceive('get')->with('dao.clearing')->andReturn(M::mock());
    $container->shouldReceive('get')->with('dao.license')->andReturn(M::mock());
    $this->controller = new FileSearchController($container);

    $this->fileHelper->shouldReceive('getPfile')->andReturn(['pfile_pk' => 5,
      'pfile_sha1' => self::SHA1, 'pfile_md5' => 'stored-md5',
      'pfile_sha256' => 'stored-sha256', 'pfile_size' => 10]);
  }

  protected function tearDown() : void
  {
    $this->addToAssertionCount(M::getContainer()->mockery_getExpectationCount());
    M::close();
  }

  private function search()
  {
    $headers = new Headers();
    $headers->setHeader('Content-Type', 'application/json');
    $request = new Request("POST", new Uri("HTTP", "localhost"), $headers, [], [],
      (new StreamFactory())->createStream(json_encode([['sha1' => self::SHA1]])));
    $response = $this->controller->getFiles($request, new ResponseHelper(), []);
    $response->getBody()->seek(0);
    return json_decode($response->getBody()->getContents(), true)[0];
  }

  /**
   * @test
   * -# The file exists only in uploads the caller can not read
   * -# It is reported as not found, with no stored hash or finding added
   */
  public function testFileInInaccessibleUploadIsNotFound()
  {
    $this->fileHelper->shouldReceive('isInAccessibleUpload')->with(5, 2, false)
      ->once()->andReturn(false);
    $this->fileHelper->shouldNotReceive('pfileScannerFindings');
    $this->fileHelper->shouldNotReceive('pfileCopyright');
    $result = $this->search();
    $this->assertEquals('Not found', $result['message']);
    $this->assertArrayNotHasKey('findings', $result);
    $this->assertNull($result['hash']['md5']);
    $this->assertNull($result['hash']['sha256']);
  }

  /**
   * @test
   * -# The file is in an upload the caller can read
   * -# Its findings are returned
   */
  public function testFileInAccessibleUploadReturnsFindings()
  {
    $this->fileHelper->shouldReceive('isInAccessibleUpload')->with(5, 2, false)
      ->once()->andReturn(true);
    $this->fileHelper->shouldReceive('getPackageUpload')->with(5)->andReturn(null);
    $this->fileHelper->shouldReceive('pfileScannerFindings')->with(5)->once()->andReturn(['MIT']);
    $this->fileHelper->shouldReceive('pfileConclusions')->with(2, 5)->andReturn([]);
    $this->fileHelper->shouldReceive('pfileCopyright')->with(5)->andReturn([]);
    $result = $this->search();
    $this->assertArrayHasKey('findings', $result);
    $this->assertEquals('stored-md5', $result['hash']['md5']);
  }
}

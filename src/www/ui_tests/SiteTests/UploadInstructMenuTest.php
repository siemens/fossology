<?php
/*
 SPDX-FileCopyrightText: © 2008 Hewlett-Packard Development Company, L.P.

 SPDX-License-Identifier: GPL-2.0-only
*/

/**
 * Is the folder edit properties menu available?
 *
 * @version "$Id: UploadInstructMenuTest.php 2472 2009-08-24 19:35:52Z rrando $"
 *
 * Created on Jul 31, 2008
 */
require_once ('../../../tests/fossologyTestCase.php');
require_once ('../../../tests/TestEnvironment.php');

global $URL;

class UploadInstructMenuTest extends fossologyTestCase
{

  function testUploadInstructMenu()
  {
    global $URL;
    print "starting UploadInstrucMenuTest\n";
    $this->Login();
    /* we get the home page to get rid of the user logged in page */
    $loggedIn = $this->mybrowser->get($URL);
    $this->assertTrue($this->myassertText($loggedIn, '/Upload/'));
    $this->assertTrue($this->myassertText($loggedIn, '/Instructions/'));
    $this->assertTrue($this->myassertText($loggedIn, '/From File/'));
    $this->assertTrue($this->myassertText($loggedIn, '/From Server/'));
    $this->assertTrue($this->myassertText($loggedIn, '/From URL/'));
    $this->assertTrue($this->myassertText($loggedIn, '/One-Shot Analysis/'));
    /* ok, this proves the text is on the page, let's see if we can
     * get to the delete page.
     */
    $page = $this->mybrowser->get("$URL?mod=upload_instructions");
    $this->assertTrue($this->myassertText($page, '/Upload Instructions/'));
    $this->assertTrue($this->myassertText($page, '/On your browser system/'));
    $this->assertTrue($this->myassertText($page, '/On the FOSSology web server/'));
  }
}

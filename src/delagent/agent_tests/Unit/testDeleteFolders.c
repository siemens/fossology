/*
 SPDX-FileCopyrightText: © 2011 Hewlett-Packard Development Company, L.P.

 SPDX-License-Identifier: GPL-2.0-only
*/

/* cunit includes */
#include <CUnit/CUnit.h>
#include "delagent.h"
#include <string.h>

extern char *DBConfFile;
static PGresult *result = NULL;

/**
 * \file testDeleteFolders.c
 * \brief testing for the function DeleteFolders and DeleteUploads
 */

/**
 * \brief for function DeleteFolders
 * \test
 * -# Give a folder id to deleteFolder()
 * -# Check for return code
 */
void testDeleteFolders()
{
  long FolderId = 3;
  //char *DBConfFile = NULL;  /* use default Db.conf */
  char *ErrorBuf;
  int rc;

  pgConn = fo_dbconnect(DBConfFile, &ErrorBuf);
  /** exectue the tested function */
  rc = deleteFolder(3, FolderId, 3, 10);

  PQfinish(pgConn);
  CU_ASSERT_EQUAL(rc, 0);
  CU_PASS("DeleteFolders PASS!");
}

/**
 * \brief Deleting a folder through its parent removes the folder row
 * \test
 * -# Delete folder 4 (parent 1) with deleteFolder()
 * -# Check the folder row and its link to the parent are both gone
 */
void testDeleteFolderRemovesRow()
{
  char *ErrorBuf;
  PGresult *result;
  int rc;

  pgConn = fo_dbconnect(DBConfFile, &ErrorBuf);
  rc = deleteFolder(4, 1, 3, 10);
  CU_ASSERT_EQUAL(rc, 0);

  result = PQexec(pgConn, "SELECT 1 FROM folder WHERE folder_pk = 4");
  CU_ASSERT_FALSE(fo_checkPQresult(pgConn, result, "folder", __FILE__, __LINE__));
  CU_ASSERT_EQUAL(PQntuples(result), 0);
  PQclear(result);

  result = PQexec(pgConn, "SELECT 1 FROM foldercontents WHERE foldercontents_mode = 1 AND child_id = 4");
  CU_ASSERT_FALSE(fo_checkPQresult(pgConn, result, "foldercontents", __FILE__, __LINE__));
  CU_ASSERT_EQUAL(PQntuples(result), 0);
  PQclear(result);
  PQfinish(pgConn);
}

/**
 * \brief for function DeleteUploads
 * \test
 * -# Delete an upload using deleteUpload() and check for the return
 * -# Check if the upload is delete from database
 * -# Check if the copyrights for the given upload is also deleted
 */
void testDeleteUploads()
{
  long UploadId = 2;
  //char *DBConfFile = NULL;  /* use default Db.conf */
  char *ErrorBuf;
  char sql[1024];
  int rc;

  pgConn = fo_dbconnect(DBConfFile, &ErrorBuf);
  /** exectue the tested function */
  rc = deleteUpload(UploadId, 3, 10);
  CU_ASSERT_EQUAL(rc, 0);

  /* check if uploadtree records deleted */
  memset(sql, '\0', 1024);
  snprintf(sql, 1024, "SELECT * FROM uploadtree WHERE upload_fk = %ld;", UploadId);
  result = PQexec(pgConn, sql);
  if (fo_checkPQresult(pgConn, result, sql, __FILE__, __LINE__))
  {
    CU_FAIL("DeleteUploads FAIL!");
  }
  else
  {
    CU_ASSERT_EQUAL(PQntuples(result),0);
  }
  PQclear(result);

  /* check if copyright records deleted */
  memset(sql, '\0', 1024);
  snprintf(sql, 1024, "SELECT * FROM copyright C INNER JOIN uploadtree U ON C.pfile_fk = U.pfile_fk AND U.upload_fk = %ld;", UploadId);
  result = PQexec(pgConn, sql);
  if (fo_checkPQresult(pgConn, result, sql, __FILE__, __LINE__))
  {
    CU_FAIL("DeleteUploads FAIL!");
  }
  else
  {
    CU_ASSERT_EQUAL(PQntuples(result),0);
  }
  PQclear(result);

  /** Check false input */
  UploadId = 4;
  rc = deleteUpload(UploadId, 2, 10);
  CU_ASSERT_NOT_EQUAL(rc, 0);

  PQfinish(pgConn);
  CU_PASS("DeleteUploads PASS!");
}

/**
 * \brief testcases for function Delete
 */
CU_TestInfo testcases_DeleteFolders[] =
{
#if 0
#endif
{"Testing the function DeleteFolders:", testDeleteFolders},
{"Testing the function DeleteUploads:", testDeleteUploads},
{"Testing deleteFolder removes the folder row:", testDeleteFolderRemovesRow},
  CU_TEST_INFO_NULL
};


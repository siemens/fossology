<?php
/*
 SPDX-FileCopyrightText: © 2010-2013 Hewlett-Packard Development Company, L.P.

 SPDX-License-Identifier: GPL-2.0-only
*/

/**
 * @file library.php
 * @brief This file contains common functions for the
 * copyright ui plugin.
 */


/**
 * @brief Sort query histogram results (by content), ascend
 * @param array[][] $a
 * @param array[][] $b
 * @return int
 */
function hist_rowcmp_count_asc($a, $b)
{
  return $a['copyright_count'] - $b['copyright_count'];
}

/**
 * \brief Sort query histogram results (by content), descend
 * @param array[][] $a
 * @param array[][] $b
 * @return int
 */
function hist_rowcmp_count_desc($a, $b)
{
  $res = $a['copyright_count'] - $b['copyright_count'];
  return -$res;
}

/**
 * \brief Sort query histogram results (by content), ascend
 * @param array[][] $rowa
 * @param array[][] $rowb
 * @return int
 */
function hist_rowcmp($rowa, $rowb)
{
  return (strnatcasecmp($rowa['content'], $rowb['content']));
}

/**
 * \brief Sort query histogram results (by content), descend
 * @param array[][] $rowa
 * @param array[][] $rowb
 * @return int
 */
function hist_rowcmp_desc($rowa, $rowb)
{
  return -(strnatcasecmp($rowa['content'], $rowb['content']));
}

/**
 * \brief Sort rows by filename
 * @param array[][] $rowa
 * @param array[][] $rowb
 * @return int
 */
function copyright_namecmp($rowa, $rowb)
{
  return (strnatcasecmp($rowa['ufile_name'], $rowb['ufile_name']));
}

/**
 * \brief rearrange copyright statment to try and put the holder first,
 * followed by the rest of the statement.
 * \code
 * Exaple
 * copyright (c) aaron seigo <aseigo kde.org> \n
 * would reorder to \n
 * aaron seigo <aseigo kde.org> | copyright (c) \n
 * this way the output will be better grouped by author. \n
 * \endcode
 * \todo NOT YET IMPLEMENTED
 */
function StmtReorder($content)
{
  return $content;
}


/**
 * \brief Input row array contains: pfile, content and type
 *
 * Output records: massaged content, type, hash \n
 * where content has been simplified from
 * the raw records and hash is the md5 of this
 * new content. \n
 * If $hash non zero, only rows with that hash will
 * be returned.
 * \param array $row
 * \param string $hash
 * \return boolean On empty row, return true, else false
 */
function MassageContent(&$row, $hash)
{
  /* Step 1: Clean up content
   */
  $OriginalContent = $row['content'];

  /* remove control characters */
  $content = preg_replace('/[\x0-\x1f]/', ' ', $OriginalContent);

  if ($row['type'] == 'statement') {
    /* !"#$%&' */
    $content = preg_replace('/([\x21-\x27])|([*@])/', ' ', $content);

    /*  numbers-numbers, two or more digits, ', ' */
    $content = preg_replace('/(([0-9]+)-([0-9]+))|([0-9]{2,})|(,)/', ' ', $content);
    $content = preg_replace('/ : /', ' ', $content);  // free :, probably followed a date
  } elseif ($row['type'] == 'email') {
    $content = str_replace(":;<=>()", " ", $content);
  }

  /* remove double spaces */
  $content = preg_replace('/\s\s+/', ' ', $content);

  /* remove leading/trailing whitespace and some punctuation */
  $content = trim($content, "\t \n\r<>./\"\'");

  /* remove leading "dnl " */
  if ((strlen($content) > 4) &&
  (substr_compare($content, "dnl ", 0, 4, true) == 0)) {
    $content = substr($content, 4);
  }

  /* skip empty content */
  if (empty($content)) {
    return true;
  }

  /* Step 1B: rearrange copyright statments to try and put the holder first,
   * followed by the rest of the statement, less copyright years.
  */
  /* Not yet implemented
   if ($row['type'] == 'statement') $content = $this->StmtReorder($content);
  */

  //  $row['original'] = $OriginalContent;   // to compare original to new content
  $row['content'] = $content;
  $row['copyright_count'] = 1;
  $row['hash'] = md5($row['content']);
  if ($hash && ($row['hash'] != $hash)) {
    return true;
  }

  return false;
}  /* End of MassageContent() */

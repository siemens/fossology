/*
 SPDX-FileCopyrightText: © 2014-2015,2022 Siemens AG
 Author: Johannes Najjar

 SPDX-License-Identifier: GPL-2.0-only
*/

/**
 * \file cleanEntries.cc
 * \brief Clean strings
 * \todo rearrange copyright statments to try and put the holder first,
 * followed by the rest of the statement, less copyright years.
 * \todo skip "dnl "
*/
#include "cleanEntries.hpp"
#include <sstream>
#include <iterator>
using std::stringstream;
using std::ostream_iterator;

/**
 * \brief Trim space at beginning and end
 *
 * Since we already collapsed a sequence of spaces into one space, there can only be one space
 * \param sBegin String begin
 * \param sEnd   String end
 * \return string Trimmed string
 */
icu::UnicodeString cleanGeneral(const UChar* sBegin, const UChar* sEnd)
{
  icu::UnicodeString s = rx::u32regex_replace(
    icu::UnicodeString(sBegin, sEnd - sBegin),
    rx::make_u32regex("[[:space:]\\x0-\\x1f]{2,}"),
    " ");

  return s.trim();
}

/**
 * \brief Truncate SPDX-CopyrightText from copyright statement
 * \param sBegin String begin
 * \param sEnd   String end
 * \return string Clean spdx statements
 */
icu::UnicodeString cleanSpdxStatement(const UChar* sBegin, const UChar* sEnd)
{
  icu::UnicodeString s = rx::u32regex_replace(
    icu::UnicodeString(sBegin, sEnd - sBegin),
    rx::make_u32regex("spdx-filecopyrighttext:", rx::regex_constants::icase),
    " ");

  auto const begin = s.getBuffer();
  auto const end = begin + s.length();

  return cleanGeneral(begin, end);
}

/**
 * \brief Clean copyright statements from special characters
 * (comment characters in programming languages, multiple spaces etc.)
 * \param sBegin String begin
 * \param sEnd   String end
 * \return string Clean statements
 */
icu::UnicodeString cleanStatement(const UChar* sBegin, const UChar* sEnd)
{
  icu::UnicodeString s = rx::u32regex_replace(
    icu::UnicodeString(sBegin, sEnd - sBegin),
    rx::make_u32regex("\n[[:space:][:punct:]]*"), " ");

  auto const begin = s.getBuffer();
  auto const end = begin + s.length();

  return cleanSpdxStatement(begin, end);
}

/**
 * \brief Clean non unicode characters (binary data).
 *
 * Uses ICU library to check if the characters are unicode or not and append
 * only unicode characters to the result string.
 * \param sBegin String begin
 * \param sEnd   String end
 * \return string Clean statements
 */
string cleanNonPrint(string::const_iterator sBegin, string::const_iterator sEnd)
{
  string s(sBegin, sEnd);
  const unsigned char *in = reinterpret_cast<const unsigned char*>(s.c_str());
  int len = s.length();

  icu::UnicodeString out;
  for (int i = 0; i < len;)
  {
    UChar32 uniChar;
    size_t lastPos = i;
    U8_NEXT(in, i, len, uniChar);   // Get next UTF-8 char
    if (uniChar > 0)
    {
      out.append(uniChar);
    }
    else
    {
      i = lastPos;  // Rest pointer
      U16_NEXT(in, i, len, uniChar); // Try to get failed input as UTF-16
      if (U_IS_UNICODE_CHAR(uniChar) && uniChar > 0)
      {
        out.append(uniChar);
      }
    }
  }
  out.trim();

  string ret;
  out.toUTF8String(ret);
  return ret;
}

/**
 * \brief Clean the text based on type
 *
 * If match type is statement, clean as statement. Else clean as general text.
 * \param sText Text for cleaning
 * \param m     Matches to be cleaned
 * \return string Cleaned text
 */
icu::UnicodeString cleanMatch(const icu::UnicodeString& sText, const match& m)
{
  auto const unicodeStr = fo::recodeToUnicode(
    sText.tempSubString(m.start, m.end - m.start));

  auto const begin = unicodeStr.getBuffer();
  auto const end = begin + unicodeStr.length();

  if (m.type == "statement")
    return cleanStatement(begin, end);
  else
    return cleanGeneral(begin, end);
}

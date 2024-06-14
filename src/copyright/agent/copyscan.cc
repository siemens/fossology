/*
 SPDX-FileCopyrightText: © 2015,2022, Siemens AG
 Author: Florian Krügel

 SPDX-License-Identifier: GPL-2.0-only
*/

#include "copyscan.hpp"
#include <cctype>
#include <algorithm>
#include "regexConfProvider.hpp"

const string copyrightType("statement");  /**< A constant for default copyrightType as "statement" */

/**
 * \brief Constructor for default hCopyrightScanner
 *
 * Initialize all regex values
 */
hCopyrightScanner::hCopyrightScanner()
{
  RegexConfProvider rcp;
  rcp.maybeLoad("copyright");

  regCopyright = rx::make_u32regex(rcp.getRegexValue("copyright","REG_COPYRIGHT"),
                                   rx::regex_constants::icase);

  regException = rx::make_u32regex(rcp.getRegexValue("copyright","REG_EXCEPTION"),
                                   rx::regex_constants::icase);
  regNonBlank = rx::make_u32regex(rcp.getRegexValue("copyright","REG_NON_BLANK"));

  regSimpleCopyright = rx::make_u32regex(rcp.getRegexValue("copyright","REG_SIMPLE_COPYRIGHT"),
                                         rx::regex_constants::icase);
  regSpdxCopyright = rx::make_u32regex(rcp.getRegexValue("copyright","REG_SPDX_COPYRIGHT"),
                                       rx::regex_constants::icase);
}

/**
 * \brief Scan a given string for copyright statements
 *
 * Given a string s, scans for copyright statements using regCopyrights.
 * Then checks for an regException match.
 * \param[in]  s   String to work on
 * \param[out] results List of matchs
 */
void hCopyrightScanner::ScanString(const wstring& s, list<match>& results) const
{
  auto const begin = s.begin();
  auto pos = begin;
  auto const end = s.end();
  while (pos != end)
  {
    // Find potential copyright statement
    rx::wsmatch matches;
    if (!rx::regex_search(pos, end, matches, regCopyright))
      // No further copyright statement found
      break;
    auto foundPos = matches[0].first;

    if (!rx::regex_match(foundPos, end, regException))
    {
      /**
       * Not an exception, this means that at foundPos there is a copyright statement.
       * Try to find the proper beginning and end before adding it to the out list.
       *
       * Copyright statements should extend over the following lines until
       * a blank line or a line with a new copyright statement is found.
       * A blank line may consist of
       *   - spaces and punctuation
       *   - no word of two letters, no two consecutive digits
      */
      auto j = find(foundPos, end, '\n');
      while (j != end)
      {
        auto beginOfLine = j;
        ++beginOfLine;
        auto const endOfLine = find(beginOfLine, end, '\n');
        if (rx::regex_search(beginOfLine, endOfLine, regSpdxCopyright))
        {
          // Found end
          break;
        }
        if (rx::regex_search(beginOfLine, endOfLine, regSimpleCopyright)
          || !rx::regex_match(beginOfLine, endOfLine, regNonBlank))
        {
          // Found end
          break;
        }
        j = endOfLine;
      }
      if (j - foundPos >= 301)
        // Truncate
        results.push_back(match(foundPos - begin, (foundPos - begin) + 300, copyrightType));
      else
      {
        results.push_back(match(foundPos - begin, j - begin, copyrightType));
      }
      pos = j;
    }
    else
    {
      // An exception: this is not a copyright statement: continue at the end of this statement
      pos = matches[0].second;
    }
  }
}

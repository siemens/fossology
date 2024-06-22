/*
 SPDX-FileCopyrightText: © 2019 Siemens AG

 SPDX-License-Identifier: GPL-2.0-only
*/

#ifndef SRC_OJO_AGENT_OJOMATCH_HPP_
#define SRC_OJO_AGENT_OJOMATCH_HPP_

#include <string>
#include <utility>
#include <unicode/unistr.h>

/**
 * @struct ojomatch
 * @brief Store the results of a regex match
 */
struct ojomatch
{
  /**
   * @var long int start
   * Start position of match
   * @var long int end
   * End position of match
   * @var long int len
   * Length of match
   * @var long int license_fk
   * License ref of the match if found
   */
  long int start, end, len, license_fk;

  /**
   * @var
   * Matched string
   */
  icu::UnicodeString content;
  /**
   * Constructor for ojomatch structure
   * @param s Start of match
   * @param e End of match
   * @param l Length of match
   * @param c Content of match
   */
  ojomatch(const long int s, const long int e, const long int l,
    icu::UnicodeString c) :
    start(s), end(e), len(l), content(std::move(c))
  {
    license_fk = -1;
  }
  /**
   * Default constructor for ojomatch structure
   */
  ojomatch() :
    start(-1), end(-1), len(-1), license_fk(-1), content(u"")
  {
  }

  bool operator==(const icu::UnicodeString& matchcontent) const
  {
    if(this->content.compare(matchcontent) == 0)
    {
      return true;
    }
    else
    {
      return false;
    }
  }

  bool operator==(const ojomatch& matchcontent) const
  {
    if(this->content.compare(matchcontent.content) == 0)
    {
      return true;
    }
    else
    {
      return false;
    }
  }
};

#endif /* SRC_OJO_AGENT_OJOMATCH_HPP_ */

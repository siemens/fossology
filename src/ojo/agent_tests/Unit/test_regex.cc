/*
 SPDX-FileCopyrightText: © 2019 Siemens AG

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * \file test_regex.cc
 * \brief Test for regex accuracy
 */
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <boost/regex.hpp>
#include <boost/regex/icu.hpp>

#include "ojoregex.hpp"

using namespace std;

/**
 * \class regexTest
 * \brief Test fixture to test regex accuracy
 */
class regexTest : public CPPUNIT_NS :: TestFixture {
  CPPUNIT_TEST_SUITE (regexTest);
  CPPUNIT_TEST (regTest);
  CPPUNIT_TEST (badNameTest);
  CPPUNIT_TEST (regTestSpecialEnd);

  CPPUNIT_TEST_SUITE_END ();

protected:
  /**
   * \brief Test regex on a test string
   *
   * \test
   * -# Create a test SPDX identifier string
   * -# Load the regex patterns
   * -# Run the regex on the string
   * -# Check the actual number of matches against expected result
   * -# Check the actual findings matches the expected licenses
   */
  void regTest (void) {

    const std::string gplLicense = "GPL-2.0";
    const std::string lgplLicense = "LGPL-2.1+";
    // REUSE-IgnoreStart
    std::string contentString = "SPDX-License-Identifier: " + gplLicense + " AND "
        + lgplLicense;
    icu::UnicodeString content = icu::UnicodeString::fromUTF8(
        contentString.c_str());
    // REUSE-IgnoreStart
    boost::u32regex listRegex = boost::make_u32regex(SPDX_LICENSE_LIST, boost::regex_constants::icase);
    boost::u32regex nameRegex = boost::make_u32regex(SPDX_LICENSE_NAMES, boost::regex_constants::icase);

    boost::u16match what;

    icu::UnicodeString licenseList;
    boost::u32regex_search(content, what, listRegex);
    licenseList = icu::UnicodeString(what[1].first, what.length(1));

    std::string licenseListStr;
    licenseList.toUTF8String(licenseListStr);

    // Check if the correct license list is found
    CPPUNIT_ASSERT_EQUAL(gplLicense + " AND " + lgplLicense, licenseListStr);

    // Find the actual licenses in the list
    auto begin = licenseList.getBuffer();
    auto end = begin + licenseList.length();
    list<icu::UnicodeString> licensesFound;

    while (begin != end)
    {
      boost::u16match res;
      if (boost::u32regex_search(begin, end, res, nameRegex))
      {
        licensesFound.emplace_back(res[1].first, res.length(1));
        begin = res[0].second;
      }
      else
      {
        break;
      }
    }

    size_t expectedNos = 2;
    size_t actualNos = licensesFound.size();
    // Check if 2 licenses are found
    CPPUNIT_ASSERT_EQUAL(expectedNos, actualNos);
    // Check if the result contains the expected string
    CPPUNIT_ASSERT(
      std::find(licensesFound.begin(), licensesFound.end(), icu::UnicodeString(gplLicense.c_str()))
        != licensesFound.end());
    CPPUNIT_ASSERT(
      std::find(licensesFound.begin(), licensesFound.end(), icu::UnicodeString(lgplLicense.c_str()))
        != licensesFound.end());
  };

  /**
   * \brief Test regex on a string with bad identifier
   *
   * \test
   * -# Create a test SPDX identifier string with bad license identifier
   * -# Load the regex patterns
   * -# Run the regex on the string
   * -# Check the actual number of matches against expected result
   * -# Check the actual findings matches the expected licenses
   */
  void badNameTest (void) {

    const std::string gplLicense = "GPL-2.0";
    const std::string badLicense = "AB";
    // REUSE-IgnoreStart
    std::string contentString = "SPDX-License-Identifier: " + gplLicense + " AND "
        + badLicense;
    icu::UnicodeString content = icu::UnicodeString::fromUTF8(
        contentString.c_str());
    // REUSE-IgnoreStart
    boost::u32regex listRegex = boost::make_u32regex(SPDX_LICENSE_LIST, boost::regex_constants::icase);
    boost::u32regex nameRegex = boost::make_u32regex(SPDX_LICENSE_NAMES, boost::regex_constants::icase);

    boost::u16match what;

    icu::UnicodeString licenseList;
    boost::u32regex_search(content, what, listRegex);
    licenseList = icu::UnicodeString(what[1].first, what.length(1));

    std::string licenseListStr;
    licenseList.toUTF8String(licenseListStr);

    // Check if only correct license is found
    CPPUNIT_ASSERT_EQUAL(gplLicense, licenseListStr);

    // Find the actual license in the list
    auto begin = licenseList.getBuffer();
    auto end = begin + licenseList.length();
    list<icu::UnicodeString> licensesFound;

    while (begin != end)
    {
      boost::u16match res;
      if (boost::u32regex_search(begin, end, res, nameRegex))
      {
        licensesFound.emplace_back(res[1].first, res.length(1));
        begin = res[0].second;
      }
      else
      {
        break;
      }
    }

    size_t expectedNos = 1;
    size_t actualNos = licensesFound.size();
    // Check if only 1 license is found
    CPPUNIT_ASSERT_EQUAL(expectedNos, actualNos);
    // Check if the result contains the expected string
    CPPUNIT_ASSERT(
      std::find(licensesFound.begin(), licensesFound.end(), icu::UnicodeString(gplLicense.c_str()))
        != licensesFound.end());
    // Check if the result does not contain the bad string
    CPPUNIT_ASSERT(
      std::find(licensesFound.begin(), licensesFound.end(), icu::UnicodeString(badLicense.c_str()))
        == licensesFound.end());
  };

  /**
   * \brief Test regex on a special test string
   *
   * \test
   * -# Create a test SPDX identifier string with special characters at end
   * -# Load the regex patterns
   * -# Run the regex on the string
   * -# Check the actual number of matches against expected result
   * -# Check the actual findings matches the expected licenses
   */
  void regTestSpecialEnd (void) {

    const std::string gplLicense = "GPL-2.0-only";
    const std::string lgplLicense = "LGPL-2.1-or-later";
    const std::string mitLicense = "MIT";
    const std::string mplLicense = "MPL-1.1+";
    // REUSE-IgnoreStart
    std::string contentString = "SPDX-License-Identifier: (" + gplLicense + " AND "
        + lgplLicense + ") OR " + mplLicense + " AND " + mitLicense + ".";
    icu::UnicodeString content = icu::UnicodeString::fromUTF8(
        contentString.c_str());
    // REUSE-IgnoreStart
    boost::u32regex listRegex = boost::make_u32regex(SPDX_LICENSE_LIST, boost::regex_constants::icase);
    boost::u32regex nameRegex = boost::make_u32regex(SPDX_LICENSE_NAMES, boost::regex_constants::icase);

    boost::u16match what;

    icu::UnicodeString licenseList;
    boost::u32regex_search(content, what, listRegex);
    licenseList = icu::UnicodeString(what[1].first, what.length(1));

    std::string licenseListStr;
    licenseList.toUTF8String(licenseListStr);

    // Check if the correct license list is found
    CPPUNIT_ASSERT_EQUAL("(" + gplLicense + " AND " + lgplLicense + ") OR " +
      mplLicense + " AND " + mitLicense + ".", licenseListStr);

    // Find the actual licenses in the list
    auto begin = licenseList.getBuffer();
    auto end = begin + licenseList.length();
    list<icu::UnicodeString> licensesFound;

    while (begin != end)
    {
      boost::u16match res;
      if (boost::u32regex_search(begin, end, res, nameRegex))
      {
        licensesFound.emplace_back(res[1].first, res.length(1));
        begin = res[0].second;
      }
      else
      {
        break;
      }
    }

    size_t expectedNos = 4;
    size_t actualNos = licensesFound.size();
    // Check if 4 licenses are found
    CPPUNIT_ASSERT_EQUAL(expectedNos, actualNos);
    // Check if the result contains the expected string
    CPPUNIT_ASSERT(
      std::find(licensesFound.begin(), licensesFound.end(), icu::UnicodeString(gplLicense.c_str()))
        != licensesFound.end());
    CPPUNIT_ASSERT(
      std::find(licensesFound.begin(), licensesFound.end(), icu::UnicodeString(lgplLicense.c_str()))
        != licensesFound.end());
    CPPUNIT_ASSERT(
      std::find(licensesFound.begin(), licensesFound.end(), icu::UnicodeString(mitLicense.c_str()))
        != licensesFound.end());
    CPPUNIT_ASSERT(
      std::find(licensesFound.begin(), licensesFound.end(), icu::UnicodeString(mplLicense.c_str()))
        != licensesFound.end());
  };

};

CPPUNIT_TEST_SUITE_REGISTRATION( regexTest );

/*
 SPDX-FileCopyrightText: © 2019 Siemens AG

 SPDX-License-Identifier: GPL-2.0-only
*/

#include "OjoAgent.hpp"

using namespace std;

/**
 * Default constructor for OjoAgent.
 *
 * Also initializes the regex.
 */
OjoAgent::OjoAgent() :
    regLicenseList(
        boost::make_u32regex(SPDX_LICENSE_LIST, boost::regex_constants::icase)),
    regLicenseName(
        boost::make_u32regex(SPDX_LICENSE_NAMES, boost::regex_constants::icase)),
    regDualLicense(
        boost::make_u32regex(SPDX_DUAL_LICENSE, boost::regex_constants::icase))
{
}

/**
 * Scan a single file (when running from scheduler).
 * @param filePath        The file to be scanned.
 * @param databaseHandler Database handler to be used.
 * @param groupId         Group running the scan
 * @param userId          User running the scan
 * @return List of matches found.
 * @sa OjoAgent::scanString()
 * @sa OjoAgent::filterMatches()
 * @sa OjoAgent::findLicenseId()
 * @throws std::runtime_error() Throws runtime error if the file can not be
 * read with the file path in description.
 */
vector<ojomatch> OjoAgent::processFile(const string &filePath,
  OjosDatabaseHandler &databaseHandler, const int groupId, const int userId)
{
  icu::UnicodeString fileContent;
  if (!getFileContent(filePath, fileContent))
  {
    throw std::runtime_error(filePath);
  }
  vector<ojomatch> licenseList;
  vector<ojomatch> licenseNames;

  scanString(fileContent, regLicenseList, licenseList, 0, false);
  for (const auto& m : licenseList)
  {
    scanString(m.content, regLicenseName, licenseNames, m.start, false);
    scanString(m.content, regDualLicense, licenseNames, m.start, true);
  }

  findLicenseId(licenseNames, databaseHandler, groupId, userId);
  filterMatches(licenseNames);

  return licenseNames;
}

/**
 * Scan a single file (when running from CLI).
 *
 * This function can not interact with DB.
 * @param filePath File to be scanned
 * @return List of matches.
 */
vector<ojomatch> OjoAgent::processFile(const string &filePath)
{
  icu::UnicodeString fileContent;
  if (!getFileContent(filePath, fileContent))
  {
    throw std::runtime_error(filePath);
  }
  vector<ojomatch> licenseList;
  vector<ojomatch> licenseNames;

  scanString(fileContent, regLicenseList, licenseList, 0, false);
  for (const auto& m : licenseList)
  {
    scanString(m.content, regLicenseName, licenseNames, m.start, false);
    scanString(m.content, regDualLicense, licenseNames, m.start, true);
  }

  // Remove duplicate matches for CLI run
  const auto uniqueListIt =
    std::unique(licenseNames.begin(), licenseNames.end());
  licenseNames.resize(std::distance(licenseNames.begin(), uniqueListIt));

  return licenseNames;
}

/**
 * Scan a string based using a regex and create matches.
 * @param text        String to be scanned
 * @param reg         Regex to be used
 * @param[out] result The match list.
 * @param offset      The offset to be added for each match
 * @param isDualTest  True if testing for Dual-license, false otherwise
 */
void OjoAgent::scanString(const icu::UnicodeString& text,
  const boost::u32regex& reg, vector<ojomatch>& result, unsigned int offset,
  const bool isDualTest)
{
  auto const begin = text.getBuffer();
  auto pos = begin;
  auto const end = begin + text.length();

  while (pos != end)
  {
    // Find next match
    boost::u16match res;
    if (boost::u32regex_search(pos, end, res, reg))
    {
      auto const foundPosStart = res[1].first;
      auto const foundIndexStart = text.indexOf(*foundPosStart, foundPosStart - begin, end - foundPosStart);

      auto const foundPosEnd = foundPosStart + res[1].length();
      auto foundIndexEnd = text.indexOf(*foundPosEnd, foundPosEnd - begin, end - foundPosEnd);

      if (foundIndexEnd == -1)
      {
        foundIndexEnd = text.countChar32();
      }

      icu::UnicodeString content = u"Dual-license";
      if (! isDualTest)
      {
        content = icu::UnicodeString(text, foundIndexStart, foundIndexEnd - foundIndexStart);
      }
      // Found match
      result.emplace_back(offset + foundIndexStart,
              offset + foundIndexEnd,
              foundIndexEnd - foundIndexStart,
              content);
      pos = res[0].second;
      offset += res.position() + (foundIndexEnd - foundIndexStart);
    }
    else
    {
      // No match found
      break;
    }
  }
}

/**
 * Filter the matches list and remove entries with license id less than 1.
 * @param[in,out] matches List of matches to be filtered
 */
void OjoAgent::filterMatches(vector<ojomatch> &matches)
{
  // Remvoe entries with license_fk < 1
  matches.erase(
    std::remove_if(matches.begin(), matches.end(), [](const ojomatch& match)
    { return match.license_fk <= 0;}), matches.end());
}

/**
 * Update the license id for each match entry
 * @param[in,out] matches List of matches to be updated
 * @param databaseHandler Database handler to be used
 * @param groupId         Group running the scan
 * @param userId          User running the scan
 */
void OjoAgent::findLicenseId(vector<ojomatch> &matches,
  OjosDatabaseHandler &databaseHandler, const int groupId, const int userId)
{
  // Update license_fk
  for (auto & match : matches)
  {
    match.license_fk = databaseHandler.getLicenseIdForName(
      match.content, groupId, userId);
  }
}

/**
 * Read the content of a file.
 * @param filePath File to read
 * @param out String to store the file content
 * @return True if file was read successfully, false otherwise
 */
bool OjoAgent::getFileContent(const std::string& filePath,
                              icu::UnicodeString& out)
{
  std::ifstream stream(filePath);
  std::stringstream sstr;
  sstr << stream.rdbuf();
  out = icu::UnicodeString::fromUTF8(sstr.str());
  return !stream.fail();
}

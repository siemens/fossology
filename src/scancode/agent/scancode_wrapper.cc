/*
 SPDX-FileCopyrightText: © 2021 Sarita Singh <saritasingh.0425@gmail.com>

 SPDX-License-Identifier: GPL-2.0-only
*/

#include "scancode_wrapper.hpp"

#include <cerrno>
#include <ctime>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MINSCORE 50

/* Poll interval (seconds) for checking if the scancode child process is done. */
#define SCANCODE_SCAN_POLL_SECS 2

/* Heartbeat interval (seconds) while the scan runs. Must be well under the
 * scheduler's ~10 minute alive-flag timeout. */
#define SCANCODE_SCAN_HEARTBEAT_SECS 180

/**
 * @brief Build a byte-offset index for the start of every line in a file.
 *
 * @param[in] file open input stream for the source file
 * @param[out] offsets byte offset of each line start, 0-indexed
 */
void buildLineOffsets(ifstream &file, vector<size_t> &offsets) {
  if (!file.is_open()) {
    return;
  }
  string line;
  while (true) {
    std::streampos pos = file.tellg();
    if (pos == std::streampos(-1)) {
      break;
    }
    if (!getline(file, line)) {
      break;
    }
    offsets.push_back(static_cast<size_t>(pos));
  }
}

/**
 * @brief converts start line to start byte of the matched text
 *
 * Uses a pre-built line-offset index to avoid re-reading the whole file.
 *
 * @param file open input stream for the source file
 * @param lineOffsets byte offsets of line starts
 * @param start_line start line of the match reported by scancode
 * @param match_text text matched by scancode
 * @param filename name of the file (for logging)
 * @return start byte of the matched text, 0 on failure
 */
unsigned getFilePointer(ifstream &file, const vector<size_t> &lineOffsets,
                        size_t start_line, const string &match_text,
                        const string &filename) {
  if (!file.is_open() || start_line < 1 || start_line > lineOffsets.size()) {
    return 0;
  }
  size_t lineStart = lineOffsets[start_line - 1];
  file.clear();
  file.seekg(static_cast<std::streamoff>(lineStart));
  string str;
  if (!getline(file, str)) {
    return 0;
  }
  size_t pos = str.find(match_text);
  if (pos != string::npos) {
    return static_cast<unsigned>(lineStart + pos);
  }
  LOG_NOTICE("Failed to find startbyte for %s\n", filename.c_str());
  return 0;
}


/**
 * @brief scan file with scancode-toolkit
 *
 * using cli command for custom template
 * scancode <scancode flags> --custom-output <output> --custom-template scancode_template.html <input>
 * scancode is a parametric agent, depending upon user's choice flags will be set.
 * -l flag scans for license
 * -c flag scans for copyright and holder
 * license score in ScanCode is percentage and
 * copyright holder in scancode is author in FOSSology
 * The option --license-text is a sub-option of and requires the option --license, it provides the text
 * in the upload file matched with the scancode license rule.
 * custom template provide only those information which
 * user wants to see.
 * --quiet helps to remove summary and/or progress message
 *
 * @param state  an object of class State which can provide agent Id and CliOptions
 * @param file  code/binary file sent by scheduler
 * @return scanned data output on success, null otherwise
 *
 * @see https://scancode-toolkit.readthedocs.io/en/latest/cli-reference/list-options.html#all-basic-scan-options
 */
void scanFileWithScancode(const State &state, string fileLocation, string outputFile) {
  string projectUser = fo_config_get(sysconfig, "DIRECTORIES", "PROJECTUSER",
  NULL);
  string cacheDir = fo_config_get(sysconfig, "DIRECTORIES", "CACHEDIR",
  NULL);

  string command =
    "PYTHONPATH='/home/" + projectUser + "/pythondeps/' " +
    "python3 runscanonfiles.py -" + state.getCliOptions() + " " +
    ((state.getCliOptions().find('l') != string::npos) ? "-m " +
    to_string(MINSCORE): "") + " " + fileLocation + " " + outputFile;

  /* Run the scan in a child process so we can send heartbeats while it runs.
   * system() would block and leave the alive flag unset, causing the scheduler
   * to kill the agent after ~10 minutes. Blocking waitpid() also won't work
   * because SIGALRM uses SA_RESTART and would never interrupt it. */
  pid_t pid = fork();
  if (pid < 0) {
    LOG_FATAL("could not fork to execute scancode command: %s \n",
              command.c_str());
    bail(1);
  }

  if (pid == 0) {
    /* child: replace image with the shell running the scan command */
    execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(NULL));
    /* exec only returns on failure */
    _exit(127);
  }

  int status = 0;
  time_t lastHeartbeat = time(NULL);
  while (true) {
    pid_t waitResult = waitpid(pid, &status, WNOHANG);
    if (waitResult == pid) {
      break;
    }
    if (waitResult < 0) {
      if (errno == EINTR) {
        continue;
      }
      LOG_FATAL("waitpid failed for scancode command: %s \n", command.c_str());
      bail(1);
    }

    /* Send a heartbeat every SCANCODE_SCAN_HEARTBEAT_SECS to keep the
     * scheduler alive flag set during a long scan. */
    time_t now = time(NULL);
    if (now - lastHeartbeat >= SCANCODE_SCAN_HEARTBEAT_SECS) {
      fo_scheduler_heart(0);
      lastHeartbeat = now;
    }

    struct timespec sleepTime;
    sleepTime.tv_sec = SCANCODE_SCAN_POLL_SECS;
    sleepTime.tv_nsec = 0;
    nanosleep(&sleepTime, NULL);
  }

  int returnvalue = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

  if (returnvalue != 0) {
    LOG_FATAL("could not execute scancode command: %s \n", command.c_str());
    bail(1);
  }

  if (unlink(fileLocation.c_str()) != 0)
  {
    LOG_FATAL("Unable to delete file %s \n", fileLocation.c_str());
  }
}

/**
 * @brief extract data from scancode scanned result
 *
 * In licenses array:
 * spdx_license_key-> license spdx key
 * score-> score of a rule to matched with the output licenes
 * name-> license full name
 * text_url-> license text reference url
 * matched_text-> text in code file matched for the license
 * start_line-> matched text start line
 *
 * Incase there is no license found by scancode,
 * FOSSology has "No_license_found" license short name.
 *
 * In copyright array:
 * value-> copyright statement
 * start-> start line of copyright statement
 *
 * In holder(copyright holder) array:
 * value-> copyright holder name(author in FOSSology)
 * start-> start line of copyright holder
 *
 * @param scancodeResult  scanned result by scancode
 * @param filename        name of the file uploaded
 * @return map having key as type of scanned and value as content for the type
 */

map<string, vector<Match>> extractDataFromScancodeResult(const Json::Value& scancodevalue, const string& filename) {
  map<string, vector<Match>> result;

  // Build the line-offset index once so each match lookup is an O(1) seek.
  ifstream indexFile(filename);
  vector<size_t> lineOffsets;
  buildLineOffsets(indexFile, lineOffsets);

  vector<Match> licenses;
  Json::Value licensearrays = scancodevalue["licenses"];
  if(licensearrays.empty())
  {
    result["scancode_license"].push_back(Match("No_license_found"));
  }
  else
  {
    for (auto oneresult : licensearrays)
    {
        string licensename = oneresult["license_expression_spdx"].asString();
        int percentage = (int)oneresult["score"].asFloat();
        string full_name=oneresult["license_expression"].asString();
        string text_url=oneresult["rule_url"].asString();
        string match_text = oneresult["matched_text"].asString();
        unsigned long start_line=oneresult["start_line"].asUInt();
        string temp_text= match_text.substr(0,match_text.find("\n"));
        unsigned start_pointer = getFilePointer(indexFile, lineOffsets, start_line, temp_text, filename);
        unsigned length = match_text.length();
        result["scancode_license"].push_back(Match(licensename,percentage,full_name,text_url,start_pointer,length));
    }
  }

  Json::Value copyarrays = scancodevalue["copyrights"];
  for (auto oneresult : copyarrays) {
      string copyrightname = oneresult["value"].asString();
      unsigned long start_line=oneresult["start"].asUInt();
      string temp_text= copyrightname.substr(0,copyrightname.find("[\n\t]"));
      unsigned start_pointer = getFilePointer(indexFile, lineOffsets, start_line, temp_text, filename);
      unsigned length = copyrightname.length();
      string type="scancode_statement";
      result["scancode_statement"].push_back(Match(copyrightname,type,start_pointer,length));
  }

  Json::Value holderarrays = scancodevalue["holders"];
  for (auto oneresult : holderarrays) {
      string holdername = oneresult["value"].asString();
      unsigned long start_line=oneresult["start"].asUInt();
      string temp_text= holdername.substr(0,holdername.find("\n"));
      unsigned start_pointer = getFilePointer(indexFile, lineOffsets, start_line, temp_text, filename);
      unsigned length = holdername.length();
      string type="scancode_author";
      result["scancode_author"].push_back(Match(holdername,type,start_pointer,length));
  }

  Json::Value emailarrays = scancodevalue["emails"];
  for (auto oneresult : emailarrays) {
      string emailname = oneresult["value"].asString();
      unsigned long start_line=oneresult["start"].asUInt();
      string temp_text= emailname.substr(0,emailname.find("\n"));
      unsigned start_pointer = getFilePointer(indexFile, lineOffsets, start_line, temp_text, filename);
      unsigned length = emailname.length();
      string type="scancode_email";
      result["scancode_email"].push_back(Match(emailname,type,start_pointer,length));
  }

  Json::Value urlarrays = scancodevalue["urls"];
  for (auto oneresult : urlarrays) {
      string urlname = oneresult["value"].asString();
      unsigned long start_line=oneresult["start"].asUInt();
      string temp_text= urlname.substr(0,urlname.find("\n"));
      unsigned start_pointer = getFilePointer(indexFile, lineOffsets, start_line, temp_text, filename);
      unsigned length = urlname.length();
      string type="scancode_url";
      result["scancode_url"].push_back(Match(urlname,type,start_pointer,length));
  }
  return result;
}

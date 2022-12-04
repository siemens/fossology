/*
 SPDX-FileCopyrightText: © 2010, 2011, 2012 Hewlett-Packard Development Company, L.P.

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * \file
 * \brief Scheduler operations
 */

/* local includes */
#include <libfossrepo.h>
#include <agent.h>
#include <database.h>
#include <event.h>
#include <host.h>
#include <interface.h>
#include <scheduler.h>
#include <fossconfig.h>

/* std library includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* unix system includes */
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

/* glib includes */
#include <glib.h>
#include <gio/gio.h>

#include <json-c/json.h>
#include <curl/curl.h>

/**
 * Test if error is not NULL then print it to the log.
 */
#define TEST_ERROR(error, ...)                                     \
  if(error)                                                        \
  {                                                                \
    log_printf("ERROR %s.%d: %s\n",                                \
      __FILE__, __LINE__, error->message);                         \
    log_printf("ERROR %s.%d: ", __FILE__, __LINE__);               \
    log_printf(__VA_ARGS__);                                       \
    log_printf("\n");                                              \
    g_clear_error(&error);                                         \
    continue;                                                      \
  }

/* global flags */
int verbose = 0;        ///< The verbose level
int closing = 0;        ///< Set if scheduler is shutting down

GThread* main_thread;   ///< Pointer to the main thread

#define SELECT_DECLS(type, name, l_op, w_op, val) type CONF_##name = val;
CONF_VARIABLES_TYPES(SELECT_DECLS)
#undef SELECT_DECLS

/* ************************************************************************** */
/* **** signals and events ************************************************** */
/* ************************************************************************** */

#define MASK_SIGCHLD (1 << 0)
#define MASK_SIGALRM (1 << 1)
#define MASK_SIGTERM (1 << 2)
#define MASK_SIGQUIT (1 << 3)
#define MASK_SIGHUP  (1 << 4)

int sigmask = 0;

struct MemoryStruct
{
  char *memory;
  size_t size;
};

/**
 * @brief Callback function to get chuncks of the response and attachet to a memory location.
 *
 * @param contents  
 * @param size  
 * @param nmemb  
 * @param userp  
 * 
 */

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if (!ptr)
  {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

/**
 * @brief Handles any signals sent to the scheduler that are not SIGCHLD.
 *
 * Currently Handles:
 *
 * | Signal | Effect |
 * | ---: | :--- |
 * | SIGCHLD | Scheduler will handle to death of the child process or agent |
 * | SIGALRM | Scheduler will run agent updates and database updates |
 * | SIGTERM | Scheduler will gracefully shut down |
 * | SIGQUIT | Scheduler will forcefully shut down |
 * |  SIGHIP | Scheduler will reload configuration data |
 *
 * @param signo  the number of the signal that was sent
 */
void scheduler_sig_handle(int signo)
{
  /* Anywhere you see a "#if __GNUC__" the code is checking if GCC is the
   * compiler. This is because the __sync... set of functions are the GCC
   * version of atomics.
   *
   * This means that if you aren't compiling with GCC, you can have a race
   * condition that results in a signal being lost during the
   * signal_scheduler() function.
   *
   * What could happen:
   *   1. signal_scheduler() reads value of sigmask
   *   2. scheduler receives a SIG**** and sets the correct bit in sigmask
   *   3. signal_scheduler() clears sigmask by setting it to 0
   *
   * In this set of events, a signal has been lost. If this is a sigchld this
   * could be very bad as a job could never get marked as finished.
   */
  switch(signo)
  {
#if __GNUC__
    case SIGCHLD: __sync_fetch_and_or(&sigmask, MASK_SIGCHLD); break;
    case SIGTERM: __sync_fetch_and_or(&sigmask, MASK_SIGTERM); break;
    case SIGQUIT: __sync_fetch_and_or(&sigmask, MASK_SIGQUIT); break;
    case SIGHUP:  __sync_fetch_and_or(&sigmask, MASK_SIGHUP);  break;
#else
    case SIGCHLD: sigmask |= MASK_SIGCHLD; break;
    case SIGALRM: sigmask |= MASK_SIGALRM; break;
    case SIGTERM: sigmask |= MASK_SIGTERM; break;
    case SIGQUIT: sigmask |= MASK_SIGQUIT; break;
    case SIGHUP:  sigmask |= MASK_SIGHUP ; break;
#endif
  }
}

/**
 * @brief Function that handles certain signals being delivered to the scheduler
 *
 * This function is called every time the event loop attempts to take something
 * from the event queue. It will also get called once a second regardless of if
 * a new event has been queued.
 *
 * This function checks the sigmask variable to check what signals have been
 * received since the last time it was called. The sigmask variable should
 * always be accessed atomically since it is accessed by the event loop thread
 * as well as the signal handlers.
 *
 * @param scheduler Scheduler to sent signal to
 */
void scheduler_signal(scheduler_t* scheduler)
{
  // the last time an update was run
  static time_t last_update = 0;

  // copy of the mask
  guint mask;

  /* this will get sigmask and set it to 0 */
#if __GNUC__
  mask = __sync_fetch_and_and(&sigmask, 0);
#else
  mask = sigmask;
  sigmask = 0;
#endif

  /* initialize last_update */
  if(last_update == 0)
    last_update = time(NULL);

  /* signal: SIGCHLD
   *
   * A SIGCHLD has been received since the last time signal_scheduler() was
   * called. Get all agents that have finished since last this happened and
   * create an event for each.
   */
  if(mask & MASK_SIGCHLD)
  {
    pid_t n;          // the next pid that has died
    pid_t* pass;
    int status;       // status returned by waitpit()

    /* get all of the dead children's pids */
    while((n = waitpid(-1, &status, WNOHANG)) > 0)
    {
      V_SCHED("SIGNALS: received sigchld for pid %d\n", n);
      pass = g_new0(pid_t, 2);
      pass[0] = n;
      pass[1] = status;
      event_signal(agent_death_event, pass);
    }
  }

  /* signal: SIGTERM
   *
   * A SIGTERM has been received. simply set the closing flag to 1 so that the
   * scheduler will gracefully shutdown as all the agents finish running.
   */
  if(mask & MASK_SIGTERM)
  {
    V_SCHED("SIGNALS: Scheduler received terminate signal, shutting down gracefully\n");
    event_signal(scheduler_close_event, (void*)0);
  }

  /* signal: SIGQUIT
   *
   * A SIGQUIT has been received. Queue a scheduler_close_event so that the
   * scheduler will immediately stop running. This will cause all the agents to
   * be forcefully killed.
   */
  if(mask & MASK_SIGQUIT)
  {
    V_SCHED("SIGNALS: Scheduler received quit signal, shutting down scheduler\n");
    event_signal(scheduler_close_event, (void*)1);
  }

  /* signal: SIGHUP
   *
   * A SIGHUP has been received. reload the configuration files for the
   * scheduler. This will run here instead of being queued as an event.
   */
  if(mask & MASK_SIGHUP)
  {
    V_SCHED("SIGNALS: Scheduler received SGIHUP, reloading configuration data\n");
    scheduler_config_event(scheduler, NULL);
  }

  /* Finish by checking if an agent update needs to be performed.
   *
   * Every CONF_agent_update_interval, the agents and database should be
   * updated. The agents need to be updated to check for dead and unresponsive
   * agents. The database is updated to make sure that a new job hasn't been
   * scheduled without the scheduler being informed.
   */
  if((time(NULL) - last_update) > CONF_agent_update_interval )
  {
    V_SPECIAL("SIGNALS: Performing agent and database update\n");
    event_signal(agent_update_event, NULL);
    event_signal(database_update_event, NULL);
    last_update = time(NULL);
  }
}

/* ************************************************************************** */
/* **** The actual scheduler ************************************************ */
/* ************************************************************************** */

/**
 * @brief Create a new scheduler object.
 *
 * This will initialize everything to a point where it can be used. All regular
 * expressions, GTree's and the job_queue will be correctly created.
 *
 * @param sysconfigdir  Directory containing the fossology.conf
 * @param log           Log file to log messages to
 * @return A new scheduler_t* that can be further populated
 */
scheduler_t* scheduler_init(gchar* sysconfigdir, log_t* log)
{
  scheduler_t* ret = g_new0(scheduler_t, 1);

  ret->process_name  = NULL;
  ret->s_pid         = getpid();
  ret->s_daemon      = FALSE;
  ret->s_startup     = FALSE;
  ret->s_pause       = TRUE;

  ret->sysconfig     = NULL;
  ret->sysconfigdir  = g_strdup(sysconfigdir);
  ret->logdir        = LOG_DIR;
  ret->logcmdline    = FALSE;
  ret->main_log      = log;
  ret->host_queue    = NULL;

  ret->i_created     = FALSE;
  ret->i_terminate   = FALSE;
  ret->i_port        = 0;
  ret->server        = NULL;
  ret->workers       = NULL;
  ret->cancel        = NULL;

  ret->job_queue     = g_sequence_new(NULL);

  ret->db_conn       = NULL;
  ret->host_url      = NULL;
  ret->email_subject = NULL;
  ret->email_header  = NULL;
  ret->email_footer  = NULL;
  ret->email_command = NULL;

  /* This regex should find:
   *   1. One or more capital letters followed by a ':' followed by white space,
   *      followed by a number
   *   2. One or more capital letters followed by a ':' followed by white space,
   *      followed by a number, followed by white space, followed by a number
   *
   * Examples:
   *   HEART: 1 2   -> matches
   *   HEART: 1     -> matches
   *   HEART:       -> does not match
   *
   */
  ret->parse_agent_msg = g_regex_new(
      "([A-Z]+):([ \t]+)(\\d+)(([ \t]+)(\\d))?",
      0, 0, NULL);

  /* This regex should find:
   *   1. A '$' followed by any combination of capital letters or underscore
   *   2. A '$' followed by any combination of capital letters or underscore,
   *      followed by a '.' followed by alphabetic characters or underscore,
   *      followed by a '.' followed by alphabetic characters or underscore
   *
   * Examples:
   *   $HELLO             -> matches
   *   $SIMPLE_NAME       -> matches
   *   $DB.table.column   -> matches
   *   $bad               -> does not match
   *   $DB.table          -> does not match
   */
  ret->parse_db_email      = g_regex_new(
      "\\$([A-Z_]*)(\\.([a-zA-Z_]*)\\.([a-zA-Z_]*))?",
      0, 0, NULL);

  /* This regex should match:
   *   1. a set of alphabetical characters
   *   2. a set of alphabetical characters, followed by white space, followed by
   *      a number
   *   3. a set of alphabetical characters, followed by white space, followed by
   *      a number, followed by white space, followed by a string in quotes.
   *
   *
   * Examples:
   *   close                   -> matches
   *   stop                    -> matches
   *   pause 10                -> matches
   *   kill 10 "hello world"   -> matches
   *   pause 10 10             -> does not match
   *   kill "hello world" 10   -> does not match
   *
   *
   */
  ret->parse_interface_cmd = g_regex_new(
      "(\\w+)(\\s+(-?\\d+))?(\\s+((-?\\d+)|(\"(.*)\")))?",
      0, G_REGEX_MATCH_NEWLINE_LF, NULL);

  ret->meta_agents = g_tree_new_full(string_compare, NULL, NULL,
      (GDestroyNotify)meta_agent_destroy);
  ret->agents      = g_tree_new_full(int_compare,    NULL, NULL,
      (GDestroyNotify)agent_destroy);
  ret->host_list = g_tree_new_full(string_compare, NULL, NULL,
      (GDestroyNotify)host_destroy);
  ret->job_list     = g_tree_new_full(int_compare, NULL, NULL,
      (GDestroyNotify)job_destroy);

  main_log = log;

  return ret;
}

/**
 * @brief Free any memory associated with a scheduler_t.
 *
 * This will stop the interface if it is currently running, and free all the
 * memory associated with the different regular expression and similar
 * structures.
 *
 * @param scheduler
 * @todo Interface close
 * @todo Repo close
 */
void scheduler_destroy(scheduler_t* scheduler)
{

  event_loop_destroy();

  if(scheduler->main_log)
  {
    log_destroy(scheduler->main_log);
    main_log = NULL;
  }

  if(scheduler->process_name) g_free(scheduler->process_name);
  if(scheduler->sysconfig)    fo_config_free(scheduler->sysconfig);
  if(scheduler->sysconfigdir) g_free(scheduler->sysconfigdir);
  if(scheduler->host_queue)   g_list_free(scheduler->host_queue);
  if(scheduler->workers)      g_thread_pool_free(scheduler->workers, FALSE, TRUE);

  if(scheduler->email_subject) g_free(scheduler->email_subject);
  if(scheduler->email_command) g_free(scheduler->email_command);

  g_sequence_free(scheduler->job_queue);

  g_regex_unref(scheduler->parse_agent_msg);
  g_regex_unref(scheduler->parse_db_email);
  g_regex_unref(scheduler->parse_interface_cmd);

  g_tree_unref(scheduler->meta_agents);
  g_tree_unref(scheduler->agents);
  g_tree_unref(scheduler->host_list);
  g_tree_unref(scheduler->job_list);

  if (scheduler->db_conn) PQfinish(scheduler->db_conn);

  g_free(scheduler);
}

/**
 * @brief Check if the current agent's max limit is respected.
 *
 * Compare the number of running agents and run limit of the agent.
 * @param agent     Agent which has to be scheduled.
 * @return True if the agent can be scheduled (no. of running agents < max run
 *         limit of the agent), false otherwise.
 */
static gboolean isMaxLimitReached(meta_agent_t* agent)
{
  if (agent != NULL && agent->max_run <= agent->run_count)
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/**
 * @brief Update function called after every event
 *
 * The heart of the scheduler, the actual scheduling algorithm. This will be
 * passed to the event loop as a call back and will be called every time an event
 * is executed. Therefore the code should be light weight since it will be run
 * very frequently.
 *
 * @todo Currently this will only grab a job and create a single agent to execute
 *   the job.
 *
 * @todo Allow for runonpfile jobs to have multiple agents based on size
 * @todo Allow for job preemption. The scheduler can pause jobs, allow it
 * @todo Allow for specific hosts to be chosen.
 */
void scheduler_update(scheduler_t* scheduler)
{
  /* queue used to hold jobs if an exclusive job enters the system */
  static job_t*  job  = NULL;
  static host_t* host = NULL;
  static int lockout = 0;

  /* locals */
  int n_agents = g_tree_nnodes(scheduler->agents);
  int n_jobs   = active_jobs(scheduler->job_list);

  /* check to see if we are in and can exit the startup state */
  if(scheduler->s_startup && n_agents == 0)
  {
    event_signal(database_update_event, NULL);
    scheduler->s_startup = 0;
  }

  /* check if we are able to close the scheduler */
  if(closing && n_agents == 0 && n_jobs == 0)
  {
    event_loop_terminate();
    return;
  }

  if(lockout && n_agents == 0 && n_jobs == 0)
    lockout = 0;

  if(job == NULL && !lockout)
  {
    while((job = peek_job(scheduler->job_queue)) != NULL)
    {
      // Check the max limit of running agents
      if (isMaxLimitReached(
          g_tree_lookup(scheduler->meta_agents, job->agent_type)))
      {
        V_SCHED("JOB_INIT: Unable to run agent %s due to max_run limit.\n",
            job->agent_type);
        job = NULL;
        break;
      }
      // check if the agent is required to run on local host
      if(is_meta_special(
          g_tree_lookup(scheduler->meta_agents, job->agent_type), SAG_LOCAL))
      {
        host = g_tree_lookup(scheduler->host_list, LOCAL_HOST);
        if(!(host->running < host->max))
        {
          job = NULL;
          break;
        }
      }
      // check if the job is required to run on a specific machine
      else if((job->required_host != NULL))
      {
        host = g_tree_lookup(scheduler->host_list, job->required_host);
        if(host != NULL)
        {
          if(!(host->running < host->max))
          {
          job = NULL;
          break;
        }
       } else {
         //log_printf("ERROR %s.%d: jq_pk %d jq_host '%s' not in the agent list!\n",
         //  __FILE__, __LINE__, job->id, job->required_host);
         job->message = "ERROR: jq_host not in the agent list!";
         job_fail_event(scheduler, job);
         job = NULL;
         break;
       }
      }
      // the generic case, this can run anywhere, find a place
      else if((host = get_host(&(scheduler->host_queue), 1)) == NULL)
      {
        job = NULL;
        break;
      }

      next_job(scheduler->job_queue);
      if(is_meta_special(
          g_tree_lookup(scheduler->meta_agents, job->agent_type), SAG_EXCLUSIVE))
      {
        V_SCHED("JOB_INIT: exclusive, postponing initialization\n");
        break;
      }

      V_SCHED("Starting JOB[%d].%s\n", job->id, job->agent_type);
      agent_init(scheduler, host, job);
      job = NULL;
    }
  }

  if(job != NULL && n_agents == 0 && n_jobs == 0)
  {
    agent_init(scheduler, host, job);
    lockout = 1;
    job  = NULL;
    host = NULL;
  }

  if(scheduler->s_pause)
  {
    scheduler->s_startup = 1;
    scheduler->s_pause = 0;
  }
}

/* ************************************************************************** */
/* **** main utility functions ********************************************** */
/* ************************************************************************** */

#define GU_HEADER "DIRECTORIES"
#define GU_GROUP  "PROJECTGROUP"
#define GU_USER   "PROJECTUSER"

/**
 * Correctly set the project user and group. The fossology scheduler must run as
 * the user specified by PROJECT_USER and PROJECT_GROUP since the agents must be
 * able to connect to the database. This ensures that that happens correctly.
 *
 * @param process_name
 * @param config
 */
void set_usr_grp(gchar* process_name, fo_conf* config)
{
  /* locals */
  struct group*  grp;
  struct passwd* pwd;

  char* group =
      fo_config_has_key(config, GU_HEADER, GU_GROUP) ?
      fo_config_get    (config, GU_HEADER, GU_GROUP, NULL) : PROJECT_GROUP;
  char* user  =
      fo_config_has_key(config, GU_HEADER, GU_USER)  ?
      fo_config_get    (config, GU_HEADER, GU_USER, NULL)  : PROJECT_USER;

  /* make sure group exists */
  grp = getgrnam(group);
  if(!grp)
  {
    fprintf(stderr, "FATAL %s.%d: could not find group \"%s\"\n",
        __FILE__, __LINE__, group);
    fprintf(stderr, "FATAL set_usr_grp() aborting due to error: %s\n",
        strerror(errno));
    exit(-1);
  }

  /* set the project group */
  setgroups(1, &(grp->gr_gid));
  if((setgid(grp->gr_gid) != 0) || (setegid(grp->gr_gid) != 0))
  {
    fprintf(stderr, "FATAL %s.%d: %s must be run as root or %s\n",
        __FILE__, __LINE__, process_name, user);
    fprintf(stderr, "FATAL Set group '%s' aborting due to error: %s\n",
        group, strerror(errno));
    exit(-1);
  }

  /* run as project user */
  pwd = getpwnam(user);
  if(!pwd)
  {
    fprintf(stderr, "FATAL %s.%d: user '%s' not found\n",
        __FILE__, __LINE__, user);
    exit(-1);
  }

  /* run as correct user, not as root or any other user */
  if((setuid(pwd->pw_uid) != 0) || (seteuid(pwd->pw_uid) != 0))
  {
    fprintf(stderr, "FATAL %s.%d: %s must run this as %s\n",
        __FILE__, __LINE__, process_name, user);
    fprintf(stderr, "FATAL SETUID aborting due to error: %s\n",
        strerror(errno));
    exit(-1);
  }
}

/**
 * @brief Kills all other running scheduler
 * @param force  if the scheduler should shutdown gracefully
 * @return 0 for success (i.e. a scheduler was killed), -1 for failure.
 *
 * This uses the /proc file system to find all processes that have fo_scheduler
 * in the name and sends a kill signal to them.
 */
int kill_scheduler(int force)
{
  gchar f_name[FILENAME_MAX];
  struct dirent* ep;
  DIR* dp;
  FILE* file;
  gint num_killed = 0;
  pid_t s_pid = getpid();

  if((dp = opendir("/proc/")) == NULL)
  {
    fprintf(stderr, "ERROR %s.%d: Could not open /proc/ file system\n",
        __FILE__, __LINE__);
    exit(-1);
  }

  while((ep = readdir(dp)) != NULL)
  {
    if(string_is_num(ep->d_name))
    {
      snprintf(f_name, sizeof(f_name), "/proc/%s/cmdline", ep->d_name);
      if((file = fopen(f_name, "rt")))
      {
        if(fgets(f_name, sizeof(f_name), file) != NULL &&
            strstr(f_name, "fo_scheduler") && s_pid != atoi(ep->d_name))
        {
          NOTIFY("KILL: send signal to process %s\n", ep->d_name);
          if(force)
            kill(atoi(ep->d_name), SIGQUIT);
          else
            kill(atoi(ep->d_name), SIGTERM);
          num_killed++;
        }

        fclose(file);
      }
    }
  }

  closedir(dp);

  if(num_killed == 0)
    return -1;
  return 0;
}

/**
 * @brief Clears any information that is loaded when loading the configuration
 *
 * @param scheduler  the scheduler to reset the information on
 */
void scheduler_clear_config(scheduler_t* scheduler)
{
  g_tree_clear(scheduler->meta_agents);
  g_tree_clear(scheduler->host_list);

  g_list_free(scheduler->host_queue);
  scheduler->host_queue = NULL;

  g_free(scheduler->host_url);
  g_free(scheduler->email_subject);
  g_free(scheduler->email_command);
  PQfinish(scheduler->db_conn);
  scheduler->db_conn       = NULL;
  scheduler->host_url      = NULL;
  scheduler->email_subject = NULL;
  scheduler->email_command = NULL;

  if(scheduler->default_header)
    munmap(scheduler->email_header, strlen(scheduler->email_header));
  if(scheduler->default_footer)
    munmap(scheduler->email_footer, strlen(scheduler->email_footer));
  scheduler->email_header  = NULL;
  scheduler->email_footer  = NULL;

  fo_config_free(scheduler->sysconfig);
  scheduler->sysconfig = NULL;
}

/**
 * @brief GTraverseFunc used by g_tree_clear to collect all the keys in a tree
 *
 * @param key    The current key
 * @param value  The value mapped to the current key
 * @param data   A GList** that the key will be appended to
 * @return       Always returns 0
 */
static gboolean g_tree_collect(gpointer key, gpointer value, gpointer data)
{
  GList** ret = (GList**)data;

  *ret = g_list_append(*ret, key);

  return 0;
}

/**
 * @brief Clears the contents of a GTree
 *
 * @param tree  the tree to remove all elements from
 */
void g_tree_clear(GTree* tree)
{
  GList* keys = NULL;
  GList* iter = NULL;

  g_tree_foreach(tree, g_tree_collect, &keys);

  for(iter = keys; iter != NULL; iter = iter->next)
    g_tree_remove(tree, iter->data);

  g_list_free(keys);
}

/**
 * @brief Loads a particular agents configuration file
 *
 * This loads and saves the results as a new meta_agent. This assumes that the
 * configuration file for the agent includes the following key/value pairs:
 * -# command: The command that will be used to start the agent
 * -# max:     The maximum number of this agent that can run at once
 * -# special: Anything that is special about the agent
 */
void scheduler_agent_config(scheduler_t* scheduler)
{
  uint8_t max = -1;         // the number of agents to a host or number of one type running
  uint32_t special = 0;     // anything that is special about the agent (EXCLUSIVE)

  CURL *curl_handle;
  CURLcode res;

  struct MemoryStruct chunk;

  chunk.memory = malloc(1); /* will be grown as needed by the realloc above */
  chunk.size = 0;           /* no data at this point */

  curl_global_init(CURL_GLOBAL_ALL);

  /* init the curl session */
  curl_handle = curl_easy_init();

  /* specify URL to get */
  curl_easy_setopt(curl_handle, CURLOPT_URL, "http://etcd:2379/v2/keys/agents?recursive=true");

  /* send all data to this function  */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

  /* we pass our 'chunk' struct to the callback function */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

  /* some servers don't like requests that are made without a user-agent
     field, so we provide one */
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

  /* get it! */
  res = curl_easy_perform(curl_handle);

  /* check for errors */
  if (res != CURLE_OK)
  {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
  }
  else
  {
    /*
     * Now, our chunk.memory points to a memory block that is chunk.size
     * bytes big and contains the remote file.
     */
    printf("%s \n", chunk.memory);
    char conf_buff[20];
    char agent_name_buff[128], conf_key_buff[128], name[128], cmd[128], spc[128];
    struct json_object *parsed_json, *action, *key, *node_agents;
    struct json_object *nodes_agents, *node_conf, *nodes_conf, *value;
    struct json_object *node_special;
    int nodes_agents_len, nodes_conf_len, special_len;

    parsed_json = json_tokener_parse(chunk.memory);

    json_object_object_get_ex(parsed_json, "action", &action);
    json_object_object_get_ex(parsed_json, "node", &node_agents);

    NOTIFY("action: %s", json_object_get_string(action));
    NOTIFY("node_agents: %s", json_object_get_string(node_agents));

    parsed_json = node_agents;

    json_object_object_get_ex(parsed_json, "key", &key);
    json_object_object_get_ex(parsed_json, "nodes", &nodes_agents);

    NOTIFY("key: %s", json_object_get_string(key));
    NOTIFY("nodes_agents: %s", json_object_get_string(nodes_agents));

    nodes_agents_len = json_object_array_length(nodes_agents);

    for (size_t i = 0; i < nodes_agents_len; i++)
    {
      struct json_object *conf_special;
      node_agents = json_object_array_get_idx(nodes_agents, i);
      json_object_object_get_ex(node_agents, "key", &key);
      json_object_object_get_ex(node_agents, "nodes", &nodes_conf);

      NOTIFY("key: %s", json_object_get_string(key));
      NOTIFY("nodes_conf: %s", json_object_get_string(nodes_conf));
      strcpy(agent_name_buff, json_object_get_string(key));
      nodes_conf_len = json_object_array_length(nodes_conf);
      for (size_t j = 0; j < nodes_conf_len; j++)
      {
        node_conf = json_object_array_get_idx(nodes_conf, j);
        json_object_object_get_ex(node_conf, "key", &key);
        strcpy(conf_key_buff, json_object_get_string(key));
        memcpy(conf_buff, conf_key_buff + strlen(agent_name_buff) + 1, strlen(conf_key_buff) - strlen(agent_name_buff));
        conf_buff[strlen(conf_key_buff) - strlen(agent_name_buff)] = '\0';
        if (strncmp("name", conf_buff, 4) == 0)
        {
          json_object_object_get_ex(node_conf, "value", &value);
          strcpy(name, json_object_get_string(value));
          NOTIFY("key: %s", json_object_get_string(key));
          NOTIFY("value: %s", name);
        }
        else if (strncmp("command", conf_buff, 7) == 0)
        {
          json_object_object_get_ex(node_conf, "value", &value);
          strcpy(cmd, json_object_get_string(value));
          NOTIFY("key: %s", json_object_get_string(key));
          NOTIFY("value: %s", cmd);
        }
        else if (strncmp("max", conf_buff, 3) == 0)
        {
          json_object_object_get_ex(node_conf, "value", &value);
          max = json_object_get_int(value);
          NOTIFY("key: %s", json_object_get_string(key));
          NOTIFY("value: %d", max);
        }
        else if (strncmp("special", conf_buff, 7) == 0)
        {
          json_object_object_get_ex(node_conf, "nodes", &conf_special);
          special_len = json_object_array_length(conf_special);
          for (size_t k = 0; k < special_len; k++)
          {
            node_special = json_object_array_get_idx(conf_special, k);
            json_object_object_get_ex(node_special, "value", &value);
            strcpy(spc, json_object_get_string(value));
            if (spc[0] != '\0')
            {
                if(strncmp(spc, "EXCLUSIVE", 9) == 0)
                  special |= SAG_EXCLUSIVE;
                else if(strncmp(spc, "NOEMAIL", 7) == 0)
                  special |= SAG_NOEMAIL;
                else if(strncmp(spc, "NOKILL", 6) == 0)
                  special |= SAG_NOKILL;
                else if(strncmp(spc, "LOCAL", 6) == 0)
                  special |= SAG_LOCAL;
            }
          }
        }
      }
      NOTIFY("Debug ma list cmd %s", cmd);
      NOTIFY("Debug ma list max %d", max);
      NOTIFY("Debug ma list name %s", name);
      if(!add_meta_agent(scheduler->meta_agents, name, cmd, max, special))
      {
        V_SCHED("CONFIG: could not create meta agent\n");
      }
      else if(TVERB_SCHED)
      {
        log_printf("CONFIG: added new agent\n");
        log_printf("    name = %s\n", name);
        log_printf(" command = %s\n", cmd);
        log_printf("     max = %d\n", max);
        log_printf(" special = %d\n", special);
      }  
    }
  }

  // printf("%s", chunk.memory);

  /* cleanup curl stuff */
  curl_easy_cleanup(curl_handle);

  free(chunk.memory);

  /* we're done with libcurl, so clean it up */
  curl_global_cleanup();

  event_signal(scheduler_test_agents, NULL);
}

/**
 * @brief Loads the configuration data from fossology.conf
 *
 * This assumes that fossology.conf contains the following key/value pairs:
 *   1. port: the port that the scheduler will listen on
 *   2. LOG_DIR: the directory that the log should be in
 *
 * There should be a group named HOSTS with all of the hosts listed as
 * key/value pairs under this category. For each of these hosts, the scheduler
 * will create a new host as an internal representation.
 */
void scheduler_foss_config(scheduler_t *scheduler)
{
  gchar *tmp;       // pointer into a string
  int32_t max = -1; // the number of agents to a host or number of one type running        // anything that is special about the agent (EXCLUSIVE)
  char conf_key_buff[128];
  gchar addbuf[512];           // standard string buffer
  gchar dirbuf[FILENAME_MAX];  // standard string buffer
  gchar typebuf[FILENAME_MAX]; // standard string buffer
  GError *error = NULL;        // error return location
  int32_t i, j;                // indexing variable
  host_t *host;                // new hosts will be created in the loop
  fo_conf *version;            // information loaded from the version file

  if (scheduler->sysconfig != NULL)
    fo_config_free(scheduler->sysconfig);
    /* parse the config file */
  tmp = g_strdup_printf("%s/fossology.conf", scheduler->sysconfigdir);
  scheduler->sysconfig = fo_config_load(tmp, &error);
  if(error) FATAL("%s", error->message);
  g_free(tmp);

  /* load the version information */
  tmp = g_strdup_printf("%s/VERSION", scheduler->sysconfigdir);
  version = fo_config_load(tmp, &error);
  if (error)
    FATAL("%s", error->message);
  g_free(tmp);

  fo_config_join(scheduler->sysconfig, version, NULL);
  fo_config_free(version);

  CURL *curl_handle;
  CURLcode res;

  struct MemoryStruct chunk;

  chunk.memory = malloc(1); /* will be grown as needed by the realloc above */
  chunk.size = 0;           /* no data at this point */

  curl_global_init(CURL_GLOBAL_ALL);

  /* init the curl session */
  curl_handle = curl_easy_init();

  /* specify URL to get */
  curl_easy_setopt(curl_handle, CURLOPT_URL, "http://etcd:2379/v2/keys/fossology?recursive=true");

  /* send all data to this function  */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

  /* we pass our 'chunk' struct to the callback function */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

  /* some servers don't like requests that are made without a user-agent
     field, so we provide one */
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

  /* get it! */
  res = curl_easy_perform(curl_handle);

  /* check for errors */
  if (res != CURLE_OK)
  {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
  }
  else
  {
    /*
     * Now, our chunk.memory points to a memory block that is chunk.size
     * bytes big and contains the remote file.
     */
    NOTIFY("%s \n", chunk.memory);

    /* set the user and group before proceeding */
    set_usr_grp(scheduler->process_name, scheduler->sysconfig);

    struct json_object *parsed_json;
    struct json_object *action;
    struct json_object *key;
    struct json_object *node;
    struct json_object *nodes;
    struct json_object *node_conf;
    struct json_object *nodes_conf;
    struct json_object *value;
    int nodes_len, conf_nodes_len;

    parsed_json = json_tokener_parse(chunk.memory);

    json_object_object_get_ex(parsed_json, "action", &action);
    json_object_object_get_ex(parsed_json, "node", &node);

    NOTIFY("action: %s", json_object_get_string(action));
    NOTIFY("node: %s", json_object_get_string(node));

    parsed_json = node;

    json_object_object_get_ex(parsed_json, "key", &key);
    json_object_object_get_ex(parsed_json, "nodes", &nodes);

    NOTIFY("key: %s", json_object_get_string(key));
    NOTIFY("nodes: %s", json_object_get_string(nodes));

    nodes_len = json_object_array_length(nodes);

    /* cleanup curl stuff */
    curl_easy_cleanup(curl_handle);

    free(chunk.memory);

    /* we're done with libcurl, so clean it up */
    curl_global_cleanup();

    for (i = 0; i < nodes_len; i++)
    {
      node = json_object_array_get_idx(nodes, i);
      json_object_object_get_ex(node, "key", &key);
      strcpy(conf_key_buff, json_object_get_string(key));
      /* load the host settings */
      if (strncmp(conf_key_buff, "/fossology/hosts", 16) == 0)
      {
        json_object_object_get_ex(node, "nodes", &nodes_conf);
        conf_nodes_len = json_object_array_length(nodes_conf);
        for (j = 0; j < conf_nodes_len; j++)
        {
          node_conf = json_object_array_get_idx(nodes_conf, j);
          json_object_object_get_ex(node_conf, "key", &key);
          json_object_object_get_ex(node_conf, "value", &value);

          NOTIFY("key: %s", json_object_get_string(key));
          NOTIFY("value: %s", json_object_get_string(value));

          sscanf(json_object_get_string(value), "%s %s %d %s", addbuf, dirbuf, &max, typebuf);
          host = host_init((char *)json_object_get_string(key), addbuf, dirbuf, max, typebuf);
          host_insert(host, scheduler);
          if (TVERB_SCHED)
          {
            log_printf("CONFIG: added new host\n");
            log_printf("      name = %s\n", json_object_get_string(key));
            log_printf("   address = %s\n", addbuf);
            log_printf(" directory = %s\n", dirbuf);
            log_printf("       max = %d\n", max);
            log_printf("      type = %s\n", typebuf);
          }
        }
      }
      else if (strncmp(conf_key_buff, "/fossology/fossology", 20) == 0)
      {
        json_object_object_get_ex(node, "nodes", &nodes_conf);
        conf_nodes_len = json_object_array_length(nodes_conf);
        for (j = 0; j < conf_nodes_len; j++)
        {
          node_conf = json_object_array_get_idx(nodes_conf, j);
          json_object_object_get_ex(node_conf, "key", &key);
          strcpy(conf_key_buff, json_object_get_string(key));
          /* load the port setting */
          if (strncmp(conf_key_buff, "/fossology/fossology/port", 25) == 0)
          {
            json_object_object_get_ex(node_conf, "value", &value);
            NOTIFY("key: %s", json_object_get_string(key));
            NOTIFY("value: %s", json_object_get_string(value));
            if (scheduler->i_port == 0)
              scheduler->i_port = json_object_get_int(value);
          }
        }
      }
      else if (strncmp(conf_key_buff, "/fossology/directories", 22) == 0)
      {
        json_object_object_get_ex(node, "nodes", &nodes_conf);
        conf_nodes_len = json_object_array_length(nodes_conf);
        for (j = 0; j < conf_nodes_len; j++)
        {
          node_conf = json_object_array_get_idx(nodes_conf, j);
          json_object_object_get_ex(node_conf, "key", &key);
          strcpy(conf_key_buff, json_object_get_string(key));
          /* load the log directory */
          if (!scheduler->logcmdline && strncmp(conf_key_buff, "/fossology/directories/logdir", 29) == 0)
          {
            json_object_object_get_ex(node_conf, "value", &value);
            NOTIFY("key: %s", json_object_get_string(key));
            NOTIFY("value: %s", json_object_get_string(value));
            scheduler->logdir = (char *)json_object_get_string(value);
            scheduler->main_log = log_new(scheduler->logdir, NULL, scheduler->s_pid);
            if (main_log)
            {
              log_destroy(main_log);
              main_log = scheduler->main_log;
            }
          }
        }
      }
    }
  }



  /*
  * This will create the load and the print command for the special
  * configuration variables. This uses the l_op operation to load the variable
  * from the file and the w_op variable to write the variable to the log file.
  *
  * example:
  *   if this is in the CONF_VARIABLES_TYPES():
  *
  *     apply(char*, test_variable, NOOP, %s, "hello")
  *
  *   this is generated:
  *
  *     if(fo_config_has_key(sysconfig, "SCHEDULER", "test_variable")
  *       CONF_test_variable = fo_config_get(sysconfig, "SCHEDULER",
  *           "test_variable", NULL);
  *     V_SPECIAL("CONFIG: %s == %s\n", "test_variable", CONF_test_variable);
  *
  */
  #define SELECT_CONF_INIT(type, name, l_op, w_op, val)                                  \
    if (fo_config_has_key(scheduler->sysconfig, "SCHEDULER", #name))                     \
      CONF_##name = l_op(fo_config_get(scheduler->sysconfig, "SCHEDULER", #name, NULL)); \
    V_SPECIAL("CONFIG: %s == " MK_STRING_LIT(w_op) "\n", #name, CONF_##name);
  CONF_VARIABLES_TYPES(SELECT_CONF_INIT)
  #undef SELECT_CONF_INIT
}

/**
 * @brief Daemonizes the scheduler
 *
 * This will make sure that the pid that is maintained in the scheduler struct
 * is correct during the daemonizing process.
 *
 * @param scheduler  the scheduler_t struct
 * @return  if the daemonizing was successful.
 */
int scheduler_daemonize(scheduler_t* scheduler)
{
	int ret = 0;

	/* daemonize the process */
	if((ret = daemon(0, 0)) != 0)
	  return ret;

	scheduler->s_pid = getpid();
	return ret;
}

/**
 * @brief Load both the fossology configuration and all the agent configurations
 *
 * @param scheduler  the scheduler to load the configuration for
 * @param unused     this can be called as an event
 */
void scheduler_config_event(scheduler_t* scheduler, void* unused)
{
  if(scheduler->sysconfig)
    scheduler_clear_config(scheduler);

  scheduler_foss_config(scheduler);
  scheduler_agent_config(scheduler);

  database_init(scheduler);
  email_init(scheduler);
}

/**
 * @brief Sets the closing flag and possibly kills all currently running agents
 *
 * This function will cause the scheduler to slowly shutdown. If killed is true
 * this is a quick, ungraceful shutdown.
 *
 * @param scheduler  the scheduler
 * @param killed     should the scheduler kill all currently executing agents
 *                   before exiting the event loop, or should it wait for them
 *                   to finished first.
 */
void scheduler_close_event(scheduler_t* scheduler, void* killed)
{
  closing = 1;

  if(killed) {
    kill_agents(scheduler);
  }
}

/**
 * @brief Event used when the scheduler tests the agents
 *
 * @param scheduler  the scheduler struct
 * @param unused
 */
void scheduler_test_agents(scheduler_t* scheduler, void* unused)
{
  scheduler->s_startup = TRUE;
  test_agents(scheduler);
}

/**
 * @brief Checks if a string is entirely composed of numeric characters
 *
 * @param str the string to test
 * @return TRUE if the string is entirely numeric, FALSE otherwise
 */
gint string_is_num(gchar* str)
{
  int len = strlen(str);
  int i;

  for(i = 0; i < len; i++)
    if(!isdigit(str[i]))
      return FALSE;
  return TRUE;
}

/**
 * Utility function that enables the use of the strcmp function with a GTree.
 *
 * @param a The first string
 * @param b The second string
 * @param user_data unused in this function
 * @return Integral value indicating the relationship between the two strings
 */
gint string_compare(gconstpointer a, gconstpointer b, gpointer user_data)
{
  return strcmp((char*)a, (char*)b);
}

/**
 * Utility function that enable the agents to be stored in a GTree using
 * the PID of the associated process.
 *
 * @param a The pid of the first process
 * @param b The pid of the second process
 * @param user_data unused in this function
 * @return integral value idicating the relationship between the two pids
 */
gint int_compare(gconstpointer a, gconstpointer b, gpointer user_data)
{
  return *(int*)a - *(int*)b;
}

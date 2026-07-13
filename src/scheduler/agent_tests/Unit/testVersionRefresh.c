/*
 SPDX-FileCopyrightText: © Fossology contributors

 SPDX-License-Identifier: GPL-2.0-only
*/
/**
 * \file
 * \brief Unit tests for the per-agent-type refresh feature
 *
 * These exercise the public entry point of the new behavior,
 * agent_type_refresh_event(), which replaced the old build-wide version refresh
 * that restarted every agent. The key guarantees under test:
 *   - only agents of the named type are refreshed,
 *   - only AG_SPAWNED agents are refreshed (AG_RUNNING agents keep working so
 *     their in-progress data / pfile reuse is preserved),
 *   - a refreshed agent gets return_code 0 so the death event respawns it
 *     cleanly rather than failing its job,
 *   - refreshing a type with no eligible agents is a safe no-op.
 *
 * Each fake agent is backed by a real child process placed in its own process
 * group, so the kill(-pid) issued by the refresh is both valid and isolated to
 * that child.
 */

/* include functions to test */
#include <testRun.h>

/* scheduler includes */
#include <agent.h>
#include <host.h>
#include <job.h>
#include <scheduler.h>

/* unix includes */
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/** return_code value used to mark an agent that must NOT be refreshed */
#define UNTOUCHED_RC 7

/* ************************************************************************** */
/* **** helpers ************************************************************* */
/* ************************************************************************** */

/**
 * \brief Fork a child in its own process group that just waits to be killed.
 * \return the child's pid (also its process-group id)
 */
static pid_t spawn_dummy_child(void)
{
  pid_t p = fork();
  if(p == 0)
  {
    setpgid(0, 0);        /* own process group so kill(-pid) is isolated */
    for(;;) pause();      /* wait for the SIGKILL from the refresh */
    _exit(0);
  }
  setpgid(p, p);          /* mirror in parent to close the fork/exec race */
  return p;
}

/**
 * \brief Build a minimal but valid agent_t and insert it into scheduler->agents.
 *
 * owner is left NULL on purpose: agent_type_refresh_event()/type_refresh_kill()
 * never dereference it, and this keeps the fixture free of job bookkeeping.
 */
static agent_t* make_fake_agent(scheduler_t* scheduler, meta_agent_t* ma,
    host_t* host, const char* type, const char* hostname,
    agent_status status, pid_t pid)
{
  agent_t* a = g_new0(agent_t, 1);
  a->type   = ma;
  a->host   = host;
  a->status = status;
  a->pid    = pid;
  a->return_code = UNTOUCHED_RC;
  a->accounted   = TRUE;
  a->owner  = NULL;
  a->read = NULL; a->write = NULL;
  a->from_parent = -1; a->to_child = -1; a->from_child = -1; a->to_parent = -1;
  g_strlcpy(a->type_name, type,     sizeof(a->type_name));
  g_strlcpy(a->host_name, hostname, sizeof(a->host_name));
  g_tree_insert(scheduler->agents, &a->pid, a);
  return a;
}

/** \brief Reap a child, killing its group first if still alive. */
static int reap(pid_t pid)
{
  int status = 0;
  kill(-pid, SIGKILL);
  waitpid(pid, &status, 0);
  return status;
}

/* ************************************************************************** */
/* **** tests *************************************************************** */
/* ************************************************************************** */

/**
 * \brief Only AG_SPAWNED agents of the named type are refreshed.
 * \test
 * -# Create nomos {SPAWNED, RUNNING} and copyright {SPAWNED} agents.
 * -# Fire agent_type_refresh_event() for "nomos".
 * -# The nomos SPAWNED agent is refreshed (return_code 0) and its child killed.
 * -# The nomos RUNNING agent and the copyright agent are untouched.
 */
void test_refresh_only_changed_type(void)
{
  scheduler_t* scheduler = scheduler_init(testdb, NULL);
  scheduler_foss_config(scheduler);

  meta_agent_t* nomos = meta_agent_init("nomos",     "nomos",     1, 0);
  meta_agent_t* copyr = meta_agent_init("copyright", "copyright", 1, 0);
  g_tree_insert(scheduler->meta_agents, nomos->name, nomos);
  g_tree_insert(scheduler->meta_agents, copyr->name, copyr);

  host_t* host = g_tree_lookup(scheduler->host_list, LOCAL_HOST);
  if(host == NULL)
  {
    host = host_init(LOCAL_HOST, LOCAL_HOST, "/tmp", 10);
    host_insert(host, scheduler);
  }

  pid_t p_ns = spawn_dummy_child();
  pid_t p_nr = spawn_dummy_child();
  pid_t p_cs = spawn_dummy_child();

  agent_t* n_spawn   = make_fake_agent(scheduler, nomos, host, "nomos",     LOCAL_HOST, AG_SPAWNED, p_ns);
  agent_t* n_running = make_fake_agent(scheduler, nomos, host, "nomos",     LOCAL_HOST, AG_RUNNING, p_nr);
  agent_t* c_spawn   = make_fake_agent(scheduler, copyr, host, "copyright", LOCAL_HOST, AG_SPAWNED, p_cs);

  /* the event takes ownership of the g_strdup'd name */
  agent_type_refresh_event(scheduler, g_strdup("nomos"));

  /* only the spawned nomos agent was refreshed */
  FO_ASSERT_EQUAL(n_spawn->return_code,   0);
  FO_ASSERT_EQUAL(n_running->return_code, UNTOUCHED_RC);
  FO_ASSERT_EQUAL(c_spawn->return_code,   UNTOUCHED_RC);

  /* its child must have been SIGKILLed by the refresh */
  int status = 0;
  FO_ASSERT_EQUAL(waitpid(p_ns, &status, 0), p_ns);
  FO_ASSERT_TRUE(WIFSIGNALED(status));

  /* the other two must still be alive; clean them up */
  reap(p_nr);
  reap(p_cs);

  scheduler_destroy(scheduler);
}

/**
 * \brief Refreshing a type that has only RUNNING agents changes nothing.
 * \test
 * -# Create a single nomos RUNNING agent.
 * -# Fire agent_type_refresh_event() for "nomos".
 * -# The agent is untouched and its child survives.
 */
void test_refresh_skips_running_agents(void)
{
  scheduler_t* scheduler = scheduler_init(testdb, NULL);
  scheduler_foss_config(scheduler);

  meta_agent_t* nomos = meta_agent_init("nomos", "nomos", 1, 0);
  g_tree_insert(scheduler->meta_agents, nomos->name, nomos);

  host_t* host = g_tree_lookup(scheduler->host_list, LOCAL_HOST);
  if(host == NULL)
  {
    host = host_init(LOCAL_HOST, LOCAL_HOST, "/tmp", 10);
    host_insert(host, scheduler);
  }

  pid_t p = spawn_dummy_child();
  agent_t* running = make_fake_agent(scheduler, nomos, host, "nomos", LOCAL_HOST, AG_RUNNING, p);

  agent_type_refresh_event(scheduler, g_strdup("nomos"));

  FO_ASSERT_EQUAL(running->return_code, UNTOUCHED_RC);

  /* child should still be alive (waitpid WNOHANG returns 0) */
  int status = 0;
  FO_ASSERT_EQUAL(waitpid(p, &status, WNOHANG), 0);

  reap(p);
  scheduler_destroy(scheduler);
}

/**
 * \brief Refreshing an unknown type name is a safe no-op.
 * \test
 * -# Create a nomos SPAWNED agent.
 * -# Fire agent_type_refresh_event() for a type name that does not exist.
 * -# Nothing is refreshed and the child survives.
 */
void test_refresh_unknown_type_is_noop(void)
{
  scheduler_t* scheduler = scheduler_init(testdb, NULL);
  scheduler_foss_config(scheduler);

  meta_agent_t* nomos = meta_agent_init("nomos", "nomos", 1, 0);
  g_tree_insert(scheduler->meta_agents, nomos->name, nomos);

  host_t* host = g_tree_lookup(scheduler->host_list, LOCAL_HOST);
  if(host == NULL)
  {
    host = host_init(LOCAL_HOST, LOCAL_HOST, "/tmp", 10);
    host_insert(host, scheduler);
  }

  pid_t p = spawn_dummy_child();
  agent_t* a = make_fake_agent(scheduler, nomos, host, "nomos", LOCAL_HOST, AG_SPAWNED, p);

  agent_type_refresh_event(scheduler, g_strdup("does-not-exist"));

  FO_ASSERT_EQUAL(a->return_code, UNTOUCHED_RC);

  int status = 0;
  FO_ASSERT_EQUAL(waitpid(p, &status, WNOHANG), 0);

  reap(p);
  scheduler_destroy(scheduler);
}

/**
 * \brief The stable type_name copy matches even if agent->type is repointed.
 * \test
 * -# Create a nomos SPAWNED agent, then swap agent->type to a different struct
 *    (as a reload's re-point would) while keeping type_name = "nomos".
 * -# Fire agent_type_refresh_event("nomos").
 * -# The agent is still matched via type_name and refreshed.
 */
void test_refresh_matches_on_stable_name(void)
{
  scheduler_t* scheduler = scheduler_init(testdb, NULL);
  scheduler_foss_config(scheduler);

  meta_agent_t* nomos_a = meta_agent_init("nomos", "nomos", 1, 0);
  meta_agent_t* nomos_b = meta_agent_init("nomos", "nomos", 1, 0); /* "rebuilt" struct */
  g_tree_insert(scheduler->meta_agents, nomos_b->name, nomos_b);

  host_t* host = g_tree_lookup(scheduler->host_list, LOCAL_HOST);
  if(host == NULL)
  {
    host = host_init(LOCAL_HOST, LOCAL_HOST, "/tmp", 10);
    host_insert(host, scheduler);
  }

  pid_t p = spawn_dummy_child();
  agent_t* a = make_fake_agent(scheduler, nomos_a, host, "nomos", LOCAL_HOST, AG_SPAWNED, p);
  a->type = nomos_b;  /* simulate reload re-point; type_name stays "nomos" */

  agent_type_refresh_event(scheduler, g_strdup("nomos"));

  FO_ASSERT_EQUAL(a->return_code, 0);
  int status = 0;
  FO_ASSERT_EQUAL(waitpid(p, &status, 0), p);
  FO_ASSERT_TRUE(WIFSIGNALED(status));

  meta_agent_destroy(nomos_a); /* not inserted in the tree, free by hand */
  scheduler_destroy(scheduler);
}

/* ************************************************************************** */
/* **** suite table ********************************************************* */
/* ************************************************************************** */

CU_TestInfo tests_version_refresh[] =
{
    {"Test refresh only changed type",     test_refresh_only_changed_type    },
    {"Test refresh skips running agents",  test_refresh_skips_running_agents },
    {"Test refresh unknown type no-op",    test_refresh_unknown_type_is_noop },
    {"Test refresh matches stable name",   test_refresh_matches_on_stable_name},
    CU_TEST_INFO_NULL
};

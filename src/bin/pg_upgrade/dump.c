/*
 *	dump.c
 *
 *	dump functions
 *
 *	Copyright (c) 2010-2016, PostgreSQL Global Development Group
 *	src/bin/pg_upgrade/dump.c
 */

#include "postgres_fe.h"

#include "pg_upgrade.h"

#include <sys/types.h>
#include "fe_utils/string_utils.h"


void
generate_old_dump(void)
{
	int			dbnum;
	mode_t		old_umask;

	prep_status("Creating dump of global objects");

	/* run new pg_dumpall binary for globals */
	exec_prog(UTILITY_LOG_FILE, NULL, true,
			  "\"%s/pg_dumpall\" %s --globals-only --quote-all-identifiers "
			  "--binary-upgrade %s -f %s",
			  new_cluster.bindir, cluster_conn_opts(&old_cluster),
			  log_opts.verbose ? "--verbose" : "",
			  GLOBALS_DUMP_FILE);
	check_ok();

	prep_status("Creating dump of database schemas\n");

	/*
	 * Set umask for this function, all functions it calls, and all
	 * subprocesses/threads it creates.  We can't use fopen_priv() as Windows
	 * uses threads and umask is process-global.
	 */
	old_umask = umask(S_IRWXG | S_IRWXO);

	/* create per-db dump files */
	for (dbnum = 0; dbnum < old_cluster.dbarr.ndbs; dbnum++)
	{
		char		sql_file_name[MAXPGPATH],
					log_file_name[MAXPGPATH];
		DbInfo	   *old_db = &old_cluster.dbarr.dbs[dbnum];
		PQExpBufferData connstr,
					escaped_connstr;

		initPQExpBuffer(&connstr);
		appendPQExpBuffer(&connstr, "dbname=");
		appendConnStrVal(&connstr, old_db->db_name);
		initPQExpBuffer(&escaped_connstr);
		appendShellString(&escaped_connstr, connstr.data);
		termPQExpBuffer(&connstr);

		pg_log(PG_STATUS, "%s", old_db->db_name);
		snprintf(sql_file_name, sizeof(sql_file_name), DB_DUMP_FILE_MASK, old_db->db_oid);
		snprintf(log_file_name, sizeof(log_file_name), DB_DUMP_LOG_FILE_MASK, old_db->db_oid);

		parallel_exec_prog(log_file_name, NULL,
				   "\"%s/pg_dump\" %s --schema-only --quote-all-identifiers "
					  "--binary-upgrade --format=custom %s --file=\"%s\" %s",
						 new_cluster.bindir, cluster_conn_opts(&old_cluster),
						   log_opts.verbose ? "--verbose" : "",
						   sql_file_name, escaped_connstr.data);

		termPQExpBuffer(&escaped_connstr);
	}

	/* reap all children */
	while (reap_child(true) == true)
		;

	umask(old_umask);

	end_progress_output();
	check_ok();
}

#ifndef FORK_PARENT_H_
#define FORK_PARENT_H_

/* example use:
 * 
 * fork_parent();
 *
 * perform setup routines. write errors to stderr 
 *
 * ...
 *
 * write_pid_file();
 * chdir("/");
 *
 * ...
 * 
 * fflush(stderr);
 * close(STDERR_FILENO);
 *
 * optionally: 
 * point STDERR_FILENO at /dev/null or a logfile
 *
 */

/* returns zero on success, nonzero otherwise */
int fork_parent();

#endif

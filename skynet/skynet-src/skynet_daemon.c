#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>

#include "skynet_daemon.h"

static int
check_pid(const char *pidfile) {
	int pid = 0;

  // open process id file, if fail just return 0
	FILE *f = fopen(pidfile,"r");
	if (f == NULL)
		return 0;

  // read process id from the file and then close file
	int n = fscanf(f,"%d", &pid);
	fclose(f);

  // if read fail or read process id is 0 or is equal to current process id, return 0
	if (n !=1 || pid == 0 || pid == getpid()) {
		return 0;
	}
  
  // int kill(pid_t pid, int sig):
  // 1. if `sig` is 0 (the null signal), error checking is performed but no signal is actually sent.
  // 2. the null signal can be used to check the validity of `pid`.
  // 3. this function will return 0 if success
  // 4. ESRCH: no process or process group can be found corresponding to that specified by `pid`
  
  // check whether `pid` is valid or not, if not valid then return 0
	if (kill(pid, 0) && errno == ESRCH)
		return 0;

  // if `pid` is actully valid then return this `pid` back.
	return pid;
}

static int 
write_pid(const char *pidfile) {
	FILE *f;
	int pid = 0;
	int fd = open(pidfile, O_RDWR|O_CREAT, 0644);
	if (fd == -1) {
		fprintf(stderr, "Can't create %s.\n", pidfile);
		return 0;
	}
	f = fdopen(fd, "r+");
	if (f == NULL) {
		fprintf(stderr, "Can't open %s.\n", pidfile);
		return 0;
	}

	if (flock(fd, LOCK_EX|LOCK_NB) == -1) {
		int n = fscanf(f, "%d", &pid);
		fclose(f);
		if (n != 1) {
			fprintf(stderr, "Can't lock and read pidfile.\n");
		} else {
			fprintf(stderr, "Can't lock pidfile, lock is held by pid %d.\n", pid);
		}
		return 0;
	}
	
	pid = getpid();
	if (!fprintf(f,"%d\n", pid)) {
		fprintf(stderr, "Can't write pid.\n");
		close(fd);
		return 0;
	}
	fflush(f);

	return pid;
}

int
daemon_init(const char *pidfile) {
  // check stored process id is valid or not
  // if it is valid then indicate the process is already running, just return
	int pid = check_pid(pidfile);
	if (pid) {
		fprintf(stderr, "Skynet is already running, pid = %d.\n", pid);
		return 1;
	}

#ifdef __APPLE__
	fprintf(stderr, "'daemon' is deprecated: first deprecated in OS X 10.5 , use launchd instead.\n");
#else
  // int daemon(int nochdir, int noclose):
  // 1. this function is for programs wishing to detach themselves form the controlling terminal
  // and run in the background as system daemons.
  // 2. if `nochdir` is 0, it changes the calling process's current working directory
  // to the root directory (/); otherwise, the current working directory is left unchanged
  // 3. if `noclose` is 0, it redirects stdin stdout stderr to `/dev/null`, 
  // otherwise, no changes are made to these file descriptors
  
  // daemonize and keep current working directory and redirect standand stream to `/dev/null`.
	if (daemon(1,0)) {
		fprintf(stderr, "Can't daemonize.\n");
		return 1;
	}
#endif

	pid = write_pid(pidfile);
	if (pid == 0) {
		return 1;
	}

	return 0;
}

int 
daemon_exit(const char *pidfile) {
	return unlink(pidfile);
}

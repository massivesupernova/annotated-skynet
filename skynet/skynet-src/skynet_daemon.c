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
  // will fail for the first time if this file is not exist
	FILE *f = fopen(pidfile,"r");
	if (f == NULL)
		return 0;

  // read process id from the file and then close file
	int n = fscanf(f,"%d", &pid);
	fclose(f);
  
  // if "read fail" or "read process id is 0" or "is equal to current process id", return 0
  // for the first time, it will be read fail because of no content in the file
	if (n !=1 || pid == 0 || pid == getpid()) {
		return 0;
	}
  
  // int kill(pid_t pid, int sig):
  // 0. this function shall send a signal to a process or a group of processes specified by `pid`.
  // 1. if `sig` is 0 (the null signal), error checking is performed but no signal is actually sent.
  // 2. the null signal can be used to check the validity of `pid`.
  // 3. this function will return 0 if success
  // 4. ESRCH: no process or process group can be found corresponding to that specified by `pid`
  
  // check whether `pid` is valid or not, if invalid then return 0
	if (kill(pid, 0) && errno == ESRCH)
		return 0;

  // if `pid` is actully valid then return this `pid` back.
	return pid;
}

static int 
write_pid(const char *pidfile) {
	FILE *f;
	int pid = 0;

  // int open(const char* file, int oflag, ...);
  // 0. this function shall establish the connection between a file and a file descriptor
  // 1. it shall return a file descriptor for the named file that 
  // is lowest file descriptor not currently open for that process

  // get file descriptor for the pidfile
  // open for read and write, and if the file is not exist creat it
  // the open mode is 0644: `6` for user read and write; `44` for group and others read
	int fd = open(pidfile, O_RDWR|O_CREAT, 0644);
	if (fd == -1) {
		fprintf(stderr, "Can't create %s.\n", pidfile);
		return 0;
	}

  // FILE* fdopen(int fd, const char* mode);
  // open file with file descriptor, mode is read and write
	f = fdopen(fd, "r+");
	if (f == NULL) {
		fprintf(stderr, "Can't open %s.\n", pidfile);
		return 0;
	}

  // <sys/file.h> int flock(int fd, int operation);
  // 1. LOCK_EX: exclusive lock, one file can only have one exclusive lock
  // 2. LOCK_NB: not block, if the lock operation cannot be executed, return immediately
  // 3. return 0 for success else return -1 for error
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
  
  // get current process id and print to the pid file
  // flush it and return (it is not closed)
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
  
  // daemonize current process: daemon or service is a background process that is designed to run auto without ui
  // and keep current working directory and redirect standand stream to `/dev/null`.
	if (daemon(1,0)) {
		fprintf(stderr, "Can't daemonize.\n");
		return 1;
	}
#endif

  // create the pid file if not exist
  // lock this pid file
  // get current process id and write to this file
  // note that the pid file is not closed
	pid = write_pid(pidfile);
	if (pid == 0) {
		return 1;
	}

	return 0;
}

int 
daemon_exit(const char *pidfile) {
  // <unistd.h> int unlink(const char* file);
  // 1. this function shall remove a link to a file.
  // 2. if `file` is a symbolic link, just remote this symbolic link and 
  // shall not affect any file or directory named by the contents of the symbolic link.
  // 3. otherwise, it shall remove the link of the file and shall decrement the link count of the file
  // 4. when the file's link count becomes 0 and no process has this file open,
  // the space occupied by the file shall be freed and the file shall no longer be accessible.
	return unlink(pidfile);
}

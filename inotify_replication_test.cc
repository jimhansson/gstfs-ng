// -*- tab-width: 4; c-basic-offset: 4; -*-
/**
 * Test-cases for the inotify replication functions
 */

#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/inotify.h>

/**
 * this black box test should only be a driver and use a real gstfs
 * process, so we will set it up here in this fixture
 */
class black_box_inotify_test : public ::testing::Test {
private:
	/**
	 * holder for a subprocess that will be terminated if this object is
	 * destructed.
	 */
	struct ProcessHolder {
    
		ProcessHolder() : pid(0) {
		}

		/**
		 * uses fork
		 */
		int start(char* what, 
				  char *const args[],
				  char *const envs[]) {
			pid_t lpid = fork();
			if (lpid == 0) {
				// child code
				if (execve(what,args,envs) == -1) {
					perror("error in child process(execve)");
				}
			} else if (lpid == -1) {
				// error in parent
				perror("could not fork child process");
			} else {
				// in parent everthing ok
				pid = lpid;
			}
		}

		virtual ~ProcessHolder() {
			if(pid != 0) {
				// kill process(send signals)
				kill(pid,SIGTERM);
			}
		}
		int pid;
	private:
		// don't allow copying of this class, we only have one pid and if
		// some temp object is created somewhere it would kill our process
		// before we want it to.
		ProcessHolder(ProcessHolder& p);
		ProcessHolder& operator=(ProcessHolder& p);
	};

public:
	virtual void SetUp() {
	}

	virtual void TearDown() {
	}

	static void SetUpTestCase() {
		inotify_fd = inotify_init();
		if(inotify_fd == -1) {
			perror("could not init inotify");
			exit(errno);
		}
	}

	static int inotify_fd;
};

int black_box_inotify_test::inotify_fd = 0;

TEST_F(black_box_inotify_test,CREATED) {
	// setup...
	//   setup test directories
	//   setup gstfs with directories
	//   register with inotify
	// test...
	//   execute trigger action (this case: create file)
	//   check we got inotify message and it is of right type.
	// cleanup...
	//   deregister with inotify
	//   kill gstfs
	//   cleanup directories
}

TEST_F(black_box_inotify_test,DELETED) {
	//
}

TEST_F(black_box_inotify_test,MODIFIED) {
	// This is so we discover download program that batches its writes
	// to files. 
}

TEST_F(black_box_inotify_test,CLOSE_WRITE) {
	// This is so we discover download program that batches its writes
	// to files. 
}

TEST_F(black_box_inotify_test,MOVED_TO) {
	// Moved to should result in a created
}

TEST_F(black_box_inotify_test,MOVED_TO_from_the_outside) {
	// Moved to should result in a created
}

TEST_F(black_box_inotify_test,MOVED_FROM) {
	// Moved from should result in a deleted
}

TEST_F(black_box_inotify_test,MOVED_FROM_to_the_outside) {
	// Moved from should result in a deleted
}




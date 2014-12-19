/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include "Child.h"
#include "SessionData.h"
#include "OlySocket.h"
#include "Logging.h"
#include "OlyUtility.h"

#define DEBUG false

extern Child* child;
extern void handleException();
int shutdownFilesystem();
static pthread_mutex_t numSessions_mutex;
static int numSessions = 0;
static OlySocket* socket = NULL;
static bool driverRunningAtStart = false;
static bool driverMountedAtStart = false;

struct cmdline_t {
	int port;
	char* sessionXML;
};

void cleanUp() {
	if (shutdownFilesystem() == -1) {
		logg->logMessage("Error shutting down gator filesystem");
	}
	delete socket;
	delete util;
	delete logg;
}

// CTRL C Signal Handler
void handler(int signum) {
	logg->logMessage("Received signal %d, gator daemon exiting", signum);

	// Case 1: both child and parent receive the signal
	if (numSessions > 0) {
		// Arbitrary sleep of 1 second to give time for the child to exit;
		// if something bad happens, continue the shutdown process regardless
		sleep(1);
	}

	// Case 2: only the parent received the signal
	if (numSessions > 0) {
		// Kill child threads - the first signal exits gracefully
		logg->logMessage("Killing process group as %d child was running when signal was received", numSessions);
		kill(0, SIGINT);

		// Give time for the child to exit
		sleep(1);

		if (numSessions > 0) {
			// The second signal force kills the child
			logg->logMessage("Force kill the child");
			kill(0, SIGINT);
			// Again, sleep for 1 second
			sleep(1);

			if (numSessions > 0) {
				// Something bad has really happened; the child is not exiting and therefore may hold the /dev/gator resource open
				printf("Unable to kill the gatord child process, thus gator.ko may still be loaded.\n");
			}
		}
	}

	cleanUp();
	exit(0);
}

// Child exit Signal Handler
void child_exit(int signum) {
	int status;
	int pid = wait(&status);
	if (pid != -1) {
		pthread_mutex_lock(&numSessions_mutex);
		numSessions--;
		pthread_mutex_unlock(&numSessions_mutex);
		logg->logMessage("Child process %d exited with status %d", pid, status);
	}
}

// retval: -1 = failure; 0 = was already mounted; 1 = successfully mounted
int mountGatorFS() {
	// If already mounted,
	if (access("/dev/gator/buffer", F_OK) == 0)
		return 0;

	// else, mount the filesystem
	mkdir("/dev/gator", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if (mount("nodev", "/dev/gator", "gatorfs", 0, NULL) != 0)
		return -1;
	else
		return 1;
}

int setupFilesystem() {
	int retval;

	// Verify root permissions
	uid_t euid = geteuid();
	if (euid) {
		logg->logError(__FILE__, __LINE__, "gatord must be launched with root privileges");
		handleException();
	}

	retval = mountGatorFS();
	if (retval == 1) {
		logg->logMessage("Driver already running at startup");
		driverRunningAtStart = true;
	} else if (retval == 0) {
		logg->logMessage("Driver already mounted at startup");
		driverRunningAtStart = driverMountedAtStart = true;
	} else {
		char command[256]; // arbitrarily large amount

		// Is the driver co-located in the same directory?
		if (util->getApplicationFullPath(command, sizeof(command)) != 0) { // allow some buffer space
			logg->logMessage("Unable to determine the full path of gatord, the cwd will be used");
		}
		strcat(command, "gator.ko");
		if (access(command, F_OK) == -1) {
			logg->logError(__FILE__, __LINE__, "Unable to locate gator.ko driver:\n  >>> gator.ko should be co-located with gatord in the same directory\n  >>> OR insmod gator.ko prior to launching gatord");
			handleException();
		}

		// Load driver
		strcpy(command, "insmod ");
		util->getApplicationFullPath(&command[7], sizeof(command) - 64); // allow some buffer space
		strcat(command, "gator.ko >/dev/null 2>&1");

		if (system(command) != 0) {
			logg->logMessage("Unable to load gator.ko driver with command: %s", command);
			logg->logError(__FILE__, __LINE__, "Unable to load (insmod) gator.ko driver:\n  >>> gator.ko must be built against the current kernel version & configuration\n  >>> See dmesg for more details");
			handleException();
		}

		if (mountGatorFS() == -1) {
			logg->logError(__FILE__, __LINE__, "Unable to mount the gator filesystem needed for profiling.");
			handleException();
		}
	}

	return 0;
}

int shutdownFilesystem() {
	if (driverMountedAtStart == false)
		umount("/dev/gator");
	if (driverRunningAtStart == false)
		if (system("rmmod gator >/dev/null 2>&1") != 0)
			return -1;

	return 0; // success
}

struct cmdline_t parseCommandLine(int argc, char** argv) {
	struct cmdline_t cmdline;
	cmdline.port = 8080;
	cmdline.sessionXML = NULL;
	int c;

	while ((c = getopt (argc, argv, "hvp:s:c:")) != -1) {
		switch(c) {
			case 'p':
				cmdline.port = strtol(optarg, NULL, 10);
				break;
			case 's':
				cmdline.sessionXML = optarg;
				break;
			case 'c':
				gSessionData->configurationXMLPath = optarg;
				break;
			case 'h':
			case '?':
				logg->logError(__FILE__, __LINE__,
					"Streamline gatord version %d. All parameters are optional:\n"
					"-p port_number\tport upon which the server listens; default is 8080\n"
					"-s session_xml\tpath and filename of a session xml used for local capture\n"
					"-c config_xml\tpath and filename of the configuration.xml to use\n"
					"-v\t\tversion information\n"
					"-h\t\tthis help page\n", PROTOCOL_VERSION);
				handleException();
				break;
			case 'v':
				logg->logError(__FILE__, __LINE__, "Streamline gatord version %d", PROTOCOL_VERSION);
				handleException();
				break;
		}
	}

	// Error checking
	if (cmdline.port != 8080 && cmdline.sessionXML != NULL) {
		logg->logError(__FILE__, __LINE__, "Only a port or a session xml can be specified, not both");
		handleException();
	}

	if (optind < argc) {
		logg->logError(__FILE__, __LINE__, "Unknown argument: %s. Use '-h' for help.", argv[optind]);
		handleException();
	}

	return cmdline;
}

// Gator data flow: collector -> collector fifo -> sender
int main(int argc, char** argv, char *envp[]) {
	gSessionData = new SessionData(); // Global data class
	logg = new Logging(DEBUG);  // Set up global thread-safe logging
	util = new OlyUtility();	// Set up global utility class

	prctl(PR_SET_NAME, (unsigned int)&"gatord-main", 0, 0, 0);
	pthread_mutex_init(&numSessions_mutex, NULL);

	signal(SIGINT, handler);
	signal(SIGTERM, handler);
	signal(SIGABRT, handler);

	// Set to high priority
	if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), -19) == -1)
		logg->logMessage("setpriority() failed");

	// Initialize session data
	gSessionData->initialize();

	// Parse the command line parameters
	struct cmdline_t cmdline = parseCommandLine(argc, argv);

	// Call before setting up the SIGCHLD handler, as system() spawns child processes
	setupFilesystem();

	// Handle child exit codes
	signal(SIGCHLD, child_exit);

	// Ignore the SIGPIPE signal so that any send to a broken socket will return an error code instead of asserting a signal
	// Handling the error at the send function call is much easier than trying to do anything intelligent in the sig handler
	signal(SIGPIPE, SIG_IGN);

	// If the command line argument is a session xml file, no need to open a socket
	if (cmdline.sessionXML) {
		child = new Child(cmdline.sessionXML);
		child->run();
		delete child;
	} else {
		socket = new OlySocket(cmdline.port, true);
		// Forever loop, can be exited via a signal or exception
		while (1) {
			logg->logMessage("Waiting on connection...");
			socket->acceptConnection();

			int pid = fork();
			if (pid < 0) {
				// Error
				logg->logError(__FILE__, __LINE__, "Fork process failed. Please power cycle the target device if this error persists.");
			} else if (pid == 0) {
				// Child
				socket->closeServerSocket();
				child = new Child(socket, numSessions + 1);
				child->run();
				delete child;
				exit(0);
			} else {
				// Parent
				socket->closeSocket();

				pthread_mutex_lock(&numSessions_mutex);
				numSessions++;
				pthread_mutex_unlock(&numSessions_mutex);

				// Maximum number of connections is 2
				int wait = 0;
				while (numSessions > 1) {
					// Throttle until one of the children exits before continuing to accept another socket connection
					logg->logMessage("%d sessions active!", numSessions);
					if (wait++ >= 10) { // Wait no more than 10 seconds
						// Kill last created child
						kill(pid, SIGALRM);
						break;
					}
					sleep(1);
				}
			}
		}
	}

	cleanUp();
	return 0;
}

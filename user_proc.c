// Rebecca Hanessian
// CS 4760
// Project 6: Memory Management
// user_proc file

#include "user_proc.h"

int shmid = -1;
int msqid = -1;
int pcbIndex = -1;
int addReq = -1;
bool writeReq = 0;
int writeChance = 30;
bool terminating = 0;
long long secDuration = 0;
long long nanoDuration = 0;
struct sharedMem *shm = NULL;
struct simClock termTime;
struct msgbufOSS msgOSS;
struct msgbufUser msgUser;

void cleanup();
void cleanTerm(int sig);
int parseArgs(int argc, char *argv[]);
int attachSharedMemory();
int connectMessageQueue();
struct simClock addClocks(struct simClock a, struct simClock b);
void calculateTerminateTime();
int timeComplete(struct simClock a, struct simClock b);
int userReceiveMsg();
int shouldTerminate();
int getAddress();
bool chooseWrite();
int chooseNextAction();
int userSendMsg();

// Main function: attach to shared resources, wait for OSS turns, and send memory requests.
int main(int argc, char *argv[]) {
	int done = 0;

	atexit(cleanup);
	signal(SIGINT, cleanTerm);
	signal(SIGTERM, cleanTerm);
	signal(SIGALRM, cleanTerm);

	if (parseArgs(argc, argv) == -1) {
		return EXIT_FAILURE;
	}

	srand((unsigned int)(time(NULL) ^ getpid()));

	if (attachSharedMemory() == -1) {
		return EXIT_FAILURE;
	}

	if (connectMessageQueue() == -1) {
		return EXIT_FAILURE;
	}

	calculateTerminateTime();

	while (!done) {
		if (userReceiveMsg() == -1) {
			return EXIT_FAILURE;
		}

		done = chooseNextAction();

		if (userSendMsg() == -1) {
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

// Function to detach from shared memory when the process exits.
void cleanup() {
	if (shm != NULL && shm != (void *)-1) {
		shmdt(shm);
		shm = NULL;
	}
}

// Function to exit cleanly when the worker receives a termination signal.
void cleanTerm(int sig) {
	(void)sig;
	exit(EXIT_FAILURE);
}

// Function to parse the worker runtime limit and PCB index passed by OSS.
int parseArgs(int argc, char *argv[]) {
	char *endptr = NULL;
	long parsedIndex = 0;

	if (argc < 4) {
		fprintf(stderr, "user_proc: expected arguments: seconds nanoseconds pcbIndex\n");
		return -1;
	}

	errno = 0;
	secDuration = strtoll(argv[1], &endptr, 10);
	if (errno != 0 || *endptr != '\0' || secDuration < 0) {
		fprintf(stderr, "user_proc: invalid seconds value %s\n", argv[1]);
		return -1;
	}

	errno = 0;
	nanoDuration = strtoll(argv[2], &endptr, 10);
	if (errno != 0 || *endptr != '\0' || nanoDuration < 0 || nanoDuration >= NANO_IN_SEC) {
		fprintf(stderr, "user_proc: invalid nanoseconds value %s\n", argv[2]);
		return -1;
	}

	errno = 0;
	parsedIndex = strtol(argv[3], &endptr, 10);
	if (errno != 0 || *endptr != '\0') {
		fprintf(stderr, "user_proc: invalid PCB index %s\n", argv[3]);
		return -1;
	}
	pcbIndex = (int)parsedIndex;

	if (pcbIndex < 0 || pcbIndex >= MAXPROC) {
		fprintf(stderr, "user_proc: invalid PCB index %d\n", pcbIndex);
		return -1;
	}

	return 0;
}

// Function to attach this worker to the shared memory segment created by OSS.
int attachSharedMemory() {
	key_t ossKey = ftok("oss.c", 'c');
	if (ossKey == -1) {
		perror("user_proc: ftok for shared memory failed");
		return -1;
	}

	shmid = shmget(ossKey, sizeof(struct sharedMem), PERMS);
	if (shmid == -1) {
		perror("user_proc: shmget failed");
		return -1;
	}

	shm = shmat(shmid, NULL, 0);
	if (shm == (void *)-1) {
		perror("user_proc: shmat failed");
		shm = NULL;
		return -1;
	}

	return 0;
}

// Function to connect this worker to the message queue created by OSS.
int connectMessageQueue() {
	key_t msgkey = ftok("msgQueue.txt", 1);
	if (msgkey == -1) {
		perror("user_proc: ftok for message queue failed");
		return -1;
	}

	msqid = msgget(msgkey, PERMS);
	if (msqid == -1) {
		perror("user_proc: msgget failed");
		return -1;
	}

	return 0;
}

// Function to add two simulated clock values and normalize nanoseconds.
struct simClock addClocks(struct simClock a, struct simClock b) {
	struct simClock result;

	result.seconds = a.seconds + b.seconds;
	result.nanoseconds = a.nanoseconds + b.nanoseconds;

	if (result.nanoseconds >= NANO_IN_SEC) {
		result.seconds += result.nanoseconds / NANO_IN_SEC;
		result.nanoseconds %= NANO_IN_SEC;
	}

	return result;
}

// Function to calculate the simulated clock time when this worker should terminate.
void calculateTerminateTime() {
	struct simClock duration;

	duration.seconds = secDuration;
	duration.nanoseconds = nanoDuration;
	termTime = addClocks(shm->ossClock, duration);
}

// Function to check whether clock a has reached or passed clock b.
int timeComplete(struct simClock a, struct simClock b) {
	if (a.seconds > b.seconds) return 1;
	if (a.seconds < b.seconds) return 0;
	return a.nanoseconds >= b.nanoseconds;
}

// Function to wait until OSS sends this worker permission to run.
int userReceiveMsg() {
	long msgType = pcbIndex + 1;

	if (msgrcv(msqid, &msgOSS, sizeof(msgOSS) - sizeof(long), msgType, 0) == -1) {
		if (errno != EINTR) {
			perror("user_proc: msgrcv from oss failed");
		}
		return -1;
	}

	return 0;
}

// Function to check whether this worker's assigned lifetime has expired.
int shouldTerminate() {
	return timeComplete(shm->ossClock, termTime);
}

// Function to generate a random logical memory address in this process's address space.
int getAddress() {
	int pageNum = rand() % TOTAL_PAGES;
	int offset = rand() % PAGE_SIZE;

	return (pageNum * PAGE_SIZE) + offset;
}

// Function to choose whether the next memory request is a write, biased toward reads.
bool chooseWrite() {
	return (rand() % 100) < writeChance;
}

// Function to decide whether to terminate or prepare the next memory request.
int chooseNextAction() {
	if (shouldTerminate()) {
		terminating = true;
		addReq = -1;
		writeReq = false;
		return 1;
	}

	terminating = false;
	addReq = getAddress();
	writeReq = chooseWrite();
	return 0;
}

// Function to send the selected action or memory request back to OSS.
int userSendMsg() {
	msgUser.mtype = USER_TO_OSS;
	msgUser.pcbIndex = pcbIndex;
	msgUser.addReq = addReq;
	msgUser.write = writeReq;
	msgUser.terminating = terminating;

	if (msgsnd(msqid, &msgUser, sizeof(msgUser) - sizeof(long), 0) == -1) {
		perror("user_proc: msgsnd to oss failed");
		return -1;
	}

	return 0;
}

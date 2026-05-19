//Rebecca Hanessian
//CS 4760
//Project 6: Memory Management
//oss file

#include "user_proc.h"

// Define options struct for command line arguments
typedef struct {
    int proc;
    int simul;
    float time;
    float inter;
    char logfile[50];
} options_t;

// Create an array of state names used for printing table
const char* stateNames[] = {
    "EMPTY",
    "RUNNING",
    "BLOCKED",
    "TERMINATED"
};

// Create global variables
int totalWorkers = 0;
int launchNumber = 0;
int activeWorkers = 0;
int shmid = -1;
struct sharedMem *shm = NULL;
int printNano = 500000000;
int printSec = 0;
options_t options;
FILE *fptr = NULL;
int totalWorkersToLaunch = 0;
struct simClock nextLaunch;
int maxSimul = 0;
struct msgbufUser msgUser;
struct msgbufOSS msgOSS;
int msqid = -1;
struct blockedReqQ blockQ;
struct frameTable frameTable;
struct pageTable pageTables[MAXPROC];
struct fifoFrameQ fifoQ;
int linesNum = 0;
const int MAXLINES = 10000;
char printMessage[500] = {0};
int logLimitAlert = 0;
int nanoInc = 10000000;
const long long MEMORY_HIT_NS = 100;
const long long DISK_DELAY_NS = 14000000;
long long timeMaxNano = 0;
long long interNano = 0;
int nextRunIndex = 0;
int terminatedWorkers = 0;
long long totalWaitSec = 0;
long long totalWaitNano = 0;
int totalRequests = 0;
int totalReads = 0;
int totalWrites = 0;
int totalPageFaults = 0;
int stopLaunching = 0;

// Function prototypes
void logLimit(const char *output);
void writeOutput(FILE *stream, const char *message);
void printBlockedQueue(FILE *stream);
void initFrameTable();
void initPageTables();
void initFIFOQueue();
int getEmptyFrame();
int pushFIFOFrame(int frameNum);
int popFIFOFrame();
void removeFIFOFrame(int frameNum);
void loadPageIntoFrame(int processNum, int pageNum, int frameNum, bool write);
void releaseProcessFrames(int processNum);
void initQueue(struct blockedReqQ *q);
int isEmpty(struct blockedReqQ *q);
int enqueue(struct blockedReqQ *q, struct blockedQEntry request);
struct blockedQEntry dequeue(struct blockedReqQ *q);
struct blockedQEntry createBlockedRequest(int processNum, int pageNum, int frameNum, bool write, long long delayNs);
struct simClock addClocks(struct simClock a, struct simClock b);
long long clockDiffNs(struct simClock start, struct simClock end);
void incClock();
void incClockBy(long long nanoseconds);
void ossSendMsg(int i);
void handle(struct msgbufUser request);
void handleMemoryHit(struct msgbufUser request, int pageNum);
void handlePageFault(struct msgbufUser request, int pageNum);
int checkBlockQ();
int allActiveProcessesBlocked();
void advanceClockToTerm();
void clearPCBslot(int i);
int getNextRun();
void runOneProcess();
long long assignTime(long long maxNs);
int getEmpty();
struct simClock getNextLaunchTime();
int timeComplete(struct simClock a, struct simClock b);
void forkUser(int i, long long secDuration, long long nanoDuration);
int chooseLaunch();
int parsePositiveInt(const char *value, const char *optionName, int *result);
int parseNonNegativeDouble(const char *value, const char *optionName, float *result);
void cleanup();
void cleanTerm(int signal);
void printUsage(const char* argmt);
void printOSSstart(pid_t osspid, FILE *stream, options_t options);
void printProcessTable(FILE *stream);
void printMemoryMap(FILE *stream);
void printMemoryLayout(FILE *stream);
void printFinalStats(FILE *stream);

// Main function
int main (int argc, char *argv[]){
// Cleanup upon termination
	atexit(cleanup);
	
// Signal variables
	signal(SIGINT, cleanTerm);
	signal(SIGALRM, cleanTerm);
	signal(SIGSEGV, cleanTerm);

// Default options if none are provided by user
    options.proc = 15;
    options.simul = 7;
    options.time = 5.7;
    options.inter = 0.2;
	
	const char *fileName = "logfile.txt";
    snprintf(options.logfile, sizeof(options.logfile), "%s", fileName);
	
	char opt;
	opterr = 0;
	
	// Getopt loop to get options values
	while ((opt = getopt (argc, argv, "hn:s:t:i:f:")) != -1)
			switch (opt) {
	            case 'h':
	                printUsage (argv[0]);
	                return (EXIT_SUCCESS);
				case 'n':
					if (!parsePositiveInt(optarg, "-n", &options.proc)) {
						printUsage(argv[0]);
						return EXIT_FAILURE;
					}
					break;
				case 's':
					if (!parsePositiveInt(optarg, "-s", &options.simul)) {
						printUsage(argv[0]);
						return EXIT_FAILURE;
					}
					break;
				case 't':
					if (!parseNonNegativeDouble(optarg, "-t", &options.time)) {
						printUsage(argv[0]);
						return EXIT_FAILURE;
					}
					break;
				case 'i':
					if (!parseNonNegativeDouble(optarg, "-i", &options.inter)) {
						printUsage(argv[0]);
						return EXIT_FAILURE;
					}
					break;
				case 'f':
					if (snprintf(options.logfile, sizeof(options.logfile), "%s", optarg) >= (int)sizeof(options.logfile)) {
						fprintf(stderr, "Invalid value for -f: logfile name is too long\n");
						return EXIT_FAILURE;
					}
					break;
				default:
					fprintf(stderr, "Invalid option %s\n", argv[optind - 1]);
					printUsage (argv[0]);
					return (EXIT_FAILURE);		
			}
    
// Get oss pid and set option variables
    pid_t osspid = getpid();
    totalWorkersToLaunch = options.proc;
    maxSimul = options.simul;

	if (maxSimul > MAXPROC) {
		maxSimul = MAXPROC;
	}
	if (maxSimul < 1) {
		maxSimul = 1;
	}
    
// Seed random number
	srand((unsigned int)time(NULL));
	alarm(5);
	
// Opening logfile
	fptr = fopen(options.logfile, "w");
    if (fptr == NULL) {
        perror("Error opening logfile.\n");
        return EXIT_FAILURE;
    }
	
// Print oss starting to console and logfile
	printOSSstart(osspid, fptr, options);
	printOSSstart(osspid, stdout, options);
	
// Create shared memory
	key_t ossKey = ftok("oss.c", 'c');
	if (ossKey == -1) {
		perror("OSS: ftok for shared memory failed\n");
		exit(EXIT_FAILURE);
	}
	shmid = shmget(ossKey, sizeof(struct sharedMem), PERMS | IPC_CREAT | IPC_EXCL);
	if (shmid == -1 && errno == EEXIST) {
		int oldShmid = shmget(ossKey, sizeof(struct sharedMem), PERMS);
		if (oldShmid != -1) {	
			shmctl(oldShmid, IPC_RMID, NULL);
		}
		shmid = shmget(ossKey, sizeof(struct sharedMem), PERMS | IPC_CREAT | IPC_EXCL);
	}
	if (shmid == -1) {
		perror("OSS: shmget failed\n");
		exit(EXIT_FAILURE);
	}
	shm = shmat(shmid, NULL, 0);
	if (shm == (void*) -1) {
		perror("OSS: shmat failed");
		shm = NULL;
		exit(EXIT_FAILURE);
	}
	memset(shm, 0, sizeof(struct sharedMem));
	
	fprintf(stdout, "OSS: Shared memory set up\n");
	logLimit("OSS: Shared memory set up\n");
		
// Create message queue
	FILE *queueFile = fopen("msgQueue.txt", "a");
	if (queueFile == NULL) {
		perror("OSS: failed to create msgQueue.txt");
		exit(EXIT_FAILURE);
	}
	fclose(queueFile);
	key_t msgkey;
	
	if ((msgkey = ftok("msgQueue.txt", 1)) == -1) {
		perror("OSS: ftok in OSS failed\n");
		exit(EXIT_FAILURE);
	}
	
	msqid = msgget(msgkey, PERMS | IPC_CREAT | IPC_EXCL); 
	if (msqid == -1 && errno == EEXIST) {
		int oldMsqid = msgget(msgkey, PERMS);
		if (oldMsqid != -1) {
			msgctl(oldMsqid, IPC_RMID, NULL);
		}
		msqid = msgget(msgkey, PERMS | IPC_CREAT | IPC_EXCL);
	}
	if (msqid == -1) {
		perror("OSS: msgget in OSS failed\n");
		exit(EXIT_FAILURE);
	}
	
	fprintf(stdout, "OSS: Message queue set up\n");
	logLimit("OSS: Message queue set up\n");
	
// Initialize occupied and pid columns in process table to 0
	for (int i = 0; i < MAXPROC; i++) {
    	shm->processTable[i].occupied = 0;
    	shm->processTable[i].pid = 0;
    	shm->processTable[i].active = 0;
    	shm->processTable[i].state = EMPTY;
	}
	
// Initialize system clock to 0s 0ns
	shm->ossClock.seconds = 0;
	shm->ossClock.nanoseconds = 0;
	
	timeMaxNano = options.time * NANO_IN_SEC;
    interNano = (long long)(options.inter * 1e9);

// Initialize blocked queue
	initQueue(&blockQ);
	initFrameTable();
	initPageTables();
	initFIFOQueue();
	nextLaunch = shm->ossClock;
	
// Create simClocks to track when to print table/blocked q	
	struct simClock printTableClock;
	struct simClock printTableInc = {printSec, printNano};
	struct simClock printMapClock;
	struct simClock printMapInc = {1, 0};
	printTableClock = addClocks(shm->ossClock, printTableInc);
	printMapClock = addClocks(shm->ossClock, printMapInc);

	// Create loop to launch processes and send/receive messages
	while ((!stopLaunching && totalWorkers < totalWorkersToLaunch) || activeWorkers > 0) {
// Determine if a user process should be launched
			if (!stopLaunching && chooseLaunch()) {
				int emptySlot = getEmpty();
				if (emptySlot != -1) {
					long long burstTime = assignTime(timeMaxNano);
					long long secDuration = burstTime / NANO_IN_SEC;
    				long long nanoDuration = burstTime % NANO_IN_SEC;
					shm->processTable[emptySlot].occupied = true;
					forkUser(emptySlot, secDuration, nanoDuration);
					snprintf(printMessage, sizeof(printMessage), 
						"OSS: Generating process with PID %d at %lld:%lld\n", 
						shm->processTable[emptySlot].pid, shm->ossClock.seconds, shm->ossClock.nanoseconds);
					writeOutput(stdout, printMessage);
					writeOutput(fptr, printMessage);
					nextLaunch = getNextLaunchTime();
				}
			}

			incClock();
			checkBlockQ();

			if (allActiveProcessesBlocked()) {
				advanceClockToTerm();
				checkBlockQ();
			}

			if (activeWorkers > 0) {
				runOneProcess();
			}

			if (timeComplete(shm->ossClock, printTableClock)) {
				printProcessTable(stdout);
				printProcessTable(fptr);
				printMemoryLayout(stdout);
				printMemoryLayout(fptr);
				printTableClock = addClocks(shm->ossClock, printTableInc);
			}

			if (timeComplete(shm->ossClock, printMapClock)) {
				printMemoryMap(stdout);
				printMemoryMap(fptr);
				printMapClock = addClocks(shm->ossClock, printMapInc);
			}
		}

		printFinalStats(stdout);
		printFinalStats(fptr);
		return EXIT_SUCCESS;
	}
// Function to limit logfile to 10000 lines
void logLimit(const char *output) {
	if (fptr == NULL) {
		return;
	}
	if (output == NULL || output[0] == '\0') {
		return;
	}
	if (linesNum >= MAXLINES) {
		if (!logLimitAlert) {
			fprintf(stdout, "Maximum logfile output reached. Nothing more will be logged.\n");
			logLimitAlert = 1;
		}
		return;
	}

    size_t outputLen = strlen(output);
    int lines = 0;
    for (int i = 0; output[i]; i++) {
        if (output[i] == '\n') lines++;
    }
    if (output[outputLen - 1] != '\n') lines++;

    if (linesNum + lines <= MAXLINES) {
        fprintf(fptr, "%s", output);
        fflush(fptr);
        linesNum += lines; 
    } else {
    	if (!logLimitAlert) {
    		fprintf(stdout, "Maximum logfile output reached. Nothing more will be logged.\n");
    		logLimitAlert = 1;
    	}
    	linesNum = MAXLINES;
    }
}

// Function to direct output to correct stream utilizing logfile limit
void writeOutput(FILE *stream, const char *message) {
    if (stream == fptr) {
        logLimit(message);
    } else {
        fprintf(stream, "%s", message);
    }
}

// Function to print blocked device request queue
void printBlockedQueue(FILE *stream) {
    char message[500];

    writeOutput(stream, "OSS: Blocked queue [ ");

    if (blockQ.count == 0) {
        writeOutput(stream, " ]\n");
        return;
    }

    int ind = blockQ.front;
    for (int i = 0; i < blockQ.count; i++) {
        struct blockedQEntry req = blockQ.requests[ind];
        snprintf(message, sizeof(message), "P%d/page%d/frame%d",
            req.processNum, req.pageNum, req.frameNum);
        writeOutput(stream, message);

        if (i < blockQ.count - 1) {
            writeOutput(stream, " ");
        }

        ind = (ind + 1) % MAXPROC;
    }

    writeOutput(stream, " ]\n");
}

// Function to initialize frame table
void initFrameTable() {
	    for (int i = 0; i < TOTAL_FRAMES; i++) {
	        frameTable.frames[i].ind = i;
	        frameTable.frames[i].occupied = false;
	        frameTable.frames[i].loading = false;
	        frameTable.frames[i].dirtyBit = false;
	        frameTable.frames[i].processNum = -1;
	        frameTable.frames[i].pageNum = -1;
    }
}

// Function to initialize all process page tables
void initPageTables() {
    for (int i = 0; i < MAXPROC; i++) {
        for (int j = 0; j < TOTAL_PAGES; j++) {
            pageTables[i].pages[j].frameNum = -1;
            pageTables[i].pages[j].valid = false;
            pageTables[i].pages[j].dirtyBit = false;
        }
    }
}

// Function to initialize FIFO frame queue
void initFIFOQueue() {
    fifoQ.front = -1;
    fifoQ.back = -1;
    fifoQ.size = 0;

    for (int i = 0; i < TOTAL_FRAMES; i++) {
        fifoQ.frames[i] = -1;
    }
}

// Function to find an unused physical frame
int getEmptyFrame() {
    for (int i = 0; i < TOTAL_FRAMES; i++) {
        if (!frameTable.frames[i].occupied && !frameTable.frames[i].loading) {
            return i;
        }
    }

    return -1;
}

// Function to add a frame number to the FIFO replacement queue
int pushFIFOFrame(int frameNum) {
    if (frameNum < 0 || frameNum >= TOTAL_FRAMES || fifoQ.size >= TOTAL_FRAMES) {
        return -1;
    }

    if (fifoQ.front == -1) {
        fifoQ.front = 0;
        fifoQ.back = 0;
    } else {
        fifoQ.back = (fifoQ.back + 1) % TOTAL_FRAMES;
    }

    fifoQ.frames[fifoQ.back] = frameNum;
    fifoQ.size++;
    return 0;
}

// Function to remove the oldest frame number from the FIFO replacement queue
int popFIFOFrame() {
    int frameNum;

    if (fifoQ.front == -1 || fifoQ.size == 0) {
        return -1;
    }

    frameNum = fifoQ.frames[fifoQ.front];
    fifoQ.frames[fifoQ.front] = -1;

    if (fifoQ.front == fifoQ.back) {
        fifoQ.front = -1;
        fifoQ.back = -1;
    } else {
        fifoQ.front = (fifoQ.front + 1) % TOTAL_FRAMES;
    }

    fifoQ.size--;
    return frameNum;
}

// Function to remove a specific frame from the FIFO replacement queue
void removeFIFOFrame(int frameNum) {
    int oldFrames[TOTAL_FRAMES];
    int oldCount = fifoQ.size;
    int ind = fifoQ.front;

    for (int i = 0; i < TOTAL_FRAMES; i++) {
        oldFrames[i] = -1;
    }

    for (int i = 0; i < oldCount; i++) {
        oldFrames[i] = fifoQ.frames[ind];
        ind = (ind + 1) % TOTAL_FRAMES;
    }

    initFIFOQueue();

    for (int i = 0; i < oldCount; i++) {
        if (oldFrames[i] != frameNum && oldFrames[i] != -1) {
            pushFIFOFrame(oldFrames[i]);
        }
    }
}

// Function to load a process page into a physical frame
void loadPageIntoFrame(int processNum, int pageNum, int frameNum, bool write) {
    int oldProcess;
    int oldPage;

    if (processNum < 0 || processNum >= MAXPROC ||
        pageNum < 0 || pageNum >= TOTAL_PAGES ||
        frameNum < 0 || frameNum >= TOTAL_FRAMES) {
        return;
    }

    if (frameTable.frames[frameNum].occupied) {
        oldProcess = frameTable.frames[frameNum].processNum;
        oldPage = frameTable.frames[frameNum].pageNum;

        if (oldProcess >= 0 && oldProcess < MAXPROC &&
            oldPage >= 0 && oldPage < TOTAL_PAGES) {
            pageTables[oldProcess].pages[oldPage].frameNum = -1;
            pageTables[oldProcess].pages[oldPage].valid = false;
            pageTables[oldProcess].pages[oldPage].dirtyBit = false;
        }
    }

	    frameTable.frames[frameNum].occupied = true;
	    frameTable.frames[frameNum].loading = false;
	    frameTable.frames[frameNum].dirtyBit = write;
	    frameTable.frames[frameNum].processNum = processNum;
    frameTable.frames[frameNum].pageNum = pageNum;

    pageTables[processNum].pages[pageNum].frameNum = frameNum;
    pageTables[processNum].pages[pageNum].valid = true;
    pageTables[processNum].pages[pageNum].dirtyBit = write;

    pushFIFOFrame(frameNum);
}

// Function to release all frames owned by a process
void releaseProcessFrames(int processNum) {
    if (processNum < 0 || processNum >= MAXPROC) {
        return;
    }

    for (int i = 0; i < TOTAL_FRAMES; i++) {
	        if ((frameTable.frames[i].occupied || frameTable.frames[i].loading) &&
	            frameTable.frames[i].processNum == processNum) {
	            frameTable.frames[i].occupied = false;
	            frameTable.frames[i].loading = false;
	            frameTable.frames[i].dirtyBit = false;
	            frameTable.frames[i].processNum = -1;
            frameTable.frames[i].pageNum = -1;
            removeFIFOFrame(i);
        }
    }

    for (int i = 0; i < TOTAL_PAGES; i++) {
        pageTables[processNum].pages[i].frameNum = -1;
        pageTables[processNum].pages[i].valid = false;
        pageTables[processNum].pages[i].dirtyBit = false;
    }
}

// Function to initialize queue
void initQueue(struct blockedReqQ *q) {
	q->front = -1;
    q->back = -1;
    q->count = 0;
    for (int i = 0; i < MAXPROC; i++) {
        q->requests[i].processNum = -1;
        q->requests[i].pageNum = -1;
        q->requests[i].frameNum = -1;
        q->requests[i].write = false;
        q->requests[i].startTime.seconds = 0;
        q->requests[i].startTime.nanoseconds = 0;
        q->requests[i].finishTime.seconds = 0;
        q->requests[i].finishTime.nanoseconds = 0;
    }
}

// Check if queue is full or empty
int isEmpty(struct blockedReqQ *q) {
    return q->count == 0;
}

// Function to add a blocked request to queue
int enqueue(struct blockedReqQ *q, struct blockedQEntry request) {
    if (q->count >= MAXPROC) {
        return -1;
    }

    if (q->front == -1) {
        q->front = 0;
        q->back = 0;
    } else {
        q->back = (q->back + 1) % MAXPROC;
    }

    q->requests[q->back] = request;
    q->count++;
    return 0;
}

// Function to remove the oldest blocked request from queue
struct blockedQEntry dequeue(struct blockedReqQ *q) {
    struct blockedQEntry value;
    value.processNum = -1;
    value.pageNum = -1;
    value.frameNum = -1;
    value.write = false;
    value.startTime.seconds = 0;
    value.startTime.nanoseconds = 0;
    value.finishTime.seconds = 0;
    value.finishTime.nanoseconds = 0;

    if (q->front == -1) return value;

    value = q->requests[q->front];
    q->requests[q->front].processNum = -1;
    q->requests[q->front].pageNum = -1;
    q->requests[q->front].frameNum = -1;
    q->requests[q->front].write = false;
    q->requests[q->front].startTime.seconds = 0;
    q->requests[q->front].startTime.nanoseconds = 0;
    q->requests[q->front].finishTime.seconds = 0;
    q->requests[q->front].finishTime.nanoseconds = 0;

    if (q->front == q->back) {
        q->front = -1;
        q->back = -1;
    } else {
        q->front = (q->front + 1) % MAXPROC;
    }
    q->count--;
    return value;
}

// Function to create a blocked device request with finish time
struct blockedQEntry createBlockedRequest(int processNum, int pageNum, int frameNum, bool write, long long delayNs) {
    struct blockedQEntry request;
    struct simClock delay;

    delay.seconds = delayNs / NANO_IN_SEC;
    delay.nanoseconds = delayNs % NANO_IN_SEC;

    request.processNum = processNum;
    request.pageNum = pageNum;
    request.frameNum = frameNum;
    request.write = write;
    request.startTime = shm->ossClock;
    request.finishTime = addClocks(shm->ossClock, delay);

    return request;
}

// Function to add two clock times together
struct simClock addClocks(struct simClock a, struct simClock b) {
	struct simClock result;
	result.seconds = a.seconds + b.seconds;
	result.nanoseconds = a.nanoseconds + b.nanoseconds;
	if (result.nanoseconds >= NANO_IN_SEC) {
		result.seconds += result.nanoseconds / NANO_IN_SEC;
		result.nanoseconds = result.nanoseconds % NANO_IN_SEC;
	}
	return result;
}

// Function to get elapsed nanoseconds between two clock values
long long clockDiffNs(struct simClock start, struct simClock end) {
    long long startNs = (start.seconds * NANO_IN_SEC) + start.nanoseconds;
    long long endNs = (end.seconds * NANO_IN_SEC) + end.nanoseconds;

    if (endNs < startNs) {
        return 0;
    }

    return endNs - startNs;
}

// Function to increment clock
void incClock() {
	shm->ossClock.nanoseconds += nanoInc;
	
	if (shm->ossClock.nanoseconds >= NANO_IN_SEC) {
		(shm->ossClock.seconds)++;
		shm->ossClock.nanoseconds -= NANO_IN_SEC;
	}
}

// Function to increment clock by a specific number of nanoseconds
void incClockBy(long long nanoseconds) {
    if (nanoseconds < 0) {
        return;
    }

    shm->ossClock.nanoseconds += nanoseconds;

    if (shm->ossClock.nanoseconds >= NANO_IN_SEC) {
        shm->ossClock.seconds += shm->ossClock.nanoseconds / NANO_IN_SEC;
        shm->ossClock.nanoseconds = shm->ossClock.nanoseconds % NANO_IN_SEC;
    }
}

// Function to send message to a user process
void ossSendMsg(int i) {
	msgOSS.mtype = i + 1;
	if (msgsnd(msqid, &msgOSS, sizeof(msgOSS) - sizeof(long), 0) != 0) {
		fprintf(stderr, "OSS: msgsnd to user process %ld failed\n", msgOSS.mtype);
	}
}

// Function to handle a message received from a user process
void handle(struct msgbufUser request) {
    int ind = request.pcbIndex;

    if (ind < 0 || ind >= MAXPROC || !shm->processTable[ind].occupied) {
        return;
    }

    if (request.terminating) {
        pid_t childPid = shm->processTable[ind].pid;
        long long totalAccessTime = shm->processTable[ind].totalAccessTime;
        int memoryRequests = shm->processTable[ind].memoryRequests;
        double effectiveAccessTime = 0.0;

        if (memoryRequests > 0) {
            effectiveAccessTime = (double)totalAccessTime / (double)memoryRequests;
        }

        snprintf(printMessage, sizeof(printMessage),
            "OSS: P%d terminating at time %lld:%lld; effective memory access time %.2f ns\n",
            ind,
            shm->ossClock.seconds,
            shm->ossClock.nanoseconds,
            effectiveAccessTime);
        writeOutput(stdout, printMessage);
        writeOutput(fptr, printMessage);

        shm->processTable[ind].active = false;
        shm->processTable[ind].state = TERMINATED;
        activeWorkers--;
        terminatedWorkers++;
        releaseProcessFrames(ind);
        clearPCBslot(ind);
        if (childPid > 0) {
            waitpid(childPid, NULL, 0);
        }
        return;
    }

    shm->processTable[ind].memoryRequests++;
    totalRequests++;
    snprintf(printMessage, sizeof(printMessage),
        "OSS: P%d requesting %s of address %d at time %lld:%lld\n",
        ind,
        request.write ? "write" : "read",
        request.addReq,
        shm->ossClock.seconds,
        shm->ossClock.nanoseconds);
    writeOutput(stdout, printMessage);
    writeOutput(fptr, printMessage);

    int pageNum = request.addReq / PAGE_SIZE;
    if (pageNum < 0 || pageNum >= TOTAL_PAGES) {
        return;
    }

    if (pageTables[ind].pages[pageNum].valid) {
        handleMemoryHit(request, pageNum);
    } else {
        handlePageFault(request, pageNum);
    }
}

// Function to handle a memory request when the page is already loaded
void handleMemoryHit(struct msgbufUser request, int pageNum) {
    int ind = request.pcbIndex;
    int frameNum = pageTables[ind].pages[pageNum].frameNum;

    if (request.write) {
        totalWrites++;
        frameTable.frames[frameNum].dirtyBit = true;
        pageTables[ind].pages[pageNum].dirtyBit = true;
    } else {
        totalReads++;
    }

    incClockBy(MEMORY_HIT_NS);
    shm->processTable[ind].totalAccessTime += MEMORY_HIT_NS;

    snprintf(printMessage, sizeof(printMessage),
        "OSS: Address %d in frame %d, %s data for P%d at time %lld:%lld\n",
        request.addReq,
        frameNum,
        request.write ? "writing" : "giving",
        ind,
        shm->ossClock.seconds,
        shm->ossClock.nanoseconds);
    writeOutput(stdout, printMessage);
    writeOutput(fptr, printMessage);
}

// Function to handle a memory request that causes a page fault
void handlePageFault(struct msgbufUser request, int pageNum) {
    int ind = request.pcbIndex;
    int frameNum = getEmptyFrame();
    int oldProcess;
    int oldPage;
    long long delay = DISK_DELAY_NS;

    totalPageFaults++;
    if (request.write) {
        totalWrites++;
    } else {
        totalReads++;
    }

    shm->processTable[ind].pageFaults++;
    shm->processTable[ind].state = BLOCKED;

    if (frameNum == -1) {
        frameNum = popFIFOFrame();
        if (frameNum != -1 && frameTable.frames[frameNum].dirtyBit) {
            delay += DISK_DELAY_NS;
            snprintf(printMessage, sizeof(printMessage),
                "OSS: Dirty bit of frame %d set, adding additional disk delay\n",
                frameNum);
            writeOutput(stdout, printMessage);
            writeOutput(fptr, printMessage);
        }
    }

    if (frameNum == -1) {
        return;
    }

    if (frameTable.frames[frameNum].occupied) {
        oldProcess = frameTable.frames[frameNum].processNum;
        oldPage = frameTable.frames[frameNum].pageNum;

        if (oldProcess >= 0 && oldProcess < MAXPROC &&
            oldPage >= 0 && oldPage < TOTAL_PAGES) {
            pageTables[oldProcess].pages[oldPage].frameNum = -1;
            pageTables[oldProcess].pages[oldPage].valid = false;
            pageTables[oldProcess].pages[oldPage].dirtyBit = false;
        }
    }

    frameTable.frames[frameNum].occupied = false;
    frameTable.frames[frameNum].loading = true;
    frameTable.frames[frameNum].dirtyBit = false;
    frameTable.frames[frameNum].processNum = ind;
    frameTable.frames[frameNum].pageNum = pageNum;

    struct blockedQEntry blockedRequest = createBlockedRequest(
        ind, pageNum, frameNum, request.write, delay);

    shm->processTable[ind].eventStartSec = blockedRequest.startTime.seconds;
    shm->processTable[ind].eventStartNano = blockedRequest.startTime.nanoseconds;
    shm->processTable[ind].eventEndSec = blockedRequest.finishTime.seconds;
    shm->processTable[ind].eventEndNano = blockedRequest.finishTime.nanoseconds;

    if (enqueue(&blockQ, blockedRequest) == -1) {
        frameTable.frames[frameNum].occupied = false;
        frameTable.frames[frameNum].loading = false;
        frameTable.frames[frameNum].dirtyBit = false;
        frameTable.frames[frameNum].processNum = -1;
        frameTable.frames[frameNum].pageNum = -1;
        shm->processTable[ind].state = RUNNING;
        shm->processTable[ind].eventStartSec = 0;
        shm->processTable[ind].eventStartNano = 0;
        shm->processTable[ind].eventEndSec = 0;
        shm->processTable[ind].eventEndNano = 0;
        snprintf(printMessage, sizeof(printMessage),
            "OSS: Blocked queue full; could not queue page fault for P%d page %d\n",
            ind,
            pageNum);
        writeOutput(stdout, printMessage);
        writeOutput(fptr, printMessage);
        return;
    }

    snprintf(printMessage, sizeof(printMessage),
        "OSS: Address %d is not in a frame, pagefault; queued P%d page %d for frame %d until %lld:%lld\n",
        request.addReq,
        ind,
        pageNum,
        frameNum,
        blockedRequest.finishTime.seconds,
        blockedRequest.finishTime.nanoseconds);
    writeOutput(stdout, printMessage);
    writeOutput(fptr, printMessage);
}

// Function to complete the blocked request at the head of the device queue
int checkBlockQ() {
    struct blockedQEntry request;
    int processNum;
    long long accessTime;

    if (blockQ.count == 0 || blockQ.front == -1) {
        return 0;
    }

    request = blockQ.requests[blockQ.front];

    if (!timeComplete(shm->ossClock, request.finishTime)) {
        return 0;
    }

    request = dequeue(&blockQ);
    processNum = request.processNum;

    if (processNum < 0 || processNum >= MAXPROC) {
        return 0;
    }

    loadPageIntoFrame(processNum, request.pageNum, request.frameNum, request.write);
    shm->processTable[processNum].state = RUNNING;
    shm->processTable[processNum].eventStartSec = 0;
    shm->processTable[processNum].eventStartNano = 0;
    shm->processTable[processNum].eventEndSec = 0;
    shm->processTable[processNum].eventEndNano = 0;

    accessTime = clockDiffNs(request.startTime, request.finishTime);
    shm->processTable[processNum].totalAccessTime += accessTime;

    snprintf(printMessage, sizeof(printMessage),
        "OSS: Completed page fault for P%d page %d in frame %d at time %lld:%lld\n",
        processNum,
        request.pageNum,
        request.frameNum,
        shm->ossClock.seconds,
        shm->ossClock.nanoseconds);
    writeOutput(stdout, printMessage);
    writeOutput(fptr, printMessage);

    return 1;
}

// Function to check if all active processes are blocked
int allActiveProcessesBlocked() {
    int activeFound = 0;

    for (int i = 0; i < MAXPROC; i++) {
        if (shm->processTable[i].occupied && shm->processTable[i].active) {
            activeFound = 1;

            if (shm->processTable[i].state != BLOCKED) {
                return 0;
            }
        }
    }

    return activeFound;
}

// Function to advance clock to finish the head blocked request
void advanceClockToTerm() {
    struct blockedQEntry request;

    if (blockQ.count == 0 || blockQ.front == -1) {
        return;
    }

    request = blockQ.requests[blockQ.front];

    if (!timeComplete(shm->ossClock, request.finishTime)) {
        shm->ossClock = request.finishTime;
    }
}

// Function to clear a PCB slot
void clearPCBslot(int i) {
	shm->processTable[i].occupied = false;
	shm->processTable[i].launched = 0;
	shm->processTable[i].pid = 0;
    shm->processTable[i].startS = 0;
    shm->processTable[i].startN = 0;
    shm->processTable[i].eventStartSec = 0;
    shm->processTable[i].eventStartNano = 0;
    shm->processTable[i].eventEndSec = 0;
    shm->processTable[i].eventEndNano = 0;
    shm->processTable[i].state = EMPTY;
    shm->processTable[i].active = false;
    shm->processTable[i].memoryRequests = 0;			
    shm->processTable[i].pageFaults = 0;				
	shm->processTable[i].totalAccessTime = 0;
}

// Function to choose next active, unblocked process in round-robin order
int getNextRun() {
    for (int i = 0; i < MAXPROC; i++) {
        int ind = (nextRunIndex + i) % MAXPROC;

        if (shm->processTable[ind].occupied &&
            shm->processTable[ind].active &&
            shm->processTable[ind].state != BLOCKED) {
            nextRunIndex = (ind + 1) % MAXPROC;
            return ind;
        }
    }

    return -1;
}

// Function to run one unblocked process and handle its message back to OSS
void runOneProcess() {
	int ind = getNextRun();

	if (ind == -1) {
		return;
	}

	ossSendMsg(ind);

	if (msgrcv(msqid, &msgUser, sizeof(msgUser) - sizeof(long), USER_TO_OSS, 0) == -1) {
		if (errno != EINTR) {
			perror("OSS: msgrcv from user process failed");
		}
		return;
	}

	handle(msgUser);
}

// Function to assign a total run time to a process
long long assignTime(long long maxNs) {
	if (maxNs <= 0) {
		return 0;
	}

	long long randomValue = ((long long)rand() * (RAND_MAX + 1LL)) + rand();
	return (randomValue % maxNs) + 1;
}

// Function to find empty slot in process table
int getEmpty() {
	for (int i = 0; i < MAXPROC; i++) {
		if (shm->processTable[i].occupied == false) {
			return i;
		} 
	}
	for (int j = 0; j < MAXPROC; j++) {
		if (shm->processTable[j].active == false) {
			clearPCBslot(j);
			return j;
		}
	}
	return -1;
}

// Function to get next launch time
struct simClock getNextLaunchTime() {
	struct simClock intervalClock = {0, interNano};
	return addClocks(shm->ossClock, intervalClock); 
}

// Function to check if enough time has passed to launch
int timeComplete(struct simClock a, struct simClock b) {
	if (a.seconds > b.seconds) return 1;
    if (a.seconds < b.seconds) return 0;
    return a.nanoseconds >= b.nanoseconds;	
}

// Function to fork and exec a user process
void forkUser(int i, long long secDuration, long long nanoDuration) {
	pid_t pid = fork();
	if (pid == 0) {
		char sec[256], nano[256], ind[10];
		snprintf(sec, sizeof(sec), "%lld", secDuration);
		snprintf(nano, sizeof(nano), "%lld", nanoDuration);
		snprintf(ind, sizeof(ind), "%d", i);
        char *newargv[] = {"./user_proc", sec, nano, ind, NULL};
        execvp(newargv[0],newargv);
        perror("Execvp error\n");
        exit(EXIT_FAILURE);
	} else if (pid > 0) {
		totalWorkers++;
		activeWorkers++;
		launchNumber = totalWorkers;
						
		shm->processTable[i].launched = totalWorkers;
		shm->processTable[i].occupied = true;
		shm->processTable[i].pid = pid;
		shm->processTable[i].active = true;
    	shm->processTable[i].startS = shm->ossClock.seconds;
    	shm->processTable[i].startN = shm->ossClock.nanoseconds;
    	shm->processTable[i].state = RUNNING;
	} else {
		perror("OSS: Fork error\n");
		exit(EXIT_FAILURE);
	}
}

// Function to determine if oss should launch a new process
int chooseLaunch() {
	if (totalWorkers >= totalWorkersToLaunch) return 0;
	if (activeWorkers >= maxSimul) return 0;
	if (!timeComplete(shm->ossClock, nextLaunch)) return 0;
	return 1;
}

// Function to parse a positive integer command line argument
int parsePositiveInt(const char *value, const char *optionName, int *result) {
	char *end = NULL;

	if (value == NULL || *value == '\0') {
		fprintf(stderr, "Invalid value for %s\n", optionName);
		return 0;
	}

	errno = 0;
	long parsed = strtol(value, &end, 10);
	if (errno == ERANGE || *end != '\0' || parsed <= 0 || parsed > INT_MAX) {
		fprintf(stderr, "Invalid value for %s: %s\n", optionName, value);
		return 0;
	}

	*result = (int)parsed;
	return 1;
}

// Function to parse a non-negative decimal command line argument
int parseNonNegativeDouble(const char *value, const char *optionName, float *result) {
	char *end = NULL;

	if (value == NULL || *value == '\0') {
		fprintf(stderr, "Invalid value for %s\n", optionName);
		return 0;
	}

	errno = 0;
	double parsed = strtod(value, &end);
	if (errno == ERANGE || *end != '\0' || parsed < 0.0 || !isfinite(parsed)) {
		fprintf(stderr, "Invalid value for %s: %s\n", optionName, value);
		return 0;
	}

	*result = (float)parsed;
	return 1;
}

// Function to clean up at the end of program
void cleanup() {
	if (msqid != -1 && msgctl(msqid, IPC_RMID, NULL) == -1) {
		perror("OSS: msgctl to get rid of queue in parent failed\n");
	}
	msqid = -1;
	if (fptr != NULL) {
		fclose(fptr);
		fptr = NULL;
	}
	for (int i = 0; shm != NULL && shm != (void *)-1 && i < MAXPROC; i++) {
		if (shm->processTable[i].occupied == 1 && shm->processTable[i].pid > 0) {
			kill(shm->processTable[i].pid, SIGTERM);
		}
	}
	while(wait(NULL) > 0);
	if (shm != NULL && shm != (void *) -1) {
		shmdt(shm);
		shm = NULL;
	}
	if (shmid > 0) {
		shmctl(shmid, IPC_RMID, NULL);
		shmid = -1;
	}
}

// Function to catch termination signals from ctrl-C and sigalrm
void cleanTerm (int signal) {
	if (signal == SIGALRM) {
		fprintf(stderr, "\n5 seconds passed. Terminating.\n");
		if (shm != NULL && shm != (void *)-1) {
			printFinalStats(stdout);
			printFinalStats(fptr);
		}
	} else if (signal == SIGINT) {
		fprintf(stderr, "\nCtrl-C entered. Terminating.\n");
	} else if (signal == SIGSEGV) {
		fprintf(stderr, "\nSeg fault occurred. Terminating.\n");
	}
    exit(EXIT_FAILURE);
}

// Function to print -h usage message
void printUsage (const char* argmt){
	fprintf(stderr, "Usage: %s [-h] [-n proc] [-s simul] [-t timeLimitForChildren] [-i fractionOfSecondToLaunchChildren] [-f logfile]\n", argmt);
	fprintf(stderr, "	proc is the number of user processes to launch\n");
	fprintf(stderr, "	simul is the number of processes that can run simultaneously\n");
	fprintf(stderr, "	timeLimitForChildren is the maximum simulated time that should pass before child process terminates (in seconds).\n");
	fprintf(stderr, "	fractionOdSecondToLaunchChildren is the minimum interval between launching child processes.\n");
	fprintf(stderr, "	logfile is the filename to write OSS output.\n");
	fprintf(stderr, "Default proc is 15, default simul is 7, default timeLimitForChildren is 5.7,\n default fractionOfSecondToLaunchChildren is 0.2, default logfile is 'logfile.txt'.\n");
}

// Function to print start of OSS to console and logfile
void printOSSstart(pid_t osspid, FILE *stream, options_t options){
	char message[500];
	snprintf(message, sizeof(message),
		"OSS starting, PID: %d\n"
		"Called with:\n"
		"-n %d\n-s %d\n-t %f\n-i %f\n-f %s\n", 
		osspid, options.proc, options.simul, options.time, options.inter, options.logfile);
	writeOutput(stream, message);
}

// Function to print process table=
void printProcessTable(FILE *stream) {
	char message[500];
	char pcb[500];
	snprintf(message, sizeof(message),
		"OSS PID:%d  SysClock Seconds: %lld SysClock Nanoseconds: %lld\n"
		"Process Table:\n"
		"%-6s %-10s %-9s %-7s %-10s %-10s %-13s %-16s %-17s %-14s %-15s %-16s %-12s %-18s\n",
		getpid(), 
		shm->ossClock.seconds, 
		shm->ossClock.nanoseconds,
		"Entry:", 
		"Occupied:", 
		"Launch #:", 
		"PID:",
		"State:",
		"Start Sec:", 
		"Start Nano:",  
		"Event Start Sec:", 
		"Event Start Nano:",
		"Event End Sec:",
		"Event End Nano:",
		"Memory Requests:",
		"Page Faults:",
		"Total Access Time:"
	);
	writeOutput(stream, message);
	for (int i = 0; i < MAXPROC; i++) {
        snprintf(pcb, sizeof(pcb),
        	"%-6d %-10d %-9d %-7d %-10s %-10d %-13lld %-16d %-17lld %-14d %-15lld %-16d %-12d %-18lld\n",
            i + 1, 
            shm->processTable[i].occupied,
            shm->processTable[i].launched, 
            shm->processTable[i].pid, 
            stateNames[shm->processTable[i].state],
            shm->processTable[i].startS, 
            shm->processTable[i].startN,
            shm->processTable[i].eventStartSec,
           	shm->processTable[i].eventStartNano,
           	shm->processTable[i].eventEndSec,
           	shm->processTable[i].eventEndNano,
           	shm->processTable[i].memoryRequests,
           	shm->processTable[i].pageFaults,
           	shm->processTable[i].totalAccessTime
        );
        writeOutput(stream, pcb);
    }
    writeOutput(stream, "----------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	printBlockedQueue(stream);
}

// Function to print shorthand memory map
void printMemoryMap(FILE *stream) {
    char message[500];

    snprintf(message, sizeof(message),
        "Current memory map at time %lld:%lld is:\nFrames: [ ",
        shm->ossClock.seconds,
        shm->ossClock.nanoseconds);
    writeOutput(stream, message);

	    for (int i = 0; i < TOTAL_FRAMES; i++) {
	        writeOutput(stream, (frameTable.frames[i].occupied || frameTable.frames[i].loading) ? "+" : ".");
	    }
    writeOutput(stream, " ]\n");
}

// Function to print detailed frame table and page tables
void printMemoryLayout(FILE *stream) {
    char message[500];

    snprintf(message, sizeof(message),
        "Current page/frame tables at time %lld:%lld are:\n",
        shm->ossClock.seconds,
        shm->ossClock.nanoseconds);
    writeOutput(stream, message);
	    writeOutput(stream, "Frame Table:\n");
	    snprintf(message, sizeof(message),
	        "%5s %8s %8s %8s %7s %4s\n",
	        "Frame", "Occupied", "Loading", "Dirty", "Process", "Page");
	    writeOutput(stream, message);
	    for (int i = 0; i < TOTAL_FRAMES; i++) {
	        snprintf(message, sizeof(message),
	            "%5d %8s %8s %8d %7d %4d\n",
	            i,
	            frameTable.frames[i].occupied ? "Yes" : "No",
	            frameTable.frames[i].loading ? "Yes" : "No",
	            frameTable.frames[i].dirtyBit ? 1 : 0,
	            frameTable.frames[i].processNum,
            frameTable.frames[i].pageNum);
        writeOutput(stream, message);
    }

    for (int i = 0; i < MAXPROC; i++) {
        if (!shm->processTable[i].occupied) {
            continue;
        }

        snprintf(message, sizeof(message), "P%d page table: [ ", i);
        writeOutput(stream, message);

        for (int j = 0; j < TOTAL_PAGES; j++) {
            snprintf(message, sizeof(message), "%3d",
                pageTables[i].pages[j].valid ? pageTables[i].pages[j].frameNum : -1);
            writeOutput(stream, message);

            if (j < TOTAL_PAGES - 1) {
                writeOutput(stream, " ");
            }
        }

        writeOutput(stream, " ]\n");
    }
}

// Function to print final memory statistics
void printFinalStats(FILE *stream) {
    char message[500];
    double faultPercent = 0.0;

    if (totalRequests > 0) {
        faultPercent = ((double)totalPageFaults / (double)totalRequests) * 100.0;
    }

    snprintf(message, sizeof(message),
        "OSS final statistics:\n"
        "Total memory requests: %d\n"
        "Total reads: %d\n"
        "Total writes: %d\n"
        "Total page faults: %d\n"
        "Page fault percentage: %.2f%%\n",
        totalRequests,
        totalReads,
        totalWrites,
        totalPageFaults,
        faultPercent);
    writeOutput(stream, message);
}

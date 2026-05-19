//Rebecca Hanessian
//CS 4760
//Project 6
//header file

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#define MAXPROC 18
#define PERMS 0644
#define EMPTY 0
#define RUNNING 1
#define BLOCKED 2
#define TERMINATED 3
#define USER_TO_OSS 100
#define TOTAL_FRAMES 64
#define TOTAL_PAGES 16
#define PAGE_SIZE 1024
#define NANO_IN_SEC 1000000000LL

struct PCB{
	bool occupied;				// is the slot being used
	int launched;				// number order launched (if 5th then 5)
	pid_t pid;  				// pid of child
	bool active;				// is child still running 
    int startS;					// time created seconds
    long long startN;			// time created nanoseconds
    int eventStartSec;			// time process became blocked, seconds
    long long eventStartNano;	// time process became blocked, nanoseconds
    int eventEndSec;			// time process will be unblocked, seconds
    long long eventEndNano;		// time process will be unblocked, nanoseconds
    int state;					// what is state?
    int memoryRequests;			// counter of how many requests process made
    int pageFaults;				// how many page faults occurred? 
    long long totalAccessTime;	// total time 
};

struct simClock{
	long long seconds;
	long long nanoseconds;
};

struct sharedMem{
	struct simClock ossClock;
	struct PCB processTable[MAXPROC];
};

struct msgbufUser {
    long mtype;
    int pcbIndex;		// the process number/pcb table index
    int addReq;			// the requested address
    bool write;			// is it read or write?
    bool terminating;	// is it terminating?
};

struct msgbufOSS {
	long mtype;
};

struct frameTableEntry {
	int ind;			// frame number/index
	bool occupied;		// is the frame occupied
	bool loading;		// is a page fault currently loading into this frame?
	bool dirtyBit;		// has this frame been modified?
	int processNum;		// the process number (index in PCB table)
	int pageNum;		// the page number
};

struct frameTable {
	struct frameTableEntry frames[TOTAL_FRAMES];
};

struct pageTableEntry {
	int frameNum;
	bool valid;
	bool dirtyBit;
};

struct pageTable {
	struct pageTableEntry pages[TOTAL_PAGES];
};

struct fifoFrameQ {
	int frames[TOTAL_FRAMES];
	int front;
	int back;
	int size;
};

struct blockedQEntry {
	int processNum;
	int pageNum;
	int frameNum;
	bool write;				// does dirty bit need to be set?
	struct simClock startTime;
	struct simClock finishTime;
};

struct blockedReqQ {
	struct blockedQEntry requests[MAXPROC];
	int front;
	int back;
	int count;
};

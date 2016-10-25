#pragma once
#include "part.h"
#include "fs.h"

#include <Windows.h>
#include <list>
#include <queue>



#define NUM_OF_PARTITIONS 26


using namespace std;

enum state { none, format, unmount };


struct OpenFileEntry
{
	SRWLOCK srwLock;
	ClusterNo clusterNumber;
	unsigned int clusterOffset;
	char* fname;
	int32_t fileOpenerCounter;
};


struct PartitionModel
{
	Partition* partition;
	char rootClusterContent[2048];
	ClusterNo rootClusterNumber;
	CRITICAL_SECTION criticalSection;
	CONDITION_VARIABLE cvWaitForFilesToClose;
	CONDITION_VARIABLE cvWaitForFormatsAndUnmount;
	uint16_t ticket, next;
	int beingFormatted = 0;
	state waitAllFilesToClose = state::none;
	queue<state>  conditiion_queue;
	int lastEntry = -1, lastCluster = -1;
	list <OpenFileEntry> OpenFilesTable;
	char partitionName;
};


class KernelFS
{
public:
	KernelFS();
	~KernelFS();
	char mount(Partition*);
	char unmount(char);
	char readRootDir(char, EntryNum, Directory&);
	char format(char);
	
	char doesExist(char*);
	File* open(char*, char);
	char deleteFile(char*);


private:
	PartitionModel partitions[NUM_OF_PARTITIONS];
	bool used[NUM_OF_PARTITIONS];
	bool legit(char);
	bool isItBeingUnmounted(char);
protected:
	bool compareNames(Entry * entry, char * fname);
	void destroyFile(char*, ClusterNo, unsigned int);
	ClusterNo allocate(char);
	list<ClusterNo> freeClusters[NUM_OF_PARTITIONS];
	void free(char, ClusterNo);
	void reset(char);
	char bitVector[NUM_OF_PARTITIONS][ClusterSize];
	ClusterNo nextFree[NUM_OF_PARTITIONS];
	void flushBitVector(char);
	void ticket(char, state);
	void waitUntilFilesAreClosed(char, state);
	void updateTicket(char);
	void setWhoIsWaitingForFilesToClose(char, state);
	void initSyncVariables();
	friend class KernelFile;
};
#include "kernelFS.h"
#include "kernelfile.h"
#include "file.h"
#include <iostream>
#include <algorithm>
#include <fstream>
#include <stdint.h>
using namespace std;


#define STARTING_CLUSTER 2
#define MAX_NUM_OF_CLUSTERS 2048 * 8

KernelFS::KernelFS()
{
	for (int i = 0; i < NUM_OF_PARTITIONS; i++) used[i] = 0;
	initSyncVariables();
}

void KernelFS::initSyncVariables()
{
	for (int i = 0; i < NUM_OF_PARTITIONS; i++)
	{
		InitializeConditionVariable(&partitions[i].cvWaitForFilesToClose);
		InitializeConditionVariable(&partitions[i].cvWaitForFormatsAndUnmount);
		InitializeCriticalSection(&partitions[i].criticalSection);
	}
}
KernelFS::~KernelFS()
{

}

char KernelFS::mount(Partition *p)
{


	for (int i = 0; i < NUM_OF_PARTITIONS; i++)
	{
		EnterCriticalSection(&partitions[i].criticalSection);
		if (!used[i])
		{

			partitions[i].partition = p;
			unsigned int size = p->getNumOfClusters();
			partitions[i].rootClusterNumber = 1;
			partitions[i].partitionName = i;
			p->readCluster(partitions[i].rootClusterNumber, partitions[i].rootClusterContent);
			used[i] = true;
			partitions[i].ticket = 0;
			partitions[i].next = 0;
			ClusterNo* rootDir = (ClusterNo*) partitions[i].rootClusterContent;
			int k = 0, l = 0;
			while (rootDir[k] != 0) k++;
			k--;

			if (k >= 0)
			{
				char buf[ClusterSize];
				p->readCluster(rootDir[k], buf);
				Entry* entries = (Entry*) buf;
				for (l; l < 102; l++)
				{
					bool valid = false;
					for (int j = 0; j < sizeof(Entry); j++) 
						if (buf[j + l * sizeof(Entry)] != 0)
						{
							valid = true;
							break;
						}
					if (!valid) break;
				}		
			}
			nextFree[i] = 2;
			char buf[ClusterSize];
			p->readCluster(0, buf);
			buf[0] |= 0b11000000;
			p->writeCluster(0, buf);
			partitions[i].lastEntry = l - 1;
			partitions[i].lastCluster = k;
			LeaveCriticalSection(&partitions[i].criticalSection);
			return i + 'A';
		}
		LeaveCriticalSection(&partitions[i].criticalSection);

	}

	return '0';
}


char KernelFS::unmount(char p)
{
	EnterCriticalSection(&partitions[p - 'A'].criticalSection);

	if (!legit(p) || !used[p - 'A'])
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return 0;
	}

	if (partitions[p - 'A'].waitAllFilesToClose == state::unmount || isItBeingUnmounted(p))
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return 0;
	}

	ticket(p, state::unmount);

	if (!partitions[p - 'A'].conditiion_queue.empty()) partitions[p - 'A'].conditiion_queue.pop();

	waitUntilFilesAreClosed(p, state::unmount);

	updateTicket(p);

	setWhoIsWaitingForFilesToClose(p , state::none);

	used[p - 'A'] = 0;

	WakeAllConditionVariable(&partitions[p - 'A'].cvWaitForFormatsAndUnmount);

	LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
	return 1;
	
}

void KernelFS::ticket(char p, state myState)
{
	auto myTicket = partitions[p - 'A'].ticket++;
	if (myTicket != partitions[p - 'A'].next)
	{
		partitions[p - 'A'].conditiion_queue.push(myState);

		while (myTicket != partitions[p - 'A'].next)
		{
			SleepConditionVariableCS(&partitions[p - 'A'].cvWaitForFormatsAndUnmount, &partitions[p - 'A'].criticalSection, INFINITE);
		}
	}
}


char KernelFS::readRootDir(char p, EntryNum en, Directory &d)
{
	EnterCriticalSection(&partitions[p - 'A'].criticalSection);
	if (!legit(p) || !used[p - 'A'])
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return 0;
	}

	char rootDirr[2048];
	partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, rootDirr);
	ClusterNo* rootDir = (ClusterNo*) rootDirr;

	unsigned int alreadyRead = 0;
	for (int i = 0; i <= partitions[p - 'A'].lastCluster ; i++)
	{
		if (rootDir[i] != 0)
		{
			char index1[2048];
			partitions[p - 'A'].partition->readCluster(rootDir[i], index1);
			Entry* entries = (Entry*) index1;
			for (int j = 0; j < ClusterSize / sizeof(Entry); j++)
			{
				if (i == partitions[p - 'A'].lastCluster && j > partitions[p - 'A'].lastEntry) break;
				bool exist = false;

				for (int k = 0; k < FNAMELEN; k++)
					if ((entries[j].name[k] >= 'A' && entries[j].name[k] <= 'Z') || (entries[j].name[k] >= 'a' && entries[j].name[k] <= 'z')) 
					{
						exist = true;
						break;
					}

				if (exist) {
					if (alreadyRead >= en && alreadyRead - en <= 64) {
						memcpy(d[alreadyRead - en].name, entries[j].name, FNAMELEN);
						memcpy(d[alreadyRead - en].ext, entries[j].ext, FEXTLEN);
						d[alreadyRead - en].size = entries[j].size;
						d[alreadyRead - en].indexCluster = entries[j].size;
					}
					alreadyRead++;
				}
			}
		}
	}


	if (alreadyRead - en > 64) 
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return 65;
	}
	else 
	if (alreadyRead > en)  
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return alreadyRead - en;
	}
	else 
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return 0;
	};
	
}


bool KernelFS::legit(char a)
{
	return a >= 'A' && a <= 'Z';
}


char KernelFS::format(char p)
{
	EnterCriticalSection(&partitions[p - 'A'].criticalSection);

	if (!legit(p) || !used[p - 'A'])
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return 0;
	}

	if (partitions[p - 'A'].waitAllFilesToClose == state::unmount || isItBeingUnmounted(p))
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return 0;
	}

	ticket(p, state::format);

	if (!partitions[p - 'A'].conditiion_queue.empty()) partitions[p - 'A'].conditiion_queue.pop();

	waitUntilFilesAreClosed(p, state::format);

	updateTicket(p);

	setWhoIsWaitingForFilesToClose(p, state::none);

	reset(p - 'A');

	char root[ClusterSize];
	partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, root);
	ClusterNo* rootDir = (ClusterNo*) root;

	char buf[ClusterSize];
	for (int i = 0; i < ClusterSize; i++) buf[i] = 0;

	partitions[p - 'A'].lastCluster	= -1;
	partitions[p - 'A'].lastEntry	= -1;

	for (int i = 0; i < 512; i++)
		if (rootDir[i] != 0) 
			partitions[p - 'A'].partition->writeCluster(rootDir[i], buf);

	partitions[p - 'A'].partition->writeCluster(partitions[p - 'A'].rootClusterNumber, buf);

	WakeAllConditionVariable(&partitions[p - 'A'].cvWaitForFormatsAndUnmount);
	
	LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
	return 1;
}

void KernelFS::waitUntilFilesAreClosed(char p, state myState)
{
	uint16_t size = partitions[p - 'A'].OpenFilesTable.size();
	if (size > 0)
	{
		setWhoIsWaitingForFilesToClose(p , myState);
		while (partitions[p - 'A'].OpenFilesTable.size() > 0)
			SleepConditionVariableCS(&partitions[p - 'A'].cvWaitForFormatsAndUnmount, &partitions[p - 'A'].criticalSection, INFINITE);
	}
}

void KernelFS::setWhoIsWaitingForFilesToClose(char p, state myState)
{
	partitions[p - 'A'].waitAllFilesToClose = myState;
}

void KernelFS::updateTicket(char p)
{
	partitions[p - 'A'].next += 1;
}

bool KernelFS::isItBeingUnmounted(char p)
{
	uint16_t size = partitions[p - 'A'].conditiion_queue.size();
	if (size == 0) return false;
	auto ret = partitions[p - 'A'].conditiion_queue.back();
	if (ret == state::unmount)	return true;
								return false;

}

char KernelFS::doesExist(char* fname)
{
	EnterCriticalSection(&partitions[fname[0] - 'A'].criticalSection);

	if (!legit(fname[0]) || !used[fname[0] - 'A'])
	{
		LeaveCriticalSection(&partitions[fname[0] - 'A'].criticalSection);
		return 0;
	}

	char p = fname[0];


	char indexx[2048];
	partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, indexx);
	ClusterNo* index0 = (ClusterNo*) indexx;


	for (int i = 0; i < 512; i++)
		if (index0[i] != 0)
		{
			char index1[2048];
			partitions[p - 'A'].partition->readCluster(index0[i], index1);
			Entry* entries = (Entry*) index1;
			for (int j = 0; j < 102; j++)
				if (compareNames(entries + j, fname)) 
				{
					LeaveCriticalSection(&partitions[fname[0] - 'A'].criticalSection);
					return 1;
				}
		}

	LeaveCriticalSection(&partitions[fname[0] - 'A'].criticalSection);
	return 0;

}


bool KernelFS::compareNames(Entry* entry, char* fname) {
	char* entryName = new char[12];
	int i;
	for (i = 0; i < FNAMELEN && entry->name[i] != ' '; i++) entryName[i] = entry->name[i];
	entryName[i++] = '.';
	for (int k = 0; i < FEXTLEN + FNAMELEN + 1 && k < FEXTLEN; k++, i++) entryName[i] = entry->ext[k];
	int len = i;
	for (int i = 0; i < len; i++) {
		if (entryName[i] != fname[i + 3]) return false;
	}
	return true;
}

void KernelFS::destroyFile(char *fname, ClusterNo clusterNo, unsigned int clusterOff)
{
	char entriesCluster[2048];
	char p = fname[0];
	partitions[p - 'A'].partition->readCluster(clusterNo, entriesCluster);
	Entry* entry = (Entry*) entriesCluster;
	unsigned long indexClusterNo = entry[clusterOff].indexCluster;
	if (indexClusterNo != 0)
	{
		char indexCluster[ClusterSize];
		partitions[p - 'A'].partition->readCluster(indexClusterNo, indexCluster);
		ClusterNo* firstLevelCluster = (ClusterNo*) indexCluster;
		for (int i = 0; i < 512; i++)
			if (firstLevelCluster[i] != 0)
			{
				if (i < 256)
				{
					free(p - 'A', firstLevelCluster[i]);
				}
				else
				{
					char buf[ClusterSize];
					partitions[p - 'A'].partition->readCluster(firstLevelCluster[i], buf);
					ClusterNo* secondLevelCluster = (ClusterNo*) buf;

					for (int j = 0; j < 512; j++)
						if (secondLevelCluster[j] < ClusterSize * 8 && secondLevelCluster[j] > 0)
						free(p - 'A', secondLevelCluster[j]);
				}
			}	

		free(p - 'A', indexClusterNo);
		
	}
	
	

	char root[ClusterSize];
	partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, root);
	ClusterNo* rootDir = (ClusterNo*) root;

	char buf[ClusterSize];
	partitions[p - 'A'].partition->readCluster(rootDir[partitions[p - 'A'].lastCluster], buf);
	Entry temp = ((Entry*) buf) [partitions[p - 'A'].lastEntry];
	
	char *tempBuf = new char[sizeof(Entry)];
	int cnt = 0;
	for (int i = partitions[p - 'A'].lastEntry * sizeof(Entry); i < (partitions[p - 'A'].lastEntry + 1) * sizeof(Entry); i++) 	
	{
		tempBuf[cnt++] = buf[i];
		buf[i] = 0;
	}
	partitions[p - 'A'].partition->writeCluster(rootDir[partitions[p - 'A'].lastCluster], buf);
	
	bool byebye = false;
	if (clusterOff == partitions[p - 'A'].lastEntry && clusterNo == rootDir[partitions[p - 'A'].lastCluster]) byebye = true;

	bool deallocating = false;

	if (partitions[p - 'A'].lastEntry == 0)
	{
		deallocating = true;
		free(p - 'A', rootDir[partitions[p - 'A'].lastCluster]);
		rootDir[partitions[p - 'A'].lastCluster] = 0;
		partitions[p - 'A'].partition->writeCluster(partitions[p - 'A'].rootClusterNumber, (char*)rootDir);
	}

	flushBitVector(p - 'A');

	partitions[p - 'A'].lastEntry--;

	if (deallocating) 	
	{
		int k = --partitions[p - 'A'].lastCluster;
		if (k >= 0) partitions[p - 'A'].lastEntry = ClusterSize / sizeof(Entry) - 1;
	}

	if (!byebye)
	{
		char buf[ClusterSize];
		partitions[p - 'A'].partition->readCluster(clusterNo, buf);
		
		memcpy(buf + clusterOff * sizeof(Entry), tempBuf, sizeof(Entry));

		partitions[p - 'A'].partition->writeCluster(clusterNo, buf);
	}


}

ClusterNo KernelFS::allocate(char p)
{

	partitions[p].partition->readCluster(0, bitVector[p]);
	

	while (bitVector[p][nextFree[p] / 8] & (1 << (7 - (nextFree[p] % 8)))) 
	{
		nextFree[p]++;
		if (nextFree[p] == MAX_NUM_OF_CLUSTERS) nextFree[p] = STARTING_CLUSTER;
	}

	ClusterNo ret = nextFree[p]++;
	bitVector[p][ret / 8] |= (1 << (7 - (ret % 8)));

	flushBitVector(p);

	return ret;
	
}

void KernelFS::free(char p, ClusterNo clusterNo)
{
	if (clusterNo > ClusterSize * 8) return;
	freeClusters[p].push_back(clusterNo);
	bitVector[p][clusterNo / 8] &= ~(1 << (7 - (clusterNo % 8)));
}

void KernelFS::reset(char p)
{
	for (int i = 0; i < ClusterSize; i++) bitVector[p][i] = 0;
	nextFree[p] = 2;
	bitVector[p][0] |= 0b11000000;
	flushBitVector(p);
}


void KernelFS::flushBitVector(char p)
{
	partitions[p].partition->writeCluster(0, bitVector[p]);
}


File* KernelFS::open(char* fname, char mode)
{
	EnterCriticalSection(&partitions[fname[0] - 'A'].criticalSection);
	char p = fname[0];
	if (!used[p - 'A'] || partitions[p - 'A'].waitAllFilesToClose != state::none || partitions[p - 'A'].conditiion_queue.size() > 0)
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return nullptr;
	}

	list<OpenFileEntry>::iterator it;
	list<OpenFileEntry>::iterator start = partitions[p - 'A'].OpenFilesTable.begin();
	list<OpenFileEntry>::iterator end	= partitions[p - 'A'].OpenFilesTable.end();
	
	bool fileIsOpen = false;
	bool exists		= false;
	for (it = start; it != end; it++)
	{
		if (strcmp(fname, it->fname) == 0)
		{
			fileIsOpen	= true;
			exists		= true;
			break;
		}
	}

	ClusterNo clusterNo = 0;
	unsigned int clusterOff = 0;
	

	if (!fileIsOpen)
	{
		/*
			Search through all files on partition
		*/
		char rootDirr[2048];
		partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, rootDirr);
		ClusterNo* rootDir = (ClusterNo*)rootDirr;

		for (int i = 0; i <= partitions[p - 'A'].lastCluster; i++)
		{
			if (rootDir[i] != 0)
			{
				char index1[2048];
				partitions[p - 'A'].partition->readCluster(rootDir[i], index1);
				Entry* entries = (Entry*)index1;
				for (int j = 0; j < ClusterSize / sizeof(Entry); j++)
				{
					if (i == partitions[p - 'A'].lastCluster && j > partitions[p - 'A'].lastEntry) break;
					if (compareNames(entries + j, fname))
					{
						clusterNo = rootDir[i];
						clusterOff = j;
						exists = true;
						break;
					}
				}
				if (exists) break;
			}
		}
	}

	/* File is not open and doesn't exist */
	
	if (!exists && !fileIsOpen && mode != 'w')  
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return nullptr;
	}

	/* File is open and mode is not write */
	if (fileIsOpen && mode != 'w')  
	{

		it->fileOpenerCounter++;
		if (mode == 'r')
		{
			if (TryAcquireSRWLockShared(&it->srwLock) == 0)
			{
				LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
				AcquireSRWLockShared(&it->srwLock);
				EnterCriticalSection(&partitions[p - 'A'].criticalSection);
			}
		}
		else
		{
			if (TryAcquireSRWLockExclusive(&it->srwLock) == 0)
			{
				LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
				AcquireSRWLockExclusive(&it->srwLock);
				EnterCriticalSection(&partitions[p - 'A'].criticalSection);
			}
		}

		File *ret = new File();
		ret->myImpl = new KernelFile(it, &partitions[p - 'A'], mode, this, fname);
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return ret;

	}
	

	/* File is open and we want to write */
	if (mode == 'w' && fileIsOpen)
	{
		it->fileOpenerCounter++;
		if (TryAcquireSRWLockExclusive(&it->srwLock) == 0)
		{
			LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
			AcquireSRWLockExclusive(&it->srwLock);
			EnterCriticalSection(&partitions[p - 'A'].criticalSection);
		}

		destroyFile(fname, it->clusterNumber, it->clusterOffset);

		ClusterNo newCluster = allocate(p - 'A');

		partitions[p - 'A'].lastEntry = (partitions[p - 'A'].lastEntry + 1) % 102;
		if (partitions[p - 'A'].lastEntry == 0)
		{
			partitions[p - 'A'].lastCluster = (partitions[p - 'A'].lastCluster + 1) % 512;
			ClusterNo newClusterForRoot = allocate(p - 'A');
			
			char rootIndex[2048];
			partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, rootIndex);
			ClusterNo* root = (ClusterNo*) rootIndex;
			root[partitions[p - 'A'].lastCluster] = newClusterForRoot;
			partitions[p - 'A'].partition->writeCluster(partitions[p - 'A'].rootClusterNumber, (char*)root);
		}

		char rootIndex[ClusterSize];
		partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, rootIndex);
		ClusterNo* root = (ClusterNo*) rootIndex;

		char entriesCluster[ClusterSize];
		partitions[p - 'A'].partition->readCluster(root[partitions[p - 'A'].lastCluster], entriesCluster);

		Entry* entries = (Entry*) entriesCluster;
		int k, dotPos;
		bool foundDot = false;
		int j = partitions[p - 'A'].lastEntry;
		for (k = 0; k < FNAMELEN; k++) {
			if (fname[k + 3] == '.' || strlen(fname) == k + 3) {
				foundDot = true;
				dotPos = k;
			}
			if (!foundDot) entries[j].name[k] = fname[k + 3];
			else entries[j].name[k] = ' ';
		}
		for (k = 0; k < FEXTLEN; k++) {
			if (strlen(fname) <= k + 3) entries[j].ext[k] = ' ';
			else entries[j].ext[k] = fname[k + 4 + dotPos];
		}

		entries[j].indexCluster = newCluster;
		entries[j].size = 0;

		partitions[p - 'A'].partition->writeCluster(root[partitions[p - 'A'].lastCluster], (char*) entries);

		it->clusterNumber = root[partitions[p - 'A'].lastCluster];
		it->clusterOffset = j;
		File *ret = new File();
		ret->myImpl = new KernelFile(it, &partitions[p - 'A'], mode, this, fname);
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return ret;

	}


	/* File is not open, but exists, and we want to write */
	if (mode == 'w' && !fileIsOpen && exists)
	{

		destroyFile(fname, clusterNo, clusterOff);

		ClusterNo newCluster = allocate(p - 'A');

		partitions[p - 'A'].lastEntry = (partitions[p - 'A'].lastEntry + 1) % 102;
		if (partitions[p - 'A'].lastEntry == 0)
		{
			partitions[p - 'A'].lastCluster = (partitions[p - 'A'].lastCluster + 1) % 512;
			ClusterNo newClusterForRoot = allocate(p - 'A');
		
			char rootIndex[2048];
			partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, rootIndex);
			ClusterNo* root = (ClusterNo*)rootIndex;
			root[partitions[p - 'A'].lastCluster] = newClusterForRoot;
			partitions[p - 'A'].partition->writeCluster(partitions[p - 'A'].rootClusterNumber, (char*)root);
		}

		char rootIndex[ClusterSize];
		partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, rootIndex);
		ClusterNo* root = (ClusterNo*)rootIndex;

		char entriesCluster[ClusterSize];
		partitions[p - 'A'].partition->readCluster(root[partitions[p - 'A'].lastCluster], entriesCluster);

		Entry* entries = (Entry*)entriesCluster;
		int k, dotPos;
		bool foundDot = false;
		int j = partitions[p - 'A'].lastEntry;
		for (k = 0; k < FNAMELEN; k++) {
			if (fname[k + 3] == '.' || strlen(fname) == k + 3) {
				foundDot = true;
				dotPos = k; 
			}
			if (!foundDot) entries[j].name[k] = fname[k + 3];
			else entries[j].name[k] = ' ';
		}
		for (k = 0; k < FEXTLEN; k++) {
			if (strlen(fname) <= k + 3) entries[j].ext[k] = ' ';
			else entries[j].ext[k] = fname[k + 4 + dotPos];
		}

		entries[j].indexCluster = newCluster;
		entries[j].size = 0;

		partitions[p - 'A'].partition->writeCluster(root[partitions[p - 'A'].lastCluster], (char*)entries);
		
		OpenFileEntry ofe = OpenFileEntry();
		ofe.clusterNumber = root[partitions[p - 'A'].lastCluster];
		ofe.clusterOffset = j;
		ofe.fname = new char[12];
		strcpy(ofe.fname, fname);
		ofe.srwLock = SRWLOCK_INIT;
		ofe.fileOpenerCounter = 1;
		partitions[p - 'A'].OpenFilesTable.push_back(ofe);

		
		it = partitions[p - 'A'].OpenFilesTable.end();
		--it;


		
		if (TryAcquireSRWLockExclusive(&it->srwLock) == 0)
		{
			LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
			AcquireSRWLockExclusive(&it->srwLock);
			EnterCriticalSection(&partitions[p - 'A'].criticalSection);
		}
		


		File *ret = new File();
		ret->myImpl = new KernelFile(it, &partitions[p - 'A'], mode, this, fname);
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return ret;

	}

	/* We want to create brand new file */
	if (mode == 'w' && !fileIsOpen && !exists)
	{
		

		ClusterNo newCluster = allocate(p - 'A');

		partitions[p - 'A'].lastEntry = (partitions[p - 'A'].lastEntry + 1) % 102;
		if (partitions[p - 'A'].lastEntry == 0)
		{
			partitions[p - 'A'].lastCluster = (partitions[p - 'A'].lastCluster + 1) % 512;
			ClusterNo newClusterForRoot = allocate(p - 'A');
			
			char rootIndex[2048];
			partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, rootIndex);
			ClusterNo* root = (ClusterNo*)rootIndex;
			root[partitions[p - 'A'].lastCluster] = newClusterForRoot;
			partitions[p - 'A'].partition->writeCluster(partitions[p - 'A'].rootClusterNumber, (char*)root);
		}

		char rootIndex[ClusterSize];
		partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, rootIndex);
		ClusterNo* root = (ClusterNo*)rootIndex;

		char entriesCluster[ClusterSize];
		partitions[p - 'A'].partition->readCluster(root[partitions[p - 'A'].lastCluster], entriesCluster);

		Entry* entries = (Entry*)entriesCluster;
		int k, dotPos;
		bool foundDot = false;
		int j = partitions[p - 'A'].lastEntry;
		for (k = 0; k < FNAMELEN; k++) {
			if (fname[k + 3] == '.' || strlen(fname) == k + 3) {
				foundDot = true;
				dotPos = k;
			}
			if (!foundDot) entries[j].name[k] = fname[k + 3];
			else entries[j].name[k] = ' ';
		}
		for (k = 0; k < FEXTLEN; k++) {
			if (strlen(fname) <= k + 3) entries[j].ext[k] = ' ';
			else entries[j].ext[k] = fname[k + 4 + dotPos];
		}

		entries[j].indexCluster = newCluster;
		entries[j].size = 0;

		partitions[p - 'A'].partition->writeCluster(root[partitions[p - 'A'].lastCluster], (char*)entries);

		OpenFileEntry ofe = OpenFileEntry();
		ofe.clusterNumber = root[partitions[p - 'A'].lastCluster];
		ofe.clusterOffset = j;
		ofe.fname = new char[9];
		strcpy(ofe.fname, fname);
		ofe.srwLock = SRWLOCK_INIT;
		ofe.fileOpenerCounter = 1;
		partitions[p - 'A'].OpenFilesTable.push_back(ofe);
		it = partitions[p - 'A'].OpenFilesTable.end();
		it--;

		
		if (TryAcquireSRWLockExclusive(&it->srwLock) == 0)
		{
			LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
			AcquireSRWLockExclusive(&it->srwLock);
			EnterCriticalSection(&partitions[p - 'A'].criticalSection);
		}
		
		File *ret = new File();
		ret->myImpl = new KernelFile(it, &partitions[p - 'A'], mode, this, fname);
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return ret;
	}

	/* File is not open but exists on disk, and we don't want to write over */
	if (mode != 'w' && !fileIsOpen && exists)
	{
		
		OpenFileEntry ofe = OpenFileEntry();
		ofe.clusterNumber = clusterNo;
		ofe.clusterOffset = clusterOff;
		ofe.fname = new char[9];
		strcpy(ofe.fname, fname);
		ofe.srwLock = SRWLOCK_INIT;
		ofe.fileOpenerCounter++;
		partitions[p - 'A'].OpenFilesTable.push_back(ofe);
		it = partitions[p - 'A'].OpenFilesTable.end();
		it--;

		if (mode == 'r')
		{
			if (TryAcquireSRWLockShared(&it->srwLock) == 0)
			{
				LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
				AcquireSRWLockShared(&it->srwLock);
				EnterCriticalSection(&partitions[p - 'A'].criticalSection);
			}
		}
		else
		{
			if (TryAcquireSRWLockExclusive(&it->srwLock) == 0)
			{
				LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
				AcquireSRWLockExclusive(&it->srwLock);
				EnterCriticalSection(&partitions[p - 'A'].criticalSection);
			}
		}

		File *ret = new File();
		ret->myImpl = new KernelFile(it, &partitions[p - 'A'], mode, this, fname);
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return ret;
	}


	LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
	return nullptr;


}

char KernelFS::deleteFile(char *fname)
{
	EnterCriticalSection(&partitions[fname[0] - 'A'].criticalSection);
	char p = fname[0];
	if (!legit(p) || !used[p - 'A'])
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return 0;
	}

	list<OpenFileEntry>::iterator it;
	list<OpenFileEntry>::iterator start = partitions[p - 'A'].OpenFilesTable.begin();
	list<OpenFileEntry>::iterator end = partitions[p - 'A'].OpenFilesTable.end();

	bool fileIsOpen = false;
	for (it = start; it != end; it++)
	{
		if (strcmp(fname, it->fname) == 0)
		{
			fileIsOpen = true;
			break;
		}
	}

	if (fileIsOpen)
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return 0;
	}

	bool exists = false;
	ClusterNo clusterNo;
	unsigned int clusterOff;
	char buf[ClusterSize];
	partitions[p - 'A'].partition->readCluster(partitions[p - 'A'].rootClusterNumber, buf);
	ClusterNo* index0 = (ClusterNo*) buf;

	for (int i = 0; i < 512; i++)
		if (index0[i] != 0)
		{
			char data[2048];
			partitions[p - 'A'].partition->readCluster(index0[i], data);
			Entry* entries = (Entry*)data;
			for (int j = 0; j < 102; j++)
				if (compareNames(entries + j, fname))
				{
					clusterNo = index0[i];
					clusterOff = j;
					exists = true;
					break;
				}
		}
	

	if (!exists)
	{
		LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
		return 0;
	}


	destroyFile(fname, clusterNo, clusterOff);
	LeaveCriticalSection(&partitions[p - 'A'].criticalSection);
	return 0;
}


#include "KernelFile.h"
#include "kernelFS.h"
#include <iostream>
using namespace std;
KernelFile::KernelFile() {}


KernelFile::KernelFile(list<OpenFileEntry>::iterator _it, PartitionModel *pm, char _mode, KernelFS *fs, char* fname) {
	it = _it;
	myFileSystem = fs;
	mode = _mode;
	dirtyBit = 0;
	this->pm = pm;
	char entries[2048];
	ofe = *it;
	pm->partition->readCluster(ofe.clusterNumber, entries);
	Entry* entries1 = (Entry*) entries;
	fileEntry = (Entry) entries1[ofe.clusterOffset];
	sizeOfFile = fileEntry.size;
	myName = new char[15];
	strcpy(myName, fname);

	firstIndexClusterNo = fileEntry.indexCluster;
	pm->partition->readCluster(firstIndexClusterNo, firstIndex);
	
	initialSize = sizeOfFile;

	if (mode == 'r' || mode == 'w') {
		cursor = 0;
		cursorOffset = 0;
		cursorCluster = 0;
	}
	else {
		cursor = sizeOfFile;
		cursorOffset = sizeOfFile % 2048;
		cursorCluster = sizeOfFile / 2048;
	}
	

	dataBufferClusterEntry	= -1;
	dataBufferClusterNo		= -1;
	secondIndexClusterNo	= -1;

	modifiedFirstIndex		= 0;
	modifiedSecondIndex		= 0;
}

char KernelFile::write(BytesCnt bc, char* buffer) 
{
	int i;

	if (mode != 'w' && mode != 'a')		return 0;
	

	for (i = 0; i < bc; i++) 
	{
		
		if (cursorCluster == dataBufferClusterEntry) 
		{
			dataBuffer[cursorOffset] = buffer[i];
			dirtyBit = 1;

			cursorUpdate();
		}
		else
			if (cursorCluster < 256) 
			{
				flushDataBuffer();
				dataBufferClusterNo = ((ClusterNo*)firstIndex)[cursorCluster];
				if (eof() == 2 && onEdgeOfCluster()) 
				{
					EnterCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
					dataBufferClusterNo = myFileSystem->allocate(pm->partitionName);
					LeaveCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
					((ClusterNo*)firstIndex)[cursorCluster] = dataBufferClusterNo;
					modifiedFirstIndex = 1;
				}
				dataBufferClusterEntry = cursorCluster;
				for (int j = 0; j < ClusterSize; j++) dataBuffer[j] = 0;
				if (!pm->partition->readCluster(dataBufferClusterNo, dataBuffer)) break;
				dataBuffer[cursorOffset] = buffer[i];
				dirtyBit = 1;

				cursorUpdate();
			}
			else 
			{
				uint32_t firstLevelEntry = calculateEntry();
				ClusterNo secondLevelClusterNo = ((ClusterNo*)firstIndex)[firstLevelEntry];

				if (eof() == 2 && needNewCluster()) {
					EnterCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
					secondLevelClusterNo = myFileSystem->allocate(pm->partitionName);
					LeaveCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);

					((ClusterNo*)firstIndex)[firstLevelEntry] = secondLevelClusterNo;
					modifiedFirstIndex = 1;
				}

				if (secondIndexClusterNo != secondLevelClusterNo)
				{
					flushSecondIndex();
					secondIndexClusterNo = secondLevelClusterNo;
					if (!pm->partition->readCluster(secondIndexClusterNo, secondIndex)) break;
				}
				ClusterNo dataEntryInSecondIndex = calculateEntrySecondLevel();
				ClusterNo dataCluster = ((ClusterNo*)secondIndex)[dataEntryInSecondIndex];
				if (eof() == 2 && onEdgeOfCluster()) 
				{
					EnterCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
					dataCluster = myFileSystem->allocate(pm->partitionName);
					LeaveCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
					((ClusterNo*)secondIndex)[dataEntryInSecondIndex] = dataCluster;
					modifiedSecondIndex = true;
				}
				if (dataBufferClusterNo != dataCluster)
				{
					flushDataBuffer();
					dataBufferClusterNo = dataCluster;
					if (!pm->partition->readCluster(dataBufferClusterNo, dataBuffer)) break;
				}
				dataBufferClusterEntry = cursorCluster;
				dataBuffer[cursorOffset] = buffer[i];
				dirtyBit = 1;

				cursorUpdate();
			}
			if (cursor > sizeOfFile) sizeOfFile = cursor;
	}
	

	flushFirstIndex();
	
	flushSecondIndex();

	if (cursor > sizeOfFile) sizeOfFile = cursor;

	

	if (i == bc) return 1;
	return 0;
}

bool KernelFile::onEdgeOfCluster()
{
	uint32_t tmp = getFileSize();
	if (tmp % ClusterSize == 0)		return 1;
									return 0;
}

uint32_t KernelFile::calculateEntry()
{
	uint32_t tmp = cursorCluster;
	tmp			-= 256;
	tmp			/= 512;
	tmp			+= 256;

	return tmp;
}

ClusterNo KernelFile::calculateEntrySecondLevel()
{
	ClusterNo tmp = cursorCluster;
	tmp			 -= 256;
	tmp			 %= 512;

	return tmp;
}

void KernelFile::flushDataBuffer()
{
	if (dataIsDirty())
	{
		pm->partition->writeCluster(dataBufferClusterNo, dataBuffer);
		dirtyBit = 0;	
	}
}

void KernelFile::flushFirstIndex()
{
	if (firstIndexIsDirty())
	{
		modifiedFirstIndex = 0;
		pm->partition->writeCluster(firstIndexClusterNo, firstIndex);
	}
}

void KernelFile::flushSecondIndex()
{
	if (secondIndexIsDirty())
	{
		modifiedSecondIndex = 0;
		pm->partition->writeCluster(secondIndexClusterNo, secondIndex);
	}
}

bool KernelFile::firstIndexIsDirty()
{
	return modifiedFirstIndex == 1;
}

bool KernelFile::dataIsDirty()
{
	return dirtyBit == 1;
}

bool KernelFile::secondIndexIsDirty()
{
	return modifiedSecondIndex == 1;
}

bool KernelFile::needNewCluster()
{
	uint32_t tmp = sizeOfFile;
	tmp -= ClusterSize * 256;
	uint32_t mod = ClusterSize * 512;
	uint32_t res = tmp % mod;

	if	(res == 0)		return 1;
						return 0;
}

void KernelFile::cursorUpdate()
{
	cursorCluster	= (cursor + 1) / ClusterSize;
	cursorOffset	= (cursor + 1) % ClusterSize;
	cursor			+= 1;
}

BytesCnt KernelFile::read(BytesCnt bc, char* buffer)
{
	int i;
	for (i = 0; i < bc && cursor < sizeOfFile; i++)
	{
		
		if (cursorCluster == dataBufferClusterEntry) 
		{
			buffer[i] = dataBuffer[cursorOffset];
			cursorUpdate();
			continue;
		}
		uint32_t firstLevelEntry = cursorCluster;
		if (firstLevelEntry < 256) 
		{
			
			flushDataBuffer();
			dataBufferClusterNo = ((ClusterNo*)firstIndex)[firstLevelEntry];
			dataBufferClusterEntry = firstLevelEntry;
			if (!pm->partition->readCluster(dataBufferClusterNo, dataBuffer)) break;
			buffer[i] = dataBuffer[cursorOffset];

			cursorUpdate();
			continue;
		}
		else 
		{
			firstLevelEntry = calculateEntry();
			ClusterNo secondLevelClusterNo = ((ClusterNo*)firstIndex)[firstLevelEntry];
			
			if (secondIndexClusterNo != secondLevelClusterNo)
			{
				secondIndexClusterNo = secondLevelClusterNo;
				if (!pm->partition->readCluster(secondIndexClusterNo, secondIndex)) break;
			}
			

			ClusterNo dataEntryInSecondIndex = calculateEntrySecondLevel();
			ClusterNo dataCluster = ((ClusterNo*)secondIndex)[dataEntryInSecondIndex];

			flushDataBuffer();

			dataBufferClusterNo = dataCluster;
			dataBufferClusterEntry = cursorCluster;
			if (!pm->partition->readCluster(dataBufferClusterNo, dataBuffer)) break;
			buffer[i] = dataBuffer[cursorOffset];
			
			cursorUpdate();
		}
	}
	return i;
}

char KernelFile::seek(BytesCnt bc)
{
	if (bc > sizeOfFile) return 0;
	cursor			= bc;
	cursorOffset	= bc % ClusterSize;
	cursorCluster	= bc / ClusterSize;
	return 1;
}

BytesCnt KernelFile::filePos()
{
	return cursor;
}

char KernelFile::eof()
{
	if (filePos() == getFileSize())	return 2;
	if (filePos() > getFileSize())	return 1;
									return 0;
}

BytesCnt KernelFile::getFileSize()
{
	return sizeOfFile;
}

char KernelFile::truncate()
{

	if (mode == 'r')
	{
		return 0;
	}


	char buf[ClusterSize];
	pm->partition->readCluster(firstIndexClusterNo, buf);
	ClusterNo* firstIndexC = (ClusterNo*) buf;
	for (uint32_t i = cursorCluster + (cursorOffset != 0); i < sizeOfFile / ClusterSize + (cursorOffset != 0); i++)
	{
		if (i < 256)
		{
			EnterCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
			myFileSystem->free(myName[0] - 'A', firstIndexC[i]);
			LeaveCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
			firstIndexC[i] = 0;
		}
		else
		{
			uint32_t entry = (i - 256) / 512 + 256;
			char buf[ClusterSize];
			pm->partition->readCluster(firstIndexC[entry], buf);
			ClusterNo* secondIndexC = (ClusterNo*) buf;
			for (int j = 0; j < 512; j++)
			{
				EnterCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
				myFileSystem->free(myName[0] - 'A', secondIndexC[j]);
				LeaveCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
				secondIndexC[j] = 0;
			}
			EnterCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
			myFileSystem->free(myName[0] - 'A', firstIndexC[entry]);
			LeaveCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
			firstIndexC[entry] = 0;
		}
	}

	EnterCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);
	myFileSystem->flushBitVector(myName[0] - 'A');
	LeaveCriticalSection(&myFileSystem->partitions[myName[0] - 'A'].criticalSection);

	sizeOfFile = cursor;

	return 1;
}


KernelFile::~KernelFile() {

	EnterCriticalSection(&pm->criticalSection);


	if (initialSize != sizeOfFile)
	{
		fileEntry.size = sizeOfFile;
		list<OpenFileEntry>::iterator it;
		for (it = pm->OpenFilesTable.begin(); it != pm->OpenFilesTable.end(); it++)
			if (strcmp(it->fname, myName) == 0) break;
		ofe = *it;

		ClusterNo clusterNo = ofe.clusterNumber;
		uint32_t clusterOff = ofe.clusterOffset;
		char buf[ClusterSize];
		pm->partition->readCluster(clusterNo, buf);
		Entry* entries = (Entry*)buf;
		entries[clusterOff].size = sizeOfFile;
		pm->partition->writeCluster(clusterNo, (char*)entries);

	}

	
	flushDataBuffer();
	flushFirstIndex();
	flushSecondIndex();

	char *fname = new char[FNAMELEN + 1];
	strcpy(fname, myName);
	list<OpenFileEntry>::iterator it;
	for (it = pm->OpenFilesTable.begin(); it != pm->OpenFilesTable.end(); it++)
		if (strcmp(it->fname, myName) == 0) break;
	ofe = *it;
	if (mode == 'r') ReleaseSRWLockShared(&((*it).srwLock));
	else ReleaseSRWLockExclusive(&((*it).srwLock));

	(*it).fileOpenerCounter--;

	if ((*it).fileOpenerCounter == 0)
	{
		pm->OpenFilesTable.erase(it);
		if (pm->OpenFilesTable.size() == 0) WakeAllConditionVariable(&pm->cvWaitForFilesToClose);
	}

	LeaveCriticalSection(&pm->criticalSection);
}

#pragma once
#include "fs.h"
#include "kernelFS.h"


using namespace std;

class KernelFile {
public:
	KernelFile();
	KernelFile(list<OpenFileEntry>::iterator, PartitionModel*, char , KernelFS*, char*);
	~KernelFile();
	char write(BytesCnt, char* buffer);
	BytesCnt read(BytesCnt, char* buffer);
	char seek(BytesCnt);
	BytesCnt filePos();
	char eof();
	BytesCnt getFileSize();
	char truncate();
private:
	list<OpenFileEntry>::iterator it;
	OpenFileEntry ofe;
	KernelFS* myFileSystem;
	PartitionModel* pm;
	Entry fileEntry;
	int dirtyBit, modifiedFirstIndex, modifiedSecondIndex;
	char mode;
	char *myName;
	char dataBuffer[2048], firstIndex[2048], secondIndex[2048];
	ClusterNo firstIndexClusterNo, dataBufferClusterNo, dataBufferClusterEntry, secondIndexClusterNo;
	unsigned long cursorCluster, cursorOffset, cursor, sizeOfFile, initialSize;
	friend class File;
	friend class KernelFS;

	bool needNewCluster();
	void cursorUpdate();
	void flushDataBuffer();
	bool dataIsDirty();
	void flushFirstIndex();
	bool firstIndexIsDirty();
	void flushSecondIndex();
	bool secondIndexIsDirty();
	bool onEdgeOfCluster();
	uint32_t calculateEntry();
	ClusterNo calculateEntrySecondLevel();

};
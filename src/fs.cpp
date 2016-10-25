#include "fs.h"

#include "kernelFS.h"

KernelFS* FS::myImpl = new KernelFS;

FS::FS()
{

}

FS::~FS()
{
	delete myImpl;
}

char FS::mount(Partition *p)
{
	return myImpl->mount(p);
}

char FS::unmount(char part)
{
	return myImpl->unmount(part);
}

char FS::readRootDir(char part, EntryNum n, Directory &d)
{
	return myImpl->readRootDir(part, n, d);
}

char FS::format(char part)
{
	return myImpl->format(part);
}

char FS::doesExist(char *fname)
{
	return myImpl->doesExist(fname);
}

File* FS::open(char *fname, char mode)
{
	return myImpl->open(fname, mode);
}

char FS::deleteFile(char *fname)
{
	return myImpl->deleteFile(fname);
}
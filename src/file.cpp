#include "file.h"
#include "KernelFile.h"


File::File() {
	myImpl = new KernelFile();
}

File::~File() {
	delete myImpl;
}

char File::write(BytesCnt bc, char * buffer)
{
	return myImpl->write(bc, buffer);
}

BytesCnt File::read(BytesCnt bc, char * buffer)
{
	return myImpl->read(bc, buffer);
}

char File::seek(BytesCnt bc)
{
	return myImpl->seek(bc);
}

BytesCnt File::filePos()
{
	return myImpl->filePos();
}

char File::eof()
{
	return myImpl->eof();
}

BytesCnt File::getFileSize()
{
	return myImpl->getFileSize();
}

char File::truncate()
{
	return myImpl->truncate();
}



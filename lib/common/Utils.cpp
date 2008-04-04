// --------------------------------------------------------------------------
//
// File
//		Name:    Utils.cpp
//		Purpose: Utility function
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef SHOW_BACKTRACE_ON_EXCEPTION
	#include <execinfo.h>
	#include <stdlib.h>
#endif

#include "Utils.h"
#include "CommonException.h"
#include "Logging.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    SplitString(const std::string &, char, std::vector<std::string> &)
//		Purpose: Splits a string at a given character
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void SplitString(const std::string &String, char SplitOn, std::vector<std::string> &rOutput)
{
	// Split it up.
	std::string::size_type b = 0;
	std::string::size_type e = 0;
	while(e = String.find_first_of(SplitOn, b), e != String.npos)
	{
		// Get this string
		unsigned int len = e - b;
		if(len >= 1)
		{
			rOutput.push_back(String.substr(b, len));
		}
		b = e + 1;
	}
	// Last string
	if(b < String.size())
	{
		rOutput.push_back(String.substr(b));
	}
/*#ifndef NDEBUG
	TRACE2("Splitting string '%s' on %c\n", String.c_str(), SplitOn);
	for(unsigned int l = 0; l < rOutput.size(); ++l)
	{
		TRACE2("%d = '%s'\n", l, rOutput[l].c_str());
	}
#endif*/
}

#ifdef SHOW_BACKTRACE_ON_EXCEPTION
void DumpStackBacktrace()
{
	void *array[10];
	size_t size;
	char **strings;
	size_t i;

	size = backtrace (array, 10);
	strings = backtrace_symbols (array, size);

	BOX_TRACE("Obtained " << size << " stack frames.");

	for(i = 0; i < size; i++)
	{
		BOX_TRACE(strings[i]);
	}

#include "MemLeakFindOff.h"
	free (strings);
#include "MemLeakFindOn.h"
}
#endif



// --------------------------------------------------------------------------
//
// Function
//		Name:    FileExists(const char *)
//		Purpose: Does a file exist?
//		Created: 20/11/03
//
// --------------------------------------------------------------------------
bool FileExists(const char *Filename, int64_t *pFileSize, bool TreatLinksAsNotExisting)
{
	struct stat st;
	if(::lstat(Filename, &st) != 0)
	{
		if(errno == ENOENT)
		{
			return false;
		}
		else
		{
			THROW_EXCEPTION(CommonException, OSFileError);
		}
	}

	// is it a file?	
	if((st.st_mode & S_IFDIR) == 0)
	{
		if(TreatLinksAsNotExisting && ((st.st_mode & S_IFLNK) != 0))
		{
			return false;
		}
	
		// Yes. Tell caller the size?
		if(pFileSize != 0)
		{
			*pFileSize = st.st_size;
		}
	
		return true;
	}
	else
	{
		return false;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ObjectExists(const std::string& rFilename)
//		Purpose: Does a object exist, and if so, is it a file or a directory?
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
int ObjectExists(const std::string& rFilename)
{
	struct stat st;
	if(::stat(rFilename.c_str(), &st) != 0)
	{
		if(errno == ENOENT)
		{
			return ObjectExists_NoObject;
		}
		else
		{
			THROW_EXCEPTION(CommonException, OSFileError);
		}
	}

	// is it a file or a dir?
	return ((st.st_mode & S_IFDIR) == 0)?ObjectExists_File:ObjectExists_Dir;
}





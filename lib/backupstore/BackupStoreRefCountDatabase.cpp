// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreRefCountDatabase.cpp
//		Purpose: Backup store object reference count database storage
//		Created: 2009/06/01
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <algorithm>

#include "BackupStoreRefCountDatabase.h"
#include "BackupStoreException.h"
#include "BackupStoreAccountDatabase.h"
#include "BackupStoreAccounts.h"
#include "RaidFileController.h"
#include "RaidFileUtil.h"
#include "RaidFileException.h"
#include "Utils.h"

#include "MemLeakFindOn.h"

#define REFCOUNT_MAGIC_VALUE	0x52656643 // RefC
#define REFCOUNT_FILENAME	"refcount"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::BackupStoreRefCountDatabase()
//		Purpose: Default constructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreRefCountDatabase::BackupStoreRefCountDatabase(const
	BackupStoreAccountDatabase::Entry& rAccount)
: mAccount(rAccount),
  mFilename(GetFilename(rAccount)),
  mReadOnly(true),
  mIsModified(false)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::~BackupStoreRefCountDatabase
//		Purpose: Destructor
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
BackupStoreRefCountDatabase::~BackupStoreRefCountDatabase()
{
}

std::string BackupStoreRefCountDatabase::GetFilename(const
	BackupStoreAccountDatabase::Entry& rAccount)
{
	std::string RootDir = BackupStoreAccounts::GetAccountRoot(rAccount);
	ASSERT(RootDir[RootDir.size() - 1] == '/' ||
		RootDir[RootDir.size() - 1] == DIRECTORY_SEPARATOR_ASCHAR);

	std::string fn(RootDir + REFCOUNT_FILENAME ".db");
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(rAccount.GetDiscSet()));
	return RaidFileUtil::MakeWriteFileName(rdiscSet, fn);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::Create(int32_t,
//			 const std::string &, int, bool)
//		Purpose: Create a new database, overwriting an existing
//			 one only if AllowOverwrite is true.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreRefCountDatabase::Create(const
	BackupStoreAccountDatabase::Entry& rAccount, bool AllowOverwrite)
{
	// Initial header
	refcount_StreamFormat hdr;
	hdr.mMagicValue = htonl(REFCOUNT_MAGIC_VALUE);
	hdr.mAccountID = htonl(rAccount.GetID());
	
	// Generate the filename
	std::string Filename = GetFilename(rAccount);

	// Open the file for writing
	if (FileExists(Filename) && !AllowOverwrite)
	{
		THROW_FILE_ERROR("Failed to overwrite refcount database: "
			"not allowed here", Filename, RaidFileException,
			CannotOverwriteExistingFile);
	}

	int flags = O_CREAT | O_BINARY | O_RDWR;
	if (!AllowOverwrite)
	{
		flags |= O_EXCL;
	}

	std::auto_ptr<FileStream> DatabaseFile(new FileStream(Filename, flags));
	
	// Write header
	DatabaseFile->Write(&hdr, sizeof(hdr));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::Load(int32_t AccountID,
//			 BackupStoreAccountDatabase& rAccountDatabase,
//			 bool ReadOnly);
//		Purpose: Loads the info from disc, given the root
//			 information. Can be marked as read only.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreRefCountDatabase> BackupStoreRefCountDatabase::Load(
	const BackupStoreAccountDatabase::Entry& rAccount, bool ReadOnly)
{
	// Generate the filename
	std::string filename = GetFilename(rAccount);
	int flags = ReadOnly ? O_RDONLY : O_RDWR;

	// Open the file for read/write
	std::auto_ptr<FileStream> dbfile(new FileStream(filename,
		flags | O_BINARY));
	
	// Read in a header
	refcount_StreamFormat hdr;
	if(!dbfile->ReadFullBuffer(&hdr, sizeof(hdr), 0 /* not interested in bytes read if this fails */))
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"short read", filename, BackupStoreException,
			CouldNotLoadStoreInfo);
	}
	
	// Check it
	if(ntohl(hdr.mMagicValue) != REFCOUNT_MAGIC_VALUE ||
		(int32_t)ntohl(hdr.mAccountID) != rAccount.GetID())
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"bad magic number", filename, BackupStoreException,
			BadStoreInfoOnLoad);
	}
	
	// Make new object
	std::auto_ptr<BackupStoreRefCountDatabase> refcount(new BackupStoreRefCountDatabase(rAccount));
	
	// Put in basic location info
	refcount->mReadOnly = ReadOnly;
	refcount->mapDatabaseFile = dbfile;
	
	// return it to caller
	return refcount;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreRefCountDatabase::GetRefCount(int64_t
//			 ObjectID)
//		Purpose: Get the number of references to the specified object
//			 out of the database
//		Created: 2009/06/01
//
// --------------------------------------------------------------------------
BackupStoreRefCountDatabase::refcount_t
BackupStoreRefCountDatabase::GetRefCount(int64_t ObjectID) const
{
	IOStream::pos_type offset = GetOffset(ObjectID);

	if (GetSize() < offset + GetEntrySize())
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"attempted read of unknown refcount for object " <<
			BOX_FORMAT_OBJECTID(ObjectID), mFilename,
			BackupStoreException, UnknownObjectRefCountRequested);
	}

	mapDatabaseFile->Seek(offset, SEEK_SET);

	refcount_t refcount;
	if (mapDatabaseFile->Read(&refcount, sizeof(refcount)) !=
		sizeof(refcount))
	{
		THROW_FILE_ERROR("Failed to read refcount database: "
			"short read at offset " << offset, mFilename,
			BackupStoreException, CouldNotLoadStoreInfo);
	}

	return ntohl(refcount);
}

int64_t BackupStoreRefCountDatabase::GetLastObjectIDUsed() const
{
	return (GetSize() - sizeof(refcount_StreamFormat)) /
		sizeof(refcount_t);
}

void BackupStoreRefCountDatabase::AddReference(int64_t ObjectID)
{
	refcount_t refcount;

	if (ObjectID > GetLastObjectIDUsed())
	{
		// new object, assume no previous references
		refcount = 0;
	}
	else
	{
		// read previous value from database
		refcount = GetRefCount(ObjectID);
	}

	refcount++;

	SetRefCount(ObjectID, refcount);
}

void BackupStoreRefCountDatabase::SetRefCount(int64_t ObjectID,
	refcount_t NewRefCount)
{
	IOStream::pos_type offset = GetOffset(ObjectID);
	mapDatabaseFile->Seek(offset, SEEK_SET);
	refcount_t RefCountNetOrder = htonl(NewRefCount);
	mapDatabaseFile->Write(&RefCountNetOrder, sizeof(RefCountNetOrder));
}

bool BackupStoreRefCountDatabase::RemoveReference(int64_t ObjectID)
{
	refcount_t refcount = GetRefCount(ObjectID); // must exist in database
	ASSERT(refcount > 0);
	refcount--;
	SetRefCount(ObjectID, refcount);
	return (refcount > 0);
}


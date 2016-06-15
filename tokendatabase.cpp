/**
 * Database responsible for resolving tokens between translation units
 * There is a filename database that manages filename to ID mappings. To
 * facilitate data updates between multiple token databases, each token
 * database should have a reference to the same filename database.
 *
 */

#include "tokendatabase.h"

#include <wx/filename.h>
#include <wx/string.h>
#include <iostream>
#include <wx/mstream.h>

#include "cclogger.h"

enum
{
    ClTokenPacketType_filenames = 1<<0,
    ClTokenPacketType_tokens = 1<<1
};

/** @brief Write an int to an output stream
 *
 * @param out wxOutputStream&
 * @param val const int
 * @return bool
 *
 */
static bool WriteInt( wxOutputStream& out, const int val )
{
    out.Write((const void*)&val, sizeof(val));
    return true;
}

/** @brief Write a long long to an outputstream
 *
 * @param out wxOutputStream&
 * @param val const longlong
 * @return bool
 *
 */
static bool WriteLongLong( wxOutputStream& out, const long long val )
{
    out.Write((const void*)&val, sizeof(val));
    return true;
}

/** @brief Write a string to an output stream
 *
 * @param out wxOutputStream&
 * @param str const char*
 * @return bool
 *
 */
static bool WriteString( wxOutputStream& out, const char* str )
{
    int len = 0;

    if (str != nullptr)
        len = strlen(str); // Need size in amount of bytes
    if (!WriteInt(out, len))
        return false;
    if (len > 0)
        out.Write((const void*)str, len);
    return true;
}

/** @brief Read an int from an input stream
 *
 * @param in wxInputStream&
 * @param out_Int int&
 * @return bool
 *
 */
static bool ReadInt( wxInputStream& in, int& out_Int )
{
    int val = 0;
    if (!in.CanRead())
        return false;
    in.Read(&val, sizeof(val));
    if (in.LastRead() != sizeof(val))
        return false;
    out_Int = val;

    return true;
}

/** @brief Read a long long from an input stream
 *
 * @param in wxInputStream&
 * @param out_LongLong long long&
 * @return bool
 *
 */
static bool ReadLongLong( wxInputStream& in, long long& out_LongLong )
{
    long long val = 0;
    if (!in.CanRead())
        return false;
    in.Read(&val, sizeof(val));
    if (in.LastRead() != sizeof(val))
        return false;
    out_LongLong = val;

    return true;
}

/** @brief Read a string from an input stream
 *
 * @param in wxInputStream&
 * @param out_String wxString&
 * @return bool
 *
 */
static bool ReadString( wxInputStream& in, wxString& out_String )
{
    int len;
    if (!ReadInt(in, len))
        return false;
    if (len == 0)
    {
        out_String = out_String.Truncate(0);
        return true;
    }
    if (!in.CanRead())
        return false;
    char buffer[len + 1];

    in.Read( buffer, len );
    buffer[len] = '\0';

    out_String = wxString::FromUTF8(buffer);

    return true;
}

/** @brief Write a token to an output stream
 *
 * @param token const ClAbstractToken&
 * @param out wxOutputStream&
 * @return bool
 *
 */
bool ClIndexToken::WriteOut( const ClIndexToken& token,  wxOutputStream& out )
{
    // This is a cached database so we don't care about endianness for now. Who will ever copy these from one platform to another?
    WriteInt(out, token.fileId);
    WriteInt(out, token.tokenTypeMask);
    return true;
}

/** @brief Read a token from an input stream
 *
 * @param token ClAbstractToken&
 * @param in wxInputStream&
 * @return bool
 *
 */
bool ClIndexToken::ReadIn( ClIndexToken& token, wxInputStream& in )
{
    int val = 0;
    if (!ReadInt(in, val))
        return false;
    token.tokenTypeMask = (ClTokenType)val;
    if (!ReadInt(in, val))
        return false;
    token.fileId = val;
    return true;
}

/** @brief Filename database constructor
 */
ClFilenameDatabase::ClFilenameDatabase() :
    m_pFileEntries(new ClTreeMap<ClFilenameEntry>())
{
}

ClFilenameDatabase::ClFilenameDatabase(const ClFilenameDatabase& Other) :
    m_pFileEntries( Other.m_pFileEntries )
{

}

ClFilenameDatabase::~ClFilenameDatabase()
{
    delete m_pFileEntries;
}

/** @brief Write a filename database to an output stream
 *
 * @param db const ClFilenameDatabase&
 * @param out wxOutputStream&
 * @return bool
 *
 */
bool ClFilenameDatabase::WriteOut( const ClFilenameDatabase& db, wxOutputStream& out )
{
    int i;
    wxMutexLocker l(db.m_Mutex);
    int cnt = db.m_pFileEntries->GetCount();
    WriteInt(out, cnt);
    for (i = 0; i < cnt; ++i)
    {
        ClFilenameEntry entry = db.m_pFileEntries->GetValue((ClFileId)i);
        if (!WriteString(out, entry.filename.mb_str()))
            return false;
        if (!WriteLongLong(out, entry.timestamp.GetValue().GetValue()))
            return false;
    }
    return true;
}

/** @brief Read a filename database from an input stream
 *
 * @param db ClFilenameDatabase&
 * @param in wxInputStream&
 * @return bool
 *
 */
bool ClFilenameDatabase::ReadIn( ClFilenameDatabase& db, wxInputStream& in )
{
    int i;
    wxMutexLocker l(db.m_Mutex);
    int packetCount = 0;
    if (!ReadInt(in, packetCount))
        return false;
    CCLogger::Get()->DebugLog( F(wxT("Reading %d filenames"), packetCount) );
    for (i = 0; i < packetCount; ++i)
    {
        wxString filename;
        if (!ReadString(in, filename))
            return false;
        long long ts = 0;
        if (!ReadLongLong(in, ts))
            return false;
        db.m_pFileEntries->Insert(filename, ClFilenameEntry(filename, wxDateTime(wxLongLong(ts))));
    }
    return true;
}

bool ClFilenameDatabase::HasFilename( const wxString &filename ) const
{
    wxMutexLocker lock(m_Mutex);
    assert(m_pFileEntries);
    wxFileName fln(filename.c_str());
    fln.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE);
    const wxString& normFile = fln.GetFullPath(wxPATH_UNIX);
    std::set<int> idList;
    m_pFileEntries->GetIdSet(normFile, idList);
    if (idList.empty())
        return false;
    return true;
}

/** @brief Get a filename id from a filename. Creates a new ID if the filename was not known yet.
 *
 * @param filename const wxString&
 * @return ClFileId
 *
 */
ClFileId ClFilenameDatabase::GetFilenameId(const wxString& filename) const
{
    wxMutexLocker lock(m_Mutex);
    assert(m_pFileEntries);
    wxFileName fln(filename.c_str());
    fln.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE);
    const wxString& normFile = fln.GetFullPath(wxPATH_UNIX);
    std::set<int> idList;
    m_pFileEntries->GetIdSet(normFile, idList);
    if (idList.empty())
    {
        wxString f = wxString(normFile.c_str());
        wxDateTime ts; // Timestamp updated when file was parsed into the token database.
        ClFilenameEntry entry(f,ts);
        return m_pFileEntries->Insert(f, entry);
    }
    return *idList.begin();
}

/** @brief Get the filename from its ID
 *
 * @param fId const ClFileId
 * @return wxString
 *
 */
wxString ClFilenameDatabase::GetFilename( const ClFileId fId) const
{
    wxMutexLocker lock(m_Mutex);

    assert(m_pFileEntries);

    if (!m_pFileEntries->HasValue(fId))
    {
        return wxEmptyString;
    }

    ClFilenameEntry entry = m_pFileEntries->GetValue(fId);
    const wxChar* val = entry.filename.c_str();
    if (val == nullptr)
    {
        return wxEmptyString;
    }

    return wxString(val);
}

/** @brief Get the timestamp from a filename
 *
 * @param fId const ClFileId
 * @return const wxDateTime
 *
 * @note Not a reference returned due to multithreading
 */
const wxDateTime ClFilenameDatabase::GetFilenameTimestamp( const ClFileId fId ) const
{
    wxMutexLocker lock(m_Mutex);

    assert(m_pFileEntries->HasValue(fId));

    ClFilenameEntry entry = m_pFileEntries->GetValue(fId);
    return entry.timestamp;
}

/** @brief Update the filename timestamp to indicate we have processed this file at this timestamp.
 *
 * @param fId const ClFileId
 * @param timestamp const wxDateTime&
 * @return void
 *
 */
void ClFilenameDatabase::UpdateFilenameTimestamp( const ClFileId fId, const wxDateTime& timestamp )
{
    wxMutexLocker lock(m_Mutex);

    assert(m_pFileEntries->HasValue(fId));

    ClFilenameEntry& entryRef = m_pFileEntries->GetValue(fId);
    entryRef.timestamp = timestamp;
}

ClTokenDatabase::ClTokenDatabase(ClTokenIndexDatabase* IndexDB) :
        m_pTokenIndexDB( IndexDB ),
        m_pLocalTokenIndexDB( nullptr ),
        m_pTokens(new ClTreeMap<ClAbstractToken>()),
        m_pFileTokens(new ClTreeMap<int>()),
        m_Mutex(wxMUTEX_RECURSIVE)
{
    if (!IndexDB)
    {
        m_pLocalTokenIndexDB = new ClTokenIndexDatabase();
        m_pTokenIndexDB = m_pLocalTokenIndexDB;
    }
}

/** @brief Copy constructor
 *
 * @param other The other ClTokenDatabase
 *
 */
ClTokenDatabase::ClTokenDatabase( const ClTokenDatabase& other) :
    m_pTokenIndexDB(other.m_pTokenIndexDB),
    m_pLocalTokenIndexDB(nullptr),
    m_pTokens(nullptr),
    m_pFileTokens(nullptr),
    m_Mutex(wxMUTEX_RECURSIVE)
{
    if (other.m_pLocalTokenIndexDB)
    {
        m_pLocalTokenIndexDB = new ClTokenIndexDatabase(*other.m_pLocalTokenIndexDB);
        m_pTokenIndexDB = m_pLocalTokenIndexDB;
    }
    wxMutexLocker lock(other.m_Mutex);
    m_pTokens = new ClTreeMap<ClAbstractToken>(*other.m_pTokens);
    m_pFileTokens = new ClTreeMap<int>(*other.m_pFileTokens);
}

/** @brief Destructor
 */
ClTokenDatabase::~ClTokenDatabase()
{
    delete m_pTokens;
    delete m_pFileTokens;
    delete m_pLocalTokenIndexDB;
}

/** @brief Swap 2 token databases
 *
 * @param first ClTokenDatabase&
 * @param second ClTokenDatabase&
 * @return void
 *
 */
void swap( ClTokenDatabase& first, ClTokenDatabase& second )
{
    using std::swap;

    // Let's assume no inverse swap will be performed at the same time for now
    wxMutexLocker l1(first.m_Mutex);
    wxMutexLocker l2(second.m_Mutex);

    swap(first.m_pTokenIndexDB, second.m_pTokenIndexDB);
    swap(first.m_pLocalTokenIndexDB, second.m_pLocalTokenIndexDB);
    swap(first.m_pTokens, second.m_pTokens);
    swap(first.m_pFileTokens, second.m_pFileTokens);
}


/** @brief Read a tokendatabase from disk
 *
 * @param tokenDatabase The tokendatabase to read into
 * @param in The wxInputStream to read from
 * @return true if the operation succeeded, false otherwise
 *
 */
bool ClTokenIndexDatabase::ReadIn( ClTokenIndexDatabase& tokenDatabase, wxInputStream& in )
{
    char buffer[5];
    in.Read( buffer, 4 );
    buffer[4] = '\0';
    int version = 0xff;
    if (!ReadInt(in, version))
        return false;
    int i = 0;
    if (version != 0x01)
    {
        CCLogger::Get()->DebugLog(F(wxT("Wrong version of token database: %d"), version));
        return false;
    }
    tokenDatabase.Clear();
    int read_count = 0;

    wxMutexLocker(tokenDatabase.m_Mutex);
    while (in.CanRead())
    {
        int packetType = 0;
        if (!ReadInt(in, packetType))
        {
            CCLogger::Get()->DebugLog(wxT("TokenIndexDatabase: Could not read packet type"));
            return false;
        }
        switch (packetType)
        {
        case 0:
            return true;
        case ClTokenPacketType_filenames:
            if (!ClFilenameDatabase::ReadIn(tokenDatabase.m_FileDB, in))
            {
                CCLogger::Get()->DebugLog( wxT("Failed to read filename database") );
                return false;
            }
            break;
        case ClTokenPacketType_tokens:
            int packetCount = 0;
            if (!ReadInt(in, packetCount))
                return false;
            for (i = 0; i < packetCount; ++i)
            {
                wxString identifier;
                if (!ReadString( in, identifier ))
                    return false;
                ClIndexToken token;
                int tokenCount = 0;
                if (!ReadInt(in, tokenCount))
                    return false;
                for (int j=0; j<tokenCount; ++j)
                {
                    if (!ClIndexToken::ReadIn( token, in ))
                        return false;
                    read_count++;
                }
            }
            break;
        }
    }
    return true;
}

/** @brief Write the database to an output stream
 *
 * @param tokenDatabase The database to write
 * @param out Were to write the database to
 * @return true if the operation was successful, false otherwise
 *
 */
bool ClTokenIndexDatabase::WriteOut( const ClTokenIndexDatabase& tokenDatabase, wxOutputStream& out )
{
    int cnt;
    out.Write("ClDb", 4); // Magic number
    WriteInt(out, 1); // Version number

    WriteInt(out, ClTokenPacketType_filenames);
    if (!ClFilenameDatabase::WriteOut(tokenDatabase.m_FileDB, out))
        return false;

    wxMutexLocker(tokenDatabase.m_Mutex);

    WriteInt(out, ClTokenPacketType_tokens);
    std::set<wxString> tokens = tokenDatabase.m_pIndexTokenMap->GetKeySet();
    cnt = tokens.size();
    WriteInt(out, cnt);
    //CCLogger::Get()->DebugLog( F(wxT("Token count: %d at %d"), (int)cnt, (int)out.TellO() ) );

    uint32_t written_count = 0;
    for (std::set<wxString>::const_iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
        WriteString( out, (const char*)it->utf8_str() );

        std::set<int> tokenIds;
        tokenDatabase.m_pIndexTokenMap->GetIdSet( *it, tokenIds );
        cnt = tokenIds.size();
        WriteInt(out, cnt);
        for (std::set<int>::const_iterator it = tokenIds.begin(); it != tokenIds.end(); ++it)
        {
            ClIndexToken tok = tokenDatabase.m_pIndexTokenMap->GetValue(*it);
            if (!ClIndexToken::WriteOut(tok, out))
                return false;
            written_count++;
        }
    }
    //CCLogger::Get()->DebugLog(F(_T("Wrote token index database: %d tokens at %d"), (int)written_count, (int)out.TellO()));
    WriteInt(out, 0);
    return true;
}

/** @brief Clear the token database
 *
 */
void ClTokenDatabase::Clear()
{
    wxMutexLocker lock(m_Mutex);
    delete m_pTokens;
    delete m_pFileTokens;
    m_pTokens = new ClTreeMap<ClAbstractToken>(),
    m_pFileTokens = new ClTreeMap<int>();
}

/** @brief Get an ID for a filename. Creates a new ID if the filename was not known yet.
 *
 * @param filename Full path to the filename
 * @return ClFileId
 *
 */
ClFileId ClTokenDatabase::GetFilenameId(const wxString& filename) const
{
    return m_pTokenIndexDB->GetFilenameId(filename);
}

/** @brief Get the filename that is known with an ID
 *
 * @param fId The file ID
 * @return Full path to the file
 *
 */
wxString ClTokenDatabase::GetFilename(ClFileId fId) const
{
    return m_pTokenIndexDB->GetFilename( fId );
}

/** @brief Get the timestamp of the filename entry when the file was last parsed
 *
 * @param fId The file id
 * @return wxDateTime object which represents a timestamp
 *
 */
wxDateTime ClTokenDatabase::GetFilenameTimestamp( const ClFileId fId ) const
{
    return m_pTokenIndexDB->GetFilenameTimestamp(fId);
}

/** @brief Insert or update a token into the token database
 *
 * @param token The token to insert/update
 * @return ClTokenId of the updated token or if the token allready existed the newly inserted token
 *
 */
ClTokenId ClTokenDatabase::InsertToken( const ClAbstractToken& token )
{
    wxMutexLocker lock(m_Mutex);

    ClTokenId tId = GetTokenId(token.identifier, token.fileId, token.tokenType, token.tokenHash);
    if (tId == wxNOT_FOUND)
    {
        tId = m_pTokens->Insert(wxString(token.identifier), token);
        wxString filen = wxString::Format(wxT("%d"), token.fileId);
        m_pFileTokens->Insert(filen, tId);
    }
    return tId;
}

/** @brief Find a token ID by value
 *
 * @param identifier const wxString&
 * @param fileId ClFileId
 * @param tokenType ClTokenType
 * @param tokenHash unsigned
 * @return ClTokenId
 *
 */
ClTokenId ClTokenDatabase::GetTokenId( const wxString& identifier, ClFileId fileId, ClTokenType tokenType, unsigned tokenHash ) const
{
    wxMutexLocker lock(m_Mutex);
    std::set<int> ids;
    m_pTokens->GetIdSet(identifier, ids);
    for (std::set<int>::const_iterator itr = ids.begin();
            itr != ids.end(); ++itr)
    {
        if (m_pTokens->HasValue(*itr))
        {
            ClAbstractToken tok = m_pTokens->GetValue(*itr);
            if (   (tok.tokenHash == tokenHash)
                && ((tok.tokenType == tokenType) || (tokenType == ClTokenType_Unknown))
                && ((tok.fileId == fileId) || (fileId == wxNOT_FOUND)) )
            {

                return *itr;
            }
        }
    }
    return wxNOT_FOUND;
}

/** @brief Get a token with it's ID
 *
 * @param tId const ClTokenId
 * @return ClAbstractToken
 *
 * @note No reference returned for multi-threading reasons
 */
ClAbstractToken ClTokenDatabase::GetToken(const ClTokenId tId) const
{
    wxMutexLocker lock(m_Mutex);
    assert(m_pTokens->HasValue(tId));
    return m_pTokens->GetValue(tId);
}

/** @brief Find the token IDs of all matches of an identifier
 *
 * @param identifier const wxString&
 * @return std::vector<ClTokenId>
 *
 */
void ClTokenDatabase::GetTokenMatches(const wxString& identifier, std::set<ClTokenId>& out_List) const
{
    wxMutexLocker lock(m_Mutex);
    m_pTokens->GetIdSet(identifier, out_List);
}

/** @brief Get all tokens linked to a file ID
 *
 * @param fId const ClFileId
 * @return std::vector<ClTokenId>
 *
 */
void ClTokenDatabase::GetFileTokens(const ClFileId fId, std::set<ClTokenId>& out_tokens) const
{
    wxMutexLocker lock(m_Mutex);
    wxString key = wxString::Format(wxT("%d"), fId);
    m_pFileTokens->GetIdSet(key, out_tokens);
}

/** @brief Shrink the database to reclaim some memory
 *
 * @return void
 *
 */
void ClTokenDatabase::Shrink()
{
    wxMutexLocker lock(m_Mutex);
    m_pTokens->Shrink();
    m_pFileTokens->Shrink();
}

/** @brief Update a token that was previously cleared
 *
 * @param freeTokenId const ClTokenId
 * @param token const ClAbstractToken&
 *
 */
void ClTokenDatabase::UpdateToken( const ClTokenId freeTokenId, const ClAbstractToken& token )
{
    ClAbstractToken tokenRef = m_pTokens->GetValue(freeTokenId);
    m_pTokens->RemoveIdKey(tokenRef.identifier, freeTokenId);
    assert((tokenRef.fileId == wxNOT_FOUND) && "Only an unused token can be updated");
    tokenRef.fileId = token.fileId;
    tokenRef.identifier = token.identifier;
    tokenRef.location = token.location;
    tokenRef.tokenHash = token.tokenHash;
    tokenRef.tokenType = token.tokenType;
    wxString filen = wxString::Format(wxT("%d"), token.fileId);
    m_pFileTokens->Insert(filen, freeTokenId);
}

/** @brief Remove a token from the token database
 *
 * @param tokenId const ClTokenId
 * @return void
 *
 * This will not remove the token, it will only clear it in memory and will later on be reused.
 */
void ClTokenDatabase::RemoveToken( const ClTokenId tokenId )
{
    ClAbstractToken oldToken = GetToken(tokenId);
    wxString key = wxString::Format(wxT("%d"), oldToken.fileId);
    m_pFileTokens->Remove(key, tokenId);
    ClAbstractToken t;
    // We just invalidate it here. Real removal is rather complex
    UpdateToken(tokenId, t);
}

unsigned long ClTokenDatabase::GetTokenCount()
{
    return m_pTokens->GetCount();
}

void ClTokenDatabase::StoreIndexes() const
{
    wxMutexLocker lock(m_Mutex);
    ClTokenId id;
    uint32_t cnt = m_pTokenIndexDB->GetTokenCount();
    for (id=0; id < m_pTokens->GetCount(); ++id)
    {
        ClAbstractToken& token = m_pTokens->GetValue( id );
        m_pTokenIndexDB->UpdateToken( token.identifier, token.fileId, token.tokenType );
    }
    uint32_t cnt2 = m_pTokenIndexDB->GetTokenCount();
    //CCLogger::Get()->DebugLog( F(wxT("StoreIndexes: Stored %d tokens. %d tokens extra. Total: %d (merged) IndexDb: %p"), (int)m_pTokens->GetCount(), (int)cnt2 - cnt, cnt2, m_pTokenIndexDB) );
}

bool ClTokenDatabase::LookupTokenDefinition( const ClFileId fileId, const wxString& identifier, const wxString& usr, ClTokenPosition& out_Position) const
{
    std::set<ClTokenId> tokenIdList;
    GetTokenMatches(identifier, tokenIdList);

    for (std::set<ClTokenId>::const_iterator it = tokenIdList.begin(); it != tokenIdList.end(); ++it)
    {
        ClAbstractToken tok = GetToken( *it );
        if (tok.fileId == fileId)
        {
            if ( (tok.tokenType&ClTokenType_DefGroup) == ClTokenType_DefGroup ) // We only want token definitions
            {
                CCLogger::Get()->DebugLog( wxT("Candidate: ")+tok.identifier+wxT(" USR=")+tok.USR );
                if( (usr.Length() == 0)||(tok.USR == usr))
                {
                    out_Position = tok.location;
                    return true;
                }
            }
        }
    }
    return false;
}



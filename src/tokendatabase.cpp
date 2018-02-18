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
#include <clang-c/Index.h>
#include <string>

#define CL_INDEXFILE_VERSION 0x07

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
    if (len > 32*1024)
    {
        return false;
    }
    if (!in.CanRead())
        return false;
    char buffer[len + 1];

    in.Read( buffer, len );
    if (in.LastRead() != len)
        return false;
    buffer[len] = '\0';

    out_String = wxString::FromUTF8(buffer);

    return true;
}

/** @brief Read a string from an input stream
 *
 * @param in wxInputStream&
 * @param out_String wxString&
 * @return bool
 *
 */
static bool ReadString( wxInputStream& in, std::string& out_String )
{
    int len;
    if (!ReadInt(in, len))
        return false;
    if (len == 0)
    {
        out_String.clear();
        return true;
    }
    if (len > 32*1024)
    {
        return false;
    }
    if (!in.CanRead())
        return false;
    char buffer[len + 1];

    in.Read( buffer, len );
    if (in.LastRead() != len)
        return false;
    buffer[len] = '\0';

    out_String = buffer;

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
    WriteString( out, token.displayName.utf8_str());
    WriteString( out, token.USR.c_str() );
    WriteInt(out, (int)token.category);
    WriteInt(out, (int)token.locationList.size());
    for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it )
    {
        WriteInt(out, it->tokenType);
        WriteInt(out, it->fileId);
        WriteInt(out, it->range.beginLocation.line);
        WriteInt(out, it->range.beginLocation.column);
        WriteInt(out, it->range.endLocation.line);
        WriteInt(out, it->range.endLocation.column);
    }
    WriteInt(out, (int)token.parentTokenList.size());
    for (std::vector< std::pair<ClIdentifierString, ClUSRString> >::const_iterator it = token.parentTokenList.begin(); it != token.parentTokenList.end(); ++it)
    {
        WriteString( out, it->first.c_str() );
        WriteString( out, it->second.c_str() );
    }
    WriteString( out, token.semanticScope.first.c_str() );
    WriteString( out, token.semanticScope.second.c_str() );
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
    token.locationList.clear();
    token.parentTokenList.clear();
    int val = 0;
    if (!ReadString( in, token.displayName) )
        return false;
    if (!ReadString( in, token.USR ))
        return false;
    if (!ReadInt( in, val ))
        return false;
    token.category = (ClTokenCategory)val;
    // Token position list
    if (!ReadInt(in, val))
        return false;
    for (unsigned int i = 0; i < (unsigned int)val; ++i)
    {
        int typ = 0;
        int fileId = 0;
        int beginLine = 0;
        int beginColumn = 0;
        int endLine = 0;
        int endColumn = 0;
        if (!ReadInt(in, typ))
            return false;
        if (!ReadInt(in, fileId))
            return false;
        if (!ReadInt(in, beginLine))
            return false;
        if (!ReadInt(in, beginColumn))
            return false;
        if (!ReadInt(in, endLine))
            return false;
        if (!ReadInt(in, endColumn))
            return false;
        token.locationList.push_back( ClIndexTokenLocation(static_cast<ClTokenType>(typ), fileId, ClTokenRange(ClTokenPosition(beginLine,beginColumn), ClTokenPosition(endLine, endColumn))));
        token.tokenTypeMask = static_cast<ClTokenType>(token.tokenTypeMask | typ);
    }

    // Child USR list
    if (!ReadInt(in, val))
        return false;

    for (unsigned int i = 0; i< (unsigned int)val; ++i)
    {
        ClIdentifierString identifier;
        if (!ReadString( in, identifier ))
            return false;
        ClUSRString USR;
        if (!ReadString( in, USR ))
            return false;
        token.parentTokenList.push_back( std::make_pair( identifier, USR ) );
    }
    if (!ReadString( in, token.semanticScope.first ))
        return false;
    if (!ReadString( in, token.semanticScope.second ))
        return false;

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

void ClFilenameDatabase::AddFilename(const ClFilenameEntry& file)
{
    m_pFileEntries->Insert( file.filename, file );
}

std::vector<ClFilenameEntry> ClFilenameDatabase::GetFilenames() const
{
    std::vector<ClFilenameEntry> filenames;
    unsigned int cnt = m_pFileEntries->GetCount();
    for (unsigned int i = 0; i<cnt; ++i)
    {
        filenames.push_back(m_pFileEntries->GetValue(i));
    }
    return filenames;
}


bool ClFilenameDatabase::HasFilename( const std::string &filename ) const
{
    assert(m_pFileEntries);
    wxFileName fln(wxString::FromUTF8( filename.c_str() ));
    fln.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE);
#ifdef __WXMSW__
    wxPathFormat pathFormat = wxPATH_WIN;
#else
    wxPathFormat pathFormat = wxPATH_UNIX;
#endif // __WXMSW__

    const wxString& normFile = fln.GetFullPath(pathFormat);
    if (normFile.Len() <= 0 )
        return false;
    std::set<int> idList;

    m_pFileEntries->GetIdSet(normFile.ToUTF8().data(), idList);
    if (idList.empty())
        return false;
    return true;
}

/** @brief Get a filename id from a filename. Creates a new ID if the filename was not known yet.
 *
 * @param filename const std::string&
 * @return ClFileId
 *
 */
ClFileId ClFilenameDatabase::GetFilenameId(const std::string& filename) const
{
    assert(m_pFileEntries);
    wxFileName fln(wxString::FromUTF8( filename.c_str() ));
    fln.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE);
#ifdef __WXMSW__
    wxPathFormat pathFormat = wxPATH_WIN;
#else
    wxPathFormat pathFormat = wxPATH_UNIX;
#endif // __WXMSW__
    const wxString& normFile = fln.GetFullPath(pathFormat);
    std::set<int> idList;

    m_pFileEntries->GetIdSet(normFile.ToUTF8().data(), idList);
    if (idList.empty())
    {
        wxString f = wxString(normFile.c_str());
        wxDateTime ts; // Timestamp updated when file was parsed into the token database.
        ClFilenameEntry entry(f.ToUTF8().data(),ts);
        return m_pFileEntries->Insert(entry.filename, entry);
    }
    return *idList.begin();
}

/** @brief Get the filename from its ID
 *
 * @param fId const ClFileId
 * @return wxString
 *
 */
std::string ClFilenameDatabase::GetFilename( const ClFileId fId) const
{
    assert(m_pFileEntries);

    if (!m_pFileEntries->HasValue(fId))
    {
        return "";
    }

    ClFilenameEntry entry = m_pFileEntries->GetValue(fId);
    return entry.filename;
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
    assert(m_pFileEntries->HasValue(fId));

    ClFilenameEntry& entryRef = m_pFileEntries->GetValue(fId);
    entryRef.timestamp = timestamp;
}

/** @brief Constructor
 *
 * @param pIndexDB ClTokenIndexDatabase*
 *
 */
ClTokenDatabase::ClTokenDatabase(ClTokenIndexDatabase* pIndexDB) :
        m_pTokenIndexDB( pIndexDB ),
        m_pLocalTokenIndexDB( nullptr ),
        m_pTokens(new ClTreeMap<ClAbstractToken>()),
        m_pFileTokens(new ClTreeMap<ClTokenId>())
{
    if (!pIndexDB)
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
    m_pTokenIndexDB(nullptr),
    m_pLocalTokenIndexDB(nullptr),
    m_pTokens(nullptr),
    m_pFileTokens(nullptr)
{
    if (other.m_pLocalTokenIndexDB)
    {
        m_pLocalTokenIndexDB = new ClTokenIndexDatabase(*other.m_pLocalTokenIndexDB);
        m_pTokenIndexDB = m_pLocalTokenIndexDB;
    }
    else
    {
        m_pTokenIndexDB = other.m_pTokenIndexDB;
    }
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

    swap(first.m_pTokenIndexDB, second.m_pTokenIndexDB);
    swap(first.m_pLocalTokenIndexDB, second.m_pLocalTokenIndexDB);
    swap(first.m_pTokens, second.m_pTokens);
    swap(first.m_pFileTokens, second.m_pFileTokens);
}

std::set<ClFileId> ClTokenIndexDatabase::LookupTokenFileList( const ClIdentifierString& identifier, const ClUSRString& USR, const ClTokenType typeMask ) const
{
    std::set<ClFileId> retList;
    std::set<int> idList;

    wxMutexLocker locker(m_Mutex);

    m_pIndexTokenMap->GetIdSet( identifier, idList );
    for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
    {
        ClIndexToken& token = m_pIndexTokenMap->GetValue(*it);
        if (((token.tokenTypeMask&typeMask)==typeMask )&&((USR.empty())||(token.USR.empty())||(USR == token.USR)))
        {
            for (std::vector<ClIndexTokenLocation>::const_iterator it2 = token.locationList.begin(); it2 != token.locationList.end(); ++it2)
                retList.insert( it2->fileId );
        }
    }
    return retList;
}

bool ClTokenIndexDatabase::LookupTokenOverrideParents( const ClIdentifierString& identifier, const std::string& USR, std::vector<ClUSRString>& out_overrideList ) const
{
    bool found = false;
    std::set<int> idList;
    wxMutexLocker locker(m_Mutex);

    m_pIndexTokenMap->GetIdSet( identifier, idList );
    for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
    {
        ClIndexToken& token = m_pIndexTokenMap->GetValue(*it);
        if (USR.empty()||(token.USR == USR))
        {
            for (std::vector<std::pair<ClIdentifierString,ClUSRString> >::const_iterator usrIt = token.parentTokenList.begin(); usrIt != token.parentTokenList.end(); ++usrIt)
            {
                if (std::find(out_overrideList.begin(),out_overrideList.end(),usrIt->second)==out_overrideList.end())
                {
                    out_overrideList.push_back(usrIt->second);
                    found = true;
                }
            }
        }
    }
    return found;
}

bool ClTokenIndexDatabase::LookupTokenOverrideChildren( const ClIdentifierString &identifier, const std::string &USR, std::vector<ClUSRString> &out_overrideList ) const
{
    bool found = false;
    std::set<int> idList;

    wxMutexLocker locker(m_Mutex);

    m_pIndexTokenMap->GetIdSet( identifier, idList );
    for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
    {
        ClIndexToken& token = m_pIndexTokenMap->GetValue(*it);
        if (std::find( token.parentTokenList.begin(), token.parentTokenList.end(), std::make_pair(identifier,USR) ) != token.parentTokenList.end())
        {
            if (std::find(out_overrideList.begin(), out_overrideList.end(), token.USR)==out_overrideList.end())
                out_overrideList.push_back( token.USR );
            found = true;
        }
    }
    return found;
}

/** @brief Update a token in the token database
 *
 * @param identifier const wxString&
 * @param displayName const wxString&
 * @param fileId const ClFileId
 * @param USR const ClUSRString&
 * @param tokType const ClTokenType
 * @param tokenRange const ClTokenRange&
 * @param overrideTokenList std::pair<wxString const std::vector<ClUSRString> >&
 * @param scope std::pair<wxString const, scope ClUSRString>&
 * @return void
 *
 */
void ClTokenIndexDatabase::UpdateToken( const ClIdentifierString& identifier, const wxString& displayName, const ClFileId fileId, const ClUSRString& USR, const ClTokenType tokType, const ClTokenRange& tokenRange, const ClTokenCategory category, const std::vector< std::pair<ClIdentifierString,ClUSRString> >& overrideTokenList, const std::pair<ClIdentifierString,ClUSRString>& scope)
{
    std::set<int> idList;

    wxMutexLocker locker(m_Mutex);

    m_pIndexTokenMap->GetIdSet( identifier, idList );
    for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
    {
        ClIndexToken& token = m_pIndexTokenMap->GetValue( *it );
        if ( token.USR == USR )
        {
            token.tokenTypeMask = static_cast<ClTokenType>(token.tokenTypeMask | tokType);
            ClIndexTokenLocation location(tokType, fileId, tokenRange);
            if (token.locationList.empty())
            {
                token.displayName = displayName;
                token.locationList.push_back( location );
            }
            else
            {
                if (token.displayName.IsEmpty())
                    token.displayName = displayName;
                if (std::find(token.locationList.begin(), token.locationList.end(), location) == token.locationList.end())
                    token.locationList.push_back( location );
            }
            for (std::vector<std::pair<ClIdentifierString,ClUSRString> >::const_iterator usrIt = overrideTokenList.begin(); usrIt != overrideTokenList.end(); ++usrIt)
            {
                if (std::find( token.parentTokenList.begin(), token.parentTokenList.end(), *usrIt ) == token.parentTokenList.end())
                {
                    token.parentTokenList.push_back( *usrIt );
                }
            }
            wxString fid = wxString::Format( wxT("%d"), fileId );
            //m_pFileTokens->Remove( fid, *it );
            m_pFileTokens->Insert( fid.ToUTF8().data(), *it );
            m_bModified = true;
            return;
        }
    }
    ClTokenId id = m_pIndexTokenMap->Insert( identifier, ClIndexToken(identifier, displayName, fileId, USR, tokType, tokenRange, category, overrideTokenList, scope) );
    wxString fid = wxString::Format( wxT("%d"), fileId );
    m_pFileTokens->Insert( fid.ToUTF8().data(), id );
    m_bModified = true;
}

/** @brief Removes all token references that refer to the specified file
 *
 * @param fileId const ClFileId
 * @return void
 *
 */
void ClTokenIndexDatabase::RemoveFileTokens(const ClFileId fileId)
{
    std::string key = wxString::Format(wxT("%d"), fileId).ToUTF8().data();
    std::set<ClTokenId> tokenList;

    wxMutexLocker locker(m_Mutex);
    m_pFileTokens->GetIdSet(key, tokenList);
    for (std::set<ClTokenId>::iterator it = tokenList.begin(); it != tokenList.end(); ++it)
    {
        if (!m_pIndexTokenMap->HasValue( *it ))
            continue;
        ClIndexToken& tok = m_pIndexTokenMap->GetValue( m_pFileTokens->GetValue( *it ) );
        for ( std::vector< ClIndexTokenLocation >::iterator itLoc = tok.locationList.begin(); itLoc != tok.locationList.end(); ++itLoc )
        {
            if (itLoc->fileId == fileId)
            {
                itLoc = tok.locationList.erase( itLoc );
                if (itLoc == tok.locationList.end())
                    break;
            }
        }
    }
    m_pFileTokens->Remove(key);
}


bool ClTokenIndexDatabase::LookupTokenPosition(const ClIdentifierString& identifier, const ClFileId fileId, const ClUSRString& USR, const ClTokenType tokenTypeMask, ClTokenPosition& out_Position) const
{
    std::set<int> idList;
    wxMutexLocker locker(m_Mutex);

    m_pIndexTokenMap->GetIdSet( identifier, idList );
    for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
    {
        ClIndexToken& token = m_pIndexTokenMap->GetValue( *it );
        if ((token.tokenTypeMask&tokenTypeMask)==tokenTypeMask)
        {
            if ((USR.empty())||(USR == token.USR))
            {
                for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it)
                {
                    if (((it->tokenType&tokenTypeMask)==tokenTypeMask)&&(it->fileId == fileId) )
                    {
                        out_Position = it->range.beginLocation;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool ClTokenIndexDatabase::LookupTokenPosition(const ClIdentifierString& identifier, const ClFileId fileId, const ClUSRString& USR, const ClTokenType tokenTypeMask, ClTokenRange& out_Range) const
{
    std::set<int> idList;

    wxMutexLocker locker(m_Mutex);

    m_pIndexTokenMap->GetIdSet( identifier, idList );
    for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
    {
        ClIndexToken& token = m_pIndexTokenMap->GetValue( *it );
        if ((token.tokenTypeMask&tokenTypeMask)==tokenTypeMask)
        {
            if ((USR.empty())||(USR == token.USR))
            {
                for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it)
                {
                    if (((it->tokenType&tokenTypeMask)==tokenTypeMask)&&(it->fileId == fileId) )
                    {
                        out_Range = it->range;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool ClTokenIndexDatabase::LookupTokenDisplayName(const ClIdentifierString& identifier, const ClUSRString& USR, wxString& out_DisplayName) const
{
    std::set<int> idList;

    wxMutexLocker locker(m_Mutex);

    m_pIndexTokenMap->GetIdSet( identifier, idList );
    for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
    {
        ClIndexToken& token = m_pIndexTokenMap->GetValue( *it );
        if ((USR.empty())||(USR == token.USR))
        {
            out_DisplayName = token.displayName;
            wxString parentScopeDisplayName;
            if (LookupTokenDisplayName( token.semanticScope.first, token.semanticScope.second, parentScopeDisplayName ))
            {
                out_DisplayName = parentScopeDisplayName + wxT("::")+out_DisplayName;
            }
            return true;
        }
    }
    return false;
}

bool ClTokenIndexDatabase::LookupTokenType(const ClIdentifierString& identifier, const ClFileId fileId, const ClUSRString& USR, const ClTokenPosition Position,  ClTokenType& out_TokenType) const
{
    std::set<int> idList;
    if (identifier.empty())
        return false;
    wxMutexLocker locker(m_Mutex);

    m_pIndexTokenMap->GetIdSet( identifier, idList );
    for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
    {
        ClIndexToken& token = m_pIndexTokenMap->GetValue( *it );
        if ((USR.empty())||(USR == token.USR))
        {
            for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it)
            {
                if (it->fileId == fileId)
                {
                    if (it->range.InRange( Position ))
                    {
                        out_TokenType = it->tokenType;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/** @brief Get all tokens linked to a file ID
 *
 * @param fId const ClFileId
 * @return std::vector<ClTokenId>
 *
 */
void ClTokenIndexDatabase::GetFileTokens(const ClFileId fId, const int tokenTypeMask, std::vector<ClIndexToken>& out_tokens) const
{
    std::string key = wxString::Format(wxT("%d"), fId).ToUTF8().data();
    std::set<int> tokenList;

    wxMutexLocker locker(m_Mutex);
    m_pFileTokens->GetIdSet(key, tokenList);
    for (std::set<int>::const_iterator it = tokenList.begin(); it != tokenList.end(); ++it)
    {
        const ClIndexToken& tok = m_pIndexTokenMap->GetValue( m_pFileTokens->GetValue( *it ) );
        if ( (tokenTypeMask==0)||(tok.tokenTypeMask&tokenTypeMask) )
        {
            out_tokens.push_back(tok);
            for (std::vector<ClIndexTokenLocation>::iterator itLoc = out_tokens.back().locationList.begin(); itLoc != out_tokens.back().locationList.end(); ++itLoc)
            {
                if( (itLoc->fileId != fId)||( (tokenTypeMask != 0)&&( (itLoc->tokenType&tokenTypeMask) ==0) )  )
                {
                    itLoc = out_tokens.back().locationList.erase( itLoc );
                    if (itLoc == out_tokens.back().locationList.end())
                        break;
                }
            }
        }
    }
}

/** @brief Get all tokens linked to a file ID with a specific USR
 *
 * @param fId const ClFileId
 * @return std::vector<ClTokenId>
 *
 */
void ClTokenIndexDatabase::GetFileTokens(const ClFileId fId, const int tokenTypeMask, const ClUSRString& usr, std::vector<ClIndexToken>& out_tokens) const
{
    std::string key = wxString::Format(wxT("%d"), fId).ToUTF8().data();
    std::set<int> tokenList;

    wxMutexLocker locker(m_Mutex);
    m_pFileTokens->GetIdSet(key, tokenList);
    for (std::set<int>::const_iterator it = tokenList.begin(); it != tokenList.end(); ++it)
    {
        const ClIndexToken& tok = m_pIndexTokenMap->GetValue( m_pFileTokens->GetValue( *it ) );
        if ( ((tokenTypeMask==0)||(tok.tokenTypeMask&tokenTypeMask))&&(tok.USR == usr) )
        {
            out_tokens.push_back(tok);
            for (std::vector<ClIndexTokenLocation>::iterator itLoc = out_tokens.back().locationList.begin(); itLoc != out_tokens.back().locationList.end(); ++itLoc)
            {
                if( (itLoc->fileId != fId)||( (tokenTypeMask != 0)&&( (itLoc->tokenType&tokenTypeMask) ==0) )  )
                {
                    itLoc = out_tokens.back().locationList.erase( itLoc );
                    if (itLoc == out_tokens.back().locationList.end())
                        break;
                }
            }
        }
    }
}


void ClTokenIndexDatabase::AddToken( const ClIndexToken& token)
{
    if (token.identifier.empty())
    {
        return;
    }
    if (token.locationList.empty())
    {
        return;
    }
    ClTokenId id = wxNOT_FOUND;
    wxMutexLocker locker(m_Mutex);
    std::set<ClTokenId> ids;
    m_pIndexTokenMap->GetIdSet( token.identifier, ids );
    for (std::set<ClTokenId>::const_iterator it = ids.begin(); it != ids.end(); ++it)
    {
        ClIndexToken& tokenRef = m_pIndexTokenMap->GetValue( *it );
        if (tokenRef.USR == token.USR)
        {
            CCLogger::Get()->DebugLog( wxT("ERROR: internal db consistency error updating ")+wxString::FromUTF8(token.identifier.c_str()) );
            for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it)
            {
                tokenRef.locationList.push_back( *it );
            }
            id = *it;
        }
    }

    if (id == wxNOT_FOUND)
        id = m_pIndexTokenMap->Insert( token.identifier, token );
    for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it)
    {
        wxString fid = wxString::Format( wxT("%d"), it->fileId );
        //m_pFileTokens->Remove( fid, id );
        m_pFileTokens->Insert( fid.ToUTF8().data(), id );
    }
    m_bModified = true;
}

std::vector<ClIndexToken> ClTokenIndexDatabase::GetTokens() const
{
    std::vector<ClIndexToken> tokenList;
    wxMutexLocker locker(m_Mutex);
    std::set<ClIdentifierString> tokens = m_pIndexTokenMap->GetKeySet();
    for (std::set<ClIdentifierString>::const_iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
        std::set<int> tokenIds;
        m_pIndexTokenMap->GetIdSet( *it, tokenIds );
        for (std::set<int>::const_iterator it = tokenIds.begin(); it != tokenIds.end(); ++it)
        {
            tokenList.push_back( m_pIndexTokenMap->GetValue(*it) );
        }

    }
    return tokenList;
}


/** @brief Read a tokendatabase from disk
 *
 * @param tokenDatabase The tokendatabase to read into
 * @param in The wxInputStream to read from
 * @return true if the operation succeeded, false otherwise
 *
 */
bool CTokenIndexDatabasePersistence::ReadIn( IPersistentTokenIndexDatabase& tokenDatabase, wxInputStream& in )
{
    char buffer[5];
    in.Read( buffer, 4 );
    buffer[4] = '\0';
    int version = 0xff;
    if (!ReadInt(in, version))
        return false;
    int i = 0;
    if (version != CL_INDEXFILE_VERSION)
    {
        CCLogger::Get()->DebugLog(F(wxT("Wrong version of token database: %d"), version));
        return false;
    }
    int major = 0;
    int minor = 0;
    if (!ReadInt(in, major))
        return false;
    if (!ReadInt(in, minor))
        return false;
    if (major != CINDEX_VERSION_MAJOR)
    {
        CCLogger::Get()->Log( wxT("Major version mismatch between Clang indexdb and libclang") );
        return false;
    }
    if (minor != CINDEX_VERSION_MINOR)
    {
        CCLogger::Get()->Log( wxT("Minor version mismatch between Clang indexdb and libclang") );
        return false;
    }

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
            {
                int i;
                int packetCount = 0;
                if (!ReadInt(in, packetCount))
                    return false;
                for (i = 0; i < packetCount; ++i)
                {
                    wxString filename;
                    if (!ReadString(in, filename))
                        return false;
                    long long ts = 0;
                    if (!ReadLongLong(in, ts))
                        return false;
                    tokenDatabase.GetFilenameDatabase()->AddFilename( ClFilenameEntry(filename.ToUTF8().data(), wxDateTime(wxLongLong(ts))) );
                }
            }
            break;
        case ClTokenPacketType_tokens:
            {
                int packetCount = 0;
                if (!ReadInt(in, packetCount))
                    return false;
                for (i = 0; i < packetCount; ++i)
                {
                    ClIdentifierString identifier;
                    if (!ReadString( in, identifier ))
                    {
                        return false;
                    }
                    ClIndexToken token(identifier);
                    if (!ClIndexToken::ReadIn( token, in ))
                    {
                        return false;
                    }
                    tokenDatabase.AddToken( token );
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
bool CTokenIndexDatabasePersistence::WriteOut( const IPersistentTokenIndexDatabase& tokenDatabase, wxOutputStream& out )
{
    out.Write("ClDb", 4); // Magic number
    WriteInt(out, CL_INDEXFILE_VERSION); // Version number
    WriteInt(out, CINDEX_VERSION_MAJOR); // CLANG VERSION, USR strings are clang-version specific
    WriteInt(out, CINDEX_VERSION_MINOR);

    WriteInt(out, ClTokenPacketType_filenames);
    {
        std::vector<ClFilenameEntry> filenames = tokenDatabase.GetFilenameDatabase()->GetFilenames();
        WriteInt(out, (int)filenames.size());
        for (std::vector<ClFilenameEntry>::const_iterator it = filenames.begin(); it != filenames.end(); ++it)
        {
            if (!WriteString(out, it->filename.c_str()))
                return false;
            long long ts = 0;
            if (it->timestamp.IsValid())
                ts = it->timestamp.GetValue().GetValue();
            if (!WriteLongLong(out, ts))
                return false;
        }
    }

    WriteInt(out, ClTokenPacketType_tokens);
    std::vector<ClIndexToken> tokens = tokenDatabase.GetTokens();
    WriteInt(out, (int)tokens.size());

    for (std::vector<ClIndexToken>::const_iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
        if (!WriteString( out, (const char*)it->identifier.c_str() ) )
        {
            return false;
        }
        if (!ClIndexToken::WriteOut( *it, out ))
        {
            return false;
        }
    }
    WriteInt(out, 0);
    return true;
}

/** @brief Clear the token database
 *
 */
void ClTokenDatabase::Clear()
{
    delete m_pTokens;
    m_pTokens = new ClTreeMap<ClAbstractToken>(),
    delete m_pFileTokens;
    m_pFileTokens = new ClTreeMap<int>();
}

/** @brief Get an ID for a filename. Creates a new ID if the filename was not known yet.
 *
 * @param filename Full path to the filename
 * @return ClFileId
 *
 */
ClFileId ClTokenDatabase::GetFilenameId(const std::string& filename) const
{
    return m_pTokenIndexDB->GetFilenameId(filename);
}

/** @brief Get the filename that is known with an ID
 *
 * @param fId The file ID
 * @return Full path to the file
 *
 */
std::string ClTokenDatabase::GetFilename(ClFileId fId) const
{
    assert(m_pTokenIndexDB);
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
    ClTokenId tId = m_pTokens->Insert(token.identifier, token);
    wxString filen = wxString::Format(wxT("%d"), token.fileId);
    m_pFileTokens->Insert(filen.ToUTF8().data(), tId);
    return tId;
}

/** @brief Find a token ID by value
 *
 * @param identifier const wxString&
 * @param fileId ClFileId
 * @param tokenType ClTokenType
 * @param USR wxString
 * @return ClTokenId
 *
 */
ClTokenId ClTokenDatabase::GetTokenId( const ClIdentifierString& identifier, const ClFileId fileId, const ClTokenType tokenType, const int tokenHash ) const
{
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
    assert(m_pTokens->HasValue(tId));
    return m_pTokens->GetValue(tId);
}

/** @brief Find the token IDs of all matches of an identifier
 *
 * @param identifier const ClIdentifierString&
 * @return std::vector<ClTokenId>
 *
 */
void ClTokenDatabase::GetTokenMatches(const ClIdentifierString& identifier, std::set<ClTokenId>& out_List) const
{
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
    std::string key = wxString::Format(wxT("%d"), fId).ToUTF8().data();
    m_pFileTokens->GetIdSet(key, out_tokens);
}

/** @brief Get all FileIds referenced by tokens in this Token Database
 * @param fileIds[out] Returns a set of all FileIds.
 */
void ClTokenDatabase::GetAllTokenFiles(std::set<ClFileId>& out_fileIds) const
{
    std::set<ClIdentifierString> keys = m_pFileTokens->GetKeySet();
    for (std::set<ClIdentifierString>::const_iterator it = keys.begin(); it != keys.end(); ++it)
    {
        out_fileIds.insert( std::atoi( it->c_str() ) );
    }
}


void ClTokenDatabase::GetAllFileTokenScopes(const ClFileId fileId, const unsigned TokenTypeMask, std::vector<ClTokenScope>& out_Scopes) const
{
    std::set<ClTokenId> tokenIds;
    std::string key = wxString::Format(wxT("%d"), fileId).ToUTF8().data();
    m_pFileTokens->GetIdSet(key, tokenIds);
    for (std::set<ClTokenId>::const_iterator it = tokenIds.begin(); it != tokenIds.end(); ++it)
    {
        ClAbstractToken token = GetToken( *it );
        if (!token.displayName.IsEmpty())
        {
            if (token.tokenType&TokenTypeMask)
            {
                wxString scopeName = wxString::FromUTF8( token.scope.first.c_str() );
                GetTokenIndexDatabase()->LookupTokenDisplayName( token.scope.first, token.scope.second, scopeName );
                out_Scopes.push_back( ClTokenScope(wxString::FromUTF8(token.identifier.c_str()), token.displayName, scopeName, token.range, token.category) );
            }
        }
    }
}

void ClTokenDatabase::GetTokenScopeAt(const ClFileId fileId, const ClTokenPosition pos, const unsigned TokenTypeMask, ClTokenScope& out_Scope) const
{
    ClTokenScope scope;
    std::set<ClTokenId> tokenIds;
    std::string key = wxString::Format(wxT("%d"), fileId).ToUTF8().data();
    m_pFileTokens->GetIdSet(key, tokenIds);
    for (std::set<ClTokenId>::const_iterator it = tokenIds.begin(); it != tokenIds.end(); ++it)
    {
        ClAbstractToken token = GetToken( *it );
        if (!token.displayName.IsEmpty())
        {
            if ( token.tokenType&TokenTypeMask )
            {
                if (token.range.InRange( pos ))
                {
                    if (token.range.beginLocation > scope.GetTokenRange().beginLocation)
                    {
                        scope.GetTokenIdentifier() = wxString::FromUTF8( token.identifier.c_str() );
                        scope.GetTokenDisplayName() = token.displayName;
                        scope.GetTokenRange() = token.range;
                        scope.GetTokenCategory() = token.category;
                        GetTokenIndexDatabase()->LookupTokenDisplayName( token.scope.first, token.scope.second, scope.GetScopeName() );
                    }
                }
            }
        }
    }
    out_Scope = scope;
}


/** @brief Shrink the database to reclaim some memory
 *
 * @return void
 *
 */
void ClTokenDatabase::Shrink()
{
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
    ClAbstractToken& tokenRef = m_pTokens->GetValue(freeTokenId);
    m_pTokens->RemoveIdKey(tokenRef.identifier, freeTokenId);
    assert((tokenRef.fileId == wxNOT_FOUND) && "Only an unused token can be updated");
    tokenRef.fileId = token.fileId;
    tokenRef.identifier = token.identifier;
    tokenRef.range = token.range;
    tokenRef.tokenHash = token.tokenHash;
    tokenRef.tokenType = token.tokenType;
    wxString filen = wxString::Format(wxT("%d"), token.fileId);
    m_pFileTokens->Insert(filen.ToUTF8().data(), freeTokenId);
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
    std::string key = wxString::Format(wxT("%d"), oldToken.fileId).ToUTF8().data();
    m_pFileTokens->Remove(key, tokenId);
    ClAbstractToken t;
    // We just invalidate it here. Real removal is rather complex
    UpdateToken(tokenId, t);
}

void ClTokenDatabase::RemoveFileTokens( const ClFileId fileId )
{
    std::string key = wxString::Format(wxT("%d"), fileId).ToUTF8().data();
    std::set<ClTokenId> ids;
    m_pFileTokens->GetIdSet( key, ids );
    for (std::set<ClTokenId>::const_iterator it = ids.begin(); it != ids.end(); ++it)
    {
        RemoveToken( *it );
    }
    m_pFileTokens->Remove( key );
}

unsigned long ClTokenDatabase::GetTokenCount()
{
    return m_pTokens->GetCount();
}

void ClTokenDatabase::StoreIndexes() const
{
    ClTokenId id;
    std::set<std::string> keySet = m_pFileTokens->GetKeySet();
    for (std::set<std::string>::const_iterator it = keySet.begin(); it != keySet.end(); ++it)
    {
        ClFileId fId = atoi( it->c_str() );
        //if (!m_pTokenIndexDB->GetFilename( fId ).StartsWith( wxT("/usr") ))
        //    CCLogger::Get()->DebugLog( wxT("Removing all tokens for file ")+m_pTokenIndexDB->GetFilename( fId ) );
        m_pTokenIndexDB->RemoveFileTokens( fId );
    }
    std::set<ClFileId> createdFiles;
    //uint32_t cnt = m_pTokenIndexDB->GetTokenCount();
    for (id=0; id < m_pTokens->GetCount(); ++id)
    {
        ClAbstractToken& token = m_pTokens->GetValue( id );
        m_pTokenIndexDB->UpdateToken( token.identifier, token.displayName, token.fileId, token.USR, token.tokenType, token.range, token.category, token.parentTokenList, token.scope );
        createdFiles.insert( token.fileId );
    }
#if 0
    //uint32_t cnt2 = m_pTokenIndexDB->GetTokenCount();
    //CCLogger::Get()->DebugLog( F(wxT("StoreIndexes: Stored %d tokens. %d tokens extra. Total: %d (merged) IndexDb: %p"), (int)m_pTokens->GetCount(), (int)cnt2 - cnt, cnt2, m_pTokenIndexDB) );
    for( std::set<ClFileId>::const_iterator it = createdFiles.begin();it != createdFiles.end();++it )
    {
        if (!m_pTokenIndexDB->GetFilename( *it ).StartsWith( wxT("/usr") ))
            CCLogger::Get()->DebugLog( wxT("Inserted all tokens for file ")+m_pTokenIndexDB->GetFilename( *it ) );
    }
#endif
}

bool ClTokenDatabase::LookupTokenDefinition( const ClFileId fileId, const ClIdentifierString& identifier, const std::string& usr, ClTokenPosition& out_Position) const
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
                if( (usr.empty())||(tok.USR == usr))
                {
                    out_Position = tok.range.beginLocation;
                    return true;
                }
            }
        }
    }
    return false;
}

bool ClTokenDatabase::LookupTokenDefinition( const ClFileId fileId, const ClIdentifierString& identifier, const ClUSRString& usr, ClTokenRange& out_Range) const
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
                if( (usr.empty())||(tok.USR == usr))
                {
                    out_Range = tok.range;
                    return true;
                }
            }
        }
    }
    return false;
}

bool ClTokenDatabase::LookupTokenOverrideParents( const ClIdentifierString& identifier, const ClUSRString& usr, std::vector<ClUSRString>& out_List) const
{
    bool found = false;
    std::set<ClTokenId> tokenIdList;
    GetTokenMatches(identifier, tokenIdList);

    for (std::set<ClTokenId>::const_iterator it = tokenIdList.begin(); it != tokenIdList.end(); ++it)
    {
        ClAbstractToken tok = GetToken( *it );
        if ( (tok.USR == usr)||(usr.empty()) )
        {
            for (std::vector<std::pair<ClIdentifierString, ClUSRString> >::const_iterator it = tok.parentTokenList.begin(); it != tok.parentTokenList.end(); ++it)
            {
                out_List.push_back(it->second);
                found = true;
            }
        }
    }
    return found;
}

bool ClTokenDatabase::LookupTokenOverrideChildren( const ClIdentifierString& identifier, const ClUSRString& usr, std::vector<ClUSRString>& out_List) const
{
    bool found = false;
    std::set<ClTokenId> tokenIdList;
    GetTokenMatches(identifier, tokenIdList);

    for (std::set<ClTokenId>::const_iterator it = tokenIdList.begin(); it != tokenIdList.end(); ++it)
    {
        ClAbstractToken tok = GetToken( *it );
        for (std::vector<std::pair<ClIdentifierString, ClUSRString> >::const_iterator it = tok.parentTokenList.begin(); it != tok.parentTokenList.end(); ++it)
        {
            if ( (it->second == usr)||(usr.empty()) )
            {
                out_List.push_back(tok.USR);
                found = true;
            }
        }
    }
    return found;
}


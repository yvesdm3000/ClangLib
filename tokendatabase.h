#ifndef TOKENDATABASE_H
#define TOKENDATABASE_H

#include "clangpluginapi.h"
#include "treemap.h"
#include "cclogger.h"

#include <vector>
#include <algorithm>
#include <wx/thread.h>
#include <wx/string.h>
#include <wx/archive.h>

template<typename _Tp> class ClTreeMap;
class wxString;
typedef int ClFileId;

struct ClAbstractToken
{
    ClAbstractToken() :
        tokenType(ClTokenType_Unknown), fileId(-1), location(ClTokenPosition( 0, 0 )), identifier(), USR(), tokenHash(0) {}
    ClAbstractToken(ClTokenType typ, ClFileId fId, const ClTokenPosition& loc, const wxString& name, const wxString& usr, unsigned tknHash) :
        tokenType(typ), fileId(fId), location(loc), identifier(name), USR(usr), tokenHash(tknHash) {}
    ClAbstractToken( const ClAbstractToken& other ) :
        tokenType(other.tokenType), fileId(other.fileId), location(other.location),
        identifier(other.identifier), USR(other.USR), tokenHash(other.tokenHash)
    {
        for (std::vector<wxString>::const_iterator it = other.parentUSRList.begin(); it != other.parentUSRList.end(); ++it)
        {
            parentUSRList.push_back( *it );
        }
    }

    ClTokenType tokenType;
    ClFileId fileId;
    ClTokenPosition location;
    wxString identifier;
    wxString USR;
    unsigned tokenHash;
    std::vector<wxString> parentUSRList; // overrides and parent classes
};

struct ClIndexTokenLocation
{
    ClTokenType tokenType;
    ClFileId fileId;
    ClTokenPosition position;
    ClIndexTokenLocation(ClTokenType tokType, ClFileId filId, ClTokenPosition tokPosition) : tokenType(tokType), fileId(filId), position(tokPosition){}
    bool operator==(const ClIndexTokenLocation& other) const
    {
        if (tokenType != other.tokenType)
            return false;
        if (fileId != other.fileId)
            return false;
        if (position.line != other.position.line)
            return false;
        if (position.column != other.position.column)
            return false;
        return true;
    }
};

struct ClIndexToken
{
    wxString USR;
    ClTokenType tokenTypeMask; // Different token types for the token that can be found in the file
    std::vector< ClIndexTokenLocation > locationList;
    std::vector<wxString> parentUSRList;   // Overrides and parent classes

    ClIndexToken() : USR(wxEmptyString), tokenTypeMask(ClTokenType_Unknown){}
    ClIndexToken(const ClFileId fId, const wxString& usr, const ClTokenType tokType, const ClTokenPosition& tokenPosition, const std::vector<wxString>& parentUsrList) : USR(usr.c_str()), parentUSRList(parentUsrList) { locationList.push_back( ClIndexTokenLocation( tokType, fId, tokenPosition ) ); }
    ClIndexToken(const ClIndexToken& other) : USR(other.USR), tokenTypeMask(other.tokenTypeMask)
    {
        for (std::vector< ClIndexTokenLocation >::const_iterator it = other.locationList.begin(); it != other.locationList.end(); ++it)
        {
            locationList.push_back( *it );
        }
        for (std::vector<wxString>::const_iterator it = other.parentUSRList.begin(); it != other.parentUSRList.end(); ++it)
        {
            parentUSRList.push_back( it->c_str() );
        }
    }

    static bool ReadIn(ClIndexToken& token, wxInputStream& in);
    static bool WriteOut(const ClIndexToken& token,  wxOutputStream& out);
};

class ClFilenameEntry
{
public:
    ClFilenameEntry(wxString _filename, wxDateTime _timestamp) :
        filename(_filename),
        timestamp(_timestamp) {}
    wxString filename;
    wxDateTime timestamp;
};

class ClFilenameDatabase
{
public:
    ClFilenameDatabase();
    ClFilenameDatabase(const ClFilenameDatabase& Other);
    ~ClFilenameDatabase();

    static bool ReadIn(ClFilenameDatabase& tokenDatabase, wxInputStream& in);
    static bool WriteOut(const ClFilenameDatabase& db, wxOutputStream& out);

    bool HasFilename( const wxString& filename )const;
    ClFileId GetFilenameId(const wxString& filename) const;
    wxString GetFilename(const ClFileId fId) const;
    const wxDateTime GetFilenameTimestamp(const ClFileId fId) const;
    void UpdateFilenameTimestamp(const ClFileId fId, const wxDateTime& timestamp);
private:
    ClTreeMap<ClFilenameEntry>* m_pFileEntries;
};

class ClTokenIndexDatabase
{
public:
    ClTokenIndexDatabase() :
        m_FileDB(),
        m_pIndexTokenMap( new ClTreeMap<ClIndexToken>() ),
        m_bModified(false),
        m_Mutex(wxMUTEX_RECURSIVE)
    {
    }
    ClTokenIndexDatabase(const ClTokenIndexDatabase& other) :
        m_FileDB(other.m_FileDB),
        m_bModified(other.m_bModified),
        m_Mutex(wxMUTEX_RECURSIVE)
    {
    }
    ~ClTokenIndexDatabase()
    {
        delete m_pIndexTokenMap;
    }

    static bool ReadIn(ClTokenIndexDatabase& tokenDatabase, wxInputStream& in);
    static bool WriteOut(const ClTokenIndexDatabase& tokenDatabase, wxOutputStream& out);

    bool HasFilename( const wxString& filename ) const
    {
        wxMutexLocker locker(m_Mutex);

        return m_FileDB.HasFilename(filename);
    }
    ClFileId GetFilenameId( const wxString& filename ) const
    {
        wxMutexLocker locker(m_Mutex);

        return m_FileDB.GetFilenameId( filename );
    }
    wxString GetFilename(ClFileId fId) const
    {
        wxMutexLocker locker(m_Mutex);

        return m_FileDB.GetFilename( fId );
    }
    wxDateTime GetFilenameTimestamp( const ClFileId fId ) const
    {
        wxMutexLocker locker(m_Mutex);

        return m_FileDB.GetFilenameTimestamp( fId );
    }
    void UpdateFilenameTimestamp(const ClFileId fId, const wxDateTime& timestamp)
    {
        wxMutexLocker locker(m_Mutex);
        m_FileDB.UpdateFilenameTimestamp( fId, timestamp );
    }

    std::set<ClFileId> LookupTokenFileList( const wxString& identifier, const wxString& USR, const ClTokenType typeMask ) const
    {
        std::set<ClFileId> retList;
        std::set<int> idList;

        wxMutexLocker locker(m_Mutex);

        m_pIndexTokenMap->GetIdSet( identifier, idList );
        for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
        {
            ClIndexToken& token = m_pIndexTokenMap->GetValue(*it);
            if (((token.tokenTypeMask&typeMask)==typeMask )&&((USR.Length() == 0)||(token.USR.Length() == 0)||(USR == token.USR)))
            {
                for (std::vector<ClIndexTokenLocation>::const_iterator it2 = token.locationList.begin(); it2 != token.locationList.end(); ++it2)
                    retList.insert( it2->fileId );
            }
        }
        return retList;
    }

    std::set< std::pair<ClFileId, wxString> > LookupTokenOverrides( const wxString& identifier, const wxString& USR, const ClTokenType typeMask ) const
    {
        std::set<std::pair<ClFileId, wxString> > retList;
        std::set<int> idList;

        wxMutexLocker locker(m_Mutex);

        m_pIndexTokenMap->GetIdSet( identifier, idList );
        for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
        {
            ClIndexToken& token = m_pIndexTokenMap->GetValue(*it);
            if ((token.tokenTypeMask&typeMask)==typeMask)
            {
                if (std::find( token.parentUSRList.begin(), token.parentUSRList.end(), USR ) != token.parentUSRList.end())
                {
                    for (std::vector<ClIndexTokenLocation>::const_iterator it2 = token.locationList.begin(); it2 != token.locationList.end(); ++it2)
                        retList.insert( std::make_pair( it2->fileId, token.USR ) );
                }
            }
        }
        return retList;
    }

    uint32_t GetTokenCount() const
    {
        wxMutexLocker locker(m_Mutex);

        return m_pIndexTokenMap->GetCount();
    }

    void UpdateToken( const wxString& identifier, const ClFileId fileId, const wxString& USR, const ClTokenType tokType, const ClTokenPosition& tokenPosition, const std::vector< wxString >& overrideUSRList)
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
                ClIndexTokenLocation location(tokType, fileId, tokenPosition);
                if (std::find(token.locationList.begin(), token.locationList.end(), location) == token.locationList.end())
                    token.locationList.push_back( location );
                for (std::vector<wxString>::const_iterator usrIt = overrideUSRList.begin(); usrIt != overrideUSRList.end(); ++usrIt)
                {
                    if (std::find( token.parentUSRList.begin(), token.parentUSRList.end(), *usrIt ) == token.parentUSRList.end())
                    {
                        token.parentUSRList.push_back( *usrIt );
                    }
                }
                m_bModified = true;
                return;
            }
        }
        m_pIndexTokenMap->Insert( identifier, ClIndexToken(fileId, USR, tokType, tokenPosition, overrideUSRList) );
        m_bModified = true;
    }

    bool LookupTokenPosition(const wxString& identifier, const ClFileId fileId, const wxString& USR, const ClTokenType tokenTypeMask, ClTokenPosition& out_Position) const
    {
        std::set<int> idList;

        wxMutexLocker locker(m_Mutex);

        m_pIndexTokenMap->GetIdSet( identifier, idList );
        for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
        {
            ClIndexToken& token = m_pIndexTokenMap->GetValue( *it );
            if ((token.tokenTypeMask&tokenTypeMask)==tokenTypeMask)
            {
                if ((USR.Length() == 0)||(USR == token.USR))
                {
                    for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it)
                    {
                        if (((it->tokenType&tokenTypeMask)==tokenTypeMask)&&(it->fileId == fileId) )
                        {
                            out_Position = it->position;
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    void AddToken( const wxString& identifier, const ClIndexToken& token)
    {
        if (identifier.Length() > 0)
        {
            wxMutexLocker locker(m_Mutex);

            m_pIndexTokenMap->Insert( identifier, token );
            m_bModified = true;
        }
    }
    void Clear()
    {
        wxMutexLocker locker(m_Mutex);

        delete(m_pIndexTokenMap);
        m_pIndexTokenMap = new ClTreeMap<ClIndexToken>();
    }
    bool IsModified() const
    {
        wxMutexLocker locker(m_Mutex);

        return m_bModified;
    }

private:
    ClFilenameDatabase m_FileDB;
    ClTreeMap<ClIndexToken>* m_pIndexTokenMap;
    bool m_bModified;
    mutable wxMutex m_Mutex;
};

typedef std::map<wxString, ClTokenIndexDatabase*> ClTokenIndexDatabaseMap_t;

class ClTokenDatabase
{
public:
    ClTokenDatabase(ClTokenIndexDatabase* IndexDB);
    ClTokenDatabase(const ClTokenDatabase& other);
    ~ClTokenDatabase();

    friend void swap(ClTokenDatabase& first, ClTokenDatabase& second);

    bool HasFilename(const wxString& filename) const { return m_pTokenIndexDB->HasFilename(filename); }
    ClFileId GetFilenameId(const wxString& filename) const;
    wxString GetFilename(const ClFileId fId) const;
    wxDateTime GetFilenameTimestamp(const ClFileId fId) const;
    ClTokenId GetTokenId(const wxString& identifier, ClFileId fId, ClTokenType tokenType, unsigned tokenHash) const; ///< returns wxNOT_FOUND on failure
    ClTokenId InsertToken(const ClAbstractToken& token); // duplicate tokens are discarded
    ClAbstractToken GetToken(const ClTokenId tId) const;

    void RemoveToken(const ClTokenId tokenId);
    /**
     * Return a list of tokenId's for the given token identifier
     */
    void GetTokenMatches(const wxString& identifier, std::set<ClTokenId>& out_tokenList) const;

    bool LookupTokenDefinition( const ClFileId fileId, const wxString& identifier, const wxString& usr, ClTokenPosition& out_Position) const;

    /**
     * Return a list of tokenId's that are found in the given file
     */
    void GetFileTokens(const ClFileId fId, std::set<ClTokenId>& out_tokens) const;

    /**
     * Clears the database
     */
    void Clear();
    /**
     * Shrinks the database by removing all unnecessary elements and memory
     */
    void Shrink();

    unsigned long GetTokenCount();

    void StoreIndexes() const;
    ClTokenIndexDatabase* GetTokenIndexDatabase() { return m_pTokenIndexDB; }
    const ClTokenIndexDatabase* GetTokenIndexDatabase() const { return m_pTokenIndexDB; }
private:
    void UpdateToken(const ClTokenId tokenId, const ClAbstractToken& token);
private:
    ClTokenIndexDatabase* m_pTokenIndexDB;
    ClTokenIndexDatabase* m_pLocalTokenIndexDB;
    ClTreeMap<ClAbstractToken>* m_pTokens;
    ClTreeMap<int>* m_pFileTokens;
};

#endif // TOKENDATABASE_H

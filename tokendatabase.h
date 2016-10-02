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
        identifier(other.identifier), USR(other.USR), tokenHash(other.tokenHash), parentUSRList(other.parentUSRList)
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

struct ClIndexToken
{
    ClFileId fileId;
    wxString USR;
    ClTokenType tokenTypeMask; // Different token types for the token that can be found in the file
    std::vector< std::pair<ClTokenType,ClTokenPosition> > positionList;
    std::vector<wxString> parentUSRList;   // Overrides and parent classes

    ClIndexToken() : fileId(wxID_ANY), USR(wxEmptyString), tokenTypeMask(ClTokenType_Unknown){}
    ClIndexToken(const ClFileId fId, const wxString& usr, const ClTokenType tokTypeMask, const ClTokenPosition& tokenPosition, const std::vector<wxString>& parentUsrList) : fileId(fId), USR(usr.c_str()), tokenTypeMask(tokTypeMask), parentUSRList(parentUsrList) { positionList.push_back( std::make_pair( tokTypeMask, tokenPosition ) ); }
    ClIndexToken(const ClIndexToken& other) : fileId(other.fileId), USR(other.USR), tokenTypeMask(other.tokenTypeMask), positionList(other.positionList), parentUSRList(other.parentUSRList)
    {
        for (std::vector< std::pair<ClTokenType,ClTokenPosition> >::const_iterator it = other.positionList.begin(); it != other.positionList.end(); ++it)
        {
            positionList.push_back( *it );
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
    mutable wxMutex m_Mutex;
};

class ClTokenIndexDatabase
{
public:
    ClTokenIndexDatabase() :
        m_FileDB(),
        m_pIndexTokenMap( new ClTreeMap<ClIndexToken>() ),
        m_bModified(false),
        m_Mutex(wxMUTEX_RECURSIVE){}
    ClTokenIndexDatabase(const ClTokenIndexDatabase& other) :
        m_FileDB(other.m_FileDB),
        m_bModified(other.m_bModified),
        m_Mutex(wxMUTEX_RECURSIVE){}
    ~ClTokenIndexDatabase()
    {
        delete m_pIndexTokenMap;
    }

    static bool ReadIn(ClTokenIndexDatabase& tokenDatabase, wxInputStream& in);
    static bool WriteOut(const ClTokenIndexDatabase& tokenDatabase, wxOutputStream& out);

    bool HasFilename( const wxString& filename ) const { return m_FileDB.HasFilename(filename); }
    ClFileId GetFilenameId( const wxString& filename ) const { return m_FileDB.GetFilenameId( filename ); }
    wxString GetFilename(ClFileId fId) const { return m_FileDB.GetFilename( fId );}
    wxDateTime GetFilenameTimestamp( const ClFileId fId ) const { return m_FileDB.GetFilenameTimestamp( fId );}
    void UpdateFilenameTimestamp(const ClFileId fId, const wxDateTime& timestamp) { m_FileDB.UpdateFilenameTimestamp( fId, timestamp );}

    std::set<ClFileId> LookupTokenFileList( const wxString& identifier, const wxString& USR, const ClTokenType typeMask ) const
    {
        std::set<ClFileId> retList;
        std::set<int> idList;
        m_pIndexTokenMap->GetIdSet( identifier, idList );
        for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
        {
            ClIndexToken& token = m_pIndexTokenMap->GetValue(*it);
            if (((token.tokenTypeMask&typeMask)==typeMask )&&((USR.Length() == 0)||(token.USR.Length() == 0)||(USR == token.USR)))
            {
                retList.insert( token.fileId );
            }
        }
        return retList;
    }

    std::set< std::pair<ClFileId, wxString> > LookupTokenOverrides( const wxString& identifier, const wxString& USR, const ClTokenType typeMask ) const
    {
        std::set<std::pair<ClFileId, wxString> > retList;
        std::set<int> idList;
        m_pIndexTokenMap->GetIdSet( identifier, idList );
        for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
        {
            ClIndexToken& token = m_pIndexTokenMap->GetValue(*it);
            if ((token.tokenTypeMask&typeMask)==typeMask)
            {
                if (std::find( token.parentUSRList.begin(), token.parentUSRList.end(), USR ) != token.parentUSRList.end())
                {
                    retList.insert( std::make_pair( token.fileId, token.USR ) );
                }
            }
        }
        return retList;
    }

    uint32_t GetTokenCount() const
    {
        return m_pIndexTokenMap->GetCount();
    }

    void UpdateToken( const wxString& identifier, const ClFileId fileId, const wxString& USR, const ClTokenType tokType, const ClTokenPosition& tokenPosition, const std::vector< wxString >& overrideUSRList)
    {
        std::set<int> idList;
        m_pIndexTokenMap->GetIdSet( identifier, idList );
        for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
        {
            ClIndexToken& token = m_pIndexTokenMap->GetValue( *it );
            if (token.fileId == fileId)
            {
                ClTokenType tokenTypeMask = static_cast<ClTokenType>(token.tokenTypeMask | tokType);
                if ( (token.fileId == fileId)&&(token.USR == USR))
                {
                    token.tokenTypeMask = tokenTypeMask;
                    token.positionList.push_back( std::make_pair(tokType, tokenPosition));
                    for (std::vector<wxString>::const_iterator usrIt = overrideUSRList.begin(); usrIt != overrideUSRList.end(); ++usrIt)
                    {
                        if (std::find( token.parentUSRList.begin(), token.parentUSRList.end(), *usrIt ) == token.parentUSRList.end())
                        {
                            token.parentUSRList.push_back( *usrIt );
                        }
                    }
                    m_bModified = true;
                }
                return;
            }
        }
        m_pIndexTokenMap->Insert( identifier, ClIndexToken(fileId, USR, tokType, tokenPosition, overrideUSRList) );
        m_bModified = true;
    }

    bool LookupTokenPosition(const wxString& identifier, const ClFileId fileId, const wxString& USR, const ClTokenType tokenTypeMask, ClTokenPosition& out_Position) const
    {
        std::set<int> idList;
        m_pIndexTokenMap->GetIdSet( identifier, idList );
        for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
        {
            ClIndexToken& token = m_pIndexTokenMap->GetValue( *it );
            if (token.fileId == fileId)
            {
                if ((token.tokenTypeMask&tokenTypeMask)==tokenTypeMask)
                {
                    if ((USR.Length() == 0)||(USR == token.USR))
                    {
                        for (std::vector< std::pair< ClTokenType, ClTokenPosition > >::const_iterator it = token.positionList.begin(); it != token.positionList.end(); ++it)
                        {
                            if ((it->first&tokenTypeMask)==tokenTypeMask)
                            {
                                out_Position = it->second;
                                return true;
                            }
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
            m_pIndexTokenMap->Insert( identifier, token );
            m_bModified = true;
        }
    }
    void Clear()
    {
        delete(m_pIndexTokenMap);
        m_pIndexTokenMap = new ClTreeMap<ClIndexToken>();
    }
    bool IsModified() const { return m_bModified; }

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

    bool HasFilename(const wxString& filename)const { return m_pTokenIndexDB->HasFilename(filename); }
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
    mutable wxMutex m_Mutex;
};

#endif // TOKENDATABASE_H

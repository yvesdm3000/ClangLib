#ifndef TOKENDATABASE_H
#define TOKENDATABASE_H

#include "clangpluginapi.h"
#include "treemap.h"
#include "cclogger.h"

#include <vector>
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
        identifier(other.identifier), USR(other.USR), tokenHash(other.tokenHash) {}

    ClTokenType tokenType;
    ClFileId fileId;
    ClTokenPosition location;
    wxString identifier;
    wxString USR;
    unsigned tokenHash;
};

struct ClIndexToken
{
    ClFileId fileId;
    ClTokenType tokenTypeMask; // Different token types for the token that can be found in the file
    ClIndexToken() : fileId(wxID_ANY), tokenTypeMask(ClTokenType_Unknown){}
    ClIndexToken(const ClFileId fId, const ClTokenType tokTypeMask) : fileId(fId), tokenTypeMask(tokTypeMask){}

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
    ~ClFilenameDatabase();

    static bool ReadIn(ClFilenameDatabase& tokenDatabase, wxInputStream& in);
    static bool WriteOut(const ClFilenameDatabase& db, wxOutputStream& out);

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
    ClTokenIndexDatabase(ClFilenameDatabase& fileDB) :
        m_FileDB(fileDB),
        m_pIndexTokenMap( new ClTreeMap<ClIndexToken>() ),
        m_Mutex(wxMUTEX_RECURSIVE){}
    ClTokenIndexDatabase(const ClTokenIndexDatabase& other) :
        m_FileDB(other.m_FileDB),
        m_Mutex(wxMUTEX_RECURSIVE){}
    ~ClTokenIndexDatabase()
    {
        delete m_pIndexTokenMap;
    }

    static bool ReadIn(ClTokenIndexDatabase& tokenDatabase, wxInputStream& in);
    static bool WriteOut(const ClTokenIndexDatabase& tokenDatabase, wxOutputStream& out);

    ClFileId GetFilenameId( const wxString& filename ){ return m_FileDB.GetFilenameId( filename ); }
    wxString GetFilename(ClFileId fId) const { return m_FileDB.GetFilename( fId );}
    wxDateTime GetFilenameTimestamp( const ClFileId fId ) const { return m_FileDB.GetFilenameTimestamp( fId );}

    std::set<ClFileId> LookupTokenFileList( const wxString& identifier, const ClTokenType typeMask ) const
    {
        std::set<ClFileId> retList;
        std::set<int> idList;
        m_pIndexTokenMap->GetIdSet( identifier, idList );
        CCLogger::Get()->DebugLog(F(wxT("Found %d tokens for identifier ")+identifier, idList.size()));
        for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
        {
            ClIndexToken& token = m_pIndexTokenMap->GetValue(*it);
            if ((token.tokenTypeMask&typeMask)==typeMask )
            {
                CCLogger::Get()->DebugLog( F(wxT("Token mask %x matches criteria %x for file (%d)")+GetFilename( token.fileId ), token.tokenTypeMask, typeMask, token.fileId) );
                retList.insert( token.fileId );
            }
            else
            {
                CCLogger::Get()->DebugLog( F(wxT("Token mask %x doesn't match criteria %x for file (%d) ")+GetFilename( token.fileId ), token.tokenTypeMask, typeMask, token.fileId) );
            }
        }
        return retList;
    }

    uint32_t GetTokenCount() const
    {
        return m_pIndexTokenMap->GetCount();
    }

    void UpdateToken( const wxString& identifier, const ClFileId fileId, const ClTokenType tokType)
    {
        std::set<int> idList;
        m_pIndexTokenMap->GetIdSet( identifier, idList );
        for (std::set<int>::const_iterator it = idList.begin(); it != idList.end(); ++it)
        {
            ClIndexToken& token = m_pIndexTokenMap->GetValue( *it );
            if (token.fileId == fileId)
            {
                token.tokenTypeMask = static_cast<ClTokenType>(token.tokenTypeMask | tokType);
                return;
            }
        }
        m_pIndexTokenMap->Insert( identifier, ClIndexToken(fileId, tokType) );
    }
    void Clear()
    {
        delete(m_pIndexTokenMap);
        m_pIndexTokenMap = new ClTreeMap<ClIndexToken>();
    }

private:
    ClFilenameDatabase& m_FileDB;
    ClTreeMap<ClIndexToken>* m_pIndexTokenMap;
    mutable wxMutex m_Mutex;
};

class ClTokenDatabase
{
public:
    ClTokenDatabase(ClTokenIndexDatabase& IndexDB);
    ClTokenDatabase(const ClTokenDatabase& other);
    ~ClTokenDatabase();

    friend void swap(ClTokenDatabase& first, ClTokenDatabase& second);

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
    ClTokenIndexDatabase& GetTokenIndexDatabase() const { return m_TokenIndexDB; }
private:
    void UpdateToken(const ClTokenId tokenId, const ClAbstractToken& token);
private:
    ClTokenIndexDatabase& m_TokenIndexDB;
    ClTreeMap<ClAbstractToken>* m_pTokens;
    ClTreeMap<int>* m_pFileTokens;
    mutable wxMutex m_Mutex;
};

#endif // TOKENDATABASE_H

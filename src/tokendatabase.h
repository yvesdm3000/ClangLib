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
        tokenType(ClTokenType_Unknown), fileId(-1), range(ClTokenPosition( 0, 0 ), ClTokenPosition(0,0) ), identifier(), USR(), tokenHash(0) {}
    ClAbstractToken(ClTokenType typ, ClFileId fId, const ClTokenRange tokRange, const wxString& name, const wxString& dispName, const wxString& usr, int tokHash) :
        tokenType(typ), fileId(fId), range(tokRange), identifier(name), displayName(dispName), USR(usr), tokenHash( tokHash ) {}
    ClAbstractToken( const ClAbstractToken& other ) :
        tokenType(other.tokenType), fileId(other.fileId), range(other.range),
        identifier(other.identifier), displayName( other.displayName ),
        USR(other.USR), scope(other.scope)
    {
        for (std::vector<std::pair<wxString,wxString> >::const_iterator it = other.parentTokenList.begin(); it != other.parentTokenList.end(); ++it)
        {
            parentTokenList.push_back( *it );
        }
    }

    ClTokenType tokenType;
    ClFileId fileId;
    ClTokenRange range;
    wxString identifier;
    wxString displayName; ///< Human readable representation of the token, e.g. function name + parameters and return type
    wxString USR;
    int tokenHash;
    std::vector<std::pair<wxString,wxString> > parentTokenList; // overrides and parent classes
    std::pair<wxString,wxString> scope;
};

struct ClIndexTokenLocation
{
    ClTokenType tokenType;
    ClFileId fileId;
    ClTokenRange range;

    ClIndexTokenLocation(ClTokenType tokType, ClFileId filId, struct ClTokenPosition tokPosition) : tokenType(tokType), fileId(filId), range(tokPosition, tokPosition){}
    ClIndexTokenLocation(ClTokenType tokType, ClFileId filId, struct ClTokenRange tokRange) : tokenType(tokType), fileId(filId), range( tokRange ){}
    bool operator==(const ClIndexTokenLocation& other) const
    {
        if (tokenType != other.tokenType)
            return false;
        if (fileId != other.fileId)
            return false;
        if (range.beginLocation != other.range.beginLocation)
            return false;
        if (range.endLocation != other.range.endLocation)
            return false;
        return true;
    }
};

struct ClIndexToken
{
    wxString identifier;  ///< Identifier of the token e.g. function name (without arguments)
    wxString displayName; ///< Human readable representation of the token e.g. function name + arguments + return type
    class wxString USR;
    ClTokenType tokenTypeMask; // Different token types for the token that can be found in the file
    std::vector< ClIndexTokenLocation > locationList;
    std::vector< std::pair<wxString,wxString> > parentTokenList;   // Overrides and parent classes, first=identifier, second=USR
    std::pair<wxString,wxString> scope; // namespaces, outer class in case of inner class, first=identifier, second=USR

    ClIndexToken() : identifier( wxEmptyString ), USR(wxEmptyString), tokenTypeMask(ClTokenType_Unknown){}
    ClIndexToken( const wxString& ident, const wxString& name, const ClFileId fId, const wxString& usr, const ClTokenType tokType, const ClTokenRange& tokenRange, const std::vector<std::pair<wxString,wxString> >& parentTokList, const std::pair<wxString,wxString>& parentScope )
        : identifier( ident ), displayName( name.c_str() ), USR(usr.c_str()), parentTokenList(parentTokList),
          scope(std::make_pair( parentScope.first.c_str(), parentScope.second.c_str()))
    {
        locationList.push_back( ClIndexTokenLocation( tokType, fId, tokenRange ) );
    }

    ClIndexToken(const ClIndexToken& other) : identifier( other.identifier.c_str() ), displayName(other.displayName.c_str()), USR(other.USR.c_str()), tokenTypeMask(other.tokenTypeMask), scope(std::make_pair(other.scope.first.c_str(),other.scope.second.c_str()) )
    {
        for (std::vector< ClIndexTokenLocation >::const_iterator it = other.locationList.begin(); it != other.locationList.end(); ++it)
        {
            locationList.push_back( *it );
        }
        for (std::vector<std::pair<wxString,wxString> >::const_iterator it = other.parentTokenList.begin(); it != other.parentTokenList.end(); ++it)
        {
            parentTokenList.push_back( std::make_pair(it->first.c_str(), it->second.c_str()) );
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
        m_pFileTokens(new ClTreeMap<ClTokenId>()),
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
        delete m_pFileTokens;
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

    std::set<ClFileId> LookupTokenFileList( const wxString& identifier, const wxString& USR, const ClTokenType typeMask ) const;

    std::set< std::pair<ClFileId, wxString> > LookupTokenOverrides( const wxString& identifier, const wxString& USR, const ClTokenType typeMask ) const;

    uint32_t GetTokenCount() const
    {
        wxMutexLocker locker(m_Mutex);

        return m_pIndexTokenMap->GetCount();
    }

    void UpdateToken( const wxString& identifier, const wxString& displayName, const ClFileId fileId, const wxString& USR, const ClTokenType tokType, const ClTokenRange& tokenRange, const std::vector< std::pair<wxString,wxString> >& overrideTokenList, const std::pair<wxString,wxString>& scope);
    void RemoveFileTokens(const ClFileId fileId);

    bool LookupTokenPosition(const wxString& identifier, const ClFileId fileId, const wxString& USR, const ClTokenType tokenTypeMask, ClTokenPosition& out_Position) const;

    bool LookupTokenPosition(const wxString& identifier, const ClFileId fileId, const wxString& USR, const ClTokenType tokenTypeMask, ClTokenRange& out_Range) const;

    bool LookupTokenDisplayName(const wxString& identifier, const wxString& USR, wxString& out_DisplayName) const;

    void GetFileTokens(const ClFileId fId, const int tokenTypeMask, std::vector<ClIndexToken>& out_tokens) const;

    void AddToken( const wxString& identifier, const ClIndexToken& token);

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
    ClTreeMap<ClTokenId>* m_pFileTokens;
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
    ClTokenId GetTokenId(const wxString& identifier, const ClFileId fId, const ClTokenType tokenType, const int tokenHash) const; ///< returns wxNOT_FOUND on failure
    ClAbstractToken GetToken(const ClTokenId tId) const;

    ClTokenId InsertToken(const ClAbstractToken& token); // duplicate tokens are discarded
    void RemoveToken(const ClTokenId tokenId);

    void GetTokenMatches(const wxString& identifier, std::set<ClTokenId>& out_tokenList) const;

    bool LookupTokenDefinition( const ClFileId fileId, const wxString& identifier, const wxString& usr, ClTokenPosition& out_Position) const;
    bool LookupTokenDefinition( const ClFileId fileId, const wxString& identifier, const wxString& usr, ClTokenRange& out_Range) const;

    void GetFileTokens(const ClFileId fId, std::set<ClTokenId>& out_tokens) const;
    void GetTokenScopes(const ClFileId fileId, const unsigned TokenTypeMask, std::vector<ClTokenScope>& out_Scopes) const;

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
    ClTreeMap<ClTokenId>* m_pFileTokens;
};

#endif // TOKENDATABASE_H

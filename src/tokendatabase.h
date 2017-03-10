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
typedef std::string ClUSRString;

struct ClAbstractToken
{
    ClAbstractToken() :
        tokenType(ClTokenType_Unknown), fileId(-1), range(ClTokenPosition( 0, 0 ), ClTokenPosition(0,0) ), identifier(), USR(), tokenHash(0) {}
    ClAbstractToken(ClTokenType typ, ClFileId fId, const ClTokenRange tokRange, const wxString& name, const wxString& dispName, const ClUSRString& usr, int tokHash) :
        tokenType(typ), fileId(fId), range(tokRange), identifier(name), displayName(dispName), USR(usr), tokenHash( tokHash ) {}
    ClAbstractToken( const ClAbstractToken& other ) :
        tokenType(other.tokenType), fileId(other.fileId), range(other.range),
        identifier(other.identifier), displayName( other.displayName ),
        USR(other.USR), scope(other.scope)
    {
        for (std::vector<std::pair<wxString,ClUSRString> >::const_iterator it = other.parentTokenList.begin(); it != other.parentTokenList.end(); ++it)
        {
            parentTokenList.push_back( *it );
        }
    }

    ClTokenType tokenType;
    ClFileId fileId;
    ClTokenRange range;
    wxString identifier;
    wxString displayName; ///< Human readable representation of the token, e.g. function name + parameters and return type
    ClUSRString USR;
    int tokenHash;
    std::vector<std::pair<wxString,ClUSRString> > parentTokenList; // overrides and parent classes
    std::pair<wxString,ClUSRString> scope;
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
    ClUSRString USR;
    ClTokenType tokenTypeMask; // Different token types for the token that can be found in the file
    std::vector< ClIndexTokenLocation > locationList;
    std::vector< std::pair<wxString,ClUSRString> > parentTokenList;   // Overrides and parent classes, first=identifier, second=USR
    std::pair<wxString,ClUSRString> scope; // namespaces, outer class in case of inner class, first=identifier, second=USR

    ClIndexToken(const wxString& ident) : identifier(ident.c_str()), USR(), tokenTypeMask(ClTokenType_Unknown){}
    ClIndexToken( const wxString& ident, const wxString& name, const ClFileId fId, const std::string& usr, const ClTokenType tokType, const ClTokenRange& tokenRange, const std::vector<std::pair<wxString,ClUSRString> >& parentTokList, const std::pair<wxString,ClUSRString>& parentScope )
        : identifier( ident.c_str() ),
          displayName( name.c_str() ),
          USR(usr),
          tokenTypeMask( tokType ),
          parentTokenList(parentTokList),
          scope(std::make_pair( parentScope.first.c_str(), parentScope.second.c_str()))
    {
        locationList.push_back( ClIndexTokenLocation( tokType, fId, tokenRange ) );
    }

    ClIndexToken(const ClIndexToken& other) : identifier( other.identifier.c_str() ), displayName(other.displayName.c_str()), USR(other.USR), tokenTypeMask(other.tokenTypeMask), scope(std::make_pair(other.scope.first.c_str(),other.scope.second.c_str()) )
    {
        for (std::vector< ClIndexTokenLocation >::const_iterator it = other.locationList.begin(); it != other.locationList.end(); ++it)
        {
            locationList.push_back( *it );
        }
        for (std::vector<std::pair<wxString,ClUSRString> >::const_iterator it = other.parentTokenList.begin(); it != other.parentTokenList.end(); ++it)
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
    ClFilenameEntry(const std::string& _filename, wxDateTime _timestamp) :
        filename(_filename),
        timestamp(_timestamp) {}
    std::string filename;
    wxDateTime timestamp;
};

class IPersistentFilenameDatabase
{
public:
    virtual void AddFilename(const ClFilenameEntry& filename) = 0;
    virtual std::vector<ClFilenameEntry> GetFilenames() const = 0;
};

class IPersistentTokenIndexDatabase
{
public:
    virtual IPersistentFilenameDatabase* GetFilenameDatabase() = 0;
    virtual void AddToken(const ClIndexToken& token) = 0;
    virtual const IPersistentFilenameDatabase* GetFilenameDatabase() const = 0;
    virtual std::vector<ClIndexToken> GetTokens() const = 0;
};

class CTokenIndexDatabasePersistence
{
public:
    static bool ReadIn(IPersistentTokenIndexDatabase& tokenDatabase, wxInputStream& in);
    static bool WriteOut(const IPersistentTokenIndexDatabase& tokenDatabase, wxOutputStream& out);
};

class ClFilenameDatabase : public IPersistentFilenameDatabase
{
public:
    ClFilenameDatabase();
    ClFilenameDatabase(const ClFilenameDatabase& Other);
    ~ClFilenameDatabase();

    bool HasFilename( const std::string& filename )const;
    ClFileId GetFilenameId(const std::string& filename) const;
    std::string GetFilename(const ClFileId fId) const;
    const wxDateTime GetFilenameTimestamp(const ClFileId fId) const;
    void UpdateFilenameTimestamp(const ClFileId fId, const wxDateTime& timestamp);

public: // IPersistentFilenameDatabase
    void AddFilename(const ClFilenameEntry& filename);
    std::vector<ClFilenameEntry> GetFilenames() const;

private:
    ClTreeMap<ClFilenameEntry>* m_pFileEntries;
};

class ClTokenIndexDatabase : public IPersistentTokenIndexDatabase, public IPersistentFilenameDatabase
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
    virtual ~ClTokenIndexDatabase()
    {
        delete m_pFileTokens;
        delete m_pIndexTokenMap;
    }

    bool HasFilename( const std::string& filename ) const
    {
        wxMutexLocker locker(m_Mutex);

        return m_FileDB.HasFilename(filename);
    }
    ClFileId GetFilenameId( const std::string& filename ) const
    {
        wxMutexLocker locker(m_Mutex);

        return m_FileDB.GetFilenameId( filename );
    }
    std::string GetFilename(ClFileId fId) const
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

    std::set<ClFileId> LookupTokenFileList( const wxString& identifier, const ClUSRString& USR, const ClTokenType typeMask ) const;

    std::set< std::pair<ClFileId, ClUSRString> > LookupTokenOverrides( const wxString& identifier, const ClUSRString& USR, const ClTokenType typeMask ) const;

    uint32_t GetTokenCount() const
    {
        wxMutexLocker locker(m_Mutex);

        return m_pIndexTokenMap->GetCount();
    }

    void UpdateToken( const wxString& identifier, const wxString& displayName, const ClFileId fileId, const ClUSRString& USR, const ClTokenType tokType, const ClTokenRange& tokenRange, const std::vector< std::pair<wxString,ClUSRString> >& overrideTokenList, const std::pair<wxString,ClUSRString>& scope);
    void RemoveFileTokens(const ClFileId fileId);

    bool LookupTokenPosition(const wxString& identifier, const ClFileId fileId, const ClUSRString& USR, const ClTokenType tokenTypeMask, ClTokenPosition& out_Position) const;

    bool LookupTokenPosition(const wxString& identifier, const ClFileId fileId, const ClUSRString& USR, const ClTokenType tokenTypeMask, ClTokenRange& out_Range) const;

    bool LookupTokenDisplayName(const wxString& identifier, const ClUSRString& USR, wxString& out_DisplayName) const;

    bool LookupTokenType(const wxString& identifier, const ClFileId fileId, const ClUSRString& USR, const ClTokenPosition& Position,  ClTokenType& out_TokenType) const;

    void GetFileTokens(const ClFileId fId, const int tokenTypeMask, std::vector<ClIndexToken>& out_tokens) const;

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

private: // IPersistentTokenIndexDatabase
    virtual IPersistentFilenameDatabase* GetFilenameDatabase() { return this; }
    void AddToken(const ClIndexToken& token);
    virtual const IPersistentFilenameDatabase* GetFilenameDatabase() const { return this; }
    std::vector<ClIndexToken> GetTokens() const;

private: // IPersistentFilenameDatabase
    void AddFilename(const ClFilenameEntry& filename)
    {
        wxMutexLocker locker(m_Mutex);
        m_FileDB.AddFilename(filename);
    }
    std::vector<ClFilenameEntry> GetFilenames() const
    {
        wxMutexLocker locker(m_Mutex);
        return m_FileDB.GetFilenames();
    }

private:
    ClFilenameDatabase m_FileDB;
    ClTreeMap<ClIndexToken>* m_pIndexTokenMap;
    ClTreeMap<ClTokenId>* m_pFileTokens;
    bool m_bModified;
    mutable wxMutex m_Mutex;
};

typedef std::map<std::string, ClTokenIndexDatabase*> ClTokenIndexDatabaseMap_t;

class ClTokenDatabase
{
public:
    ClTokenDatabase(ClTokenIndexDatabase* IndexDB);
    ClTokenDatabase(const ClTokenDatabase& other);
    ~ClTokenDatabase();

    friend void swap(ClTokenDatabase& first, ClTokenDatabase& second);

    bool HasFilename(const std::string& filename) const { return m_pTokenIndexDB->HasFilename(filename); }
    ClFileId GetFilenameId(const std::string& filename) const;
    std::string GetFilename(const ClFileId fId) const;
    wxDateTime GetFilenameTimestamp(const ClFileId fId) const;
    ClTokenId GetTokenId(const wxString& identifier, const ClFileId fId, const ClTokenType tokenType, const int tokenHash) const; ///< returns wxNOT_FOUND on failure
    ClAbstractToken GetToken(const ClTokenId tId) const;

    ClTokenId InsertToken(const ClAbstractToken& token); // duplicate tokens are discarded
    void RemoveToken(const ClTokenId tokenId);
    void RemoveFileTokens( const ClFileId fileId );

    void GetTokenMatches(const wxString& identifier, std::set<ClTokenId>& out_tokenList) const;

    bool LookupTokenDefinition( const ClFileId fileId, const wxString& identifier, const ClUSRString& usr, ClTokenPosition& out_Position) const;
    bool LookupTokenDefinition( const ClFileId fileId, const wxString& identifier, const ClUSRString& usr, ClTokenRange& out_Range) const;

    void GetFileTokens(const ClFileId fId, std::set<ClTokenId>& out_tokens) const;
    void GetAllTokenFiles(std::set<ClFileId>& out_fileIds) const;
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

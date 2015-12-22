#ifndef TOKENDATABASE_H
#define TOKENDATABASE_H

#include "clangpluginapi.h"

#include <vector>
#include <wx/thread.h>
#include <wx/string.h>
#include <wx/archive.h>


template<typename _Tp> class ClTreeMap;
class wxString;
typedef int ClFileId;

typedef enum _TokenType
{
    ClTokenType_Unknown = 0,
    ClTokenType_FuncDecl  = 1<<0,
    ClTokenType_VarDecl   = 1<<1,
    ClTokenType_ParmDecl  = 1<<2,
    ClTokenType_ScopeDecl = 1<<3,

}ClTokenType;

struct ClAbstractToken
{
    ClAbstractToken() :
        tokenType(ClTokenType_Unknown), fileId(-1), location(ClTokenPosition( 0, 0 )), identifier(), displayName(), scopeName(), tokenHash(0){}
    ClAbstractToken( ClTokenType typ, ClFileId fId, ClTokenPosition location, wxString name, wxString displayName, wxString scopeName, unsigned tknHash) :
        tokenType(typ), fileId(fId), location(location), identifier(name), displayName(displayName.c_str()), scopeName(scopeName.c_str()), tokenHash(tknHash) {}
    ClAbstractToken( const ClAbstractToken& other ) :
        tokenType(other.tokenType), fileId(other.fileId), location(other.location), identifier(other.identifier), displayName( other.displayName.c_str()), scopeName(other.scopeName.c_str()), tokenHash(other.tokenHash) {}

    static void WriteOut( const ClAbstractToken& token,  wxOutputStream& out );
    static void ReadIn( ClAbstractToken& token, wxInputStream& in );

    ClTokenType tokenType;
    ClFileId fileId;
    ClTokenPosition location;
    wxString identifier;
    wxString displayName;
    wxString scopeName;
    unsigned tokenHash;
};

struct ClFileEntry
{
    ClFileEntry() : filename(), revision(0){}
    ClFileEntry(const wxString& fn) : filename(fn), revision(0) {}
    wxString filename;
    uint32_t revision; ///< Update revision. Updated when tokens associated with this file are changed
};

class ClFilenameDatabase
{
public:
    ClFilenameDatabase();
    ~ClFilenameDatabase();

    static void ReadIn( ClFilenameDatabase& tokenDatabase, wxInputStream& in );
    static void WriteOut( ClFilenameDatabase& db, wxOutputStream& out );

    ClFileId GetFilenameId(const wxString& filename);
    wxString GetFilename(ClFileId fId);
private:
    ClTreeMap<ClFileEntry>* m_pFileEntries;
    wxMutex m_Mutex;
};

class ClTokenDatabase
{
public:
    ClTokenDatabase( ClFilenameDatabase& fileDB );
    ClTokenDatabase( const ClTokenDatabase& other);
    ~ClTokenDatabase();

    friend void swap( ClTokenDatabase& first, ClTokenDatabase& second );


    static void ReadIn( ClTokenDatabase& tokenDatabase, wxInputStream& in );
    static void WriteOut( ClTokenDatabase& tokenDatabase, wxOutputStream& out );

    ClFileId GetFilenameId(const wxString& filename);
    wxString GetFilename(ClFileId fId);
    ClTokenId GetTokenId(const wxString& identifier, ClFileId fId, unsigned tokenHash); ///< returns wxNOT_FOUND on failure
    ClTokenId InsertToken(const ClAbstractToken& token); // duplicate tokens are discarded
    ClAbstractToken GetToken(ClTokenId tId);
    ClFilenameDatabase& GetFileDB() { return m_FileDB; }
    /**
     * Return a list of tokenId's for the given token identifier
     */
    std::vector<ClTokenId> GetTokenMatches(const wxString& identifier);
    /**
     * Return a list of tokenId's that are found in the given file
     */
    std::vector<ClTokenId> GetFileTokens(ClFileId fId);

    /**
     * Clears the database
     */
    void Clear();
    /**
     * Shrinks the database by removing all unnecessary elements and memory
     */
    void Shrink();

    /**
     * Updates the data from the argument into the database. This invalidates any token previously present in the database, replacing it by the matching token from the merged-in database.
     */
    void Update( ClTokenDatabase& other );
private:
    void UpdateToken( const ClTokenId freeTokenId, const ClAbstractToken& token);
private:
    ClFilenameDatabase& m_FileDB;
    ClTreeMap<ClAbstractToken>* m_pTokens;
    ClTreeMap<int>* m_pFileTokens;
    wxMutex m_Mutex;
};

#endif // TOKENDATABASE_H

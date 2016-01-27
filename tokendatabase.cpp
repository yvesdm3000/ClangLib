/*
 * Database responsible for resolving tokens between translation units
 */

#include "tokendatabase.h"

#include <wx/filename.h>
#include <wx/string.h>
#include <iostream>
#include <wx/mstream.h>

#include "treemap.h"

enum
{
    ClTokenPacketType_filenames = 1<<0,
    ClTokenPacketType_tokens = 1<<1
};

static bool WriteInt( wxOutputStream& out, const int val )
{
    out.Write( (const void*)&val, sizeof(val) );
    return true;
}

static bool WriteString( wxOutputStream& out, const char* str )
{
    int len = 0;

    if( str != NULL)
    {
        len = strlen(str); // Need size in amount of bytes
    }
    if( !WriteInt( out, len ) )
       return false;
    if (len > 0)
        out.Write( (const void*)str, len );
    return true;
}

static bool ReadInt( wxInputStream& in, int& out_Int )
{
    int val = 0;
    if( !in.CanRead() )
    {
        return false;
    }
    in.Read( &val, sizeof(val) );
    out_Int = val;
    return true;
}

static bool ReadString( wxInputStream& in, wxString& out_String )
{
    int len;
    if( !ReadInt( in, len ) )
        return false;
    if( len == 0 )
    {
        out_String = out_String.Truncate( 0 );
        return true;
    }
    if( !in.CanRead() )
    {
        return false;
    }
    char* buffer = (char*)alloca( len + 1 );

    //in.Read( wxStringBuffer(buffer, len), len );
    in.Read( buffer, len );
    buffer[len] = '\0';

    out_String = wxString::FromUTF8(buffer);

    return true;
}

bool ClAbstractToken::WriteOut( const ClAbstractToken& token,  wxOutputStream& out )
{
    // This is a cached database so we don't care about endianness for now. Who will ever copy these from one platform to another?
    WriteInt( out, token.tokenType );
    WriteInt( out, token.fileId );
    WriteInt( out, token.location.line );
    WriteInt( out, token.location.column );
    WriteString( out, token.identifier.mb_str() );
    WriteString( out, token.displayName.mb_str() );
    WriteString( out, token.scopeName.mb_str() );
    WriteInt( out, token.tokenHash );
    return true;
}

bool ClAbstractToken::ReadIn( ClAbstractToken& token, wxInputStream& in )
{
    int val = 0;
    if( !ReadInt( in, val ) )
        return false;
    token.tokenType = (ClTokenType)val;
    if( !ReadInt( in, val ) )
        return false;
    token.fileId = val;
    if( !ReadInt( in, val ) )
        return false;
    token.location.line = val;
    if( !ReadInt( in, val ) )
        return false;
    token.location.column = val;
    if( !ReadString( in, token.identifier ) )
        return false;
    if( ! ReadString( in, token.displayName ) )
        return false;
    if( !ReadString( in, token.scopeName ) )
        return false;
    if( !ReadInt( in, val ) )
        return false;
    token.tokenHash = val;
    return true;
}

ClFilenameDatabase::ClFilenameDatabase() :
    m_pFileEntries(new ClTreeMap<wxString>())
{

}

ClFilenameDatabase::~ClFilenameDatabase()
{
    delete m_pFileEntries;
}

bool ClFilenameDatabase::WriteOut( ClFilenameDatabase& db, wxOutputStream& out )
{
    int i;
    wxMutexLocker l(db.m_Mutex);
    int cnt = db.m_pFileEntries->GetCount();
    WriteInt( out, cnt );
    for( i=0; i<cnt; ++i )
    {
        wxString filename = db.m_pFileEntries->GetValue( (ClFileId)i );
        if( !WriteString( out, filename.mb_str() ) )
            return false;
    }
    return true;
}

bool ClFilenameDatabase::ReadIn( ClFilenameDatabase& db, wxInputStream& in )
{
    int i;
    wxMutexLocker l(db.m_Mutex);
    int packetCount = 0;
    if( !ReadInt(in, packetCount) )
        return false;
    for (i=0; i<packetCount; ++i)
    {
        wxString filename;
        if( ! ReadString(in, filename) )
            return false;
        db.m_pFileEntries->Insert( filename, filename );
    }
    return true;
}

ClFileId ClFilenameDatabase::GetFilenameId(const wxString& filename)
{
    wxMutexLocker lock(m_Mutex);
    assert(m_pFileEntries);
    assert(m_pTokens);
    wxFileName fln(filename.c_str());
    fln.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE);
    const wxString& normFile = fln.GetFullPath(wxPATH_UNIX);
    std::vector<int> id = m_pFileEntries->GetIdSet(normFile);
    if (id.empty())
    {
        wxString f = wxString(normFile.c_str());
        ClFileId id = m_pFileEntries->Insert( f, f );
        return id;
    }
    return id.front();
}

wxString ClFilenameDatabase::GetFilename(ClFileId fId)
{
    wxMutexLocker lock( m_Mutex);

    assert(m_pFileEntries->HasValue(fId));

    const wxChar* val = m_pFileEntries->GetValue(fId).c_str();
    if (val == NULL)
        return wxString();

    return wxString(val);
}

ClTokenDatabase::ClTokenDatabase( ClFilenameDatabase& fileDB) :
    m_FileDB(fileDB),
    m_pTokens(new ClTreeMap<ClAbstractToken>()),
    m_pFileTokens(new ClTreeMap<int>()),
    m_Mutex(wxMUTEX_RECURSIVE)
{
}

ClTokenDatabase::ClTokenDatabase( const ClTokenDatabase& other) :
    m_FileDB(other.m_FileDB),
    m_pTokens(new ClTreeMap<ClAbstractToken>(*other.m_pTokens)),
    //m_pFileEntries(new ClTreeMap<ClFileEntry>(*other.m_pFileEntries)),
    m_pFileTokens(new ClTreeMap<int>(*other.m_pFileTokens)),
    m_Mutex(wxMUTEX_RECURSIVE)
{

}

ClTokenDatabase::~ClTokenDatabase()
{
    delete m_pTokens;
}

void swap( ClTokenDatabase& first, ClTokenDatabase& second )
{
    using std::swap;

    // Let's assume no inverse swap will be performed for now
    wxMutexLocker l1(first.m_Mutex);
    wxMutexLocker l2(second.m_Mutex);

    swap(*first.m_pTokens, *second.m_pTokens);
    swap(*first.m_pFileTokens, *second.m_pFileTokens);
}


bool ClTokenDatabase::ReadIn( ClTokenDatabase& tokenDatabase, wxInputStream& in )
{
    in.SeekI( 4 ); // Magic number
    int version = 0;
    if( !ReadInt(in, version) )
        return false;
    int i = 0;
    if( version != 0x01 )
    {
        return false;
    }
    tokenDatabase.Clear();
    int read_count = 0;

    wxMutexLocker( tokenDatabase.m_Mutex );
    while( in.CanRead() )
    {
        int packetType = 0;
        if( !ReadInt(in, packetType) )
            return false;
        switch(packetType)
        {
        case ClTokenPacketType_filenames:
            if( ! ClFilenameDatabase::ReadIn( tokenDatabase.m_FileDB, in ) )
                return false;
            break;
        case ClTokenPacketType_tokens:
            int packetCount = 0;
            if( !ReadInt(in, packetCount) )
                return false;
            for (i=0; i<packetCount; ++i)
            {
                ClAbstractToken token;
                if( ! ClAbstractToken::ReadIn( token, in ) )
                    return false;
                if( token.fileId != -1 )
                {
                    ClTokenId tokId = tokenDatabase.InsertToken( token );
                    //fprintf( stdout, " '%s' / '%s' / fId=%d location=%d:%d hash=%d dbEntryId=%d\n", (const char*)token.identifier.mb_str(), (const char*)token.displayName.mbc_str(), token.fileId, token.location.line, token.location.column,  token.tokenHash, tokId );
                    read_count++;
                }
            }
            break;
        }
    }
    return true;
}

bool ClTokenDatabase::WriteOut( ClTokenDatabase& tokenDatabase, wxOutputStream& out )
{
    int i;
    int cnt;
    out.Write( "CbCc", 4 ); // Magic number
    WriteInt( out, 1 ); // Version number

    WriteInt( out, ClTokenPacketType_filenames );
    if( !ClFilenameDatabase::WriteOut( tokenDatabase.m_FileDB, out ) )
        return false;

    wxMutexLocker( tokenDatabase.m_Mutex );

    WriteInt( out, ClTokenPacketType_tokens );
    cnt = tokenDatabase.m_pTokens->GetCount();

    WriteInt( out, cnt );
    uint32_t written_count = 0;
    for( i=0; i<cnt; ++i )
    {
        ClAbstractToken tok = tokenDatabase.m_pTokens->GetValue( i );
        if( !ClAbstractToken::WriteOut( tok, out ) )
            return false;
    }
    return true;
}

void ClTokenDatabase::Clear()
{
    wxMutexLocker lock(m_Mutex);
    delete m_pTokens;
    delete m_pFileTokens;
    m_pTokens = new ClTreeMap<ClAbstractToken>(),
    m_pFileTokens = new ClTreeMap<int>();
}
ClFileId ClTokenDatabase::GetFilenameId(const wxString& filename)
{
    return m_FileDB.GetFilenameId(filename);
}
wxString ClTokenDatabase::GetFilename(ClFileId fId)
{
    return m_FileDB.GetFilename(fId);
}

ClTokenId ClTokenDatabase::InsertToken(const ClAbstractToken& token)
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

ClTokenId ClTokenDatabase::GetTokenId(const wxString& identifier, ClFileId fileId, ClTokenType tokenType, unsigned tokenHash )
{
    wxMutexLocker lock( m_Mutex);
    std::vector<int> ids = m_pTokens->GetIdSet(identifier);
    for (std::vector<int>::const_iterator itr = ids.begin();
            itr != ids.end(); ++itr)
    {
        if (m_pTokens->HasValue(*itr))
        {
            ClAbstractToken tok = m_pTokens->GetValue(*itr);
            if( (tok.tokenHash == tokenHash)&&((tok.tokenType == tokenType)||(tokenType == ClTokenType_Unknown))&&((tok.fileId == fileId)||(fileId == wxNOT_FOUND)) )
                return *itr;
        }
    }
    return wxNOT_FOUND;
}

ClAbstractToken ClTokenDatabase::GetToken(ClTokenId tId)
{
    wxMutexLocker lock( m_Mutex);
    assert( m_pTokens->HasValue(tId) );
    return m_pTokens->GetValue(tId);
}

std::vector<ClTokenId> ClTokenDatabase::GetTokenMatches(const wxString& identifier)
{
    wxMutexLocker lock( m_Mutex);
    return m_pTokens->GetIdSet(identifier);
}

std::vector<ClTokenId> ClTokenDatabase::GetFileTokens(ClFileId fId)
{
    wxMutexLocker lock( m_Mutex);
    wxString key = wxString::Format(wxT("%d"), fId);
    std::vector<ClTokenId> tokens = m_pFileTokens->GetIdSet(key);

    return tokens;
}

void ClTokenDatabase::Shrink()
{
    wxMutexLocker lock(m_Mutex);
    m_pTokens->Shrink();
    m_pFileTokens->Shrink();
}

void ClTokenDatabase::UpdateToken( const ClTokenId freeTokenId, const ClAbstractToken& token )
{
    ClAbstractToken tokenRef = m_pTokens->GetValue(freeTokenId);
    m_pTokens->RemoveIdKey( tokenRef.identifier, freeTokenId );
    assert( (tokenRef.fileId == wxNOT_FOUND)&&"Only an unused token can be updated");
    tokenRef.displayName = token.displayName;
    tokenRef.fileId = token.fileId;
    tokenRef.identifier = token.identifier;
    tokenRef.location = token.location;
    tokenRef.scopeName = token.scopeName;
    tokenRef.tokenHash = token.tokenHash;
    tokenRef.tokenType = token.tokenType;
    wxString filen = wxString::Format(wxT("%d"), token.fileId);
    m_pFileTokens->Insert(filen, freeTokenId);
}

void ClTokenDatabase::RemoveToken( const ClTokenId tokenId )
{
    ClAbstractToken oldToken = GetToken( tokenId );
    wxString key = wxString::Format(wxT("%d"), oldToken.fileId);
    m_pFileTokens->Remove( key, tokenId );
    ClAbstractToken t;
    // We just invalidate it here. Real removal is rather complex
    UpdateToken( tokenId, t );
}

void ClTokenDatabase::Update( const ClFileId fileId, const ClTokenDatabase& db )
{
    int i;
    std::vector<ClTokenId> oldTokenIds;

    wxMutexLocker lock( m_Mutex);
    oldTokenIds = GetFileTokens( fileId );
    int cnt = db.m_pTokens->GetCount();
    for (i=0;i<cnt;++i)
    {
        ClAbstractToken tok = db.m_pTokens->GetValue(i);
        ClTokenId tokId = InsertToken(tok);
        for( std::vector<ClTokenId>::iterator it = oldTokenIds.begin(); it != oldTokenIds.end();  ++it)
        {
            if( *it == tokId )
            {
                it = oldTokenIds.erase( it );
                break;
            }
        }
    }
    for( std::vector<ClTokenId>::iterator it = oldTokenIds.begin(); it != oldTokenIds.end();  ++it)
    {
        RemoveToken( *it );
    }
}

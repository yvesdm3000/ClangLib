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

static void WriteInt( wxOutputStream& out, const int val )
{
    out.Write( (const void*)&val, sizeof(val) );
}

static void WriteString( wxOutputStream& out, const wxString str )
{
    int len = 0;
    const char* buf = NULL;

    buf = (const char*)str.mb_str();
    if( buf != nullptr)
        len = strlen(buf); // Need size in amount of bytes
    WriteInt( out, len );

    if (len > 0)
        out.Write( (const void*)buf, len );
}

static int ReadInt( wxInputStream& in )
{
    int val = 0;
    if( !in.CanRead() )
    {
        return 0;
    }
    in.Read( &val, sizeof(val) );
    return val;
}

static wxString ReadString( wxInputStream& in )
{
    int len = ReadInt( in );
    if( len == 0 )
    {
        return wxString();
    }
    wxString buffer;
    //buffer.Alloc( len + 1 );
    if( !in.CanRead() )
    {
        return wxString();
    }
    in.Read( wxStringBuffer(buffer, len), len );

    return buffer;
}

void ClAbstractToken::WriteOut( const ClAbstractToken& token,  wxOutputStream& out )
{
    // This is a cached database so we don't care about endianness for now. Who will ever copy these from one platform to another?
    WriteInt( out, token.tokenType );
    WriteInt( out, token.fileId );
    WriteInt( out, token.location.line );
    WriteInt( out, token.location.column );
    WriteString( out, token.identifier );
    WriteString( out, token.displayName );
    WriteString( out, token.scopeName );
    WriteInt( out, token.tokenHash );
}

void ClAbstractToken::ReadIn( ClAbstractToken& token, wxInputStream& in )
{
    token.tokenType = (ClTokenType)ReadInt( in );
    token.fileId = ReadInt( in );
    token.location.line = ReadInt(in);
    token.location.column = ReadInt(in);
    token.identifier = ReadString( in );
    token.displayName = ReadString( in );
    token.scopeName = ReadString( in );
    token.tokenHash = ReadInt(in);
}

ClFilenameDatabase::ClFilenameDatabase() :
    m_pFileEntries(new ClTreeMap<ClFileEntry>())
{

}

ClFilenameDatabase::~ClFilenameDatabase()
{
    delete m_pFileEntries;
}

void ClFilenameDatabase::WriteOut( ClFilenameDatabase& db, wxOutputStream& out )
{
    int i;
    wxMutexLocker l(db.m_Mutex);
    int cnt = db.m_pFileEntries->GetCount();
    fprintf( stdout, "Writing %d file entries\n", cnt );
    WriteInt( out, cnt );
    for( i=0; i<cnt; ++i )
    {
        ClFileEntry e = db.m_pFileEntries->GetValue( i );
        WriteString( out, e.filename );
    }
}

void ClFilenameDatabase::ReadIn( ClFilenameDatabase& db, wxInputStream& in )
{
    int i;
    wxMutexLocker l(db.m_Mutex);
    int packetCount = ReadInt(in);
    for (i=0; i<packetCount; ++i)
    {
        wxString filename = ReadString(in);
        ClFileEntry entry(filename);
        db.m_pFileEntries->Insert( filename, entry );
    }
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
        ClFileEntry e(f);
        ClFileId id = m_pFileEntries->Insert( f, e );
        //fprintf(stdout,"%s this=%p Storing %s(%p) as %d\n", __PRETTY_FUNCTION__, (void*)this, (const char*)f.mb_str(), (void*)f.c_str(), (int)id );
        return id;
    }
    return id.front();
}

wxString ClFilenameDatabase::GetFilename(ClFileId fId)
{
    wxMutexLocker lock( m_Mutex);

    assert(m_pFileEntries->HasValue(fId));
    //if (!m_pFileEntries->HasValue(fId))
    //    return wxString();

    const wxChar* val = m_pFileEntries->GetValue(fId).filename.c_str();
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


void ClTokenDatabase::ReadIn( ClTokenDatabase& tokenDatabase, wxInputStream& in )
{
    in.SeekI( 4 ); // Magic number
    int version = ReadInt(in);
    int i = 0;
    if( version != 0x01 )
    {
        return;
    }
    tokenDatabase.Clear();
    int read_count = 0;

    wxMutexLocker( tokenDatabase.m_Mutex );
    while( in.CanRead() )
    {
        int packetType = ReadInt(in);
        fprintf( stdout, "type=%d\n", packetType );
        int packetCount = ReadInt(in);
        fprintf( stdout, "count=%d\n", packetCount );
        switch(packetType)
        {
        case ClTokenPacketType_filenames:
            ClFilenameDatabase::ReadIn( tokenDatabase.m_FileDB, in );
            break;
        case ClTokenPacketType_tokens:
            for (i=0; i<packetCount; ++i)
            {
                ClAbstractToken token;
                ClAbstractToken::ReadIn( token, in );
                tokenDatabase.InsertToken( token );
                read_count++;
            }
            break;
        }
    }
    fprintf(stdout,"Read %d tokens from disk\n", read_count);
}

void ClTokenDatabase::WriteOut( ClTokenDatabase& tokenDatabase, wxOutputStream& out )
{
    int i;
    int cnt;
    out.Write( "CbCc", 4 ); // Magic number
    WriteInt( out, 1 ); // Version number

    WriteInt( out, ClTokenPacketType_filenames );
    ClFilenameDatabase::WriteOut( tokenDatabase.m_FileDB, out );

    wxMutexLocker( tokenDatabase.m_Mutex );

    WriteInt( out, ClTokenPacketType_tokens );
    cnt = tokenDatabase.m_pTokens->GetCount();
    fprintf( stdout, "Writing %d tokens\n", cnt );

    WriteInt( out, cnt );
    uint32_t written_count = 0;
    for( i=0; i<cnt; ++i )
    {
        ClAbstractToken tok = tokenDatabase.m_pTokens->GetValue( i );
        //if (tok.revision == tokenDatabase.m_pFileEntries->GetValue( tok.fileId ).revision)
        {
            ClAbstractToken::WriteOut( tok, out );
            written_count++;
        }
    }
    fprintf( stdout, "Written %d token entries\n", (int)written_count );
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

    ClTokenId tId = GetTokenId(token.identifier, token.fileId, token.tokenHash);
    if (tId == wxNOT_FOUND)
    {
        tId = m_pTokens->Insert(wxString(token.identifier), token);
        wxString filen = wxString::Format(wxT("%d"), token.fileId);
        m_pFileTokens->Insert(filen, tId);
    }
    return tId;
}

ClTokenId ClTokenDatabase::GetTokenId(const wxString& identifier, ClFileId fileId, unsigned tokenHash )
{
    wxMutexLocker lock( m_Mutex);
    std::vector<int> ids = m_pTokens->GetIdSet(identifier);
    for (std::vector<int>::const_iterator itr = ids.begin();
            itr != ids.end(); ++itr)
    {
        if (m_pTokens->HasValue(*itr))
        {
            ClAbstractToken tok = m_pTokens->GetValue(*itr);
            if( (tok.tokenHash == tokenHash)&&((tok.fileId == fileId)||(fileId == wxNOT_FOUND)) )
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
    ClAbstractToken& tokenRef = m_pTokens->GetValue(freeTokenId);
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

void ClTokenDatabase::Update( ClTokenDatabase& db )
{
    int i;
    std::set<ClFileId> fileIds;
    std::set<ClTokenId> freeTokenIds;

    wxMutexLocker lock( m_Mutex);
    int cnt = db.m_pTokens->GetCount();
    for (i=0;i<cnt;++i)
    {
        ClAbstractToken tok = db.m_pTokens->GetValue(i);
        if(tok.fileId == -1)
        {
            freeTokenIds.insert(i);
        }
        else
        {
            fileIds.insert(tok.fileId);
        }
    }
    // Clear old entries
    for (std::set<ClFileId>::iterator it = fileIds.begin(); it != fileIds.end(); ++it)
    {
        wxString key = wxString::Format(wxT("%d"), *it);
        std::vector<ClTokenId> l = m_pFileTokens->GetIdSet(key);
        for (std::vector<ClTokenId>::iterator it2 = l.begin(); it2 != l.end(); ++it2)
        {
            freeTokenIds.insert( *it2 );
            ClAbstractToken& tokenRef = m_pTokens->GetValue(*it2);
            tokenRef.tokenType = ClTokenType_Unknown;
            tokenRef.displayName = wxString();
            tokenRef.fileId = -1;
            tokenRef.scopeName = wxString();
        }
    }

    std::set<ClTokenId>::iterator freeTokenIt = freeTokenIds.begin();
    for (i=0;i<cnt;++i)
    {
        ClAbstractToken tok = db.m_pTokens->GetValue(i);
        if( freeTokenIt != freeTokenIds.end())
        {
            UpdateToken( *freeTokenIt, tok );
            ++freeTokenIt;
        }
        else
        {
            InsertToken(tok);
        }
    }
}

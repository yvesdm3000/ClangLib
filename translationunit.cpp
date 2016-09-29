/*
 * Wrapper class around CXTranslationUnit
 */

#include <sdk.h>
#include <iostream>
#include "translationunit.h"

#ifndef CB_PRECOMP
#include <algorithm>
#endif // CB_PRECOMP

#include "tokendatabase.h"
#include "cclogger.h"

#if 0
class ClangVisitorContext
{
public:
    ClangVisitorContext(ClTranslationUnit* pTranslationUnit) :
        m_pTranslationUnit(pTranslationUnit)
    { }
    std::deque<wxString> m_ScopeStack;
    std::deque<CXCursor> m_CursorSt
};
#endif

struct ClangVisitorContext
{
    ClangVisitorContext(ClTokenDatabase* pDatabase)
    {
        database = pDatabase;
        tokenCount = 0;
    }
    ClTokenDatabase* database;
    unsigned long long tokenCount;
    ClFunctionScopeMap functionScopes;
};

static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* inclusion_stack,
                               unsigned include_len, CXClientData client_data);

static CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor parent, CXClientData client_data);

ClTranslationUnit::ClTranslationUnit(ClTokenIndexDatabase* IndexDatabase, const ClTranslUnitId id, CXIndex clIndex) :
    m_pDatabase(new ClTokenDatabase(IndexDatabase)),
    m_Id(id),
    m_FileId(-1),
    m_ClIndex(clIndex),
    m_ClTranslUnit(nullptr),
    m_LastCC(nullptr),
    m_LastPos(-1, -1),
    m_LastParsed(wxDateTime::Now())
{
}
ClTranslationUnit::ClTranslationUnit(ClTokenIndexDatabase* indexDatabase, const ClTranslUnitId id) :
    m_pDatabase(new ClTokenDatabase(indexDatabase)),
    m_Id(id),
    m_FileId(-1),
    m_ClIndex(nullptr),
    m_ClTranslUnit(nullptr),
    m_LastCC(nullptr),
    m_LastPos(-1, -1),
    m_LastParsed(wxDateTime::Now())
{
}


#if __cplusplus >= 201103L
ClTranslationUnit::ClTranslationUnit(ClTranslationUnit&& other) :
    m_pDatabase(std::move(other.m_pDatabase)),
    m_Id(other.m_Id),
    m_FileId(other.m_FileId),
    m_Files(std::move(other.m_Files)),
    m_ClIndex(other.m_ClIndex),
    m_ClTranslUnit(other.m_ClTranslUnit),
    m_LastCC(nullptr),
    m_LastPos(-1, -1)
{
    other.m_ClTranslUnit = nullptr;
}
#else
ClTranslationUnit::ClTranslationUnit(const ClTranslationUnit& other) :
    m_pDatabase(new ClTokenDatabase(nullptr)),
    m_Id(other.m_Id),
    m_FileId( other.m_FileId ),
    m_ClIndex(other.m_ClIndex),
    m_ClTranslUnit(other.m_ClTranslUnit),
    m_LastCC(nullptr),
    m_Diagnostics(other.m_Diagnostics),
    m_LastPos(-1, -1)
{
    swap(*m_pDatabase, *const_cast<ClTranslationUnit&>(other).m_pDatabase);
    m_Files.swap(const_cast<ClTranslationUnit&>(other).m_Files);
    const_cast<ClTranslationUnit&>(other).m_ClTranslUnit = nullptr;
}
#endif

ClTranslationUnit::~ClTranslationUnit()
{
    if (m_LastCC)
        clang_disposeCodeCompleteResults(m_LastCC);
    if (m_ClTranslUnit)
    {
        clang_disposeTranslationUnit(m_ClTranslUnit);
    }
    delete m_pDatabase;
}

std::ostream& operator << (std::ostream& str, const std::vector<ClFileId> files)
{
    str<<"[ ";
    for ( std::vector<ClFileId>::const_iterator it = files.begin(); it != files.end(); ++it )
    {
        str<<*it<<", ";
    }
    str<<"]";
    return str;
}
/** \brief Calculate code completion at a certain position
 *
 * \param complete_filename The filename where code-completion is performed in
 * \param complete_location The location within filename where code-completion is requested. It should always start at the beginning of the word in case a subset is requested.
 * \param unsaved_files     List of unsaved files
 * \param num_unsaved_files Amount of element in unsaved_files
 * \param CompleteOptions   Options passed to clang. See clang_codeCompleteAt for allowed values
 * \return Pointer to a CXCodeCompleteResults that represents all results from code completion
 */
CXCodeCompleteResults* ClTranslationUnit::CodeCompleteAt(const wxString& complete_filename, const ClTokenPosition& complete_location,
                                                         struct CXUnsavedFile* unsaved_files, unsigned num_unsaved_files, unsigned CompleteOptions )
{
    if (m_ClTranslUnit == nullptr)
    {
        return nullptr;
    }
    //if (m_LastPos.Equals(complete_location.line, complete_location.column)&&(m_LastCC)&&m_LastCC->NumResults)
    //{
    //    fprintf(stdout,"%s: Returning last CC %d,%d (%d)\n", __PRETTY_FUNCTION__, (int)complete_location.line, (int)complete_location.column,  m_LastCC->NumResults);
    //    return m_LastCC;
    //}
    if (m_LastCC)
        clang_disposeCodeCompleteResults(m_LastCC);
    m_LastCC = clang_codeCompleteAt(m_ClTranslUnit, (const char*)complete_filename.ToUTF8(), complete_location.line, complete_location.column,
                                    unsaved_files, num_unsaved_files, clang_defaultCodeCompleteOptions() | CompleteOptions );
    m_LastPos.Set(complete_location.line, complete_location.column);
    if (m_LastCC)
    {
#if 0
        unsigned long long context = clang_codeCompleteGetContexts( m_LastCC );
        CCLogger::Get()->DebugLog( F(wxT("Code completion context: %llu (%x)"), context, (int)context) );
        if (context&CXCompletionContext_AnyType)
            CCLogger::Get()->DebugLog( wxT("AnyType") );
        if (context&CXCompletionContext_AnyValue)
            CCLogger::Get()->DebugLog( wxT("AnyValue") );
        if (context&CXCompletionContext_ArrowMemberAccess)
            CCLogger::Get()->DebugLog( wxT("ArrowMemberAccess") );
        if (context&CXCompletionContext_ClassTag)
            CCLogger::Get()->DebugLog( wxT("ClassTag") );
        if (context&CXCompletionContext_CXXClassTypeValue)
            CCLogger::Get()->DebugLog( wxT("CXXClassTypeValue") );
        if (context&CXCompletionContext_DotMemberAccess)
            CCLogger::Get()->DebugLog( wxT("DotMemberAccess") );
        if (context&CXCompletionContext_EnumTag)
            CCLogger::Get()->DebugLog( wxT("EnumTag") );
        if (context&CXCompletionContext_MacroName)
            CCLogger::Get()->DebugLog( wxT("MacroName") );
        if (context&CXCompletionContext_Namespace)
            CCLogger::Get()->DebugLog( wxT("Namespace") );
        if (context&CXCompletionContext_NaturalLanguage)
            CCLogger::Get()->DebugLog( wxT("NaturalLanguage") );
        if (context&CXCompletionContext_NestedNameSpecifier)
            CCLogger::Get()->DebugLog( wxT("NestedNameSpecifier") );
        if (context&CXCompletionContext_StructTag)
            CCLogger::Get()->DebugLog( wxT("StructTag") );
        if (context&CXCompletionContext_Unexposed)
            CCLogger::Get()->DebugLog( wxT("Unexposed") );
        if (context&CXCompletionContext_UnionTag)
            CCLogger::Get()->DebugLog( wxT("UnionTag") );
        if (context&CXCompletionContext_Unknown)
            CCLogger::Get()->DebugLog( wxT("Unknown") );
#endif
#if 0
        unsigned numDiag = clang_codeCompleteGetNumDiagnostics(m_LastCC);

        //unsigned int IsIncomplete = 0;
        //CXCursorKind kind = clang_codeCompleteGetContainerKind(m_LastCC, &IsIncomplete );


        unsigned int diagIdx = 0;
        std::vector<ClDiagnostic> diaglist;
        for (diagIdx=0; diagIdx < numDiag; ++diagIdx)
        {
            CXDiagnostic diag = clang_codeCompleteGetDiagnostic(m_LastCC, diagIdx);
            ExpandDiagnostic(diag, complete_filename, diaglist);
        }
#endif
    }

    return m_LastCC;
}

const CXCodeCompleteResults* ClTranslationUnit::GetCCResults() const
{
    return m_LastCC;
}

bool ClTranslationUnit::HasCCContext( CXCompletionContext ctx ) const
{
    if (!m_LastCC)
        return false;
    unsigned long long contexts = clang_codeCompleteGetContexts( m_LastCC );
    if ( ctx &&( (contexts & ctx) == ctx) )
        return true;
    return ctx == contexts;
}

const CXCompletionResult* ClTranslationUnit::GetCCResult(unsigned index) const
{
    if (m_LastCC && index < m_LastCC->NumResults)
        return m_LastCC->Results + index;
    return nullptr;
}

CXCursor ClTranslationUnit::GetTokenAt(const wxString& filename, const ClTokenPosition& location)
{
    if (m_ClTranslUnit == nullptr)
    {
        return clang_getNullCursor();
    }
    CXCursor cursor = clang_getCursor(m_ClTranslUnit, clang_getLocation(m_ClTranslUnit, GetFileHandle(filename), location.line, location.column));
    return cursor;
}

wxString ClTranslationUnit::GetTokenIdentifierAt( const wxString &filename, const ClTokenPosition &position )
{
    wxString tokenName;
    CCLogger::Get()->DebugLog(F(wxT("GetTokenIdentifierAt %d,%d")+filename, position.line, position.column));
    CXCursor cursor = GetTokenAt( filename, position );
    CXCompletionString token = clang_getCursorCompletionString(cursor);
    HashToken( token, tokenName);
    if (tokenName.length() == 0)
    {
        CXString str = clang_getCursorDisplayName(cursor);
        tokenName = wxString::FromUTF8(clang_getCString(str));
        clang_disposeString(str);
    }
    return tokenName;
}

/**
 * Parses the supplied file and unsaved files
 *
 * @arg filename The filename that is the main file of the translation unit
 * @arg fileId The fileid that is the view for this translation unit
 */
bool ClTranslationUnit::Parse(const wxString& filename, ClFileId fileId, const std::vector<const char*>& args, const std::map<wxString, wxString>& unsavedFiles, const bool bReparse )
{
    CCLogger::Get()->DebugLog(F(_T("ClTranslationUnit::Parse %s id=%d"), filename.c_str(), (int)m_Id));

    if (m_LastCC)
    {
        clang_disposeCodeCompleteResults(m_LastCC);
        m_LastCC = nullptr;
    }
    if (m_ClTranslUnit)
    {
        clang_disposeTranslationUnit(m_ClTranslUnit);
        m_ClTranslUnit = nullptr;
    }
    m_Diagnostics.clear();
    wxString viewFilename = m_pDatabase->GetFilename( fileId );

    // TODO: check and handle error conditions
    std::vector<CXUnsavedFile> clUnsavedFiles;
    std::vector<wxCharBuffer> clFileBuffer;
    for (std::map<wxString, wxString>::const_iterator fileIt = unsavedFiles.begin();
            fileIt != unsavedFiles.end(); ++fileIt)
    {
        CXUnsavedFile unit;
        clFileBuffer.push_back(fileIt->first.ToUTF8());
        unit.Filename = clFileBuffer.back().data();
        clFileBuffer.push_back(fileIt->second.ToUTF8());
        unit.Contents = clFileBuffer.back().data();
#if wxCHECK_VERSION(2, 9, 4)
        unit.Length   = clFileBuffer.back().length();
#else
        unit.Length   = strlen(unit.Contents); // extra work needed because wxString::Length() treats multibyte character length as '1'
#endif
        clUnsavedFiles.push_back(unit);
    }
    m_LastParsed = wxDateTime::Now();
    m_FunctionScopes.clear();
    m_Files.clear();
    m_FileId = wxNOT_FOUND;
    m_Diagnostics.clear();

    if (filename.length() != 0)
    {
        m_ClTranslUnit = clang_parseTranslationUnit(m_ClIndex, filename.ToUTF8().data(), args.empty() ? nullptr : &args[0], args.size(),
                         //clUnsavedFiles.empty() ? nullptr : &clUnsavedFiles[0], clUnsavedFiles.size(),
                         nullptr, 0,
                         clang_defaultEditingTranslationUnitOptions()
                         | CXTranslationUnit_CacheCompletionResults
                         | CXTranslationUnit_IncludeBriefCommentsInCodeCompletion
                         | CXTranslationUnit_DetailedPreprocessingRecord
                         | CXTranslationUnit_PrecompiledPreamble
                         //CXTranslationUnit_CacheCompletionResults |
                         //    CXTranslationUnit_Incomplete | CXTranslationUnit_DetailedPreprocessingRecord |
                         //    CXTranslationUnit_CXXChainedPCH
                                                   );
        if (m_ClTranslUnit == nullptr)
        {
            CCLogger::Get()->DebugLog( wxT("clang_parseTranslationUnit failed") );
            return false;
        }
        if (bReparse)
        {
            int ret = clang_reparseTranslationUnit(m_ClTranslUnit, clUnsavedFiles.size(),
                                                   clUnsavedFiles.empty() ? nullptr : &clUnsavedFiles[0],
                                                   clang_defaultReparseOptions(m_ClTranslUnit) );
            if (ret != 0)
            {
                CCLogger::Get()->Log(_T("ReparseTranslationUnit failed"));
                // clang spec specifies that the only valid operation on the translation unit after a failure is to dispose the TU
                clang_disposeTranslationUnit(m_ClTranslUnit);
                m_ClTranslUnit = nullptr;
                return false;
            }
        }
        m_FileId = fileId;
        m_Files.push_back( fileId );
        if (fileId != m_FileId)
            m_Files.push_back( m_FileId );
        if (m_Id != 127)
        {
            CXDiagnosticSet diagSet = clang_getDiagnosticSetFromTU(m_ClTranslUnit);
            wxString srcText;
            if (unsavedFiles.find(viewFilename) != unsavedFiles.end())
            {
                srcText = unsavedFiles.at(viewFilename);
            }
            ExpandDiagnosticSet(diagSet, viewFilename, srcText, m_Diagnostics);
            clang_disposeDiagnosticSet(diagSet);
            CCLogger::Get()->DebugLog( F(wxT("Diagnostics expanded: %d"), (int)m_Diagnostics.size()) );
        }

        return true;
    }
    return false;
}

void ClTranslationUnit::Reparse( const std::map<wxString, wxString>& unsavedFiles)
{
    CCLogger::Get()->DebugLog(F(_T("ClTranslationUnit::Reparse id=%d"), (int)m_Id));

    if (m_ClTranslUnit == nullptr)
    {
        return;
    }
    wxString filename = m_pDatabase->GetFilename( m_FileId );
    CCLogger::Get()->DebugLog( F(wxT("Filename for %d: '")+filename+wxT("'"), m_FileId) );
    std::vector<CXUnsavedFile> clUnsavedFiles;
    std::vector<wxCharBuffer> clFileBuffer;
    for (std::map<wxString, wxString>::const_iterator fileIt = unsavedFiles.begin();
         fileIt != unsavedFiles.end(); ++fileIt)
    {
        CXUnsavedFile unit;
        clFileBuffer.push_back(fileIt->first.ToUTF8());
        unit.Filename = clFileBuffer.back().data();
        clFileBuffer.push_back(fileIt->second.ToUTF8());
        unit.Contents = clFileBuffer.back().data();
#if wxCHECK_VERSION(2, 9, 4)
        unit.Length   = clFileBuffer.back().length();
#else
        unit.Length   = strlen(unit.Contents); // extra work needed because wxString::Length() treats multibyte character length as '1'
#endif
        clUnsavedFiles.push_back(unit);
    }

    m_Diagnostics.clear();

    // TODO: check and handle error conditions
    int ret = clang_reparseTranslationUnit(m_ClTranslUnit, clUnsavedFiles.size(),
                                           clUnsavedFiles.empty() ? nullptr : &clUnsavedFiles[0],
                                           clang_defaultReparseOptions(m_ClTranslUnit)
                                           //CXTranslationUnit_CacheCompletionResults | CXTranslationUnit_PrecompiledPreamble |
                                           //CXTranslationUnit_Incomplete | CXTranslationUnit_DetailedPreprocessingRecord |
                                           //CXTranslationUnit_CXXChainedPCH
                                          );
    if (ret != 0)
    {
        //assert(false&&"clang_reparseTranslationUnit should succeed");
        CCLogger::Get()->Log(_T("ReparseTranslationUnit failed"));

        // The only thing we can do according to Clang documentation is dispose it...
        clang_disposeTranslationUnit(m_ClTranslUnit);
        m_ClTranslUnit = nullptr;
        return;
    }
    if (m_LastCC)
    {
        clang_disposeCodeCompleteResults(m_LastCC);
        m_LastCC = nullptr;
    }
    m_LastParsed = wxDateTime::Now();

    CXDiagnosticSet diagSet = clang_getDiagnosticSetFromTU(m_ClTranslUnit);
    wxString srcText;
    if (unsavedFiles.find(filename) != unsavedFiles.end())
    {
        srcText = unsavedFiles.at(filename);
    }
    ExpandDiagnosticSet(diagSet, filename, srcText, m_Diagnostics);
    clang_disposeDiagnosticSet(diagSet);

    CCLogger::Get()->DebugLog(F(_T("ClTranslationUnit::Reparse id=%d finished. Diagnostics: %d"), (int)m_Id, (int)m_Diagnostics.size()));
}

bool ClTranslationUnit::ProcessAllTokens(std::vector<ClFileId>* out_pIncludeFileList, ClFunctionScopeMap* out_pFunctionScopes, ClTokenDatabase* out_pTokenDatabase) const
{
    if (m_ClTranslUnit == nullptr)
        return false;
    if (out_pIncludeFileList)
    {
        std::pair<std::vector<ClFileId>*, ClTokenDatabase*> visitorData = std::make_pair(out_pIncludeFileList, out_pTokenDatabase);
        clang_getInclusions(m_ClTranslUnit, ClInclusionVisitor, &visitorData);
        out_pIncludeFileList->push_back( m_FileId );
        std::sort(out_pIncludeFileList->begin(), out_pIncludeFileList->end());
        std::unique(out_pIncludeFileList->begin(), out_pIncludeFileList->end());
#if __cplusplus >= 201103L
        //m_Files.shrink_to_fit();
        out_pIncludeFileList->shrink_to_fit();
#else
        //std::vector<ClFileId>(m_Files).swap(m_Files);
        std::vector<ClFileId>(*out_pIncludeFileList).swap(*out_pIncludeFileList);
#endif
    }
    //m_Files.reserve(1024);
    //m_Files.push_back(m_FileId);
    //std::sort(m_Files.begin(), m_Files.end());
    //std::unique(m_Files.begin(), m_Files.end());
    if (out_pTokenDatabase)
    {
        struct ClangVisitorContext ctx(out_pTokenDatabase);
        //unsigned rc =
        clang_visitChildren(clang_getTranslationUnitCursor(m_ClTranslUnit), ClAST_Visitor, &ctx);
        CCLogger::Get()->DebugLog(F(_T("ClTranslationUnit::UpdateTokenDatabase %d finished: %d tokens processed, %d function scopes"), (int)m_Id, (int)ctx.tokenCount, (int)ctx.functionScopes.size()));
        if (out_pFunctionScopes)
            *out_pFunctionScopes = ctx.functionScopes;
    }

    return true;
}

void ClTranslationUnit::SwapTokenDatabase(ClTokenDatabase& other)
{
    swap(*m_pDatabase, other);
}

void ClTranslationUnit::GetDiagnostics(const wxString& /*filename*/, std::vector<ClDiagnostic>& out_diagnostics)
{
    if (m_ClTranslUnit == nullptr)
    {
        return;
    }
    out_diagnostics = m_Diagnostics;
    CCLogger::Get()->DebugLog( F(wxT("Returning %d diagnostics for tu %d"), (int)m_Diagnostics.size(), (int)m_Id) );
}

CXFile ClTranslationUnit::GetFileHandle(const wxString& filename) const
{
    return clang_getFile(m_ClTranslUnit, filename.ToUTF8().data());
}

static void RangeToColumns(CXSourceRange range, unsigned& rgStart, unsigned& rgEnd)
{
    CXSourceLocation rgLoc = clang_getRangeStart(range);
    clang_getSpellingLocation(rgLoc, nullptr, nullptr, &rgStart, nullptr);
    rgLoc = clang_getRangeEnd(range);
    clang_getSpellingLocation(rgLoc, nullptr, nullptr, &rgEnd, nullptr);
}

/** @brief Expand the diagnostics into the supplied list. This appends the diagnostics to the passed list.
 *
 * @param diag CXDiagnostic
 * @param filename const wxString&
 * @param inout_diagnostics std::vector<ClDiagnostic>&
 * @return void
 *
 */
void ClTranslationUnit::ExpandDiagnostic( CXDiagnostic diag, const wxString& filename, const wxString& srcText, std::vector<ClDiagnostic>& inout_diagnostics )
{
    if (diag == nullptr)
        return;
    CXSourceLocation diagLoc = clang_getDiagnosticLocation(diag);
    if (clang_equalLocations(diagLoc, clang_getNullLocation()))
        return;
    switch (clang_getDiagnosticSeverity(diag))
    {
    case CXDiagnostic_Ignored:
    case CXDiagnostic_Note:
        return;
    default:
        break;
    }
    unsigned line;
    unsigned column;
    CXFile file;
    clang_getSpellingLocation(diagLoc, &file, &line, &column, nullptr);
    CXString str = clang_getFileName(file);
    wxString flName = wxString::FromUTF8(clang_getCString(str));
    clang_disposeString(str);

    if (flName == filename)
    {
        size_t numRnges = clang_getDiagnosticNumRanges(diag);
        unsigned rgStart = 0;
        unsigned rgEnd = 0;
        for (size_t j = 0; j < numRnges; ++j) // often no range data (clang bug?)
        {
            RangeToColumns(clang_getDiagnosticRange(diag, j), rgStart, rgEnd);
            if (rgStart != rgEnd)
                break;
        }
        if (rgStart == rgEnd) // check if there is FixIt data for the range
        {
            numRnges = clang_getDiagnosticNumFixIts(diag);
            for (size_t j = 0; j < numRnges; ++j)
            {
                CXSourceRange range;
                clang_getDiagnosticFixIt(diag, j, &range);
                RangeToColumns(range, rgStart, rgEnd);
                if (rgStart != rgEnd)
                    break;
            }
        }
        if (rgEnd == 0) // still no range -> use the range of the current token
        {
            CXCursor token = clang_getCursor(m_ClTranslUnit, diagLoc);
            RangeToColumns(clang_getCursorExtent(token), rgStart, rgEnd);
        }
        if (rgEnd < column || rgStart > column) // out of bounds?
            rgStart = rgEnd = column;
        str = clang_formatDiagnostic(diag, 0);
        wxString diagText = wxString::FromUTF8(clang_getCString(str));
        clang_disposeString(str);
        if (diagText.StartsWith(wxT("warning: ")) )
        {
            diagText = diagText.Right( diagText.Length() - 9 );
        }
        else if (diagText.StartsWith(wxT("error: ")) )
        {
            diagText = diagText.Right( diagText.Length() - 7 );
        }
        ClDiagnosticSeverity sev = sWarning;
        switch ( clang_getDiagnosticSeverity(diag))
        {
        case CXDiagnostic_Error:
        case CXDiagnostic_Fatal:
            sev = sError;
            break;
        case CXDiagnostic_Note:
            sev = sNote;
            break;
        case CXDiagnostic_Warning:
        case CXDiagnostic_Ignored:
            sev = sWarning;
            break;
        }
        std::vector<ClDiagnosticFixit> fixitList;
        unsigned numFixIts = clang_getDiagnosticNumFixIts( diag );
        for (unsigned fixIdx = 0; fixIdx < numFixIts; ++fixIdx)
        {
            CXSourceRange sourceRange;
            str = clang_getDiagnosticFixIt( diag, fixIdx, &sourceRange );
            wxString text = wxString::FromUTF8( clang_getCString(str) );
            clang_disposeString(str);
            unsigned fixitStart = rgStart;
            unsigned fixitEnd = rgEnd;
            RangeToColumns(sourceRange, fixitStart, fixitEnd);

            CXSourceLocation srcLoc = clang_getRangeStart(sourceRange);
            CXFile file;
            unsigned line = 0, column = 0, offset1 = 0, offset2 = 0;
            clang_getFileLocation(srcLoc, &file, &line, &column, NULL);
            srcLoc = clang_getLocation( m_ClTranslUnit, file, line, 1 );
            clang_getFileLocation(srcLoc, NULL, NULL, NULL, &offset1);
            srcLoc = clang_getLocation( m_ClTranslUnit, file, line + 1, 1 );
            clang_getFileLocation(srcLoc, NULL, NULL, NULL, &offset2);
            wxString fixitLine;
            if (offset2 < srcText.Length())
                fixitLine = srcText.SubString( offset1, offset2 ).Trim();
            fixitList.push_back( ClDiagnosticFixit(text, fixitStart, fixitEnd, fixitLine) );
        }
        inout_diagnostics.push_back(ClDiagnostic( line, rgStart, rgEnd, sev, flName, diagText, fixitList ));
    }
}

/** @brief Expand a Clang CXDiagnosticSet into our clanglib vector representation
 *
 * @param diagSet The CXDiagnosticSet
 * @param filename Filename that this diagnostic targets
 * @param diagnostics[out] The returned diagnostics vector
 * @return void
 *
 */
void ClTranslationUnit::ExpandDiagnosticSet(CXDiagnosticSet diagSet, const wxString& filename, const wxString& srcText, std::vector<ClDiagnostic>& diagnostics)
{
    size_t numDiags = clang_getNumDiagnosticsInSet(diagSet);
    for (size_t i = 0; i < numDiags; ++i)
    {
        CXDiagnostic diag = clang_getDiagnosticInSet(diagSet, i);
        ExpandDiagnostic(diag, filename, srcText, diagnostics);
        //ExpandDiagnosticSet(clang_getChildDiagnostics(diag), filename, srcText, diagnostics);
        clang_disposeDiagnostic(diag);
    }
}

void ClTranslationUnit::UpdateFunctionScopes( const ClFileId fileId, const ClFunctionScopeList &functionScopes )
{
    m_FunctionScopes.erase(fileId);
    m_FunctionScopes.insert(std::make_pair(fileId, functionScopes));
}
#if 0
bool ClTranslationUnit::LookupTokenDefinition( const ClFileId fileId, const wxString& identifier, const wxString& usr, ClTokenPosition& out_Position)
{
    std::set<ClTokenId> tokenIdList;
    m_Database.GetTokenMatches(identifier, tokenIdList);

    for (std::set<ClTokenId>::const_iterator it = tokenIdList.begin(); it != tokenIdList.end(); ++it)
    {
        ClAbstractToken tok = m_Database.GetToken( *it );
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
#endif
/** @brief Calculate a hash from a Clang token
 *
 * @param token CXCompletionString
 * @param identifier wxString&
 * @return unsigned
 *
 */
unsigned HashToken(CXCompletionString token, wxString& identifier)
{
    unsigned hVal = 2166136261u;
    size_t upperBound = clang_getNumCompletionChunks(token);
    for (size_t i = 0; i < upperBound; ++i)
    {
        CXString str = clang_getCompletionChunkText(token, i);
        const char* pCh = clang_getCString(str);
        if (clang_getCompletionChunkKind(token, i) == CXCompletionChunk_TypedText)
            identifier = wxString::FromUTF8(*pCh =='~' ? pCh + 1 : pCh);
        for (; *pCh; ++pCh)
        {
            hVal ^= *pCh;
            hVal *= 16777619u;
        }
        clang_disposeString(str);
    }
    return hVal;
}

/** @brief Static function used in the Clang AST visitor functions
 *
 * @param inclusion_stack
 * @return void ClInclusionVisitor(CXFile included_file, CXSourceLocation*
 *
 */
static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* WXUNUSED(inclusion_stack),
                               unsigned WXUNUSED(include_len), CXClientData client_data)
{
    CXString filename = clang_getFileName(included_file);
    wxFileName inclFile(wxString::FromUTF8(clang_getCString(filename)));
    if (inclFile.MakeAbsolute())
    {
        std::pair<std::vector<ClFileId>*, ClTokenDatabase*>* data = static_cast<std::pair<std::vector<ClFileId>*, ClTokenDatabase*>*>(client_data);
        ClFileId fileId = data->second->GetFilenameId( inclFile.GetFullPath() );
        data->first->push_back( fileId );
        //clTranslUnit->first->AddInclude(clTranslUnit->second->GetFilenameId(inclFile.GetFullPath()));
    }
    clang_disposeString(filename);
}

/** @brief Static function used in the Clang AST visitor functions
 *
 * @param parent
 * @return CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor
 *
 */
static CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor WXUNUSED(parent), CXClientData client_data)
{
    ClTokenType typ = ClTokenType_Unknown;
    CXChildVisitResult ret = CXChildVisit_Break; // should never happen
    switch (cursor.kind)
    {
    case CXCursor_StructDecl:
    case CXCursor_UnionDecl:
    case CXCursor_ClassDecl:
    case CXCursor_EnumDecl:
    case CXCursor_Namespace:
    case CXCursor_ClassTemplate:
        typ = ClTokenType_Scope;
        ret = CXChildVisit_Recurse;
        break;

    case CXCursor_FieldDecl:
        typ = ClTokenType_Var;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_EnumConstantDecl:
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_FunctionDecl:
        typ = ClTokenType_Func;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_VarDecl:
        typ = ClTokenType_Var;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_ParmDecl:
        typ = ClTokenType_Parm;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_TypedefDecl:
        //case CXCursor_MacroDefinition: // this can crash Clang on Windows
        ret = CXChildVisit_Continue;
        break;
//    case CXCursor_TypeRef:
//        typ = ClTokenType_VarDef;
//        ret = CXChildVisit_Continue;
//        break;
    case CXCursor_CXXMethod:
    case CXCursor_Constructor:
    case CXCursor_Destructor:
    case CXCursor_FunctionTemplate:
        typ = ClTokenType_Func;
        //ret = CXChildVisit_Continue;
        ret = CXChildVisit_Recurse;
        break;

    default:
//        CCLogger::Get()->DebugLog( F(wxT("Unhandled cursor type: %d"), (int)cursor.kind) );
        return CXChildVisit_Recurse;
    }
    if (clang_isCursorDefinition( cursor ))
    {
        typ = (ClTokenType)(typ | ClTokenType_DefGroup);
    }
    else if (clang_isDeclaration( cursor.kind ))
    {
        typ = (ClTokenType)(typ | ClTokenType_DeclGroup);
    }
    else if (clang_isExpression( cursor.kind ))
    {
        typ = (ClTokenType)(typ | ClTokenType_RefGroup);
    }

    CXSourceLocation loc = clang_getCursorLocation(cursor);
    CXSourceRange tokenRange = clang_getCursorExtent( cursor );
    CXFile clFile;
    unsigned line = 1, col = 1;
    clang_getSpellingLocation(loc, &clFile, &line, &col, nullptr);
    CXString str = clang_getFileName(clFile);
    wxString filename = wxString::FromUTF8(clang_getCString(str));
    clang_disposeString(str);
    if (filename.IsEmpty())
        return ret;

    CXCompletionString token = clang_getCursorCompletionString(cursor);
    wxString identifier;
    str = clang_getCursorUSR( cursor );
    wxString usr = wxString::FromUTF8( clang_getCString( str ) );
    clang_disposeString( str );
    if (usr.Length() == 0)
    {
        CXCursor declCursor = clang_getCursorDefinition( cursor );
        str = clang_getCursorUSR( declCursor );
        usr = wxString::FromUTF8( clang_getCString( str ) );
        clang_disposeString( str );
    }
    unsigned tokenHash = HashToken(token, identifier);
    if (identifier.IsEmpty())
    {
        //str = clang_getCursorDisplayName(cursor);
        //wxString displayName = wxString::FromUTF8( clang_getCString( str ) );
        //CCLogger::Get()->DebugLog( F(wxT("Skipping symbol %d,%d")+displayName, line, col ));
        //clang_disposeString(str);
    }else{
        //CCLogger::Get()->DebugLog( wxT("Visited symbol ")+identifier );
        wxString displayName;
        wxString scopeName;
        CXCursor cursorWalk = cursor;
        while (!clang_Cursor_isNull(cursorWalk))
        {
            switch (cursorWalk.kind)
            {
            case CXCursor_Namespace:
            case CXCursor_StructDecl:
            case CXCursor_ClassDecl:
            case CXCursor_ClassTemplate:
            case CXCursor_ClassTemplatePartialSpecialization:
            case CXCursor_CXXMethod:
                str = clang_getCursorDisplayName(cursorWalk);
                if (displayName.Length() == 0)
                    displayName = wxString::FromUTF8(clang_getCString(str));
                else
                {
                    if (scopeName.Length() > 0)
                        scopeName = scopeName.Prepend(wxT("::"));
                    scopeName = scopeName.Prepend(wxString::FromUTF8(clang_getCString(str)));
                }
                clang_disposeString(str);
                break;
            default:
                break;
            }
            cursorWalk = clang_getCursorSemanticParent(cursorWalk);
        }
        struct ClangVisitorContext* ctx = static_cast<struct ClangVisitorContext*>(client_data);
        ClFileId fileId = ctx->database->GetFilenameId(filename);
        ClAbstractToken tok(typ, fileId, ClTokenPosition(line, col), identifier, usr, tokenHash);
        {
            CXCursor* cursorList = NULL;
            unsigned int cursorNum = 0;
            clang_getOverriddenCursors( cursor, &cursorList, &cursorNum );
            for (unsigned int i=0; i < cursorNum; ++i)
            {
                str = clang_getCursorUSR( cursorList[i] );
                wxString usr = wxString::FromUTF8( clang_getCString( str ) );
                clang_disposeString( str );
                tok.parentUSRList.push_back( usr );
            }
            clang_disposeOverriddenCursors(cursorList);
        }

        ctx->database->InsertToken(tok);
        ctx->tokenCount++;
        if (displayName.Length() > 0)
        {
            unsigned endLine = line;
            unsigned endCol = col;
            clang_getSpellingLocation(clang_getRangeEnd(tokenRange), nullptr, &endLine, &endCol, nullptr);
            if (ctx->functionScopes[fileId].size() > 0)
            {
                // Save some memory
                if (ctx->functionScopes[fileId].back().scopeName.IsSameAs( scopeName ) )
                {
                    scopeName = ctx->functionScopes[fileId].back().scopeName;
                    if (ctx->functionScopes[fileId].back().functionName.IsSameAs( displayName ))
                    {
                        return ret; // Duplicate...
                    }
                }
            }
            ctx->functionScopes[fileId].push_back( ClFunctionScope(displayName, scopeName, ClTokenPosition(line, col), ClTokenPosition(endLine, endCol)) );
        }
    }
    return ret;
}

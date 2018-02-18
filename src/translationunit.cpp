/*
 * Wrapper class around CXTranslationUnit
 */

#include "translationunit.h"
#include <sdk.h>
#include <iostream>

#ifndef CB_PRECOMP
#include <algorithm>
#endif // CB_PRECOMP

#include "tokendatabase.h"
#include "cclogger.h"

struct ClAST_VisitorContext
{
    ClAST_VisitorContext(ClTokenDatabase* pDatabase)
    {
        database = pDatabase;
        if (pDatabase)
        {
            pDatabase->GetAllTokenFiles(unprocessedFileIds);
        }
        tokenCount = 0;
    }
    ClTokenDatabase* database;
    std::set<ClFileId> unprocessedFileIds;
    unsigned long long tokenCount;
};

static void ClInclusionVisitor(CXFile included_file, CXSourceLocation* inclusion_stack,
                               unsigned include_len, CXClientData client_data);

static CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor parent, CXClientData client_data);

ClTranslationUnit::ClTranslationUnit(ClTokenIndexDatabase* IndexDatabase, const ClTranslUnitId id, CXIndex& clIndex) :
    m_pDatabase(new ClTokenDatabase(IndexDatabase)),
    m_Id(id),
    m_ClIndex(clIndex),
    m_FileId(-1),
    m_ClTranslUnit(nullptr),
    m_LastCC(nullptr),
    m_Diagnostics(),
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
    m_Diagnostics(),
    m_LastPos(-1, -1),
    m_LastParsed(wxDateTime::Now())
{
}


#if __cplusplus >= 201103L
ClTranslationUnit::ClTranslationUnit(ClTranslationUnit&& other) :
    m_pDatabase(nullptr),
    m_Id(other.m_Id),
    m_FileId(other.m_FileId),
    m_Files(),
    m_ClIndex(other.m_ClIndex),
    m_ClTranslUnit(nullptr),
    m_LastCC(nullptr),
    m_Diagnostics(other.m_Diagnostics),
    m_LastPos(-1, -1),
    m_LastParsed(other.m_LastParsed)
{
    std::swap(m_Files,other.m_Files);
	std::swap(m_pDatabase,other.m_pDatabase);
    std::swap(m_ClTranslUnit, other.m_ClTranslUnit);
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
    m_LastPos(-1, -1),
    m_LastParsed(other.m_LastParsed)
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

void ClTranslationUnit::Reset(ClTokenIndexDatabase* tokenIndexDatabase, const ClTranslUnitId id, CXIndex& clIndex )
{
    if (m_LastCC)
        clang_disposeCodeCompleteResults(m_LastCC);
    m_LastCC = nullptr;
    if (m_ClTranslUnit)
    {
        clang_disposeTranslationUnit(m_ClTranslUnit);
    }
    delete m_pDatabase;
    m_pDatabase = new ClTokenDatabase(tokenIndexDatabase);
    m_Id = id;
    m_ClIndex = clIndex;
    m_FileId = -1;
    m_ClTranslUnit = nullptr;
    m_LastCC = nullptr;
    m_Diagnostics.clear();
    m_LastPos.Set(-1, -1),
    m_LastParsed = wxDateTime::Now();
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
CXCodeCompleteResults* ClTranslationUnit::CodeCompleteAt(const std::string& complete_filename, const ClTokenPosition& complete_location,
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
    m_LastCC = clang_codeCompleteAt(m_ClTranslUnit, complete_filename.c_str(), complete_location.line, complete_location.column,
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

CXCursor ClTranslationUnit::GetTokenAt(const std::string& filename, const ClTokenPosition& location)
{
    if (m_ClTranslUnit == nullptr)
    {
        return clang_getNullCursor();
    }
    CXCursor cursor = clang_getCursor(m_ClTranslUnit, clang_getLocation(m_ClTranslUnit, GetFileHandle(filename), location.line, location.column));
    return cursor;
}

ClIdentifierString ClTranslationUnit::GetTokenIdentifier( const CXCursor& cursor )
{
    ClIdentifierString identifier;
    CXCompletionString token = clang_getCursorCompletionString(cursor);
    HashToken(token, identifier);
    if (identifier.empty())
    {
        CXString str = clang_getCursorDisplayName(cursor);
        if (clang_getCString( str ))
            identifier = clang_getCString(str);
        clang_disposeString(str);
    }
    return identifier;
}

/**
 * Parses the supplied file and unsaved files
 *
 * @arg filename The filename that is the main file of the translation unit
 * @arg fileId The fileid that is the view for this translation unit
 */
bool ClTranslationUnit::Parse(const std::string& filename, ClFileId fileId, const std::vector<std::string>& args, const std::map<std::string, wxString>& unsavedFiles, const bool bReparse )
{
    CCLogger::Get()->DebugLog(F(_T("ClTranslationUnit::Parse id=%d fn=")+wxString::FromUTF8( filename.c_str() )+wxT(" fileId=%d"), (int)m_Id, (int)fileId ));
    assert(m_pDatabase);
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
    std::string viewFilename = m_pDatabase->GetFilename( fileId );

    // TODO: check and handle error conditions
    std::vector<CXUnsavedFile> clUnsavedFiles;
    std::vector<std::string> clFileBuffer;
    for (std::map<std::string, wxString>::const_iterator fileIt = unsavedFiles.begin();
            fileIt != unsavedFiles.end(); ++fileIt)
    {
        CXUnsavedFile unit;
        unit.Filename = fileIt->first.c_str();
        clFileBuffer.push_back(fileIt->second.ToUTF8().data());
        unit.Contents = clFileBuffer.back().c_str();
        unit.Length = clFileBuffer.back().length();
        clUnsavedFiles.push_back(unit);
    }
    m_LastParsed = wxDateTime::Now();
    m_Files.clear();
    m_FileId = wxNOT_FOUND;

    if (filename.length() != 0)
    {
        const char** argList = nullptr;
        if (!args.empty())
        {
            argList = (const char**)malloc(sizeof(char*)*args.size());
            unsigned i = 0;
            for (std::vector<std::string>::const_iterator it = args.begin(); it != args.end(); ++it, ++i)
            {
                argList[i] = args[i].c_str();
            }
        }
        m_ClTranslUnit = clang_parseTranslationUnit(m_ClIndex, filename.c_str(), argList, args.size(),
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
        if (argList)
            free(argList);
        if (m_ClTranslUnit == nullptr)
        {
            CCLogger::Get()->LogError( wxT("ClangLib: Parse Translation Unit failed for ")+wxString::FromUTF8( filename.c_str()) );
            return false;
        }
        if (bReparse)
        {
            int ret = clang_reparseTranslationUnit(m_ClTranslUnit, clUnsavedFiles.size(),
                                                   clUnsavedFiles.empty() ? nullptr : &clUnsavedFiles[0],
                                                   clang_defaultReparseOptions(m_ClTranslUnit) );
            if (ret != 0)
            {
                CCLogger::Get()->LogError(wxT("ClangLib: Reparse Translation Unit failed for ")+wxString::FromUTF8( filename.c_str() ));
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
        }
        return true;
    }
    return false;
}

void ClTranslationUnit::Reparse( const std::map<std::string, wxString>& unsavedFiles)
{
    CCLogger::Get()->DebugLog(F(_T("ClTranslationUnit::Reparse id=%d"), (int)m_Id));

    if (m_ClTranslUnit == nullptr)
    {
        return;
    }
    std::string filename = m_pDatabase->GetFilename( m_FileId );
    std::vector<CXUnsavedFile> clUnsavedFiles;
    std::vector<std::string> clFileBuffer;
    for (std::map<std::string, wxString>::const_iterator fileIt = unsavedFiles.begin();
         fileIt != unsavedFiles.end(); ++fileIt)
    {
        CXUnsavedFile unit;
        unit.Filename = fileIt->first.c_str();
        clFileBuffer.push_back(fileIt->second.ToUTF8().data());
        unit.Contents = clFileBuffer.back().data();
        unit.Length = clFileBuffer.back().length();
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

bool ClTranslationUnit::ProcessAllTokens(std::vector<ClFileId>* out_pIncludeFileList, ClTokenDatabase* out_pTokenDatabase) const
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
        struct ClAST_VisitorContext ctx(out_pTokenDatabase);
        //unsigned rc =
        clang_visitChildren(clang_getTranslationUnitCursor(m_ClTranslUnit), ClAST_Visitor, &ctx);
        CCLogger::Get()->DebugLog(F(_T("ClTranslationUnit::UpdateTokenDatabase %d finished: %d tokens processed"), (int)m_Id, (int)ctx.tokenCount));
    }

    return true;
}

void ClTranslationUnit::SwapTokenDatabase(ClTokenDatabase& other)
{
    swap(*m_pDatabase, other);
}

void ClTranslationUnit::GetDiagnostics(const std::string& /*filename*/, std::vector<ClDiagnostic>& out_diagnostics)
{
    if (m_ClTranslUnit == nullptr)
    {
        return;
    }
    out_diagnostics = m_Diagnostics;
    CCLogger::Get()->DebugLog( F(wxT("Returning %d diagnostics for tu %d"), (int)m_Diagnostics.size(), (int)m_Id) );
}

CXFile ClTranslationUnit::GetFileHandle(const std::string& filename) const
{
    return clang_getFile(m_ClTranslUnit, filename.c_str());
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
void ClTranslationUnit::ExpandDiagnostic( CXDiagnostic diag, const wxString& srcText, std::vector<ClDiagnostic>& inout_diagnostics )
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
    CXString str;

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
    inout_diagnostics.push_back(ClDiagnostic( line, rgStart, rgEnd, sev, diagText, fixitList ));
}

/** @brief Structure used to add diagnostics on include statements
 */
struct UpdateIncludeDiagnosticsData
{
    std::string filename; // Translation unit filename
    std::vector<ClDiagnostic>& diagnostics;
    std::set<std::string> warningIncludes;
    std::set<std::string> errorIncludes;
    UpdateIncludeDiagnosticsData( const std::string& fName, std::vector<ClDiagnostic>& diags) : filename(fName), diagnostics(diags), warningIncludes(), errorIncludes() {}
};


/** @brief Callback function to visit the children to add diagnostics on #include statements
 *
 * @param cursor the cursor to pass
 * @param parentCursor
 * @return
 *
 */
void UpdateIncludeDiagnosticsVisitor( CXFile included_file, CXSourceLocation* inclusion_stack,
                               unsigned include_len, CXClientData clientData)
{
    CXString str;
    if (include_len == 0)
        return;
    unsigned int line = 0;
    unsigned int column = 0;
    CXFile file;
    clang_getSpellingLocation( inclusion_stack[0], &file, &line, &column, NULL );
    str = clang_getFileName( file );
    std::string srcFilename = clang_getCString( str );
    clang_disposeString( str );
    struct UpdateIncludeDiagnosticsData* data = static_cast<struct UpdateIncludeDiagnosticsData*>( clientData );

    if (data->filename != srcFilename)
        return;

    str = clang_getFileName(included_file);
    std::string includedFileName = clang_getCString(str);
    clang_disposeString(str);
    if (data->errorIncludes.find( includedFileName ) != data->errorIncludes.end())
    {
        data->diagnostics.push_back( ClDiagnostic( line, column, column, sError, wxT("Errors present"), std::vector<ClDiagnosticFixit>() ));
    }
    else if (data->warningIncludes.find( includedFileName ) != data->warningIncludes.end())
    {
        data->diagnostics.push_back( ClDiagnostic( line, column, column, sWarning, wxT("Warnings present"), std::vector<ClDiagnosticFixit>() ));
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
void ClTranslationUnit::ExpandDiagnosticSet(CXDiagnosticSet diagSet, const std::string& filename, const wxString& srcText, std::vector<ClDiagnostic>& diagnostics)
{
    size_t numDiags = clang_getNumDiagnosticsInSet(diagSet);
    struct UpdateIncludeDiagnosticsData includesData(filename, diagnostics);
    for (size_t i = 0; i < numDiags; ++i)
    {
        CXDiagnostic diag = clang_getDiagnosticInSet(diagSet, i);
        CXSourceLocation diagLoc = clang_getDiagnosticLocation(diag);
        if (clang_equalLocations(diagLoc, clang_getNullLocation()))
            continue;
        unsigned line;
        unsigned column;
        CXFile file;
        clang_getSpellingLocation(diagLoc, &file, &line, &column, nullptr);
        CXString str = clang_getFileName(file);
        std::string flName = clang_getCString(str);
        clang_disposeString(str);

        if (flName == filename)
        {
            ExpandDiagnostic(diag, srcText, diagnostics);
        }
        else
        {
            switch ( clang_getDiagnosticSeverity(diag))
            {
            case CXDiagnostic_Error:
            case CXDiagnostic_Fatal:
                includesData.errorIncludes.insert( flName );
                break;
            case CXDiagnostic_Note:
                break;
            case CXDiagnostic_Warning:
            case CXDiagnostic_Ignored:
                includesData.warningIncludes.insert( flName );
                break;
            }
        }

        clang_disposeDiagnostic(diag);
    }
    clang_getInclusions( m_ClTranslUnit, UpdateIncludeDiagnosticsVisitor, &includesData );
}

/** @brief Calculate a hash from a Clang token
 *
 * @param token CXCompletionString
 * @param identifier ClIdentifierString&
 * @return unsigned
 *
 */
unsigned HashToken(CXCompletionString token, ClIdentifierString& out_identifier)
{
    unsigned hVal = 2166136261u;
    size_t upperBound = clang_getNumCompletionChunks(token);
    for (size_t i = 0; i < upperBound; ++i)
    {
        CXString str = clang_getCompletionChunkText(token, i);
        const char* pCh = clang_getCString(str);
        if (clang_getCompletionChunkKind(token, i) == CXCompletionChunk_TypedText)
            out_identifier = *pCh =='~' ? pCh + 1 : pCh;
        for (; *pCh; ++pCh)
        {
            hVal ^= *pCh;
            hVal *= 16777619u;
        }
        clang_disposeString(str);
    }
    return hVal;
}

ClTokenCategory ClTranslationUnit::GetTokenCategory(CXCursorKind kind, CX_CXXAccessSpecifier access)
{
    switch (kind)
    {
    case CXCursor_StructDecl:
    case CXCursor_UnionDecl:
    case CXCursor_ClassDecl:
    case CXCursor_ClassTemplate:
        switch (access)
        {
        case CX_CXXPublic:
            return tcClassPublic;
        case CX_CXXProtected:
            return tcClassProtected;
        case CX_CXXPrivate:
            return tcClassPrivate;
        default:
        case CX_CXXInvalidAccessSpecifier:
            return tcClass;
        }

    case CXCursor_Constructor:
        switch (access)
        {
        default:
        case CX_CXXInvalidAccessSpecifier:
        case CX_CXXPublic:
            return tcCtorPublic;
        case CX_CXXProtected:
            return tcCtorProtected;
        case CX_CXXPrivate:
            return tcCtorPrivate;
        }

    case CXCursor_Destructor:
        switch (access)
        {
        default:
        case CX_CXXInvalidAccessSpecifier:
        case CX_CXXPublic:
            return tcDtorPublic;
        case CX_CXXProtected:
            return tcDtorProtected;
        case CX_CXXPrivate:
            return tcDtorPrivate;
        }

    case CXCursor_FunctionDecl:
    case CXCursor_CXXMethod:
    case CXCursor_FunctionTemplate:
    case CXCursor_ConversionFunction:
        switch (access)
        {
        default:
        case CX_CXXInvalidAccessSpecifier:
        case CX_CXXPublic:
            return tcFuncPublic;
        case CX_CXXProtected:
            return tcFuncProtected;
        case CX_CXXPrivate:
            return tcFuncPrivate;
        }
    case CXCursor_FieldDecl:
    case CXCursor_VarDecl:
    case CXCursor_ParmDecl:
        switch (access)
        {
        default:
        case CX_CXXInvalidAccessSpecifier:
        case CX_CXXPublic:
            return tcVarPublic;
        case CX_CXXProtected:
            return tcVarProtected;
        case CX_CXXPrivate:
            return tcVarPrivate;
        }
    case CXCursor_MacroDefinition:
        return tcMacroDef;

    case CXCursor_EnumDecl:
        switch (access)
        {
        case CX_CXXPublic:
            return tcEnumPublic;
        case CX_CXXProtected:
            return tcEnumProtected;
        case CX_CXXPrivate:
            return tcEnumPrivate;
        default:
        case CX_CXXInvalidAccessSpecifier:
            return tcEnum;
        }

    case CXCursor_EnumConstantDecl:
        return tcEnumerator;

    case CXCursor_Namespace:
        return tcNamespace;

    case CXCursor_TypedefDecl:
        switch (access)
        {
        case CX_CXXPublic:
            return tcTypedefPublic;
        case CX_CXXProtected:
            return tcTypedefProtected;
        case CX_CXXPrivate:
            return tcTypedefPrivate;
        default:
        case CX_CXXInvalidAccessSpecifier:
            return tcTypedef;
        }

    default:
        return tcNone;
    }
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
        ClFileId fileId = data->second->GetFilenameId( inclFile.GetFullPath().ToUTF8().data() );
        data->first->push_back( fileId );
        //clTranslUnit->first->AddInclude(clTranslUnit->second->GetFilenameId(inclFile.GetFullPath()));
    }
    clang_disposeString(filename);
}

static void ClImportClangToken(CXCursor cursor, CXCursor scopeCursor, ClTokenType typ, CXClientData client_data)
{
    if (typ < ClTokenType_DeclGroup)
    {
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
    }
    CXString str;
    CXCursor referencedCursor = cursor;
    CXCompletionString token = clang_getCursorCompletionString(cursor);
    ClIdentifierString identifier;
    int tokenHash = HashToken( token, identifier );
    if (identifier.empty())
    {
        if (typ&ClTokenType_RefGroup)
        {
            referencedCursor = clang_getCursorReferenced( cursor );
            token = clang_getCursorCompletionString( referencedCursor );
            tokenHash = HashToken( token, identifier );
        }
    }

    str = clang_getCursorUSR( referencedCursor );
    ClUSRString usr;
    if (clang_getCString( str ))
        usr = clang_getCString(str);
    clang_disposeString( str );
    CXCursor declCursor = referencedCursor;
    if (usr.empty())
    {
        declCursor = clang_getCursorDefinition( referencedCursor );
        str = clang_getCursorUSR( declCursor );
        if (clang_getCString(str))
            usr = clang_getCString( str );
        else
            declCursor = referencedCursor;
        clang_disposeString( str );
    }


    CXSourceLocation loc = clang_getCursorLocation(cursor);
    CXSourceRange tokenRange = clang_getCursorExtent( scopeCursor );
    CXFile clFile;
    unsigned line = 1, col = 1;
    clang_getSpellingLocation(loc, &clFile, &line, &col, nullptr);
    str = clang_getFileName(clFile);
    std::string filename;
    if (clang_getCString( str ))
        filename = clang_getCString(str);
    clang_disposeString(str);
    if (filename.empty())
    {
        if (typ == ClTokenType_FuncDecl)
            CCLogger::Get()->DebugLog( wxT("no filename") );
    }

    if (identifier.empty())
    {
        if (typ == ClTokenType_FuncDecl)
            CCLogger::Get()->DebugLog( wxT("Identifier is empty usr=")+wxString::FromUTF8(usr.c_str()) );
    }else{
        //CCLogger::Get()->DebugLog( wxT("Visited symbol ")+identifier );
        wxString displayName;
        str = clang_getCursorDisplayName(referencedCursor);
        displayName = wxString::FromUTF8(clang_getCString(str));
        clang_disposeString(str);
        if (displayName.IsEmpty())
        {
            displayName = wxString::FromUTF8(identifier.c_str());
        }

        ClIdentifierString scopeIdentifier;
        ClUSRString scopeUSR;
        CXCursor cursorWalk = clang_getCursorSemanticParent(cursor);
        while ( (!clang_Cursor_isNull(cursorWalk))&&(scopeIdentifier.empty()) )
        {
            switch (cursorWalk.kind)
            {
            case CXCursor_Namespace:
            case CXCursor_StructDecl:
            case CXCursor_ClassDecl:
            case CXCursor_ClassTemplate:
            case CXCursor_ClassTemplatePartialSpecialization:
            case CXCursor_CXXMethod:
            case CXCursor_FunctionDecl:
            case CXCursor_FunctionTemplate:
            case CXCursor_EnumDecl:
            case CXCursor_EnumConstantDecl:
            case CXCursor_ConversionFunction:
                {
                    CXCompletionString token = clang_getCursorCompletionString(cursorWalk);
                    HashToken( token, scopeIdentifier );
                }
                str = clang_getCursorUSR( cursorWalk );
                if (clang_getCString(str) != NULL)
                    scopeUSR = clang_getCString( str );
                clang_disposeString( str );
                break;
            case CXCursor_TranslationUnit:
                cursorWalk = clang_getNullCursor();
                break;
            default:
                break;
            }
            if (!clang_Cursor_isNull( cursorWalk ))
                cursorWalk = clang_getCursorSemanticParent(cursorWalk);
        }
        unsigned endLine = line;
        unsigned endCol = col;
        clang_getSpellingLocation(clang_getRangeEnd(tokenRange), nullptr, &endLine, &endCol, nullptr);
        struct ClAST_VisitorContext* ctx = static_cast<struct ClAST_VisitorContext*>(client_data);
        ClFileId fileId = ctx->database->GetFilenameId(filename);
        if (ctx->unprocessedFileIds.find( fileId ) != ctx->unprocessedFileIds.end())
        {
            ctx->database->RemoveFileTokens(fileId);
            ctx->unprocessedFileIds.erase( fileId );
        }
        ClTokenCategory category = ClTranslationUnit::GetTokenCategory( cursor.kind, clang_getCXXAccessSpecifier( cursor ) );
        if (category == tcNone)
        {
            category = ClTranslationUnit::GetTokenCategory( referencedCursor.kind, clang_getCXXAccessSpecifier( referencedCursor ) );
        }
        ClAbstractToken tok(typ, fileId, ClTokenRange(ClTokenPosition(line, col),ClTokenPosition(endLine, endCol)), identifier, displayName, usr, category, tokenHash);
        {
            CXCursor* cursorList = NULL;
            unsigned int cursorNum = 0;
            clang_getOverriddenCursors( referencedCursor, &cursorList, &cursorNum );
            for (unsigned int i=0; i < cursorNum; ++i)
            {
                str = clang_getCursorUSR( cursorList[i] );
                ClUSRString usr = clang_getCString( str );
                clang_disposeString( str );
                str = clang_getCursorSpelling( cursorList[i] );
                ClIdentifierString identifier = clang_getCString(str);
                clang_disposeString( str );
                tok.parentTokenList.push_back( std::make_pair( identifier, usr ) );
            }
            clang_disposeOverriddenCursors(cursorList);
        }
        tok.scope = std::make_pair(scopeIdentifier,scopeUSR);

        ctx->database->InsertToken(tok);
        ctx->tokenCount++;
    }
}

/** @brief Static function used in the Clang AST visitor functions
 *
 * @param parent
 * @return CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor
 *
 */
static CXChildVisitResult ClAST_Visitor(CXCursor cursor, CXCursor parent, CXClientData client_data)
{
    CXCursor scopeCursor = cursor; // Cursor that points to the scope e.g the { }
    ClTokenType typ = ClTokenType_Unknown;
    CXChildVisitResult ret = CXChildVisit_Break; // should never happen
    switch (cursor.kind)
    {
    case CXCursor_StructDecl:
    case CXCursor_UnionDecl:
    case CXCursor_ClassDecl:
    case CXCursor_EnumDecl:
        typ = ClTokenType_ScopeDecl;
        ret = CXChildVisit_Recurse;
        break;
    case CXCursor_Namespace:
    case CXCursor_ClassTemplate:
        typ = ClTokenType_Scope;
        ret = CXChildVisit_Recurse;
        break;

    case CXCursor_FieldDecl:
        typ = ClTokenType_VarDecl;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_EnumConstantDecl:
        typ = ClTokenType_ValueDecl;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_ConversionFunction:
    case CXCursor_FunctionDecl:
        typ = ClTokenType_FuncDecl;
        ret = CXChildVisit_Recurse;
        break;
    case CXCursor_TypedefDecl:
        typ = ClTokenType_TypedefDecl;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_CompoundStmt:
        switch (parent.kind)
        {
        case CXCursor_FunctionDecl:
        case CXCursor_CXXMethod:
        case CXCursor_Constructor:
        case CXCursor_Destructor:
        case CXCursor_ConversionFunction:
        case CXCursor_FunctionTemplate:
            typ = ClTokenType_FuncDef;
            cursor = parent;
            break;
        case CXCursor_Namespace:
        case CXCursor_ClassDecl:
        case CXCursor_ClassTemplate:
        case CXCursor_ClassTemplatePartialSpecialization:
            typ = ClTokenType_ScopeDef;
            cursor = parent;
            break;
        default:
            return CXChildVisit_Recurse;
        }
        ret = CXChildVisit_Recurse;
        break;
    case CXCursor_VarDecl:
        typ = ClTokenType_VarDecl;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_ParmDecl:
        typ = ClTokenType_ParmDecl;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_TypeRef:
        typ = ClTokenType_VarDecl;
        ret = CXChildVisit_Continue;
        break;
    case CXCursor_MemberRefExpr:
    case CXCursor_MemberRef:
    case CXCursor_CallExpr:
        typ = ClTokenType_FuncRef;
        ret = CXChildVisit_Recurse;
        break;
    case CXCursor_CXXMethod:
    case CXCursor_Constructor:
    case CXCursor_Destructor:
    case CXCursor_FunctionTemplate:
        typ = ClTokenType_Func;
        //ret = CXChildVisit_Continue;
        ret = CXChildVisit_Recurse;
        break;

    case CXCursor_CXXBaseSpecifier:
    default:
//        CCLogger::Get()->DebugLog( F(wxT("Unhandled cursor type: %d"), (int)cursor.kind) );
        return CXChildVisit_Recurse;
    }
    ClImportClangToken(cursor, scopeCursor, typ, client_data);
    return ret;
}

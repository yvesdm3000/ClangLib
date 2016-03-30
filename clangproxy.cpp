/*
 * Communication proxy to libclang-c
 */

#include <sdk.h>

#include "clangproxy.h"

#include <wx/tokenzr.h>

#ifndef CB_PRECOMP
#include <algorithm>
#include <wx/wxscintilla.h>
#endif // CB_PRECOMP

#include <set>

#include "tokendatabase.h"
#include "translationunit.h"
#include <cbcolourmanager.h>
#include "cclogger.h"

namespace ProxyHelper
{
static ClTokenCategory GetTokenCategory(CXCursorKind kind, CX_CXXAccessSpecifier access = CX_CXXInvalidAccessSpecifier)
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

static CXChildVisitResult ClCallTipCtorAST_Visitor(CXCursor cursor,
        CXCursor WXUNUSED(parent),
        CXClientData client_data)
{
    switch (cursor.kind)
    {
    case CXCursor_Constructor:
    {
        std::vector<CXCursor>* tokenSet
            = static_cast<std::vector<CXCursor>*>(client_data);
        tokenSet->push_back(cursor);
        break;
    }

    case CXCursor_FunctionDecl:
    case CXCursor_CXXMethod:
    case CXCursor_FunctionTemplate:
    {
        CXString str = clang_getCursorSpelling(cursor);
        if (strcmp(clang_getCString(str), "operator()") == 0)
        {
            std::vector<CXCursor>* tokenSet
                = static_cast<std::vector<CXCursor>*>(client_data);
            tokenSet->push_back(cursor);
        }
        clang_disposeString(str);
        break;
    }

    default:
        break;
    }
    return CXChildVisit_Continue;
}

static CXChildVisitResult ClInheritance_Visitor(CXCursor cursor,
        CXCursor WXUNUSED(parent),
        CXClientData client_data)
{
    if (cursor.kind != CXCursor_CXXBaseSpecifier)
        return CXChildVisit_Break;
    CXString str = clang_getTypeSpelling(clang_getCursorType(cursor));
    static_cast<wxStringVec*>(client_data)->push_back(wxString::FromUTF8(clang_getCString(str)));
    clang_disposeString(str);
    return CXChildVisit_Continue;
}

static CXChildVisitResult ClEnum_Visitor(CXCursor cursor,
        CXCursor WXUNUSED(parent),
        CXClientData client_data)
{
    if (cursor.kind != CXCursor_EnumConstantDecl)
        return CXChildVisit_Break;
    int* counts = static_cast<int*>(client_data);
    long long val = clang_getEnumConstantDeclValue(cursor);
    if (val > 0 && !((val - 1) & val)) // is power of 2
        ++counts[0];
    ++counts[1];
    counts[2] = std::max(counts[2], static_cast<int>(val));
    return CXChildVisit_Continue;
}

static void ResolveCursorDecl(CXCursor& token)
{
    CXCursor resolve = clang_getCursorDefinition(token);
    if (clang_Cursor_isNull(resolve) || clang_isInvalid(token.kind))
    {
        resolve = clang_getCursorReferenced(token);
        if (!clang_Cursor_isNull(resolve) && !clang_isInvalid(token.kind))
            token = resolve;
    }
    else
        token = resolve;
}

static bool ResolveCursorDefinition(CXCursor& token)
{
    CXCursor resolve = clang_getCursorDefinition(token);
    if (clang_Cursor_isNull(resolve) || clang_isInvalid(token.kind))
    {
        return false;
    }

    token = resolve;
    return true;
}

static CXVisitorResult ReferencesVisitor(CXClientData context,
        CXCursor WXUNUSED(cursor),
        CXSourceRange range)
{
    unsigned rgStart, rgEnd;
    CXSourceLocation rgLoc = clang_getRangeStart(range);
    clang_getSpellingLocation(rgLoc, nullptr, nullptr, nullptr, &rgStart);
    rgLoc = clang_getRangeEnd(range);
    clang_getSpellingLocation(rgLoc, nullptr, nullptr, nullptr, &rgEnd);
    if (rgStart != rgEnd)
    {
        static_cast<std::vector< std::pair<int, int> >*>(context)
        ->push_back(std::make_pair<int, int>(rgStart, rgEnd - rgStart));
    }
    return CXVisit_Continue;
}

static wxString GetEnumValStr(CXCursor token)
{
    int counts[] = {0, 0, 0}; // (numPowerOf2, numTotal, maxVal)
    clang_visitChildren(clang_getCursorSemanticParent(token), &ProxyHelper::ClEnum_Visitor, counts);
    wxLongLong val(clang_getEnumConstantDeclValue(token));
    if (( (counts[0] == counts[1])
            || (counts[1] > 5 && counts[0] * 2 >= counts[1]) ) && val >= 0)
    {
        // lots of 2^n enum constants, probably bitmask -> display in hexadecimal
        wxString formatStr
            = wxString::Format(wxT("0x%%0%ulX"),
                               wxString::Format(wxT("%X"), // count max width for 0-padding
                                                static_cast<unsigned>(counts[2])).Length());
        return wxString::Format(formatStr, static_cast<unsigned long>(val.GetValue()));
    }
    else
        return val.ToString();
}
}

namespace HTML_Writer
{
static wxString Escape(const wxString& text)
{
    wxString html;
    html.reserve(text.size());
    for (wxString::const_iterator itr = text.begin();
            itr != text.end(); ++itr)
    {
        switch (wxChar(*itr))
        {
        case wxT('&'):
            html += wxT("&amp;");
            break;
        case wxT('\"'):
            html += wxT("&quot;");
            break;
        case wxT('\''):
            html += wxT("&apos;");
            break;
        case wxT('<'):
            html += wxT("&lt;");
            break;
        case wxT('>'):
            html += wxT("&gt;");
            break;
        case wxT('\n'):
            html += wxT("<br>");
            break;
        default:
            html += *itr;
            break;
        }
    }
    return html;
}

static wxString Colourise(const wxString& text, const wxString& colour)
{
    return wxT("<font color=\"") + colour + wxT("\">") + text + wxT("</font>");
}

static wxString SyntaxHl(const wxString& code, const std::vector<wxString>& cppKeywords) // C++ style (ish)
{
    wxString html;
    html.reserve(code.size());
    int stRg = 0;
    int style = wxSCI_C_DEFAULT;
    const int codeLen = code.Length();
    for (int enRg = 0; enRg <= codeLen; ++enRg)
    {
        wxChar ch = (enRg < codeLen ? wxChar(code[enRg]) : wxT('\0'));
        wxChar nextCh = (enRg < codeLen - 1 ? wxChar(code[enRg + 1]) : wxT('\0'));
        switch (style)
        {
        default:
        case wxSCI_C_DEFAULT:
        {
            if (wxIsalpha(ch) || ch == wxT('_'))
                style = wxSCI_C_IDENTIFIER;
            else if (wxIsdigit(ch))
                style = wxSCI_C_NUMBER;
            else if (ch == wxT('"'))
                style = wxSCI_C_STRING;
            else if (ch == wxT('\''))
                style = wxSCI_C_CHARACTER;
            else if (ch == wxT('/') && nextCh == wxT('/'))
                style = wxSCI_C_COMMENTLINE;
            else if (wxIspunct(ch))
                style = wxSCI_C_OPERATOR;
            else
                break;
            if (stRg != enRg)
            {
                html += Escape(code.Mid(stRg, enRg - stRg));
                stRg = enRg;
            }
            break;
        }

        case wxSCI_C_IDENTIFIER:
        {
            if (wxIsalnum(ch) || ch == wxT('_'))
                break;
            if (stRg != enRg)
            {
                const wxString& tkn = code.Mid(stRg, enRg - stRg);
                if (std::binary_search(cppKeywords.begin(), cppKeywords.end(), tkn))
                    html += wxT("<b>") + Colourise(Escape(tkn), wxT("#00008b")) + wxT("</b>"); // DarkBlue
                else
                    html += Escape(tkn);
                stRg = enRg;
                --enRg;
            }
            style = wxSCI_C_DEFAULT;
            break;
        }

        case wxSCI_C_NUMBER:
        {
            if (wxIsalnum(ch))
                break;
            if (stRg != enRg)
            {
                html += Colourise(Escape(code.Mid(stRg, enRg - stRg)), wxT("Magenta"));
                stRg = enRg;
                --enRg;
            }
            style = wxSCI_C_DEFAULT;
            break;
        }

        case wxSCI_C_STRING:
        {
            if (ch == wxT('\\'))
            {
                if (nextCh != wxT('\n'))
                    ++enRg;
                break;
            }
            else if (ch && ch != wxT('"') && ch != wxT('\n'))
                break;
            if (stRg != enRg)
            {
                if (ch == wxT('"'))
                    ++enRg;
                html += Colourise(Escape(code.Mid(stRg, enRg - stRg)), wxT("#0000cd")); // MediumBlue
                stRg = enRg;
                --enRg;
            }
            style = wxSCI_C_DEFAULT;
            break;
        }

        case wxSCI_C_CHARACTER:
        {
            if (ch == wxT('\\'))
            {
                if (nextCh != wxT('\n'))
                    ++enRg;
                break;
            }
            else if (ch && ch != wxT('\'') && ch != wxT('\n'))
                break;
            if (stRg != enRg)
            {
                if (ch == wxT('\''))
                    ++enRg;
                html += Colourise(Escape(code.Mid(stRg, enRg - stRg)), wxT("GoldenRod"));
                stRg = enRg;
                --enRg;
            }
            style = wxSCI_C_DEFAULT;
            break;
        }

        case wxSCI_C_COMMENTLINE:
        {
            if (ch && ch != wxT('\n'))
                break;
            if (stRg != enRg)
            {
                html += Colourise(Escape(code.Mid(stRg, enRg - stRg)), wxT("#778899")); // LightSlateGray
                stRg = enRg;
            }
            style = wxSCI_C_DEFAULT;
            break;
        }

        case wxSCI_C_OPERATOR:
        {
            if (wxIspunct(ch) && ch != wxT('"') && ch != wxT('\'') && ch != wxT('_'))
                break;
            if (stRg != enRg)
            {
                html += Colourise(Escape(code.Mid(stRg, enRg - stRg)), wxT("Red"));
                stRg = enRg;
                --enRg;
            }
            style = wxSCI_C_DEFAULT;
            break;
        }
        }
    }
    return html;
}

static void FormatDocumentation(CXComment comment, wxString& doc, const std::vector<wxString>& cppKeywords)
{
    size_t numChildren = clang_Comment_getNumChildren(comment);
    for (size_t childIdx = 0; childIdx < numChildren; ++childIdx)
    {
        CXComment cmt = clang_Comment_getChild(comment, childIdx);
        switch (clang_Comment_getKind(cmt))
        {
        case CXComment_Null:
            break;

        case CXComment_Text:
        {
            CXString str = clang_TextComment_getText(cmt);
            doc += Escape(wxString::FromUTF8(clang_getCString(str)));
            clang_disposeString(str);
            break;
        }

        case CXComment_InlineCommand:
        {
            size_t numArgs = clang_InlineCommandComment_getNumArgs(cmt);
            wxString argText;
            for (size_t argIdx = 0; argIdx < numArgs; ++argIdx)
            {
                CXString str = clang_InlineCommandComment_getArgText(cmt, argIdx);
                argText += Escape(wxString::FromUTF8(clang_getCString(str)));
                clang_disposeString(str);
            }
            switch (clang_InlineCommandComment_getRenderKind(cmt))
            {
            default:
            case CXCommentInlineCommandRenderKind_Normal:
                doc += argText;
                break;

            case CXCommentInlineCommandRenderKind_Bold:
                doc += wxT("<b>") + argText + wxT("</b>");
                break;

            case CXCommentInlineCommandRenderKind_Monospaced:
                doc += wxT("<tt>") + argText + wxT("</tt>");
                break;

            case CXCommentInlineCommandRenderKind_Emphasized:
                doc += wxT("<em>") + argText + wxT("</em>");
                break;
            }
            break;
        }

        case CXComment_HTMLStartTag:
        case CXComment_HTMLEndTag:
        {
            CXString str = clang_HTMLTagComment_getAsString(cmt);
            doc += wxString::FromUTF8(clang_getCString(str));
            clang_disposeString(str);
            break;
        }

        case CXComment_Paragraph:
            if (!clang_Comment_isWhitespace(cmt))
            {
                doc += wxT("<p>");
                FormatDocumentation(cmt, doc, cppKeywords);
                doc += wxT("</p>");
            }
            break;

        case CXComment_BlockCommand: // TODO: follow the command's instructions
            FormatDocumentation(cmt, doc, cppKeywords);
            break;

        case CXComment_ParamCommand:  // TODO
        case CXComment_TParamCommand: // TODO
            break;

        case CXComment_VerbatimBlockCommand:
            doc += wxT("<table cellspacing=\"0\" cellpadding=\"1\" bgcolor=\"black\" width=\"100%\"><tr><td>"
                       "<table bgcolor=\"white\" width=\"100%\"><tr><td><pre>");
            FormatDocumentation(cmt, doc, cppKeywords);
            doc += wxT("</pre></td></tr></table></td></tr></table>");
            break;

        case CXComment_VerbatimBlockLine:
        {
            CXString str = clang_VerbatimBlockLineComment_getText(cmt);
            wxString codeLine = wxString::FromUTF8(clang_getCString(str));
            clang_disposeString(str);
            int endIdx = codeLine.Find(wxT("*/")); // clang will throw in the rest of the file when this happens
            if (endIdx != wxNOT_FOUND)
            {
                endIdx = codeLine.Truncate(endIdx).Find(wxT("\\endcode")); // try to save a bit of grace, and recover what we can
                if (endIdx == wxNOT_FOUND)
                {
                    endIdx = codeLine.Find(wxT("@endcode"));
                    if (endIdx != wxNOT_FOUND)
                        codeLine.Truncate(endIdx);
                }
                else
                    codeLine.Truncate(endIdx);
                doc += SyntaxHl(codeLine, cppKeywords) + wxT("<br><font color=\"red\"><em>__clang_doxygen_parsing_error__</em></font><br>");
                return; // abort
            }
            doc += SyntaxHl(codeLine, cppKeywords) + wxT("<br>");
            break;
        }

        case CXComment_VerbatimLine:
        {
            CXString str = clang_VerbatimLineComment_getText(cmt);
            doc += wxT("<pre>") + Escape(wxString::FromUTF8(clang_getCString(str))) + wxT("</pre>"); // TODO: syntax highlight
            clang_disposeString(str);
            break;
        }

        case CXComment_FullComment: // ignore?
        default:
            break;
        }
    }
}
}

/** @brief Call override to perform the Reparse Job.
 *
 * @param clangproxy The clangproxy to perform the job on
 * @return void
 *
 */
void ClangProxy::ReparseJob::Execute(ClangProxy& clangproxy)
{
    clangproxy.Reparse( m_TranslId, m_CompileCommand, m_UnsavedFiles);

    if ( m_Parents )
    {
        // Following code also includes children. Will fix that later
        ClFileId fileId = clangproxy.m_Database.GetFilenameId(m_Filename);
        std::set<ClTranslUnitId> parentTranslUnits;
        {
            wxMutexLocker l(clangproxy.m_Mutex);
            for (std::vector<ClTranslationUnit>::iterator it = clangproxy.m_TranslUnits.begin(); it != clangproxy.m_TranslUnits.end(); ++it)
            {
                if ( it->Contains(fileId) )
                {
                    if ( it->GetId() != m_TranslId )
                    {
                        parentTranslUnits.insert(it->GetId());
                    }
                }
            }
        }
        for (std::set<ClTranslUnitId>::iterator it = parentTranslUnits.begin(); it != parentTranslUnits.end(); ++it)
        {
            clangproxy.Reparse( *it, m_CompileCommand, m_UnsavedFiles );
        }
    }

    // Get rid of some copied memory
    m_UnsavedFiles.clear();

}

/** @brief ClangProxy constructor.
 *
 * @param pEvtCallbackHandler Pointer to the event handler where to send completed jobs to
 * @param database The main tokendatabase to work on.
 * @param cppKeywords CPP Keywords to use
 *
 */
ClangProxy::ClangProxy( wxEvtHandler* pEvtCallbackHandler, ClTokenDatabase& database, const std::vector<wxString>& cppKeywords):
    m_Mutex(),
    m_Database(database),
    m_CppKeywords(cppKeywords),
    m_pEventCallbackHandler(pEvtCallbackHandler)
{
    m_ClIndex[0] = clang_createIndex(1, 1);
    m_ClIndex[1] = clang_createIndex(1, 1);
    m_pThread = new BackgroundThread(false);
    m_pThread->SetPriority( 0 );
}

/** @brief ClangProxy destructor
 */
ClangProxy::~ClangProxy()
{
#if 0
    //TaskThread* pThread = m_pThread;
    {
        wxMutexLocker lock(m_Mutex);
        m_pThread = NULL;
        m_ConditionQueueNotEmpty.Signal();
        m_TranslUnits.clear();
    }
#endif
    //pThread->Wait();
    clang_disposeIndex(m_ClIndex[0]);
    clang_disposeIndex(m_ClIndex[1]);
}

/** @brief Create a translation unit
 *
 * @param filename The filename to create the translation unit from
 * @param commands Compile command options to give to Clang as if clang compiles the file
 * @param unsavedFiles Map of all unsaved files in the editor, with the filename as keyword
 * @param out_TranslId The translation unit id as a result of this call.
 * @return void
 *
 * This call will choose either a free slot of translation units or free one if all slots are occupied. It then parses the translation unit and when that is successfull, assign it a slot.
 */
void ClangProxy::CreateTranslationUnit(const wxString& filename, const wxString& commands, const std::map<wxString, wxString>& unsavedFiles, ClTranslUnitId& out_TranslId)
{
    if ( filename.Length() == 0 )
        return;

    wxString cmd = commands + wxT(" -ferror-limit=0");
    wxStringTokenizer tokenizer(cmd);
    if (!filename.EndsWith(wxT(".c"))) // force language reduces chance of error on STL headers
        tokenizer.SetString(cmd + wxT(" -x c++"));
    std::vector<wxString> unknownOptions;
    unknownOptions.push_back(wxT("-Wno-unused-local-typedefs"));
    unknownOptions.push_back(wxT("-Wzero-as-null-pointer-constant"));
    std::sort(unknownOptions.begin(), unknownOptions.end());
    std::vector<wxCharBuffer> argsBuffer;
    std::vector<const char*> args;
    while (tokenizer.HasMoreTokens())
    {
        const wxString& compilerSwitch = tokenizer.GetNextToken();
        if (std::binary_search(unknownOptions.begin(), unknownOptions.end(), compilerSwitch))
            continue;
        argsBuffer.push_back(compilerSwitch.ToUTF8());
        args.push_back(argsBuffer.back().data());
    }
    std::vector<ClTranslationUnit>::iterator it;
    int id = 0;
    ClTranslUnitId translId = -1;
    {
        ClTranslUnitId oldestTranslId = 0;
        wxDateTime ts = wxDateTime::Now();
        wxMutexLocker lock(m_Mutex);
        for ( it = m_TranslUnits.begin(); it != m_TranslUnits.end(); ++it, ++id)
        {
            if (it->IsEmpty())
            {
                translId = id;
                break;
            }
            if (it->GetLastParsed().GetTicks() < ts.GetTicks())
            {
                ts = it->GetLastParsed();
                oldestTranslId = id;
            }
        }
        if (it == m_TranslUnits.end())
        {
            if( m_TranslUnits.size() < 5 )
            {
                translId = m_TranslUnits.size();
            }
            else
            {
                translId = oldestTranslId;
                it = m_TranslUnits.begin();
            }
        }
    }
    ClTranslationUnit tu = ClTranslationUnit(translId, m_ClIndex[0]);
    ClFileId fileId = m_Database.GetFilenameId(filename);
    tu.Parse(filename, fileId, args, unsavedFiles);
    {
        wxMutexLocker lock(m_Mutex);
        if (it == m_TranslUnits.end())
        {
#if __cplusplus >= 201103L
            m_TranslUnits.push_back(std::move(tu));
#else
            m_TranslUnits.push_back(tu);
#endif
        }
        else
        {
            swap(m_TranslUnits[translId], tu);
        }
    }
    out_TranslId = translId;
}

/** @brief Removes a translation unit from memory.
 *
 * @param translUnitId ClTranslUnitId
 * @return void
 *
 * In reality it will just reset the translation unit to an empty state.
 */
void ClangProxy::RemoveTranslationUnit( const ClTranslUnitId translUnitId )
{
    if (translUnitId < 0)
    {
        return;
    }
    wxMutexLocker lock(m_Mutex);
    if (translUnitId >= (int)m_TranslUnits.size())
    {
        return;
    }
    // Replace with empty one
    ClTranslationUnit emptyTU(translUnitId, nullptr);
    swap( m_TranslUnits[translUnitId], emptyTU );
}

/** @brief Find a translation unit id from a file id. In case the file id is part of multiple translation units, it will search the one in the argument first.
 *
 * @param CtxTranslUnitId Translation Unit ID to search first, or wxID_ANY if you don't want this
 * @param fId The file ID to search
 * @return ClTranslUnitId The translation unit ID of the TU that contains this file or wxID_ANY if not found
 *
 */
ClTranslUnitId ClangProxy::GetTranslationUnitId( const ClTranslUnitId CtxTranslUnitId, ClFileId fId)
{
    wxMutexLocker locker(m_Mutex);

    // Prefer from current file
    if ((CtxTranslUnitId >= 0)&&(CtxTranslUnitId < (int)m_TranslUnits.size()))
    {
        if (m_TranslUnits[CtxTranslUnitId].Contains(fId))
        {
            return CtxTranslUnitId;
        }
    }
    // Is it an open file?
    for (size_t i = 0; i < m_TranslUnits.size(); ++i)
    {
        if (m_TranslUnits[i].GetFileId() == fId)
        {
            return i;
        }
    }
    // Search any include files
    for (size_t i = 0; i < m_TranslUnits.size(); ++i)
    {
        if (m_TranslUnits[i].Contains(fId))
        {
            return i;
        }
    }

    return wxNOT_FOUND;
}

/** @brief Find a translation unit id from a file id. In case the file id is part of multiple translation units, it will search the one in the argument first.
 *
 * @param CtxTranslUnitId Translation Unit ID to search first, or wxID_ANY if you don't want this
 * @param filename The filename to search
 * @return ClTranslUnitId The translation unit ID of the TU that contains this file or wxID_ANY if not found
 *
 */
ClTranslUnitId ClangProxy::GetTranslationUnitId( const ClTranslUnitId CtxTranslUnitId, const wxString& filename)
{
    return GetTranslationUnitId( CtxTranslUnitId, m_Database.GetFilenameId(filename));
}

/** @brief Perform codecompletion
 *
 * @param translUnitId The translation unit to use
 * @param filename In which file that is in the translation unit to where to do code completion
 * @param location The location of the start of the symbol to do code completion with. This is not the insertion point! (1-based)
 * @param isAuto (unused for now)
 * @param unsavedFiles The map of editor contents of all files that are open in the editor. Key is the filename
 * @param results[out] The returned results of codecompletion
 * @param diagnostics[out] The returned partial diagnostics results of codecompletion
 * @return
 *
 */
void ClangProxy::CodeCompleteAt( const ClTranslUnitId translUnitId, const wxString& filename,
                                 const ClTokenPosition& location, bool /*isAuto*/,
                                 const std::map<wxString, wxString>& unsavedFiles,
                                 std::vector<ClToken>& out_results,
                                 std::vector<ClDiagnostic>& out_diagnostics )
{
    if (translUnitId < 0)
    {
        return;
    }
    CCLogger::Get()->DebugLog( F(wxT("CodeCompleteAt %d,%d"), location.line, location.column));
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
    wxMutexLocker locker(m_Mutex);
    if (translUnitId >= (int)m_TranslUnits.size())
        return;
    CXCodeCompleteResults* clResults = m_TranslUnits[translUnitId].CodeCompleteAt(filename, location,
                                       clUnsavedFiles.empty() ? nullptr : &clUnsavedFiles[0],
                                       clUnsavedFiles.size());
    if (!clResults)
    {
        return;
    }

    //if (isAuto && clang_codeCompleteGetContexts(clResults) == CXCompletionContext_Unknown)
    //{
    //    fprintf(stdout,"%s unknown context\n", __PRETTY_FUNCTION__);
    //    return;
    //}

    const int numResults = clResults->NumResults;
    CCLogger::Get()->DebugLog( F(wxT("CodeCompleteAt results: %d"), numResults));

    out_results.reserve(numResults);
    for (int resIdx = 0; resIdx < numResults; ++resIdx)
    {
        const CXCompletionResult& token = clResults->Results[resIdx];
        if (CXAvailability_Available != clang_getCompletionAvailability(token.CompletionString))
            continue;
        const int numChunks = clang_getNumCompletionChunks(token.CompletionString);
        wxString type;
        for (int chunkIdx = 0; chunkIdx < numChunks; ++chunkIdx)
        {
            CXCompletionChunkKind kind = clang_getCompletionChunkKind(token.CompletionString, chunkIdx);
            if (kind == CXCompletionChunk_ResultType)
            {
                CXString str = clang_getCompletionChunkText(token.CompletionString, chunkIdx);
                type = wxT(": ") + wxString::FromUTF8(clang_getCString(str));
                wxString prefix;
                if (type.EndsWith(wxT(" *"), &prefix) || type.EndsWith(wxT(" &"), &prefix))
                    type = prefix + type.Last();
                clang_disposeString(str);
            }
            else if (kind == CXCompletionChunk_TypedText)
            {
                if (type.Length() > 40)
                {
                    type.Truncate(35);
                    if (wxIsspace(type.Last()))
                        type.Trim();
                    else if (wxIspunct(type.Last()))
                    {
                        for (int i = type.Length() - 2; i > 10; --i)
                        {
                            if (!wxIspunct(type[i]))
                            {
                                type.Truncate(i + 1);
                                break;
                            }
                        }
                    }
                    else if (wxIsalnum(type.Last()) || type.Last() == wxT('_'))
                    {
                        for (int i = type.Length() - 2; i > 10; --i)
                        {
                            if (!( wxIsalnum(type[i]) || type[i] == wxT('_') ))
                            {
                                type.Truncate(i + 1);
                                break;
                            }
                        }
                    }
                    type += wxT("...");
                }
                CXString completeTxt = clang_getCompletionChunkText(token.CompletionString, chunkIdx);
                out_results.push_back(ClToken(wxString::FromUTF8(clang_getCString(completeTxt)) + type,
                                          resIdx, clang_getCompletionPriority(token.CompletionString),
                                          ProxyHelper::GetTokenCategory(token.CursorKind)));
                clang_disposeString(completeTxt);
                type.Empty();
                break;
            }
        }
    }

    unsigned numDiag = clang_codeCompleteGetNumDiagnostics(clResults);
    unsigned int diagIdx = 0;
    for ( diagIdx=0; diagIdx < numDiag; ++diagIdx )
    {
        CXDiagnostic diag = clang_codeCompleteGetDiagnostic( clResults, diagIdx );
        m_TranslUnits[translUnitId].ExpandDiagnostic( diag, filename, out_diagnostics );
    }

    CCLogger::Get()->DebugLog( F(wxT("CodeCompleteAt done: %d elements"), (int)out_results.size()) );
}

/** @brief Document a Code Completion token
 *
 * @param translUnitId Translation unit where the token is located
 * @param tknId The TokenId
 * @return wxString The documentation or empty string when there is no documentation
 *
 */
wxString ClangProxy::DocumentCCToken( const ClTranslUnitId translUnitId, int tknId )
{
    if (translUnitId < 0)
    {
        return wxT("");
    }
    wxString doc;
    wxString descriptor;
    {
        wxMutexLocker  lock(m_Mutex);
        if (translUnitId >= (int)m_TranslUnits.size())
        {
            return wxT("");
        }
        const CXCompletionResult* token = m_TranslUnits[translUnitId].GetCCResult(tknId);
        if (!token)
            return wxEmptyString;
        CCLogger::Get()->DebugLog( F(_T("getdocumentation: ccresult cursor kind: %d"), (int)token->CursorKind));
        int upperBound = clang_getNumCompletionChunks(token->CompletionString);
        if (token->CursorKind == CXCursor_Namespace)
            doc = wxT("namespace ");
        for (int i = 0; i < upperBound; ++i)
        {

            CXCompletionChunkKind kind = clang_getCompletionChunkKind(token->CompletionString, i);
            if (kind == CXCompletionChunk_TypedText)
            {
                CXString str = clang_getCompletionParent(token->CompletionString, nullptr);
                wxString parent = wxString::FromUTF8(clang_getCString(str));
                if (!parent.IsEmpty())
                    doc += parent + wxT("::");
                clang_disposeString(str);
            }
            CXString str = clang_getCompletionChunkText(token->CompletionString, i);
            doc += wxString::FromUTF8(clang_getCString(str));
            if (kind == CXCompletionChunk_ResultType)
            {
                if (doc.Length() > 2 && doc[doc.Length() - 2] == wxT(' '))
                    doc.RemoveLast(2) += doc.Last();
                doc += wxT(' ');
            }
            clang_disposeString(str);
        }

        wxString identifier;
        unsigned tokenHash = HashToken(token->CompletionString, identifier);
        if (!identifier.IsEmpty())
        {
            ClTokenId tId = m_Database.GetTokenId(identifier, wxNOT_FOUND, ClTokenType_Unknown, tokenHash);
            if (tId != wxNOT_FOUND)
            {
                ClAbstractToken aTkn = m_Database.GetToken(tId);
                CXCursor clTkn = m_TranslUnits[translUnitId].GetTokenAt(m_Database.GetFilename(aTkn.fileId),
                                 aTkn.location);
                if (!clang_Cursor_isNull(clTkn) && !clang_isInvalid(clTkn.kind))
                {
                    CXComment docComment = clang_Cursor_getParsedComment(clTkn);
                    HTML_Writer::FormatDocumentation(docComment, descriptor, m_CppKeywords);
                    if (clTkn.kind == CXCursor_EnumConstantDecl)
                        doc += wxT("=") + ProxyHelper::GetEnumValStr(clTkn);
                    else if (clTkn.kind == CXCursor_TypedefDecl)
                    {
                        CXString str = clang_getTypeSpelling(clang_getTypedefDeclUnderlyingType(clTkn));
                        wxString type = wxString::FromUTF8(clang_getCString(str));
                        if (!type.IsEmpty())
                            doc.Prepend(wxT("typedef ") + type + wxT(" "));
                        clang_disposeString(str);
                    }
                }
            }
        }

        if (descriptor.IsEmpty())
        {
            CXString comment = clang_getCompletionBriefComment(token->CompletionString);
            descriptor = wxT("<p><font size=\"1\">")+HTML_Writer::Escape(wxString::FromUTF8(clang_getCString(comment)))+wxT("</font></p>");
            clang_disposeString(comment);
        }
    }
    ColourManager *colours = Manager::Get()->GetColourManager();
    wxString html = _T("<html><body bgcolor=\"");
    html += colours->GetColour(wxT("cc_docs_back")).GetAsString(wxC2S_HTML_SYNTAX) + _T("\" text=\"");
    html += colours->GetColour(wxT("cc_docs_fore")).GetAsString(wxC2S_HTML_SYNTAX) + _T("\" link=\"");
    html += colours->GetColour(wxT("cc_docs_link")).GetAsString(wxC2S_HTML_SYNTAX) + _T("\">");
    html += _T("<p><a name=\"top\"></a>");

    return html + wxT("<font size=\"2\"><code>") + HTML_Writer::SyntaxHl(doc, m_CppKeywords)
           + wxT("</code></font></p>") + descriptor + wxT("</body></html>");
}

/** @brief Get the Code Completion insert suffix. This is the operation after
 *  a user has chosen a certain code-completion string and returns the needed
 *  data to insert the string into the editor.
 *
 * @param translUnitId ClTranslUnitId
 * @param tknId int
 * @param newLine const wxString&
 * @param std::vector<std::pair<int
 * @param offsetsList int> >&
 * @return wxString
 *
 */
wxString ClangProxy::GetCCInsertSuffix( const ClTranslUnitId translUnitId, int tknId, const wxString& newLine, std::vector<std::pair<int, int> >& offsetsList )
{
    CCLogger::Get()->DebugLog( F(wxT("GetCCInsertSuffix TU Id=%d tknId=%d"), translUnitId, tknId) );
    if (translUnitId < 0)
    {
        return wxT("");
    }
    wxMutexLocker lock(m_Mutex);
    if (translUnitId >= (int)m_TranslUnits.size())
    {
        return wxT("");
    }
    const CXCompletionResult* token = m_TranslUnits[translUnitId].GetCCResult(tknId);
    if (!token)
        return wxEmptyString;

    const CXCompletionString& clCompStr = token->CompletionString;
    int upperBound = clang_getNumCompletionChunks(clCompStr);
    enum BuilderState { init, store, exit };
    BuilderState state = init;
    wxString suffix;
    for (int i = 0; i < upperBound; ++i)
    {
        switch (clang_getCompletionChunkKind(clCompStr, i))
        {
        case CXCompletionChunk_TypedText:
            if (state == init)
                state = store;
            break;

        case CXCompletionChunk_Placeholder:
            if (state == store)
            {
                CXString str = clang_getCompletionChunkText(clCompStr, i);
                const wxString& param = wxT("/*! ") + wxString::FromUTF8(clang_getCString(str)) + wxT(" !*/");
                offsetsList.push_back(std::make_pair(suffix.Length(), suffix.Length() + param.Length()));
                suffix += param;
                clang_disposeString(str);
                //state = exit;
            }
            break;
        case CXCompletionChunk_LeftParen:
            if (state == store)
            {
                suffix += wxT("( ");
            }
            break;
        case CXCompletionChunk_RightParen:
            if (state == store)
            {
                if ( offsetsList.size() == 0 )
                {
                    // When no arguments, we don't want spaces between (). Ideally we should get these rules from AStyle somehow
                    suffix = suffix.Left( suffix.Length() - 1 );
                    suffix += wxT(")");
                }
                else
                    suffix += wxT(" )");
                state = exit;
            }
            break;
        case CXCompletionChunk_LeftAngle:
            if (state == store)
            {
                suffix += wxT("< ");
            }
            break;
        case CXCompletionChunk_RightAngle:
            if (state == store)
            {
                suffix += wxT(" >");
            }
            break;
        case CXCompletionChunk_Comma:
            if (state == store)
            {
                suffix += wxT(", ");
            }
            break;
        case CXCompletionChunk_Informative:
        {
            CXString str = clang_getCompletionChunkText(clCompStr, i);
            clang_disposeString(str);
        }
        break;

        case CXCompletionChunk_VerticalSpace:
            if (state != init)
                suffix += newLine;
            break;

        default:
            if (state != init)
            {
                CXString str = clang_getCompletionChunkText(clCompStr, i);
                suffix += wxString::FromUTF8(clang_getCString(str));
                clang_disposeString(str);
            }
            break;
        }
    }
    //if (state != exit)
    //    offsets = std::make_pair(suffix.Length(), suffix.Length());
    return suffix;
}

/** @brief Return the token category of a specific token ID
 *
 * @param translUnitId ClTranslUnitId
 * @param tknId int
 * @param tknType ClTokenCategory&
 * @return void
 *
 */
void ClangProxy::RefineTokenType( const ClTranslUnitId translUnitId, int tknId, ClTokenCategory& tknType)
{
    if (translUnitId < 0)
    {
        return;
    }
    wxMutexLocker lock(m_Mutex);
    if (translUnitId >= (int)m_TranslUnits.size())
    {
        return;
    }
    const CXCompletionResult* token = m_TranslUnits[translUnitId].GetCCResult(tknId);
    if (!token)
        return;
    wxString identifier;
    unsigned tokenHash = HashToken(token->CompletionString, identifier);
    if (!identifier.IsEmpty())
    {
        ClTokenId tId = m_Database.GetTokenId(identifier, wxNOT_FOUND, ClTokenType_Unknown, tokenHash);
        if (tId != wxNOT_FOUND)
        {
            const ClAbstractToken& aTkn = m_Database.GetToken(tId);
            CXCursor clTkn = m_TranslUnits[translUnitId].GetTokenAt(m_Database.GetFilename(aTkn.fileId),
                             aTkn.location);
            if (!clang_Cursor_isNull(clTkn) && !clang_isInvalid(clTkn.kind))
            {
                ClTokenCategory tkCat
                    = ProxyHelper::GetTokenCategory(token->CursorKind, clang_getCXXAccessSpecifier(clTkn));
                if (tkCat != tcNone)
                    tknType = tkCat;
            }
        }
    }
}

/** @brief Get call tips in a translation unit + file on a specific location
 *
 * @param translUnitId The translation unit id
 * @param filename The filename
 * @param location The location of the token
 * @param tokenStr The string of the token
 * @param results[out] The list of calltips text
 * @return
 *
 */
void ClangProxy::GetCallTipsAt( const ClTranslUnitId translUnitId, const wxString& filename,
                                const ClTokenPosition& location, const wxString& tokenStr,
                                std::vector<wxStringVec>& out_results )
{
    if (translUnitId < 0)
    {
        return;
    }
    wxMutexLocker lock(m_Mutex);
    if (translUnitId >= (int)m_TranslUnits.size())
    {
        return;
    }
    std::vector<CXCursor> tokenSet;
    ClTokenPosition loc = location;
    if (loc.column > static_cast<unsigned int>(tokenStr.Length()))
    {
        loc.column -= tokenStr.Length() / 2;
        CXCursor token = m_TranslUnits[translUnitId].GetTokenAt(filename, loc);
        if (!clang_Cursor_isNull(token))
        {
            CXCursor resolve = clang_getCursorDefinition(token);
            if (clang_Cursor_isNull(resolve) || clang_isInvalid(token.kind))
            {
                resolve = clang_getCursorReferenced(token);
                if (!clang_Cursor_isNull(resolve) && !clang_isInvalid(token.kind))
                    token = resolve;
            }
            else
                token = resolve;
            tokenSet.push_back(token);
        }
    }
    // TODO: searching the database is very inexact, but necessary, as clang
    // does not resolve the token when the code is invalid (incomplete)
    std::vector<ClTokenId> tknIds = m_Database.GetTokenMatches(tokenStr);
    for (std::vector<ClTokenId>::const_iterator itr = tknIds.begin(); itr != tknIds.end(); ++itr)
    {
        const ClAbstractToken& aTkn = m_Database.GetToken(*itr);
        CXCursor token = m_TranslUnits[translUnitId].GetTokenAt(m_Database.GetFilename(aTkn.fileId),
                         aTkn.location);
        if (!clang_Cursor_isNull(token) && !clang_isInvalid(token.kind))
            tokenSet.push_back(token);
    }
    std::set<wxString> uniqueTips;
    for (size_t tknIdx = 0; tknIdx < tokenSet.size(); ++tknIdx)
    {
        CXCursor token = tokenSet[tknIdx];
        switch (ProxyHelper::GetTokenCategory(token.kind, CX_CXXPublic))
        {
        case tcVarPublic:
        {
            token = clang_getTypeDeclaration(clang_getCursorResultType(token));
            if (!clang_Cursor_isNull(token) && !clang_isInvalid(token.kind))
                tokenSet.push_back(token);
            break;
        }

        case tcTypedefPublic:
        {
            token = clang_getTypeDeclaration(clang_getTypedefDeclUnderlyingType(token));
            if (!clang_Cursor_isNull(token) && !clang_isInvalid(token.kind))
                tokenSet.push_back(token);
            break;
        }

        case tcClassPublic:
        {
            // search for constructors and 'operator()'
            clang_visitChildren(token, &ProxyHelper::ClCallTipCtorAST_Visitor, &tokenSet);
            break;
        }

        case tcCtorPublic:
        {
            if (clang_getCXXAccessSpecifier(token) == CX_CXXPrivate)
                break;
            // fall through
        }
        case tcFuncPublic:
        {
            const CXCompletionString& clCompStr = clang_getCursorCompletionString(token);
            wxStringVec entry;
            int upperBound = clang_getNumCompletionChunks(clCompStr);
            entry.push_back(wxEmptyString);
            for (int chunkIdx = 0; chunkIdx < upperBound; ++chunkIdx)
            {
                CXCompletionChunkKind kind = clang_getCompletionChunkKind(clCompStr, chunkIdx);
                if (kind == CXCompletionChunk_TypedText)
                {
                    CXString str = clang_getCompletionParent(clCompStr, nullptr);
                    wxString parent = wxString::FromUTF8(clang_getCString(str));
                    if (!parent.IsEmpty())
                        entry[0] += parent + wxT("::");
                    clang_disposeString(str);
                }
                else if (kind == CXCompletionChunk_LeftParen)
                {
                    if (entry[0].IsEmpty() || !entry[0].EndsWith(wxT("operator")))
                        break;
                }
                CXString str = clang_getCompletionChunkText(clCompStr, chunkIdx);
                entry[0] += wxString::FromUTF8(clang_getCString(str));
                if (kind == CXCompletionChunk_ResultType)
                {
                    if (entry[0].Length() > 2 && entry[0][entry[0].Length() - 2] == wxT(' '))
                        entry[0].RemoveLast(2) += entry[0].Last();
                    entry[0] += wxT(' ');
                }
                clang_disposeString(str);
            }
            entry[0] += wxT('(');
            int numArgs = clang_Cursor_getNumArguments(token);
            for (int argIdx = 0; argIdx < numArgs; ++argIdx)
            {
                CXCursor arg = clang_Cursor_getArgument(token, argIdx);

                wxString tknStr;
                const CXCompletionString& argStr = clang_getCursorCompletionString(arg);
                upperBound = clang_getNumCompletionChunks(argStr);
                for (int chunkIdx = 0; chunkIdx < upperBound; ++chunkIdx)
                {
                    CXCompletionChunkKind kind = clang_getCompletionChunkKind(argStr, chunkIdx);
                    if (kind == CXCompletionChunk_TypedText)
                    {
                        CXString str = clang_getCompletionParent(argStr, nullptr);
                        wxString parent = wxString::FromUTF8(clang_getCString(str));
                        if (!parent.IsEmpty())
                            tknStr += parent + wxT("::");
                        clang_disposeString(str);
                    }
                    CXString str = clang_getCompletionChunkText(argStr, chunkIdx);
                    tknStr += wxString::FromUTF8(clang_getCString(str));
                    if (kind == CXCompletionChunk_ResultType)
                    {
                        if (tknStr.Length() > 2 && tknStr[tknStr.Length() - 2] == wxT(' '))
                            tknStr.RemoveLast(2) += tknStr.Last();
                        tknStr += wxT(' ');
                    }
                    clang_disposeString(str);
                }

                entry.push_back(tknStr.Trim());
            }
            entry.push_back(wxT(')'));
            wxString composit;
            for (wxStringVec::const_iterator itr = entry.begin();
                    itr != entry.end(); ++itr)
            {
                composit += *itr;
            }
            if (uniqueTips.find(composit) != uniqueTips.end())
                break;
            uniqueTips.insert(composit);
            out_results.push_back(entry);
            break;
        }

        default:
            break;
        }
    }
}

/** @brief Get a list of tokens at a specific location
 *
 * @param translUnitId The translation unit ID
 * @param filename Filename
 * @param location The location where you want the list of tokens
 * @param results[out] StringVec that contains the results
 * @return
 *
 */
void ClangProxy::GetTokensAt( const ClTranslUnitId translUnitId, const wxString& filename, const ClTokenPosition& location,
                              wxStringVec& out_results )
{
    if (translUnitId < 0)
    {
        return;
    }
    wxMutexLocker lock(m_Mutex);
    if (translUnitId >= (int)m_TranslUnits.size())
    {
        return;
    }
    CXCursor token = m_TranslUnits[translUnitId].GetTokenAt(filename, location);
    if (clang_Cursor_isNull(token))
        return;
    ProxyHelper::ResolveCursorDecl(token);

    wxString tknStr;
    const CXCompletionString& clCompStr = clang_getCursorCompletionString(token);
    int upperBound = clang_getNumCompletionChunks(clCompStr);
    for (int i = 0; i < upperBound; ++i)
    {
        CXCompletionChunkKind kind = clang_getCompletionChunkKind(clCompStr, i);
        if (kind == CXCompletionChunk_TypedText)
        {
            CXString str = clang_getCompletionParent(clCompStr, nullptr);
            wxString parent = wxString::FromUTF8(clang_getCString(str));
            if (!parent.IsEmpty())
                tknStr += parent + wxT("::");
            clang_disposeString(str);
        }
        CXString str = clang_getCompletionChunkText(clCompStr, i);
        tknStr += wxString::FromUTF8(clang_getCString(str));
        if (kind == CXCompletionChunk_ResultType)
        {
            if (tknStr.Length() > 2 && tknStr[tknStr.Length() - 2] == wxT(' '))
                tknStr.RemoveLast(2) += tknStr.Last();
            tknStr += wxT(' ');
        }
        clang_disposeString(str);
    }
    if (!tknStr.IsEmpty())
    {
        switch (token.kind)
        {
        case CXCursor_StructDecl:
        case CXCursor_ClassDecl:
        case CXCursor_ClassTemplate:
        case CXCursor_ClassTemplatePartialSpecialization:
        {
            if (token.kind == CXCursor_StructDecl)
                tknStr.Prepend(wxT("struct "));
            else
                tknStr.Prepend(wxT("class "));
            wxStringVec directAncestors;
            clang_visitChildren(token, &ProxyHelper::ClInheritance_Visitor, &directAncestors);
            for (wxStringVec::const_iterator daItr = directAncestors.begin();
                    daItr != directAncestors.end(); ++daItr)
            {
                if (daItr == directAncestors.begin())
                    tknStr += wxT(" : ");
                else
                    tknStr += wxT(", ");
                tknStr += *daItr;
            }
            break;
        }

        case CXCursor_UnionDecl:
            tknStr.Prepend(wxT("union "));
            break;

        case CXCursor_EnumDecl:
            tknStr.Prepend(wxT("enum "));
            break;

        case CXCursor_EnumConstantDecl:
            tknStr += wxT("=") + ProxyHelper::GetEnumValStr(token);
            break;

        case CXCursor_TypedefDecl:
        {
            CXString str = clang_getTypeSpelling(clang_getTypedefDeclUnderlyingType(token));
            wxString type = wxString::FromUTF8(clang_getCString(str));
            clang_disposeString(str);
            if (!type.IsEmpty())
                tknStr.Prepend(wxT("typedef ") + type + wxT(" "));
            break;
        }

        case CXCursor_Namespace:
            tknStr.Prepend(wxT("namespace "));
            break;

        case CXCursor_MacroDefinition:
            tknStr.Prepend(wxT("#define ")); // TODO: show (partial) definition
            break;

        default:
            break;
        }
        out_results.push_back(tknStr);
    }
}

/** @brief Get all occurences of the token under the supplied cursor.
 *
 * @param translUnitId The translation unit id
 * @param filename The filename
 * @param location The location in the file
 * @param results[out] The returned results
 *
 */
void ClangProxy::GetOccurrencesOf( const ClTranslUnitId translUnitId, const wxString& filename, const ClTokenPosition& location,
                                  std::vector< std::pair<int, int> >& out_results )
{
    if (translUnitId < 0)
    {
        return;
    }
    wxMutexLocker lock(m_Mutex);
    if (translUnitId >= (int)m_TranslUnits.size())
    {
        return;
    }
    CXCursor token = m_TranslUnits[translUnitId].GetTokenAt(filename, location);
    if (clang_Cursor_isNull(token))
        return;
    ProxyHelper::ResolveCursorDecl(token);
    CXCursorAndRangeVisitor visitor = {&out_results, ProxyHelper::ReferencesVisitor};
    clang_findReferencesInFile(token, m_TranslUnits[translUnitId].GetFileHandle(filename), visitor);
}

/** @brief Resolve a token declaration.
 *
 * @param translUnitId const ClTranslUnitId
 * @param filename wxString&
 * @param location[in,out] The position of the token
 * @return true if the token declaration could be found, false otherwise
 *
 */
bool ClangProxy::ResolveDeclTokenAt( const ClTranslUnitId translUnitId, wxString& filename, const ClTokenPosition& location, ClTokenPosition& out_location)
{
    if (translUnitId < 0)
    {
        return false;
    }
    wxMutexLocker lock(m_Mutex);
    if (translUnitId >= (int)m_TranslUnits.size())
    {
        return false;
    }
    CXCursor token = clang_getNullCursor();
    out_location = location;
    token = m_TranslUnits[translUnitId].GetTokenAt(filename, out_location);
    if (clang_Cursor_isNull(token))
    {
        return false;
    }
    ProxyHelper::ResolveCursorDecl(token);
    CXFile file;
    if (token.kind == CXCursor_InclusionDirective)
    {
        file = clang_getIncludedFile(token);
        out_location.line = 1;
        out_location.column = 1;
    }
    else
    {
        CXSourceLocation loc = clang_getCursorLocation(token);
        unsigned ln, col;
        clang_getSpellingLocation(loc, &file, &ln, &col, nullptr);
        out_location.line   = ln;
        out_location.column = col;
    }
    CXString str = clang_getFileName(file);
    filename = wxString::FromUTF8(clang_getCString(str));
    clang_disposeString(str);
    return true;
}

/** @brief Resolve the definition of a token.
 *
 * @param translUnitId const ClTranslUnitId
 * @param inout_filename wxString&
 * @param inout_location ClTokenPosition&
 * @return bool
 *
 * The definition is where a token is actually implemented.
 */
bool ClangProxy::ResolveDefinitionTokenAt( const ClTranslUnitId translUnitId, wxString& inout_filename, const ClTokenPosition& location, ClTokenPosition& out_location)
{
    if (translUnitId < 0 )
        return false;
    wxMutexLocker lock(m_Mutex);
    if (translUnitId >= (int)m_TranslUnits.size() )
        return false;
    CXCursor token = clang_getNullCursor();
    out_location = location;
    token = m_TranslUnits[translUnitId].GetTokenAt(inout_filename, out_location);
    if ( !ProxyHelper::ResolveCursorDefinition(token) )
    {
        std::set<ClTranslUnitId> translIdList;
        std::vector< ClTokenId > tokenList;
        wxString tokenName;

        translIdList.insert( translUnitId );

        CXString str;
        str = clang_getCursorDisplayName(token);
        tokenName = wxString::FromUTF8(clang_getCString(str));
        clang_disposeString(str);
        tokenList = m_Database.GetTokenMatches( tokenName );
        for (std::vector<ClTokenId>::const_iterator tokenIt = tokenList.begin(); tokenIt != tokenList.end(); ++tokenIt)
        {
            ClAbstractToken tok = m_Database.GetToken( *tokenIt );
            for ( std::vector<ClTranslationUnit>::iterator it = m_TranslUnits.begin(); it != m_TranslUnits.end(); ++it )
            {
                if ( it->GetFileId() == tok.fileId ) // TODO: should also check children, if the definition is in a header-file that doesn't have its own TU
                {
                    if ( translIdList.find( it->GetId() ) == translIdList.end())
                    {
                        translIdList.insert( it->GetId() );
                        ClTokenPosition loc = tok.location;
                        token = it->GetTokenAt(m_Database.GetFilename(tok.fileId), loc);
                        if (ProxyHelper::ResolveCursorDefinition( token ))
                        {
                            break;
                        }
                        else
                        {
                            token = clang_getNullCursor();
                        }
                    }
                }
            }
        }
    }
    if (clang_Cursor_isNull(token))
    {
        return false;
    }
    CXFile file;
    if (token.kind == CXCursor_InclusionDirective)
    {
        file = clang_getIncludedFile(token);
        out_location.line = 1;
        out_location.column = 1;
    }
    else
    {
        CXSourceLocation loc = clang_getCursorLocation(token);
        unsigned ln, col;
        clang_getSpellingLocation(loc, &file, &ln, &col, nullptr);
        out_location.line   = ln;
        out_location.column = col;
    }
    CXString str = clang_getFileName(file);
    inout_filename = wxString::FromUTF8(clang_getCString(str));
    clang_disposeString(str);
    return true;
}

/** @brief Get the function scope of a location. When the position is in a body of a function, this will return the class and function name.
 *
 * @param translUnitId const ClTranslUnitId
 * @param filename const wxString&
 * @param location const ClTokenPosition&
 * @param out_ClassName wxString&
 * @param out_MethodName wxString&
 * @return void
 *
 */
void ClangProxy::GetFunctionScopeAt( const ClTranslUnitId translUnitId, const wxString& filename, const ClTokenPosition& location, wxString &out_ScopeName, wxString &out_MethodName )
{
    ClFileId fileId = m_Database.GetFilenameId(filename);
    if (translUnitId < 0)
    {
        out_ScopeName = wxT("");
        out_MethodName = wxT("");
        return;
    }
    ClFunctionScopeList functionScopes;
    {
        wxMutexLocker lock(m_Mutex);
        if (translUnitId >= (int)m_TranslUnits.size())
        {
            out_ScopeName = wxT("");
            out_MethodName = wxT("");
            return;
        }
        m_TranslUnits[translUnitId].GetFunctionScopes( fileId, functionScopes );
    }
    ClFunctionScopeList::const_iterator candidate = functionScopes.end();
    for (ClFunctionScopeList::const_iterator it = functionScopes.begin(); it != functionScopes.end(); ++it)
    {
        if (it->startLocation.line <= location.line)
        {
            candidate = it;
        }
        else if (candidate != functionScopes.end())
        {
            break;
        }
    }
    if (candidate != functionScopes.end())
    {
        out_ScopeName = candidate->scopeName;
        out_MethodName = candidate->functionName;
        return;
    }
    out_ScopeName = wxT("");
    out_MethodName = wxT("");
}


/** \brief Get the location of a function scope
 *
 * \param id First translation unit to find the function scope in
 * \param fId File id where the function scope is defined
 * \param scopeName Name of the scope where the function is defined in
 * \param functionName Name of the function
 * \param out_Location[out] The returned position where the function is declared. Will be (0,0) when the function declaration is not found.
 * \return void
 *
 */
void ClangProxy::GetFunctionScopeLocation( const ClTranslUnitId translUnitId, const wxString& filename, const wxString& scopeName, const wxString& functionName, ClTokenPosition& out_Location)
{
    CCLogger::Get()->DebugLog(F(_T("GetFunctionScopeLocation %d"), translUnitId));
    ClFileId fId = m_Database.GetFilenameId( filename );
    if (translUnitId < 0 )
    {
        out_Location = ClTokenPosition(0,0);
        return;
    }
    ClFunctionScopeList functionScopes;
    {
        wxMutexLocker lock(m_Mutex);
        if (translUnitId >= (int)m_TranslUnits.size())
        {
            out_Location = ClTokenPosition(0,0);
            return;
        }
        m_TranslUnits[translUnitId].GetFunctionScopes(fId, functionScopes);
    }
    for (ClFunctionScopeList::const_iterator it = functionScopes.begin(); it != functionScopes.end(); ++it )
    {
        if( (it->functionName == functionName)&&(it->scopeName == scopeName))
        {
            out_Location = it->startLocation;
            return;
        }
    }
    out_Location = ClTokenPosition(0,0);
}

void ClangProxy::GetFunctionScopes( const ClTranslUnitId translUnitId, const wxString& filename, std::vector<std::pair<wxString, wxString> >& out_Scopes  )
{
    if (translUnitId < 0 )
    {
        return;
    }
    ClFileId fId = m_Database.GetFilenameId( filename );
    ClFunctionScopeList functionScopes;
    {
        wxMutexLocker lock(m_Mutex);
        if (translUnitId >= (int)m_TranslUnits.size())
        {
            return;
        }
        m_TranslUnits[translUnitId].GetFunctionScopes(fId,functionScopes);
    }
    for (ClFunctionScopeList::const_iterator it = functionScopes.begin(); it != functionScopes.end(); ++it )
    {
        out_Scopes.push_back( std::make_pair(it->scopeName, it->functionName) );
    }
}

/** @brief Reparse a translation unit
 *
 * @param translUnitId ID of the translation unit to reparse
 * @param compileCommand The compile command to be passed to Clang
 * @param unsavedFiles Map of the contents of all unsaved files open in the IDE
 * @return void
 *
 */
void ClangProxy::Reparse( const ClTranslUnitId translUnitId, const wxString& /*compileCommand*/, const std::map<wxString, wxString>& unsavedFiles )
{
    if (translUnitId < 0 )
        return;
    ClTranslationUnit tu(translUnitId);
    {
        wxMutexLocker lock(m_Mutex);
        if (translUnitId >= (int)m_TranslUnits.size())
            return;
        swap(m_TranslUnits[translUnitId], tu);
    }
    if ( tu.IsValid() )
    {
        tu.Reparse(unsavedFiles);
    }
    {
        wxMutexLocker lock(m_Mutex);
        swap(m_TranslUnits[translUnitId], tu);
    }
}

/** @brief Update the token database with all tokens from the Clang AST in a Translation Unit
 *
 * @param translUnitId The ID of the translation unit
 * @return void
 *
 */
void ClangProxy::UpdateTokenDatabase( const ClTranslUnitId translUnitId )
{
    if (translUnitId < 0 )
        return;
    ClTranslationUnit tu(translUnitId);
    {
        wxMutexLocker lock(m_Mutex);
        if (translUnitId >= (int)m_TranslUnits.size())
            return;
        swap(m_TranslUnits[translUnitId], tu);
    }

    if ( tu.IsValid() )
    {
        std::vector<ClFileId> includeFiles;
        ClFunctionScopeMap functionScopes;
        tu.ProcessAllTokens( m_Database, includeFiles, functionScopes );
        tu.SetFiles(includeFiles);
        for (ClFunctionScopeMap::const_iterator it = functionScopes.begin(); it != functionScopes.end(); ++it)
            tu.UpdateFunctionScopes(it->first, it->second);
        CCLogger::Get()->DebugLog( F(wxT("Total token count: %d, function scopes for TU %d: %d, files: %d"), (int)m_Database.GetTokenCount(), (int)translUnitId, (int)functionScopes.size(), (int)includeFiles.size() ) );
    } else {
        CCLogger::Get()->DebugLog( F(_T("UpdateTokenDatabase: Translation unit is not valid!")) );
    }
    {
        wxMutexLocker lock(m_Mutex);
        swap(m_TranslUnits[translUnitId], tu);
    }
}

/** @brief Get the diagnostics of a file within a translation unit.
 *
 * @param translUnitId Translation unit ID
 * @param filename Filename within the translation unit
 * @param diagnostics[out] Returned diagnostics
 * @return void
 *
 */
void ClangProxy::GetDiagnostics( const ClTranslUnitId translUnitId, const wxString& filename, std::vector<ClDiagnostic>& out_diagnostics)
{
    if (translUnitId < 0)
    {
        return;
    }
    wxMutexLocker lock(m_Mutex);
    if (translUnitId >= (int)m_TranslUnits.size())
    {
        return;
    }
    m_TranslUnits[translUnitId].GetDiagnostics(filename, out_diagnostics);
}

/** @brief Append a job to the clang job queue
 *
 * @param job The job to append
 * @return void
 *
 * This will make a clone of the job and push it to the end of the job queue.
 */
void ClangProxy::AppendPendingJob( ClangProxy::ClangJob& job )
{
    if (!m_pThread)
    {
        return;
    }
    ClangProxy::ClangJob* pJob = job.Clone();
    pJob->SetProxy(this);
    m_pThread->Queue(pJob);

    return;
}


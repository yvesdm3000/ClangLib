#ifndef __CLANGPLUGINAPI_H
#define __CLANGPLUGINAPI_H

#include <cbplugin.h>
#include <cbproject.h>

#define CLANG_CONFIGMANAGER _T("ClangLib")

typedef int8_t ClTranslUnitId;
typedef int ClTokenId;

/** @brief Category of a token to identify what a token represents
 */
enum ClTokenCategory
{
    tcClassFolder,
    tcClass,            tcClassPrivate,
    tcClassProtected,   tcClassPublic,
    tcCtorPrivate,      tcCtorProtected,
    tcCtorPublic,
    tcDtorPrivate,      tcDtorProtected,
    tcDtorPublic,
    tcFuncPrivate,      tcFuncProtected,
    tcFuncPublic,
    tcVarPrivate,       tcVarProtected,
    tcVarPublic,
    tcMacroDef,
    tcEnum,             tcEnumPrivate,
    tcEnumProtected,    tcEnumPublic,
    tcEnumerator,
    tcNamespace,
    tcTypedef,          tcTypedefPrivate,
    tcTypedefProtected, tcTypedefPublic,
    tcSymbolsFolder,
    tcVarsFolder,
    tcFuncsFolder,
    tcEnumsFolder,
    tcPreprocFolder,
    tcOthersFolder,
    tcTypedefFolder,
    tcMacroUse,         tcMacroPrivate,
    tcMacroProtected,   tcMacroPublic,
    tcMacroFolder,
    tcLangKeyword, // added
    tcNone = -1
};

/** @brief Token structure. Contains a token for code-completion.
 */
struct ClToken
{
    ClToken(const wxString& nm, int _id, int _weight, ClTokenCategory categ) :
        id(_id), category(categ), weight(_weight), name(nm) {}

    int id;
    ClTokenCategory category;
    int weight;
    wxString name;
};

/** @brief TokenPosition structure. Contains line and column information. First line is '1'
 */
struct ClTokenPosition
{
    ClTokenPosition(unsigned int ln, unsigned int col)
    {
        line = ln;
        column = col;
    }
    bool operator==(const ClTokenPosition other) const
    {
        return ((line == other.line)&&(column == other.column));
    }
    bool operator!=(const ClTokenPosition other) const
    {
        return !(*this == other);
    }
    bool operator<(const ClTokenPosition other) const
    {
        if (line < other.line)
            return true;
        if (line > other.line)
            return false;
        if (column < other.column)
            return true;
        return false;
    }
    bool operator>(const ClTokenPosition other) const
    {
        if (line > other.line)
            return true;
        if (line < other.line)
            return false;
        if (column > other.column)
            return true;
        return false;
    }
    unsigned int line;
    unsigned int column;
};

struct ClTokenRange
{
    ClTokenRange() : beginLocation(0,0), endLocation(0,0) {}
    ClTokenRange(const ClTokenPosition beginPos, const ClTokenPosition endPos) : beginLocation(beginPos), endLocation(endPos) {}

    bool InRange(const ClTokenPosition pos) const
    {
        if (pos < beginLocation)
            return false;
        if (pos > endLocation)
            return false;
        return true;
    }

    ClTokenPosition beginLocation;
    ClTokenPosition endLocation;
};

struct ClTokenScope
{
    wxString tokenName;
    wxString scopeName;
    ClTokenRange range;
    ClTokenScope() : tokenName(), scopeName(), range() {}
    ClTokenScope(const wxString& tokName, const wxString& symanticScopeName, ClTokenRange scopeRange) : tokenName(tokName), scopeName(symanticScopeName), range(scopeRange){}

    bool operator==(const ClTokenScope& other) const
    {
        if (other.tokenName != tokenName)
            return false;
        if (other.scopeName != scopeName)
            return false;
        if (other.range.beginLocation != range.beginLocation)
            return false;
        if (other.range.endLocation != range.endLocation)
            return false;
        return true;
    }
};

/** @brief Level of diagnostic
 */
enum ClDiagnosticLevel { dlPartial, dlFull };

enum ClDiagnosticSeverity { sWarning, sError, sNote };

struct ClDiagnosticFixit
{
    ClDiagnosticFixit(const wxString& txt, const unsigned rgStart, const unsigned rgEnd, const wxString& srcLn) :
        text(txt), range(rgStart,rgEnd), srcLine(srcLn){}

    wxString text;                      ///< Text to insert to Fix It
    std::pair<unsigned,unsigned> range; ///< Range where the fixit applies
    wxString srcLine;                   ///< Copy of the source line of the fixit.
};

struct ClDiagnostic
{
    ClDiagnostic(const int ln, const unsigned rgStart, const unsigned rgEnd, const ClDiagnosticSeverity level, const wxString& fl, const wxString& msg, const std::vector<ClDiagnosticFixit> fixitL) :
        line(ln), range(rgStart, rgEnd), severity(level), file(fl), message(msg), fixitList(fixitL) {}

    int line;
    std::pair<unsigned, unsigned> range;
    ClDiagnosticSeverity severity;
    wxString file;
    wxString message;
    std::vector<ClDiagnosticFixit> fixitList;
};

typedef enum _TokenType
{
    ClTokenType_Unknown = 0,

    ClTokenType_DeclGroup = 1<<8,   // Token declaration
    ClTokenType_DefGroup  = 1<<9,   // Token definition
    ClTokenType_RefGroup  = 1<<10,  // Token reference (function call, type of a variable declaration etc

    ClTokenType_Func    = 1<<0,
    ClTokenType_Var     = 1<<1,
    ClTokenType_Parm    = 1<<2,
    ClTokenType_Scope   = 1<<3,
    ClTokenType_Typedef = 1<<4,
    ClTokenType_Value   = 1<<5,

    ClTokenType_FuncDecl    = ClTokenType_Func    | ClTokenType_DeclGroup,
    ClTokenType_VarDecl     = ClTokenType_Var     | ClTokenType_DeclGroup,
    ClTokenType_ParmDecl    = ClTokenType_Parm    | ClTokenType_DeclGroup,
    ClTokenType_ScopeDecl   = ClTokenType_Scope   | ClTokenType_DeclGroup,
    ClTokenType_TypedefDecl = ClTokenType_Typedef | ClTokenType_DeclGroup,
    ClTokenType_ValueDecl   = ClTokenType_Value   | ClTokenType_DeclGroup,

    ClTokenType_FuncDef   = ClTokenType_Func  | ClTokenType_DefGroup,
    ClTokenType_VarDef    = ClTokenType_Var   | ClTokenType_DefGroup,
    ClTokenType_ParmDef   = ClTokenType_Parm  | ClTokenType_DefGroup,
    ClTokenType_ScopeDef  = ClTokenType_Scope | ClTokenType_DefGroup,

    ClTokenType_FuncRef   = ClTokenType_Func  | ClTokenType_RefGroup,
    ClTokenType_VarRef    = ClTokenType_Var   | ClTokenType_RefGroup,
    ClTokenType_ParmRef   = ClTokenType_Parm  | ClTokenType_RefGroup,
    ClTokenType_ScopeRef  = ClTokenType_Scope | ClTokenType_RefGroup,

} ClTokenType;

typedef enum _ClCodeCompleteOption
{
    ClCodeCompleteOption_None                 = 0,
    ClCodeCompleteOption_IncludeCTors         = 1<<0,
    ClCodeCompleteOption_IncludeCodePatterns  = 1<<1,
    ClCodeCompleteOption_IncludeBriefComments = 1<<2,
    ClCodeCompleteOption_IncludeMacros        = 1<<3
} ClCodeCompleteOption;

class ClangFile
{
    wxString m_Project;
    wxString m_Filename;
public:
    ClangFile(const wxString& filename) :
        m_Project(wxT("")),
        m_Filename(filename){}
    ClangFile(const cbProject* pProject, const wxString& filename) :
        m_Project( pProject ? pProject->GetFilename() : wxT("") ),
        m_Filename(filename){}
    ClangFile(ProjectFile& pf):
        m_Project( pf.GetParentProject() ? pf.GetParentProject()->GetFilename() : wxT("")),
        m_Filename(pf.file.GetFullPath()){}
    ClangFile(ProjectFile* pPf, const wxString& fallbackFilename)
    {
        if (pPf)
        {
            m_Project = pPf->GetParentProject() ? pPf->GetParentProject()->GetFilename() : wxT("");
            m_Filename = pPf->file.GetFullPath();
        }
        else
        {
            m_Project = wxT("");
            m_Filename = fallbackFilename;
        }
    }

    ClangFile(const ClangFile& project, const wxString& filename) :
        m_Project(project.m_Project.c_str()),
        m_Filename(filename.c_str()){}

    ClangFile(const ClangFile& Other) :
        m_Project(Other.m_Project.c_str()),
        m_Filename(Other.m_Filename.c_str()){}

    friend bool operator<(const ClangFile& first, const ClangFile& second )
    {
        if (first.GetProject() < second.GetProject())
            return true;
        return first.GetFilename() < second.GetFilename();
    }
    bool operator==(const ClangFile& other) const
    {
        if (other.m_Project != m_Project)
            return false;
        return other.m_Filename == m_Filename;
    }
    const wxString& GetProject() const { return m_Project; }
    const wxString& GetFilename() const { return m_Filename; }
};

/** @brief Event used in wxWidgets command event returned by the plugin.
 */
class ClangEvent : public wxCommandEvent
{
public:
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const ClangFile& file ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_File(file),
        m_Position(0,0) {}
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const ClangFile& file,
                const ClTokenPosition& pos, const std::vector< std::pair<int, int> >& occurrences ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_File(file),
        m_Position(pos),
        m_GetOccurrencesResults(occurrences) {}
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const ClangFile& file,
                const ClTokenPosition& pos, const std::vector<ClToken>& completions ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_File(file),
        m_Position(pos),
        m_GetCodeCompletionResults(completions) {}
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const ClangFile& file ,
                const ClTokenPosition& loc, const std::vector<ClDiagnostic>& diag ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_File(file),
        m_Position(loc),
        m_DiagnosticResults(diag) {}
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const ClangFile& file,
                const ClTokenPosition& loc, const wxString& documentation ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_File(file),
        m_Position(loc),
        m_DocumentationResults(documentation) {}
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const ClangFile& file,
               const ClTokenPosition& loc, const std::vector< std::pair<wxString, ClTokenPosition > >& locations ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_File(file),
        m_Position(loc),
        m_LocationResults(locations) {}

    /** @brief Copy constructor
     *
     * @param other The other ClangEvent
     *
     */
    ClangEvent( const ClangEvent& other) :
        wxCommandEvent(other),
        m_TranslationUnitId(other.m_TranslationUnitId),
        m_File(other.m_File),
        m_Position(other.m_Position),
        m_GetOccurrencesResults(other.m_GetOccurrencesResults),
        m_GetCodeCompletionResults(other.m_GetCodeCompletionResults),
        m_DiagnosticResults(other.m_DiagnosticResults),
        m_DocumentationResults(other.m_DocumentationResults) {}
    virtual ~ClangEvent() {}
    virtual wxEvent *Clone() const
    {
        return new ClangEvent(*this);
    }

    ClTranslUnitId GetTranslationUnitId() const
    {
        return m_TranslationUnitId;
    }
    const ClTokenPosition& GetPosition() const
    {
        return m_Position;
    }
    const std::vector< std::pair<int, int> >& GetOccurrencesResults() const
    {
        return m_GetOccurrencesResults;
    }
    const std::vector<ClToken>& GetCodeCompletionResults() const
    {
        return m_GetCodeCompletionResults;
    }
    const std::vector<ClDiagnostic>& GetDiagnosticResults() const
    {
        return m_DiagnosticResults;
    }
    const wxString& GetDocumentationResults() const
    {
        return m_DocumentationResults;
    }
    const std::vector< std::pair<wxString, ClTokenPosition> > GetLocationResults() const
    {
        return m_LocationResults;
    }
    void SetStartedTime( const wxDateTime& tm )
    {
        m_StartedTime = tm;
    }
    const wxDateTime& GetStartedTime() const
    {
        return m_StartedTime;
    }
    const ClangFile& GetFile() const
    {
        return m_File;
    }
private:
    wxDateTime m_StartedTime;
    const ClTranslUnitId m_TranslationUnitId;
    const ClangFile m_File;
    const ClTokenPosition m_Position;
    const std::vector< std::pair<int, int> > m_GetOccurrencesResults;
    const std::vector<ClToken> m_GetCodeCompletionResults;
    const std::vector<ClDiagnostic> m_DiagnosticResults;
    const wxString m_DocumentationResults;
    const std::vector< std::pair<wxString, ClTokenPosition > > m_LocationResults;
};


extern const wxEventType clEVT_TRANSLATIONUNIT_CREATED;
extern const wxEventType clEVT_REPARSE_FINISHED;
extern const wxEventType clEVT_TOKENDATABASE_UPDATED;
extern const wxEventType clEVT_GETCODECOMPLETE_FINISHED;
extern const wxEventType clEVT_GETOCCURRENCES_FINISHED;
extern const wxEventType clEVT_GETDOCUMENTATION_FINISHED;
extern const wxEventType clEVT_DIAGNOSTICS_UPDATED;
extern const wxEventType clEVT_REINDEXFILE_FINISHED;
extern const wxEventType clEVT_GETDEFINITION_FINISHED;

/* interface */
class IClangPlugin
{
public:
    virtual ~IClangPlugin() {};

    virtual bool IsProviderFor(cbEditor* ed) = 0;
    virtual ClTranslUnitId GetTranslationUnitId(const ClangFile& file) = 0;
    virtual const wxImageList& GetImageList(const ClTranslUnitId id) = 0;
    virtual const wxStringVec& GetKeywords(const ClTranslUnitId id) = 0;
    /* Events  */
    virtual void RegisterEventSink(const wxEventType, IEventFunctorBase<ClangEvent>* functor) = 0;
    virtual void RemoveAllEventSinksFor(void* owner) = 0;

    /** Request reparsing of a Translation unit */
    virtual void RequestReparse(const ClTranslUnitId id, const ClangFile& file) = 0;

    virtual wxDateTime GetFileIndexingTimestamp(const ClangFile& file) = 0;
    /** Request reindexing of a file */
    virtual void BeginReindexFile(const ClangFile& file) = 0;

    /** Retrieve function scope */
    virtual void GetTokenScopes(const ClTranslUnitId id, const ClangFile& file, unsigned int tokenMask, std::vector<ClTokenScope>& out_Scopes) = 0;

    /** Occurrences highlighting
     *  Performs an asynchronous request for occurences highlight. Will send an event with */
    virtual void RequestOccurrencesOf(const ClTranslUnitId, const ClangFile& file, const ClTokenPosition& loc) = 0;

    /** Code completion */
    virtual wxCondError GetCodeCompletionAt(const ClTranslUnitId id, const ClangFile& file, const ClTokenPosition& loc,
                                            unsigned long timeout, const ClCodeCompleteOption complete_options, std::vector<ClToken>& out_tknResults) = 0;
    virtual wxString GetCodeCompletionTokenDocumentation(const ClTranslUnitId id, const ClangFile& file,
                                                         const ClTokenPosition& position, const ClTokenId tokenId) = 0;
    virtual wxString GetCodeCompletionInsertSuffix(const ClTranslUnitId translId, int tknId, const wxString& newLine,
                                                   std::vector< std::pair<int, int> >& offsets) = 0;

    /** Token definition lookup */
    virtual void RequestTokenDefinitionsAt(const ClTranslUnitId, const ClangFile& file, const ClTokenPosition& loc) = 0;

    /** Resolve the declaration of a token */
    virtual bool ResolveTokenDeclarationAt(const ClTranslUnitId, const ClangFile& file, const ClTokenPosition& loc, ClangFile& out_file, ClTokenPosition& out_loc) = 0;
};

/** @brief Base class for ClangPlugin components.
 *
 */
/* abstract */
class ClangPluginComponent : public wxEvtHandler
{
protected:
    ClangPluginComponent() {}
public:
    virtual void OnAttach( IClangPlugin *pClangPlugin )
    {
        m_pClangPlugin = pClangPlugin;
    }
    virtual void OnRelease(IClangPlugin* WXUNUSED(pClangPlugin))
    {
        m_pClangPlugin = NULL;
    }
    bool IsAttached() const
    {
        return m_pClangPlugin != NULL;
    }
    virtual bool ConfigurationChanged(){ return false; }
    virtual bool BuildToolBar(wxToolBar* WXUNUSED(toolBar))
    {
        return false;
    }
    /* optional */
    virtual void BuildMenu(wxMenuBar* WXUNUSED(menuBar)) {}
    /* optional */
    virtual void BuildModuleMenu(const ModuleType WXUNUSED(type), wxMenu* WXUNUSED(menu), const FileTreeData* WXUNUSED(data)) {}
    // Does this plugin handle code completion for the editor ed?
    virtual cbCodeCompletionPlugin::CCProviderStatus GetProviderStatusFor(cbEditor* WXUNUSED(ed))
    {
        return cbCodeCompletionPlugin::ccpsInactive;
    }
    // Request code completion
    virtual std::vector<cbCodeCompletionPlugin::CCToken> GetAutocompList(bool WXUNUSED(isAuto), cbEditor* WXUNUSED(ed),
                                                                         int& WXUNUSED(tknStart), int& WXUNUSED(tknEnd))
    {
        return std::vector<cbCodeCompletionPlugin::CCToken>();
    }
    virtual bool DoAutocomplete(const cbCodeCompletionPlugin::CCToken& WXUNUSED(token), cbEditor* WXUNUSED(ed))
    {
        return false;
    }

    virtual wxString GetDocumentation(const cbCodeCompletionPlugin::CCToken& WXUNUSED(token))
    {
        return wxEmptyString;
    }

protected:
    IClangPlugin* m_pClangPlugin;
};

#endif // __CLANGPLUGINAPI_H

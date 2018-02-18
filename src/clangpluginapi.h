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

    bool operator<(const ClTokenRange& other) const
    {
        if (beginLocation < other.beginLocation)
            return true;
        else if (beginLocation > other.beginLocation)
            return false;
        return endLocation < other.endLocation;
    }
    bool operator==(const ClTokenRange& other) const
    {
        if (beginLocation != other.beginLocation)
            return false;
        if (endLocation != other.endLocation)
            return false;
        return true;
    }

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

/** @brief Level of diagnostic
 */
enum ClDiagnosticLevel { dlPartial, dlFull };

enum ClDiagnosticSeverity { sWarning, sError, sNote };

struct ClDiagnosticFixit
{
    ClDiagnosticFixit(const wxString& txt, const unsigned rgStart, const unsigned rgEnd, const wxString& srcLn) :
        text(txt), range(rgStart,rgEnd), srcLine(srcLn){}
    /** @brief Deep copy constructor */
    ClDiagnosticFixit(const ClDiagnosticFixit& other) : text(other.text.c_str()), range(other.range), srcLine(other.srcLine.c_str()) {}
    wxString text;                      ///< Text to insert to Fix It
    std::pair<unsigned,unsigned> range; ///< Range where the fixit applies
    wxString srcLine;                   ///< Copy of the source line of the fixit.
};

struct ClDiagnostic
{
    ClDiagnostic(const int ln, const unsigned rgStart, const unsigned rgEnd, const ClDiagnosticSeverity level, const wxString& msg, const std::vector<ClDiagnosticFixit> fixitL) :
        line(ln), range(rgStart, rgEnd), severity(level), message(msg), fixitList(fixitL) {}

    /** @brief Deep copy constructor */
    ClDiagnostic(const ClDiagnostic& other) : line(other.line), range(other.range), severity(other.severity), /*file(other.file),*/ message(other.message.c_str()), fixitList(other.fixitList){}
    int line;
    std::pair<unsigned, unsigned> range;
    ClDiagnosticSeverity severity;
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

/** @brief Class to represent a file linked to a project
 */
class ClangFile
{
    wxString m_Project;
    wxString m_Filename;
public:
    ClangFile(const wxString& filename) :
        m_Project(wxT("")),
        m_Filename(filename){}
    ClangFile(const wxString& project, const wxString& filename) :
        m_Project(project),
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
        m_Project(project.m_Project),
        m_Filename(filename){}

    ClangFile(const ClangFile& Other) :
        m_Project(Other.m_Project),
        m_Filename(Other.m_Filename){}

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

class ClTokenScope
{
    wxString m_TokenIdentifier;
    wxString m_TokenDisplayName;
    wxString m_ScopeName;  // Sepantic scope e.g. class or namespace where the token is declared
    ClTokenRange m_Range;
    ClTokenCategory m_Category;
public:
    ClTokenScope() : m_TokenIdentifier(), m_ScopeName(), m_Range(), m_Category(tcNone) {}
    ClTokenScope(const wxString& tokIdent, const wxString& tokDisplayName, const wxString& semanticScopeName, const ClTokenRange scopeRange, const ClTokenCategory category) :
        m_TokenIdentifier(tokIdent),
        m_TokenDisplayName(tokDisplayName),
        m_ScopeName(semanticScopeName),
        m_Range(scopeRange),
        m_Category( category ){}
    bool operator==(const ClTokenScope& other) const
    {
        if (other.m_TokenIdentifier != m_TokenIdentifier)
            return false;
        if (other.m_TokenDisplayName != m_TokenDisplayName)
            return false;
        if (other.m_ScopeName != m_ScopeName)
            return false;
        if (other.m_Range.beginLocation != m_Range.beginLocation)
            return false;
        if (other.m_Range.endLocation != m_Range.endLocation)
            return false;
        return true;
    }
    const wxString& GetTokenIdentifier() const { return m_TokenIdentifier; }
    wxString& GetTokenIdentifier() { return m_TokenIdentifier; }
    const wxString& GetTokenDisplayName() const { return m_TokenDisplayName; }
    wxString& GetTokenDisplayName() { return m_TokenDisplayName; }
    const wxString& GetScopeName() const { return m_ScopeName; }
    wxString& GetScopeName() { return m_ScopeName; }
    ClTokenRange GetTokenRange() const { return m_Range; }
    ClTokenRange& GetTokenRange() { return m_Range; }
    ClTokenCategory GetTokenCategory() const { return m_Category; }
    ClTokenCategory& GetTokenCategory() { return m_Category; }
    wxString GetFullName() const
    {
        if (m_TokenDisplayName.IsEmpty())
            return wxT("");
        if (m_ScopeName.IsEmpty())
            return m_TokenDisplayName;
        return m_ScopeName+wxT("::")+m_TokenDisplayName;
    }
};

typedef enum _ClTokenReferenceType
{
    ClTokenReferenceType_None = 0,
    ClTokenReferenceType_OverrideParent = 1<<0,
    ClTokenReferenceType_OverrideChild = 1<<1,
}ClTokenReferenceType;

class ClTokenReference : public ClTokenScope
{
    ClangFile m_File;
    ClTokenScope m_ReferenceScope; // Scope where the reference is
    ClTokenReferenceType m_ReferenceType;

public:
    ClTokenReference(const class wxString& name, const wxString& displayName, const wxString& declScope, const struct ClTokenRange& range, const ClTokenCategory category, const ClangFile& file, const ClTokenScope& referenceScope, const ClTokenReferenceType referenceType) :
        ClTokenScope(name,displayName, declScope, range, category),
        m_File(file),
        m_ReferenceScope(referenceScope),
        m_ReferenceType( referenceType ){}
    const ClangFile& GetFile() const { return m_File; }
    const ClTokenScope& GetReferenceScope() const { return m_ReferenceScope; }
    const ClTokenReferenceType& GetReferenceType() const { return m_ReferenceType; }
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
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const ClangFile& file,
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
               const ClTokenPosition& loc, const std::vector< std::pair<std::string, ClTokenPosition > >& locations ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_File(file),
        m_Position(loc),
        m_LocationResults()
        {
            for (std::vector< std::pair<std::string, ClTokenPosition> >::const_iterator it = locations.begin(); it != locations.end(); ++it)
            {
                wxString filename = wxString::FromUTF8(it->first.c_str());
                m_LocationResults.push_back(std::make_pair(filename, it->second));
            }
        }
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const ClangFile& file,
                const ClTokenPosition& loc, const std::vector<ClTokenReference>& references) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_File(file),
        m_Position(loc),
        m_ReferenceResults(references) {}

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
        m_DocumentationResults(other.m_DocumentationResults),
        m_ReferenceResults(other.m_ReferenceResults) {}
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
    const std::vector< std::pair<wxString, ClTokenPosition> >& GetLocationResults() const
    {
        return m_LocationResults;
    }
    const std::vector<ClTokenReference>& GetReferenceResults() const
    {
        return m_ReferenceResults;
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
    std::vector< std::pair<wxString, ClTokenPosition > > m_LocationResults; // Pair of filename + tokenPositions
    std::vector<ClTokenReference> m_ReferenceResults;
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
extern const wxEventType clEVT_GETREFERENCESCOPES_FINISHED;

/* interface */
class IClangPlugin
{
public:
    virtual ~IClangPlugin() {};

    virtual bool IsProviderFor(cbEditor* ed) = 0;
    virtual ClTranslUnitId GetTranslationUnitId(const ClangFile& file) = 0;
    virtual const wxImageList& GetImageList(const ClTranslUnitId id) = 0;
    virtual int GetTokenImageIndex(const ClTranslUnitId id, ClTokenCategory tokenCategory, ClTokenReferenceType refType ) const = 0;
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

    /** Performs an asynchronous request for callers that references the token at the specified position */
    virtual void RequestTokenReferenceScopesAt(const ClTranslUnitId id, const ClangFile& file, const ClTokenPosition loc) = 0;
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

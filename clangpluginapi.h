#ifndef __CLANGPLUGINAPI_H
#define __CLANGPLUGINAPI_H

#include <cbplugin.h>


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
    bool operator==(const ClTokenPosition& other) const
    {
        return ((line == other.line)&&(column == other.column));
    }
    bool operator!=(const ClTokenPosition& other) const
    {
        return !(*this == other);
    }
    unsigned int line;
    unsigned int column;
};

/** @brief Level of diagnostic
 */
enum ClDiagnosticLevel { dlPartial, dlFull };

enum ClDiagnosticSeverity { sWarning, sError, sNote };

struct ClDiagnostic
{
    ClDiagnostic(const int ln, const int rgStart, const int rgEnd, const ClDiagnosticSeverity level, const wxString& fl, const wxString& msg) :
        line(ln), range(rgStart, rgEnd), severity(level), file(fl), message(msg) {}

    int line;
    std::pair<int, int> range;
    ClDiagnosticSeverity severity;
    wxString file;
    wxString message;
};

typedef enum _TokenType
{
    ClTokenType_Unknown = 0,
    ClTokenType_DeclGroup = 0,
    ClTokenType_DefGroup  = 1<<9,

    ClTokenType_FuncDecl  = 1 | ClTokenType_DeclGroup,
    ClTokenType_VarDecl   = 2 | ClTokenType_DeclGroup,
    ClTokenType_ParmDecl  = 3 | ClTokenType_DeclGroup,
    ClTokenType_ScopeDecl = 4 | ClTokenType_DeclGroup,
    ClTokenType_FuncDef   = ClTokenType_FuncDecl | ClTokenType_DefGroup,

} ClTokenType;

/** @brief Event used in wxWidgets command event returned by the plugin.
 */
class ClangEvent : public wxCommandEvent
{
public:
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const wxString& filename ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_Filename(filename),
        m_Location(0,0) {}
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const wxString& filename,
                const ClTokenPosition& pos, const std::vector< std::pair<int, int> >& occurrences ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_Filename(filename),
        m_Location(pos),
        m_GetOccurrencesResults(occurrences) {}
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const wxString& filename,
                const ClTokenPosition& pos, const std::vector<ClToken>& completions ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_Filename(filename),
        m_Location(pos),
        m_GetCodeCompletionResults(completions) {}
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const wxString& filename,
                const ClTokenPosition& loc, const std::vector<ClDiagnostic>& diag ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_Filename(filename),
        m_Location(loc),
        m_DiagnosticResults(diag) {}
    ClangEvent( const wxEventType evtId, const ClTranslUnitId id, const wxString& filename,
                const ClTokenPosition& loc, const wxString& documentation ) :
        wxCommandEvent(wxEVT_NULL, evtId),
        m_TranslationUnitId(id),
        m_Filename(filename),
        m_Location(loc),
        m_DocumentationResults(documentation) {}

    /** @brief Copy constructor
     *
     * @param other The other ClangEvent
     *
     */
    ClangEvent( const ClangEvent& other) :
        wxCommandEvent(other),
        m_TranslationUnitId(other.m_TranslationUnitId),
        m_Filename(other.m_Filename),
        m_Location(other.m_Location),
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
    const ClTokenPosition& GetLocation() const
    {
        return m_Location;
    }
    const std::vector< std::pair<int, int> >& GetOccurrencesResults()
    {
        return m_GetOccurrencesResults;
    }
    const std::vector<ClToken>& GetCodeCompletionResults()
    {
        return m_GetCodeCompletionResults;
    }
    const std::vector<ClDiagnostic>& GetDiagnosticResults()
    {
        return m_DiagnosticResults;
    }
    const wxString& GetDocumentationResults()
    {
        return m_DocumentationResults;
    }
private:
    const ClTranslUnitId m_TranslationUnitId;
    const wxString m_Filename;
    const ClTokenPosition m_Location;
    const std::vector< std::pair<int, int> > m_GetOccurrencesResults;
    const std::vector<ClToken> m_GetCodeCompletionResults;
    const std::vector<ClDiagnostic> m_DiagnosticResults;
    const wxString m_DocumentationResults;
};

extern const wxEventType clEVT_TRANSLATIONUNIT_CREATED;
extern const wxEventType clEVT_REPARSE_FINISHED;
extern const wxEventType clEVT_TOKENDATABASE_UPDATED;
extern const wxEventType clEVT_GETCODECOMPLETE_FINISHED;
extern const wxEventType clEVT_GETOCCURRENCES_FINISHED;
extern const wxEventType clEVT_GETDOCUMENTATION_FINISHED;
extern const wxEventType clEVT_DIAGNOSTICS_UPDATED;

/* interface */
class IClangPlugin
{
public:
    virtual ~IClangPlugin() {};

    virtual bool IsProviderFor(cbEditor* ed) = 0;
    virtual ClTranslUnitId GetTranslationUnitId(const wxString& filename) = 0;
    virtual const wxImageList& GetImageList(const ClTranslUnitId id) = 0;
    virtual const wxStringVec& GetKeywords(const ClTranslUnitId id) = 0;
    /* Events  */
    virtual void RegisterEventSink(wxEventType, IEventFunctorBase<ClangEvent>* functor) = 0;
    virtual void RemoveAllEventSinksFor(void* owner) = 0;

    /** Request reparsing of a Translation unit */
    virtual void RequestReparse(const ClTranslUnitId id, const wxString& filename) = 0;
    /** Retrieve unction scope */
    virtual std::pair<wxString, wxString> GetFunctionScopeAt(const ClTranslUnitId id, const wxString& filename,
                                                             const ClTokenPosition& location) = 0;
    virtual ClTokenPosition GetFunctionScopeLocation(const ClTranslUnitId id, const wxString& filename,
                                                     const wxString& scope, const wxString& functioname) = 0;
    virtual void GetFunctionScopes(const ClTranslUnitId, const wxString& filename,
                                   std::vector<std::pair<wxString, wxString> >& out_scopes) = 0;
    /** Occurrences highlighting */
    virtual wxCondError GetOccurrencesOf(const ClTranslUnitId, const wxString& filename, const ClTokenPosition& loc,
                                         unsigned long timeout, std::vector< std::pair<int, int> >& out_occurrences) = 0;
    /* Code completion */
    virtual wxCondError GetCodeCompletionAt(const ClTranslUnitId id, const wxString& filename, const ClTokenPosition& loc,
                                            bool includeCtors, unsigned long timeout, std::vector<ClToken>& out_tknResults) = 0;
    virtual wxString GetCodeCompletionTokenDocumentation(const ClTranslUnitId id, const wxString& filename,
                                                         const ClTokenPosition& location, ClTokenId tokenId) = 0;
    virtual wxString GetCodeCompletionInsertSuffix(const ClTranslUnitId translId, int tknId, const wxString& newLine,
                                                   std::vector< std::pair<int, int> >& offsets) = 0;
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
    virtual bool IsAttached()
    {
        return m_pClangPlugin != NULL;
    }
    virtual bool BuildToolBar(wxToolBar* WXUNUSED(toolBar))
    {
        return false;
    }
    virtual void BuildMenu(wxMenuBar* WXUNUSED(menuBar)) {}
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

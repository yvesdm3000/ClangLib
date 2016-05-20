#ifndef CLANGPLUGIN_H
#define CLANGPLUGIN_H

#include <cbplugin.h>
#include <cbproject.h>

#include <wx/imaglist.h>
#include <wx/timer.h>

#include "clangpluginapi.h"
#include "clangproxy.h"
#include "tokendatabase.h"
#include "clangtoolbar.h"
#include "clangcc.h"
#include "clangdiagnostics.h"
#include "clangrefactoring.h"
#include "clangindexer.h"

// milliseconds
#define CLANG_REPARSE_DELAY 10000


/* final */
class ClangPlugin : public cbCodeCompletionPlugin, public IClangPlugin
{
    friend class ClangSettingsDlg;
public:
    ClangPlugin();
    virtual ~ClangPlugin();

    bool ProcessEvent(wxEvent& event);

    /*-- Public interface --*/
    virtual int GetConfigurationGroup() const
    {
        return cgEditor;
    }
    virtual cbConfigurationPanel* GetConfigurationPanel(wxWindow* parent);


    // Does this plugin handle code completion for the editor ed?
    virtual CCProviderStatus GetProviderStatusFor(cbEditor* ed);
    // Supply content for the autocompletion list.
    virtual std::vector<CCToken> GetAutocompList(bool isAuto, cbEditor* ed, int& tknStart, int& tknEnd);
    // Supply html formatted documentation for the passed token.
    virtual wxString GetDocumentation(const CCToken& token);
    // Supply content for the calltip at the specified location.
    virtual std::vector<CCCallTip> GetCallTips(int pos, int style, cbEditor* ed, int& argsPos);
    // Supply the definition of the token at the specified location.
    virtual std::vector<CCToken> GetTokenAt(int pos, cbEditor* ed, bool& allowCallTip);
    // Handle documentation link event.
    virtual wxString OnDocumentationLink(wxHtmlLinkEvent& event, bool& dismissPopup);
    // Callback for inserting the selected autocomplete entry into the editor.
    virtual void DoAutocomplete(const CCToken& token, cbEditor* ed);

    // Build menu bar
    virtual void BuildMenu(wxMenuBar* menuBar);
    // Build popup menu
    virtual void BuildModuleMenu(const ModuleType type, wxMenu* menu, const FileTreeData* data = nullptr);

    /** build CC Toolbar */
    virtual bool BuildToolBar(wxToolBar* toolBar);

public:
    void UpdateComponents();
protected:
    virtual void OnAttach();
    virtual void OnRelease(bool appShutDown);

private:
    /**
     * Compute the locations of STL headers for the given compiler (cached)
     *
     * @param compId The id of the compiler
     * @return Include search flags pointing to said locations
     */
    wxString GetCompilerInclDirs(const wxString& compId);

#if 0
    /**
     * Search for the source file associated with a given header
     *
     * @param ed The editor representing the header file
     * @return Full path to presumed source file
     */
    wxString GetSourceOf(cbEditor* ed);
    /**
     * Find the most likely source file from a list, corresponding to a given header
     *
     * @param candidateFilesArray List of possibilities to check
     * @param activeFile Header file to compare to
     * @param[out] isCandidate Set to true if returned file may not be best match
     * @return File determined to be source (or invalid)
     */
    wxFileName FindSourceIn(const wxArrayString& candidateFilesArray, const wxFileName& activeFile, bool& isCandidate);
    /**
     * Test if a candidate source file matches a header file
     *
     * @param candidateFile The potential source file
     * @param activeFile The header file
     * @param[out] isCandidate Set to true if match is not exact
     * @return true if files match close enough
     */
    bool IsSourceOf(const wxFileName& candidateFile, const wxFileName& activeFile, bool& isCandidate);
#endif
    void OnCCLogger(CodeBlocksThreadEvent& event);
    void OnCCDebugLogger(CodeBlocksThreadEvent& event);

    /// Start up parsing timers
    void OnEditorOpen(CodeBlocksEvent& event);
    void OnEditorActivate(CodeBlocksEvent& event);
    void OnEditorSave(CodeBlocksEvent& event);
    void OnEditorClose(CodeBlocksEvent& event);
    /// Make project-dependent setup
    void OnProjectOpen(CodeBlocksEvent& event);
    void OnProjectActivate(CodeBlocksEvent& event);
    void OnProjectFileChanged(CodeBlocksEvent& event);
    /// Update project-dependent setup
    void OnProjectOptionsChanged(CodeBlocksEvent& event);
    void OnProjectTargetsModified(CodeBlocksEvent& event);
    /// Close project
    void OnProjectClose(CodeBlocksEvent& event);
    /// Generic handler for various timers
    void OnTimer(wxTimerEvent& event);
    /// Start re-parse
    void OnEditorHook(cbEditor* ed, wxScintillaEvent& event);
    /// Resolve the token under the cursor and open the relevant location
    void OnGotoDeclaration(wxCommandEvent& event);

    // Async
    void OnCreateTranslationUnit(wxCommandEvent& evt);

    /// Set the clang translation unit (callback)
    void OnClangCreateTUFinished(wxEvent& event);

    /// Update after clang has reparsing done (callback)
    void OnClangReparseFinished(wxEvent& event);

    /// Update after updating the token database has finished
    void OnClangUpdateTokenDatabaseFinished(wxEvent& event);

    /// Update after clang has built diagnostics
    void OnClangGetDiagnosticsFinished(wxEvent& event);

    /// Update after clang has finished a synchronous task
    void OnClangSyncTaskFinished(wxEvent& event);

    /// Update after clang has finished building the occurrences list
    void OnClangGetOccurrencesFinished(wxEvent& event);

    void OnClangReindexFinished(wxEvent& event);

    void OnClangLookupDefinitionFinished(wxEvent& event);

private: // Internal utility functions
    // Builds compile command
    wxString GetCompileCommand(ProjectFile* pf, const wxString& filename);
    int UpdateCompileCommand(cbEditor* ed);

    void RequestReparse(int delayMilliseconds = CLANG_REPARSE_DELAY);
    void FlushTranslationUnits();

    bool ActivateComponent(ClangPluginComponent* pComponent);
    bool DeactivateComponent(ClangPluginComponent* pComponent);
    bool ProcessEvent(ClangEvent& event);
    bool HasEventSink(const wxEventType eventType);
    void GetFileAndProject( ProjectFile& pf, wxString& out_project, wxString& out_filename);

public: // IClangPlugin
    bool IsProviderFor(cbEditor* ed);
    ClTranslUnitId GetTranslationUnitId(const ClangFile& file);
    void RegisterEventSink(const wxEventType, IEventFunctorBase<ClangEvent>* functor);
    void RemoveAllEventSinksFor(void* owner);
    void RequestReparse(const ClTranslUnitId id, const ClangFile& file);
    wxDateTime GetFileIndexingTimestamp(const ClangFile& file);
    void BeginReindexFile(const ClangFile& file);
    std::pair<wxString, wxString> GetFunctionScopeAt(const ClTranslUnitId id, const wxString& filename,
                                                     const ClTokenPosition& position);
    void GetFunctionScopePosition(const ClTranslUnitId id, const wxString& filename,
                                             const wxString& scope, const wxString& functioname, ClTokenPosition& out_Position);
    void GetFunctionScopes(const ClTranslUnitId, const wxString& filename,
                           std::vector<std::pair<wxString, wxString> >& out_scopes);
    void RequestOccurrencesOf(const ClTranslUnitId, const ClangFile& file, const ClTokenPosition& loc);
    wxCondError GetCodeCompletionAt(const ClTranslUnitId id, const wxString& filename, const ClTokenPosition& loc,
                                    bool includeCtors, unsigned long timeout, std::vector<ClToken>& out_tknResults);
    wxString GetCodeCompletionTokenDocumentation(const ClTranslUnitId id, const wxString& filename,
                                                 const ClTokenPosition& loc, const ClTokenId tokenId);
    wxString GetCodeCompletionInsertSuffix(const ClTranslUnitId translId, int tknId, const wxString& newLine,
                                           std::vector< std::pair<int, int> >& offsets);
    void RequestTokenDefinitions(const ClTranslUnitId, const ClangFile& file, const ClTokenPosition& loc);

    const wxImageList& GetImageList(const ClTranslUnitId WXUNUSED(id))
    {
        return m_ImageList;
    }
    const wxStringVec& GetKeywords(const ClTranslUnitId WXUNUSED(id))
    {
        return m_CppKeywords;
    }
private: // Members
    std::vector<ClangPluginComponent*> m_ActiveComponentList;

    typedef std::vector<IEventFunctorBase<ClangEvent>*> EventSinksArray;
    typedef std::map<wxEventType, EventSinksArray> EventSinksMap;
    EventSinksMap m_EventSinks;

    wxStringVec m_CppKeywords;
    ClangProxy m_Proxy;
    wxImageList m_ImageList;

    ClangProxy::CodeCompleteAtJob* m_pOutstandingCodeCompletion;

    wxTimer m_ReparseTimer;
    std::map<wxString, wxString> m_compInclDirs;
    cbEditor* m_pLastEditor;
    int m_TranslUnitId;
    int m_EditorHookId;
    int m_LastCallTipPos;
    std::vector<wxStringVec> m_LastCallTips;

    wxString m_CompileCommand;
    int m_UpdateCompileCommand;
    int m_ReparseNeeded;
    ClTranslUnitId m_ReparsingTranslUnitId;

    wxTimer m_StoreIndexDBTimer;

    ClangCodeCompletion m_CodeCompletion;
    ClangDiagnostics m_Diagnostics;
    ClangRefactoring m_Refactoring;
    ClangToolbar m_Toolbar;
    ClangIndexer m_Indexer;
};

#endif // CLANGPLUGIN_H

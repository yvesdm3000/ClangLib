#ifndef CLANGREFACTORING_H
#define CLANGREFACTORING_H

#include <cbplugin.h>
#include <wx/imaglist.h>
#include <wx/timer.h>

#include "clangpluginapi.h"

class ClangCallHierarchyView;

class ClangRefactoring: public ClangPluginComponent
{
public:
    ClangRefactoring();
    virtual ~ClangRefactoring();

    static const wxString SettingName;

    void OnAttach(IClangPlugin* pClangPlugin);
    void OnRelease(IClangPlugin* pClangPlugin);

    /**
     * Semantically highlight all occurrences of the token under the cursor
     * within the editor
     *
     * @param ed The editor to work in
     */
    void BeginHighlightOccurrences(cbEditor* ed);

    void LookupCallHierarchy(const ClangFile& file, const ClTokenPosition position);

    const wxImageList& GetTokenCategoryImageList(){ return m_pClangPlugin->GetImageList(GetCurrentTranslationUnitId()); }
public: // Code::Blocks events
    void OnEditorActivate(CodeBlocksEvent& event);
    void OnEditorHook(cbEditor* ed, wxScintillaEvent& event);
    void OnTimer(wxTimerEvent& event);

    // Build menu bar
    void BuildMenu(wxMenuBar* menuBar);
    // Build popup menu
    void BuildModuleMenu(const ModuleType type, wxMenu* menu, const FileTreeData* data = nullptr);

public: // UI Command Events
    /// Resolve the token under the cursor and open the relevant location
    void OnGotoDefinition(wxCommandEvent& event);
    /// Resolve the token under the cursor and open the relevant location
    void OnGotoDeclaration(wxCommandEvent& event);
    void OnShowCallHierarchy(wxCommandEvent& event);

public: // Clang events
    void OnTranslationUnitCreated(ClangEvent& event);
    void OnRequestOccurrencesFinished(ClangEvent& event);
    void OnGetDefinitionFinished(ClangEvent& event);
    void OnGetCallHierarchyFinished(ClangEvent& event);

private:
    /** Get the current translation unit id */
    ClTranslUnitId GetCurrentTranslationUnitId();

protected:
    bool ConfigurationChanged();

private:
    ClTranslUnitId m_TranslUnitId;
    int m_EditorHookId;

    bool m_bShowOccurrences;
    wxTimer m_HighlightTimer;
    ClangCallHierarchyView* m_pCallHierarchyView; // Notebook view of call hierarchy
};


#endif

#ifndef CLANGREFACTORING_H
#define CLANGREFACTORING_H

#include <cbplugin.h>
#include <wx/imaglist.h>
#include <wx/timer.h>

#include "clangpluginapi.h"

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

public: // Code::Blocks events
    void OnEditorActivate(CodeBlocksEvent& event);
    void OnEditorHook(cbEditor* ed, wxScintillaEvent& event);
    void OnTimer(wxTimerEvent& event);

public: // Clang events
    void OnTranslationUnitCreated(ClangEvent& event);
    void OnRequestOccurrencesFinished(ClangEvent& event);

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
};


#endif

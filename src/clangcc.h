#ifndef CLANGCC_H
#define CLANGCC_H

#include <cbplugin.h>
#include <wx/imaglist.h>
#include <wx/timer.h>
#include <deque>

#include "clangpluginapi.h"

class ClangCodeCompletion : public ClangPluginComponent
{
public:
    ClangCodeCompletion();
    virtual ~ClangCodeCompletion();

    static const wxString SettingName;

    void OnAttach(IClangPlugin* pClangPlugin);
    void OnRelease(IClangPlugin* pClangPlugin);

    cbCodeCompletionPlugin::CCProviderStatus GetProviderStatusFor(cbEditor* ed);
    std::vector<cbCodeCompletionPlugin::CCToken> GetAutocompList(bool isAuto, cbEditor* ed, int& tknStart, int& tknEnd);
    wxString GetDocumentation(const cbCodeCompletionPlugin::CCToken& token);
    bool DoAutocomplete(const cbCodeCompletionPlugin::CCToken& WXUNUSED(token), cbEditor* WXUNUSED(ed));

public: // Code::Blocks events
    void OnEditorActivate(CodeBlocksEvent& event);
    void OnEditorSave(CodeBlocksEvent& event);
    void OnCCDone(CodeBlocksEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnCompleteCode(CodeBlocksEvent& event);

public: // Clang events
    void OnTranslationUnitCreated(ClangEvent& event);
    void OnCodeCompleteFinished(ClangEvent& event);

private:
    /** Perform auto completion for #include filenames */
    std::vector<cbCodeCompletionPlugin::CCToken> GetAutocompListIncludes(bool isAuto, cbEditor* ed, int& tknStart, int& tknEnd);
    /** Get the current translation unit id */
    ClTranslUnitId GetCurrentTranslationUnitId();

protected: // Code completion for #include
    /** get the include paths setting (usually set by user for each C::B project)
     * note that this function is only be called in CodeCompletion::DoCodeCompleteIncludes()
     * if it finds some system level include search dirs which does not been scanned, it will start a
     * a new thread(SystemHeadersThread).
     * @param project project info
     * @param buildTargets target info
     * @return the local include paths
     */
    wxArrayString GetLocalIncludeDirs(cbProject* project, const wxArrayString& buildTargets);

    /** get the whole search dirs except the ones locally belong to the c::b project, note this
     * function is used for auto suggestion for #include directives.
     * @param force if the value is false, just return a static (cached) wxArrayString to optimize
     * the performance, if it is true, we try to update the cache.
     */
    wxArrayString& GetSystemIncludeDirs(cbProject* project, bool force);

    /** search target file names (mostly relative names) under basePath, then return the absolute dirs
     * It just did the calculation below:
     * "c:/ccc/ddd.cpp"(basePath) + "aaa/bbb.h"(target) => "c:/ccc/aaa/bbb.h"(dirs)
     * @param basePath already located file path, this is usually the currently parsing file's location
     * @param targets the relative filename, e.g. When you have #include "aaa/bbb.h", "aaa/bbb.h" is the target location
     * @param dirs result location of the targets in absolute file path format
     */
    void GetAbsolutePath(const wxString& basePath, const wxArrayString& targets, wxArrayString& dirs);

private:
    ClTranslUnitId m_TranslUnitId;
    long m_CCOutstandingLastMessageTime;
    ClTokenPosition m_CCOutstandingLoc;
    ClTokenPosition m_CCResultsLoc;
    std::vector<ClToken> m_CCResults;
    std::vector<wxString> m_TabJumpArguments;
    std::deque<wxString> m_CCHistory;
};


#endif

/*
 * A clang based plugin
 */

#include <sdk.h>
#include <stdio.h>
#include <iostream>
#include "clangplugin.h"
#include "clangccsettingsdlg.h"
#include "clangprojectsettingsdlg.h"
#include "cclogger.h"

#include <cbcolourmanager.h>
#include <cbstyledtextctrl.h>
#include <compilercommandgenerator.h>
#include <editor_hooks.h>
#include <projectloader_hooks.h>
#include <clang-c/CXCompilationDatabase.h>

#include <wx/tokenzr.h>

#ifndef CB_PRECOMP
#include <cbeditor.h>
#include <cbproject.h>
#include <compilerfactory.h>
#include <configmanager.h>
#include <editorcolourset.h>
#include <editormanager.h>
#include <logmanager.h>
#include <macrosmanager.h>
#include <projectfile.h>
#include <projectmanager.h>
#include <algorithm>
#include <wx/dir.h>
#include <wx/menu.h>
#include <wx/tokenzr.h>
#include <wx/choice.h>
#include <wx/timer.h>
#endif // CB_PRECOMP

#define CLANGPLUGIN_TRACE_FUNCTIONS

// this auto-registers the plugin
namespace
{
PluginRegistrant<ClangPlugin> reg(wxT("ClangLib"));
}

DEFINE_EVENT_TYPE(clEVT_TRANSLATIONUNIT_CREATED);
DEFINE_EVENT_TYPE(clEVT_REPARSE_FINISHED);
DEFINE_EVENT_TYPE(clEVT_GETCODECOMPLETE_FINISHED);
DEFINE_EVENT_TYPE(clEVT_GETOCCURRENCES_FINISHED);
DEFINE_EVENT_TYPE(clEVT_DIAGNOSTICS_UPDATED);
DEFINE_EVENT_TYPE(clEVT_GETDOCUMENTATION_FINISHED);
DEFINE_EVENT_TYPE(clEVT_TOKENDATABASE_UPDATED);
DEFINE_EVENT_TYPE(clEVT_REINDEXFILE_FINISHED);
DEFINE_EVENT_TYPE(clEVT_GETDEFINITION_FINISHED);
DEFINE_EVENT_TYPE(clEVT_GETREFERENCESCOPES_FINISHED);

static const wxString g_InvalidStr(wxT("invalid"));
const int idReparseTimer    = wxNewId();
const int idCreateTU = wxNewId();
const int idStoreIndexDBTimer = wxNewId();

DEFINE_EVENT_TYPE(cbEVT_COMMAND_CREATETU);
// Asynchronous events received
DEFINE_EVENT_TYPE(cbEVT_CLANG_ASYNCTASK_FINISHED);
DEFINE_EVENT_TYPE(cbEVT_CLANG_SYNCTASK_FINISHED);

const int idClangCreateTU = wxNewId();
const int idClangRemoveTU = wxNewId();
const int idClangReparse = wxNewId();
const int idClangReindex = wxNewId();
const int idClangUpdateTokenDatabase = wxNewId();
const int idClangGetDiagnostics = wxNewId();
const int idClangSyncTask = wxNewId();
const int idClangCodeComplete = wxNewId();
const int idClangGetCCDocumentation = wxNewId();
const int idClangGetOccurrences = wxNewId();
const int idClangLookupDefinition = wxNewId();
const int idClangStoreTokenIndexDB = wxNewId();
const int idClangLookupTokenReferences = wxNewId();

ClangPlugin::ClangPlugin() :
    m_Proxy(this, m_CppKeywords),
    m_ImageList(16, 16),
    m_pOutstandingCodeCompletion(nullptr),
    m_ReparseTimer(this, idReparseTimer),
    m_pLastEditor(nullptr),
    m_TranslUnitId(wxNOT_FOUND),
    m_EditorHookId( -1 ),
    m_ProjectHookId( -1 ),
    m_UpdateCompileCommand(0),
    m_ReparseNeeded(0),
    m_ReparsingTranslUnitId(wxNOT_FOUND),
    m_StoreIndexDBJobCount(0),
    m_StoreIndexDBTimer(this, idStoreIndexDBTimer)
{
    CCLogger::Get()->Init(this, g_idCCLogger, g_idCCDebugLogger, g_idCCErrorLogger);
    if (!Manager::LoadResource(wxT("clanglib.zip")))
        NotifyMissingFile(wxT("clanglib.zip"));
}

ClangPlugin::~ClangPlugin()
{
}

void ClangPlugin::OnAttach()
{
    wxBitmap bmp;
    ConfigManager* cfg = Manager::Get()->GetConfigManager(CLANG_CONFIGMANAGER);
    wxString prefix = ConfigManager::GetDataFolder() + wxT("/images/clanglib/");
    // bitmaps must be added by order of PARSER_IMG_* consts (which are also TokenCategory enums)
    const char* imgs[] =
    {
        "class_folder.png",        // PARSER_IMG_CLASS_FOLDER
        "class.png",               // PARSER_IMG_CLASS
        "class_private.png",       // PARSER_IMG_CLASS_PRIVATE
        "class_protected.png",     // PARSER_IMG_CLASS_PROTECTED
        "class_public.png",        // PARSER_IMG_CLASS_PUBLIC
        "ctor_private.png",        // PARSER_IMG_CTOR_PRIVATE
        "ctor_protected.png",      // PARSER_IMG_CTOR_PROTECTED
        "ctor_public.png",         // PARSER_IMG_CTOR_PUBLIC
        "dtor_private.png",        // PARSER_IMG_DTOR_PRIVATE
        "dtor_protected.png",      // PARSER_IMG_DTOR_PROTECTED
        "dtor_public.png",         // PARSER_IMG_DTOR_PUBLIC
        "method_private.png",      // PARSER_IMG_FUNC_PRIVATE
        "method_protected.png",    // PARSER_IMG_FUNC_PRIVATE
        "method_public.png",       // PARSER_IMG_FUNC_PUBLIC
        "var_private.png",         // PARSER_IMG_VAR_PRIVATE
        "var_protected.png",       // PARSER_IMG_VAR_PROTECTED
        "var_public.png",          // PARSER_IMG_VAR_PUBLIC
        "macro_def.png",           // PARSER_IMG_MACRO_DEF          tcLangMacro
        "enum.png",                // PARSER_IMG_ENUM
        "enum_private.png",        // PARSER_IMG_ENUM_PRIVATE
        "enum_protected.png",      // PARSER_IMG_ENUM_PROTECTED
        "enum_public.png",         // PARSER_IMG_ENUM_PUBLIC
        "enumerator.png",          // PARSER_IMG_ENUMERATOR
        "namespace.png",           // PARSER_IMG_NAMESPACE
        "typedef.png",             // PARSER_IMG_TYPEDEF
        "typedef_private.png",     // PARSER_IMG_TYPEDEF_PRIVATE
        "typedef_protected.png",   // PARSER_IMG_TYPEDEF_PROTECTED
        "typedef_public.png",      // PARSER_IMG_TYPEDEF_PUBLIC
        "symbols_folder.png",      // PARSER_IMG_SYMBOLS_FOLDER
        "vars_folder.png",         // PARSER_IMG_VARS_FOLDER
        "funcs_folder.png",        // PARSER_IMG_FUNCS_FOLDER
        "enums_folder.png",        // PARSER_IMG_ENUMS_FOLDER
        "macro_def_folder.png",    // PARSER_IMG_MACRO_DEF_FOLDER
        "others_folder.png",       // PARSER_IMG_OTHERS_FOLDER
        "typedefs_folder.png",     // PARSER_IMG_TYPEDEF_FOLDER
        "macro_use.png",           // PARSER_IMG_MACRO_USE
        "macro_use_private.png",   // PARSER_IMG_MACRO_USE_PRIVATE
        "macro_use_protected.png", // PARSER_IMG_MACRO_USE_PROTECTED
        "macro_use_public.png",    // PARSER_IMG_MACRO_USE_PUBLIC
        "macro_use_folder.png",    // PARSER_IMG_MACRO_USE_FOLDER
        "cpp_lang.png",            // tcLangKeyword
        nullptr
    };
    for (const char** itr = imgs; *itr; ++itr)
    {
        m_ImageList.Add(cbLoadBitmap(prefix + wxString::FromUTF8(*itr), wxBITMAP_TYPE_PNG));
    }

    EditorColourSet* theme = Manager::Get()->GetEditorManager()->GetColourSet();
    wxStringTokenizer tokenizer(theme->GetKeywords(theme->GetHighlightLanguage(wxT("C/C++")), 0));
    while (tokenizer.HasMoreTokens())
        m_CppKeywords.push_back(tokenizer.GetNextToken());
    std::sort(m_CppKeywords.begin(), m_CppKeywords.end());
    wxStringVec(m_CppKeywords).swap(m_CppKeywords);


    // hook to project loading procedure
    ProjectLoaderHooks::HookFunctorBase* myhook = new ProjectLoaderHooks::HookFunctor<ClangPlugin>(this, &ClangPlugin::OnProjectLoadingHook);
    m_ProjectHookId = ProjectLoaderHooks::RegisterHook(myhook);


    typedef cbEventFunctor<ClangPlugin, CodeBlocksEvent> CbEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_OPEN,             new CbEvent(this, &ClangPlugin::OnEditorOpen));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED,        new CbEvent(this, &ClangPlugin::OnEditorActivate));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_SAVE,             new CbEvent(this, &ClangPlugin::OnEditorSave));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_CLOSE,            new CbEvent(this, &ClangPlugin::OnEditorClose));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_OPEN,            new CbEvent(this, &ClangPlugin::OnProjectOpen));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_ACTIVATE,        new CbEvent(this, &ClangPlugin::OnProjectActivate));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_FILE_CHANGED,    new CbEvent(this, &ClangPlugin::OnProjectFileChanged));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_OPTIONS_CHANGED, new CbEvent(this, &ClangPlugin::OnProjectOptionsChanged));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_TARGETS_MODIFIED,new CbEvent(this, &ClangPlugin::OnProjectTargetsModified));
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_CLOSE,           new CbEvent(this, &ClangPlugin::OnProjectClose));

    Connect(g_idCCLogger,                  wxEVT_COMMAND_MENU_SELECTED,    CodeBlocksThreadEventHandler(ClangPlugin::OnCCLogger));
    Connect(g_idCCDebugLogger,             wxEVT_COMMAND_MENU_SELECTED,    CodeBlocksThreadEventHandler(ClangPlugin::OnCCDebugLogger));
    Connect(g_idCCErrorLogger,             wxEVT_COMMAND_MENU_SELECTED,    CodeBlocksThreadEventHandler(ClangPlugin::OnCCErrorLogger));
    Connect(idReparseTimer,                wxEVT_TIMER,                    wxTimerEventHandler(ClangPlugin::OnTimer));
    Connect(idStoreIndexDBTimer,           wxEVT_TIMER,                    wxTimerEventHandler(ClangPlugin::OnTimer));
    Connect(idCreateTU,                    cbEVT_COMMAND_CREATETU,         wxCommandEventHandler(ClangPlugin::OnCreateTranslationUnit), nullptr, this);

    Connect(idClangCreateTU,               cbEVT_CLANG_ASYNCTASK_FINISHED, wxEventHandler(ClangPlugin::OnClangCreateTUFinished),        nullptr, this);
    Connect(idClangReparse,                cbEVT_CLANG_ASYNCTASK_FINISHED, wxEventHandler(ClangPlugin::OnClangReparseFinished),         nullptr, this);
    Connect(idClangReindex,                cbEVT_CLANG_ASYNCTASK_FINISHED, wxEventHandler(ClangPlugin::OnClangReindexFinished),         nullptr, this);
    Connect(idClangGetDiagnostics,         cbEVT_CLANG_ASYNCTASK_FINISHED, wxEventHandler(ClangPlugin::OnClangGetDiagnosticsFinished),  nullptr, this);
    Connect(idClangGetOccurrences,         cbEVT_CLANG_ASYNCTASK_FINISHED, wxEventHandler(ClangPlugin::OnClangGetOccurrencesFinished),  nullptr, this);
    Connect(idClangSyncTask,               cbEVT_CLANG_SYNCTASK_FINISHED,  wxEventHandler(ClangPlugin::OnClangSyncTaskFinished),        nullptr, this);
    Connect(idClangCodeComplete,           cbEVT_CLANG_SYNCTASK_FINISHED,  wxEventHandler(ClangPlugin::OnClangSyncTaskFinished),        nullptr, this);
    Connect(idClangGetCCDocumentation,     cbEVT_CLANG_SYNCTASK_FINISHED,  wxEventHandler(ClangPlugin::OnClangSyncTaskFinished),        nullptr, this);
    Connect(idClangLookupDefinition,       cbEVT_CLANG_ASYNCTASK_FINISHED, wxEventHandler(ClangPlugin::OnClangLookupDefinitionFinished),nullptr, this);
    Connect(idClangStoreTokenIndexDB,      cbEVT_CLANG_ASYNCTASK_FINISHED, wxEventHandler(ClangPlugin::OnClangStoreTokenIndexDB),       nullptr, this);
    Connect(idClangLookupTokenReferences,  cbEVT_CLANG_ASYNCTASK_FINISHED, wxEventHandler(ClangPlugin::OnClangLookupTokenReferencesFinished),nullptr, this);

    m_EditorHookId = EditorHooks::RegisterHook(new EditorHooks::HookFunctor<ClangPlugin>(this, &ClangPlugin::OnEditorHook));

    m_Proxy.SetMaxTranslationUnits( cfg->ReadInt( wxT("/max_tu") ) );

    if (cfg->ReadBool(ClangCodeCompletion::SettingName, true))
        ActivateComponent(&m_CodeCompletion);
    if (cfg->ReadBool(ClangDiagnostics::SettingName, true))
        ActivateComponent(&m_Diagnostics);
    if (cfg->ReadBool(ClangRefactoring::SettingName, true))
        ActivateComponent(&m_Refactoring);
    if (cfg->ReadBool(ClangIndexer::SettingName, true))
        ActivateComponent(&m_Indexer);
}

/**
 * Dispatch events to the components
 */
bool ClangPlugin::ProcessEvent( wxEvent& event )
{
    if (cbPlugin::ProcessEvent(event))
        return true;
    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
        if ((*it)->ProcessEvent(event))
            return true;
    }
    return false;
}

void ClangPlugin::OnRelease(bool WXUNUSED(appShutDown))
{
    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
        (*it)->OnRelease(this);

    ProjectLoaderHooks::UnregisterHook(m_ProjectHookId, true);
    EditorHooks::UnregisterHook(m_EditorHookId);
    Disconnect(idClangGetCCDocumentation);
    Disconnect(idClangGetOccurrences);
    Disconnect(idClangCodeComplete);
    Disconnect(idClangSyncTask);
    Disconnect(idClangGetDiagnostics);
    Disconnect(idClangReparse);
    Disconnect(idCreateTU);
    Disconnect(idClangCreateTU);
    Disconnect(idReparseTimer);
    Disconnect(g_idCCErrorLogger);
    Disconnect(g_idCCDebugLogger);
    Disconnect(g_idCCLogger);

    Manager::Get()->RemoveAllEventSinksFor(this);
    m_ImageList.RemoveAll();
}

bool ClangPlugin::ActivateComponent( ClangPluginComponent* pComponent )
{
    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
        if (*it == pComponent)
            return false;
    }
    m_ActiveComponentList.push_back(pComponent);
    pComponent->OnAttach(this);

    return true;
}

bool ClangPlugin::DeactivateComponent(ClangPluginComponent* pComponent)
{
    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
        if (*it == pComponent)
        {
            (*it)->OnRelease(this);
            it = m_ActiveComponentList.erase(it);
            return true;
        }
    }
    return false;
}

void ClangPlugin::UpdateComponents()
{
    bool reloadEditor = false;
    ConfigManager* cfg = Manager::Get()->GetConfigManager(CLANG_CONFIGMANAGER);
    if (cfg->ReadBool(ClangCodeCompletion::SettingName, true))
    {
        if (ActivateComponent(&m_CodeCompletion))
            reloadEditor = true;
    }
    else
    {
        if (DeactivateComponent(&m_CodeCompletion))
            reloadEditor = true;
    }
    if (cfg->ReadBool(ClangDiagnostics::SettingName, true))
    {
        if (ActivateComponent(&m_Diagnostics))
            reloadEditor = true;
    }
    else
    {
        if (DeactivateComponent(&m_Diagnostics))
            reloadEditor = true;
    }
    if (cfg->ReadBool(ClangRefactoring::SettingName, true))
    {
        if (ActivateComponent(&m_Refactoring))
            reloadEditor = true;
    }
    else
    {
        if (DeactivateComponent(&m_Refactoring))
            reloadEditor = true;
    }
    if (cfg->ReadBool(ClangIndexer::SettingName, true))
    {
        if (ActivateComponent(&m_Indexer))
            reloadEditor = true;
    }
    else
    {
        if (DeactivateComponent(&m_Indexer))
            reloadEditor = true;
    }

    m_Proxy.SetMaxTranslationUnits( cfg->ReadInt( wxT("/max_tu") ));

    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
        if ((*it)->ConfigurationChanged())
            reloadEditor = true;
    }

    if (reloadEditor)
        Manager::Get()->GetEditorManager()->SetActiveEditor(Manager::Get()->GetEditorManager()->GetActiveEditor());
}

ClangPlugin::CCProviderStatus ClangPlugin::GetProviderStatusFor(cbEditor* ed)
{
    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
        ClangPlugin::CCProviderStatus status = (*it)->GetProviderStatusFor(ed);
        if (status != ccpsInactive)
            return status;

    }
    return ccpsInactive;
}

cbConfigurationPanel* ClangPlugin::GetConfigurationPanel(wxWindow* parent)
{
    return new ClangSettingsDlg(parent, this);
}

cbConfigurationPanel* ClangPlugin::GetProjectConfigurationPanel(wxWindow* parent, cbProject* project)
{
    return new ClangProjectSettingsDlg(parent, project, this);
}

std::vector<ClangPlugin::CCToken> ClangPlugin::GetAutocompList(bool isAuto, cbEditor* ed, int& tknStart, int& tknEnd)
{
    std::vector<CCToken> tokens;
    if (ed != m_pLastEditor)
        return tokens;

    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
        ClangPlugin::CCProviderStatus status = (*it)->GetProviderStatusFor(ed);
        if (status != ccpsInactive)
        {
            tokens = (*it)->GetAutocompList(isAuto, ed, tknStart, tknEnd);
            break;
        }
    }
    return tokens;
}

wxString ClangPlugin::GetDocumentation(const CCToken& token)
{
    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
        wxString ret = (*it)->GetDocumentation(token);
        if (ret != wxEmptyString)
        {
            return ret;
        }
    }

    return wxEmptyString;
}

std::vector<ClangPlugin::CCCallTip> ClangPlugin::GetCallTips(int pos, int WXUNUSED(style), cbEditor* ed, int& argsPos)
{
    std::vector<CCCallTip> tips;

    if (ed != m_pLastEditor)
    {
        return tips;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
    {
        return tips;
    }

    cbStyledTextCtrl* stc = ed->GetControl();

    int nest = 0;
    int commas = 0;
    while (--pos > 0)
    {
        const int curStyle = stc->GetStyleAt(pos);
        if (   stc->IsString(curStyle)
            || stc->IsCharacter(curStyle)
            || stc->IsComment(curStyle) )
        {
            continue;
        }

        const wxChar ch = stc->GetCharAt(pos);
        if (ch == wxT(';'))
        {
            return tips; // error?
        }
        else if (ch == wxT(','))
        {
            if (nest == 0)
                ++commas;
        }
        else if (ch == wxT(')'))
            --nest;
        else if (ch == wxT('('))
        {
            ++nest;
            if (nest > 0)
                break;
        }
    }
    while (--pos > 0)
    {
        if ( stc->GetCharAt(pos) <= wxT(' ')
            || stc->IsComment(stc->GetStyleAt(pos)) )
        {
            continue;
        }
        break;
    }
    argsPos = stc->WordEndPosition(pos, true);
    if (argsPos != m_LastCallTipPos)
    {
        m_LastCallTips.clear();
        const int line = stc->LineFromPosition(pos);
        const int column = pos - stc->PositionFromLine(line);
        const wxString& tknText = stc->GetTextRange(stc->WordStartPosition(pos, true), argsPos);
        if (!tknText.IsEmpty())
        {
            ClTokenPosition loc(line + 1, column + 1);
            ClangProxy::GetCallTipsAtJob job(cbEVT_CLANG_SYNCTASK_FINISHED, idClangSyncTask, ClangFile( ed->GetProjectFile(), ed->GetFilename()), loc, m_TranslUnitId, tknText.ToUTF8().data());
            m_Proxy.AppendPendingJob(job);
            if (job.WaitCompletion(40) == wxCOND_TIMEOUT)
                return tips;
            m_LastCallTips = job.GetResults();
        }
    }
    m_LastCallTipPos = argsPos;
    for (std::vector<wxStringVec>::const_iterator strVecItr = m_LastCallTips.begin();
            strVecItr != m_LastCallTips.end(); ++strVecItr)
    {
        int strVecSz = strVecItr->size();
        if (commas != 0 && strVecSz < commas + 3)
            continue;
        wxString tip;
        int hlStart = wxSCI_INVALID_POSITION;
        int hlEnd = wxSCI_INVALID_POSITION;
        for (int i = 0; i < strVecSz; ++i)
        {
            wxString str = (*strVecItr)[i];
            if (i == commas + 1 && strVecSz > 2)
            {
                hlStart = tip.Length();
                hlEnd = hlStart + str.Length();
            }
            tip += str;
            if (i > 0 && i < (strVecSz - 2))
                tip += wxT(", ");
        }
        tips.push_back(CCCallTip(tip, hlStart, hlEnd));
    }

    return tips;
}

std::vector<ClangPlugin::CCToken> ClangPlugin::GetTokenAt(int pos, cbEditor* ed, bool& WXUNUSED(allowCallTip))
{
    std::vector<CCToken> tokens;
    if (ed != m_pLastEditor)
        return tokens;
    if (m_TranslUnitId == wxNOT_FOUND)
        return tokens;

    cbStyledTextCtrl* stc = ed->GetControl();
    if (stc->GetTextRange(pos - 1, pos + 1).Strip().IsEmpty())
        return tokens;
    const unsigned int line = stc->LineFromPosition(pos);
    ClTokenPosition loc(line + 1, pos - stc->PositionFromLine(line) + 1);

    ClangProxy::GetTokenCompletionAtJob job( cbEVT_CLANG_SYNCTASK_FINISHED, idClangSyncTask, ClangFile( ed->GetProjectFile(), ed->GetFilename()), loc, m_TranslUnitId);
    m_Proxy.AppendPendingJob(job);

    job.WaitCompletion(40);
    ClIdentifierString name = job.GetResult();
    tokens.push_back(CCToken(-1, wxString::FromUTF8( name.c_str() )) );

    return tokens;
}


wxString ClangPlugin::OnDocumentationLink(wxHtmlLinkEvent& WXUNUSED(event), bool& WXUNUSED(dismissPopup))
{
    return wxEmptyString;
}

void ClangPlugin::DoAutocomplete(const CCToken& token, cbEditor* ed)
{
    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
        if ((*it)->DoAutocomplete(token, ed))
            return;
    }
}

void ClangPlugin::BuildMenu(wxMenuBar* menuBar)
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!IsProviderFor(ed))
        return;
    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
        (*it)->BuildMenu(menuBar);
    }
}

void ClangPlugin::BuildModuleMenu(const ModuleType type, wxMenu* menu,
                                  const FileTreeData* data)
{
    if (type != mtEditorManager)
        return;
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    if (ed != m_pLastEditor)
    {
        m_TranslUnitId = wxNOT_FOUND;
        m_pLastEditor = ed;
        m_ReparseNeeded = 0;
    }
    if (!IsProviderFor(ed))
        return;
    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
       (*it)->BuildModuleMenu(type, menu, data);
    }
}

bool ClangPlugin::BuildToolBar(wxToolBar* toolBar)
{
    for (std::vector<ClangPluginComponent*>::iterator it = m_ActiveComponentList.begin(); it != m_ActiveComponentList.end(); ++it)
    {
        if ((*it)->BuildToolBar(toolBar))
            return true;
    }
    m_Toolbar.OnAttach(this);
    if (!m_Toolbar.BuildToolBar(toolBar))
    {
        m_Toolbar.OnRelease(this);
        return false;
    }

    m_ActiveComponentList.push_back(&m_Toolbar);
    // TODO: Replay some events?
    Manager::Get()->GetEditorManager()->SetActiveEditor(Manager::Get()->GetEditorManager()->GetActiveEditor());

    return true;
}

void ClangPlugin::OnCCLogger(CodeBlocksThreadEvent& event)
{
    if (!Manager::IsAppShuttingDown())
        Manager::Get()->GetLogManager()->Log(event.GetString());
}

void ClangPlugin::OnCCDebugLogger(CodeBlocksThreadEvent& event)
{
    if (!Manager::IsAppShuttingDown())
        Manager::Get()->GetLogManager()->DebugLog(event.GetString());
}

void ClangPlugin::OnCCErrorLogger(CodeBlocksThreadEvent& event)
{
    if (!Manager::IsAppShuttingDown())
        Manager::Get()->GetLogManager()->LogError(event.GetString());
}

void ClangPlugin::OnEditorOpen(CodeBlocksEvent& event)
{
}

void ClangPlugin::OnEditorActivate(CodeBlocksEvent& event)
{
    event.Skip();
    if (!Manager::Get()->IsAppStartedUp())
    {
        return;
    }
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed && ed->IsOK())
    {
        if (ed != m_pLastEditor)
        {
            m_pLastEditor = ed;
            m_TranslUnitId = wxNOT_FOUND;
            m_ReparseNeeded = 0;
        }
        if (!IsProviderFor(ed))
            return;
        ProjectFile* projFile = ed->GetProjectFile();
        if (!projFile)
        {
            return;
        }
        wxString filename = ed->GetFilename();
        if(m_TranslUnitId == wxNOT_FOUND)
            m_TranslUnitId = GetTranslationUnitId(filename);
        UpdateCompileCommand(ed);
        if (m_TranslUnitId == wxNOT_FOUND)
        {
            wxCommandEvent evt(cbEVT_COMMAND_CREATETU, idCreateTU);
            evt.SetString(ed->GetFilename());
            AddPendingEvent(evt);
        }
        else
            RequestReparse(1);
    }
}

void ClangPlugin::OnEditorSave(CodeBlocksEvent& event)
{
    event.Skip();
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    if (!edMgr)
        return;
    cbEditor* ed = edMgr->GetBuiltinEditor(event.GetEditor());
    if (!ed)
        return;
    if (!IsProviderFor(ed))
        return;
    if (m_TranslUnitId == wxNOT_FOUND)
        return;
    ClangFile file(ed->GetFilename());
    if (ed->GetProjectFile())
        file = ClangFile(*ed->GetProjectFile());
    std::map<std::string, wxString> unsavedFiles;
    // Our saved file is not yet known to all translation units since it's no longer in the unsaved files. We update them here
    unsavedFiles.insert(std::make_pair(ed->GetFilename().ToUTF8().data(), ed->GetControl()->GetText()));
    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
    {
        cbEditor* editor = edMgr->GetBuiltinEditor(i);
        if (editor && editor->GetModified())
            unsavedFiles.insert(std::make_pair(editor->GetFilename().ToUTF8().data(), editor->GetControl()->GetText()));
    }
    ClangProxy::ReparseJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangReparse, m_TranslUnitId, m_CompileCommand, file, unsavedFiles, true);
    m_Proxy.AppendPendingJob(job);
    m_ReparseNeeded = 0;
}

void ClangPlugin::OnEditorClose(CodeBlocksEvent& event)
{
    event.Skip();
    int translId = m_TranslUnitId;
    EditorManager* edm = Manager::Get()->GetEditorManager();
    if (!edm)
    {
        event.Skip();
        return;
    }
    cbEditor* ed = edm->GetBuiltinEditor(event.GetEditor());
    if (ed && ed->IsOK())
    {
        if (ed != m_pLastEditor)
        {
            ClangFile file(ed->GetFilename());
            if (ed->GetProjectFile())
                file = ClangFile(*ed->GetProjectFile());
            translId = m_Proxy.GetTranslationUnitId(m_TranslUnitId, file);
        }
    }
    ClangProxy::RemoveTranslationUnitJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangRemoveTU, translId);
    m_Proxy.AppendPendingJob(job);
    if (translId == m_TranslUnitId)
    {
        m_TranslUnitId = wxNOT_FOUND;
        m_ReparseNeeded = 0;
    }
}

/**
 * Called from C::B when a project is opened.
 * This is called after the project has been activated!
 */
void ClangPlugin::OnProjectOpen(CodeBlocksEvent& event)
{
    CCLogger::Get()->DebugLog( wxT("OnProjectOpen") );
    cbProject* pProj = event.GetProject();
    if (pProj)
    {
        CCLogger::Get()->DebugLog( wxT(" project=")+pProj->GetFilename() );
    }
    else
    {
        CCLogger::Get()->DebugLog( wxT(" No project!") );
    }
    return;
}

void ClangPlugin::OnProjectActivate(CodeBlocksEvent& event)
{
    event.Skip();
    return;
}

void ClangPlugin::OnProjectOptionsChanged(CodeBlocksEvent& event)
{
    CCLogger::Get()->DebugLog( wxT("OnProjectOptionsChanged") );
    event.Skip();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed && ed->IsOK())
    {
        m_compInclDirs.clear();
        m_CompileCommand.clear();
        int compileCommandChanged = UpdateCompileCommand(ed);
        if (compileCommandChanged)
        {
            FlushTranslationUnits();
        }
    }
}

void ClangPlugin::OnProjectTargetsModified(CodeBlocksEvent& event)
{
    CCLogger::Get()->DebugLog( wxT("OnProjectTargetsModified") );
    event.Skip();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed && ed->IsOK())
    {
        m_compInclDirs.clear();
        m_CompileCommand.clear();
        int compileCommandChanged = UpdateCompileCommand(ed);
        if (compileCommandChanged)
        {
            FlushTranslationUnits();
        }
    }
}


void ClangPlugin::OnProjectClose(CodeBlocksEvent& event)
{
    event.Skip();
    m_compInclDirs.clear();
    m_CompileCommand.clear();
}

void ClangPlugin::OnProjectFileChanged(CodeBlocksEvent& event)
{
    event.Skip();
    if (IsAttached())
    {
        m_compInclDirs.clear();
        m_CompileCommand.clear();
        RequestReparse(1);
    }
}

void ClangPlugin::OnTimer(wxTimerEvent& event)
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if ((!ed) || (m_TranslUnitId == wxNOT_FOUND))
        return;
    cbStyledTextCtrl* stc = ed->GetControl();
    if (stc->AutoCompActive())
        return;

    const int evId = event.GetId();
    if (evId == idReparseTimer)
    {
        ConfigManager* cfg = Manager::Get()->GetConfigManager(CLANG_CONFIGMANAGER);
        if (!cfg->ReadBool(_T("/while_typing")))
        {
            return;
        }
        if (m_ReparsingTranslUnitId == m_TranslUnitId)
            return;
        if (m_ReparseNeeded > 0)
        {
            int idle_time = cfg->ReadInt( wxT("/reparse_idle_time") );
            if ( (wxDateTime::Now() - ed->GetLastModificationTime() < idle_time) )
                return;
            ClangFile file(ed->GetFilename());
            if (ed->GetProjectFile())
                file = ClangFile(*ed->GetProjectFile());
            RequestReparse(m_TranslUnitId, file);
        }
    }
    else if (evId == idStoreIndexDBTimer)
    {
        if (m_StoreIndexDBJobCount)
        {
            CCLogger::Get()->DebugLog( wxT("Delay storing the Index DB") );
            m_StoreIndexDBTimer.Stop();
            m_StoreIndexDBTimer.Start( 1000, wxTIMER_ONE_SHOT);
            return;
        }
        std::set<std::string> projectFiles;
        m_Proxy.GetLoadedTokenIndexDatabases( projectFiles );
        for (std::set<std::string>::const_iterator it = projectFiles.begin(); it != projectFiles.end(); ++it)
        {
            ClTokenIndexDatabase* db = m_Proxy.GetTokenIndexDatabase( *it );
            if (db&&db->IsModified())
            {
                ClangProxy::StoreTokenIndexDBJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangStoreTokenIndexDB, *it);
                m_Proxy.AppendPendingJob( job );
                m_StoreIndexDBJobCount++;
            }
        }
    }
}

void ClangPlugin::OnCreateTranslationUnit(wxCommandEvent& event)
{
    event.Skip();

    if (m_TranslUnitId != wxNOT_FOUND)
    {
        return;
    }
    wxString filename = event.GetString();
    if (filename.IsEmpty())
        return;

    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (ed)
    {
        if (filename != ed->GetFilename())
            return;
        ClangFile file(filename);
        ProjectFile* pf = ed->GetProjectFile();
        if (pf)
        {
            file = ClangFile(*pf);
        }
        else
        {
            ProjectFile* pf = nullptr;
            cbProject* pProject = Manager::Get()->GetProjectManager()->FindProjectForFile(ed->GetFilename(), &pf, false, false);
            if (pf)
                file = ClangFile(*pf);
            else if (pProject)
                file = ClangFile(pProject, ed->GetFilename());
        }
        std::map<std::string, wxString> unsavedFiles;
        for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
        {
            ed = edMgr->GetBuiltinEditor(i);
            if (ed && ed->GetModified())
                unsavedFiles.insert(std::make_pair(ed->GetFilename().ToUTF8().data(), ed->GetControl()->GetText()));
        }
        ClangProxy::CreateTranslationUnitJob job( cbEVT_CLANG_ASYNCTASK_FINISHED, idClangCreateTU, file, m_CompileCommand, unsavedFiles );
        m_Proxy.AppendPendingJob(job);
    }
}

void ClangPlugin::AddCompilerInclDirs(const wxString& compId, std::vector<std::string>& inout_CompileCommands)
{
    std::map<wxString, std::vector<wxString> >::const_iterator idItr = m_compInclDirs.find(compId);
    if (idItr != m_compInclDirs.end())
    {
        for (std::vector<wxString>::const_iterator it = idItr->second.begin(); it != idItr->second.end(); ++it)
        {
            wxString cmd = wxT("-I")+*it;
            inout_CompileCommands.push_back( cmd.ToUTF8().data() );
        }
        return;
    }

    Compiler* comp = CompilerFactory::GetCompiler(compId);
    wxFileName fn(wxEmptyString, comp->GetPrograms().CPP);
    wxString masterPath = comp->GetMasterPath();
    Manager::Get()->GetMacrosManager()->ReplaceMacros(masterPath);
    fn.SetPath(masterPath);
    if (!fn.FileExists())
        fn.AppendDir(wxT("bin"));
#ifdef __WXMSW__
    wxString command = fn.GetFullPath() + wxT(" -v -E -x c++ nul");
#else
    wxString command = fn.GetFullPath() + wxT(" -v -E -x c++ /dev/null");
#endif // __WXMSW__
    wxArrayString output, errors;
    wxExecute(command, output, errors, wxEXEC_NODISABLE);

    wxArrayString::const_iterator errItr = errors.begin();
    for (; errItr != errors.end(); ++errItr)
    {
        if (errItr->IsSameAs(wxT("#include <...> search starts here:")))
        {
            ++errItr;
            break;
        }
    }
    std::vector<wxString> includeDirs;
    for (; errItr != errors.end(); ++errItr)
    {
        if (errItr->IsSameAs(wxT("End of search list.")))
            break;
        wxString dir = errItr->Strip(wxString::both);
        dir.Replace(wxT("\""), wxT(""), true);
        includeDirs.push_back(dir);
        wxString cmd = wxT("-I")+dir+wxT("\"");
        inout_CompileCommands.push_back(cmd.ToUTF8().data());
    }
    m_compInclDirs.insert(std::pair<wxString, std::vector<wxString> >(compId, includeDirs));
}
#if 0
wxString ClangPlugin::GetSourceOf(cbEditor* ed)
{
    cbProject* project = nullptr;
    ProjectFile* opf = ed->GetProjectFile();
    if (opf)
        project = opf->GetParentProject();
    if (!project)
        project = Manager::Get()->GetProjectManager()->GetActiveProject();

    wxFileName theFile(ed->GetFilename());
    wxFileName candidateFile;
    bool isCandidate;
    wxArrayString fileArray;
    wxDir::GetAllFiles(theFile.GetPath(wxPATH_GET_VOLUME), &fileArray,
                       theFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
    wxFileName currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
    if (isCandidate)
        candidateFile = currentCandidateFile;
    else if (currentCandidateFile.IsOk())
        return currentCandidateFile.GetFullPath();

    fileArray.Clear();
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
    {
        cbEditor* edit = edMgr->GetBuiltinEditor(i);
        if (!edit)
            continue;

        ProjectFile* pf = edit->GetProjectFile();
        if (!pf)
            continue;

        fileArray.Add(pf->file.GetFullPath());
    }
    currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
    if (!isCandidate && currentCandidateFile.IsOk())
        return currentCandidateFile.GetFullPath();

    if (project)
    {
        fileArray.Clear();
        for (FilesList::const_iterator it = project->GetFilesList().begin();
                it != project->GetFilesList().end(); ++it)
        {
            ProjectFile* pf = *it;
            if (!pf)
                continue;

            fileArray.Add(pf->file.GetFullPath());
        }
        currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
        if (isCandidate && !candidateFile.IsOk())
            candidateFile = currentCandidateFile;
        else if (currentCandidateFile.IsOk())
            return currentCandidateFile.GetFullPath();

        wxArrayString dirs = project->GetIncludeDirs();
        for (int i = 0; i < project->GetBuildTargetsCount(); ++i)
        {
            ProjectBuildTarget* target = project->GetBuildTarget(i);
            if (target)
            {
                for (size_t ti = 0; ti < target->GetIncludeDirs().GetCount(); ++ti)
                {
                    wxString dir = target->GetIncludeDirs()[ti];
                    if (dirs.Index(dir) == wxNOT_FOUND)
                        dirs.Add(dir);
                }
            }
        }
        for (size_t i = 0; i < dirs.GetCount(); ++i)
        {
            wxString dir = dirs[i];
            Manager::Get()->GetMacrosManager()->ReplaceMacros(dir);
            wxFileName dname(dir);
            if (!dname.IsAbsolute())
                dname.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE, project->GetBasePath());
            fileArray.Clear();
            wxDir::GetAllFiles(dname.GetPath(), &fileArray, theFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
            currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
            if (isCandidate)
                candidateFile = currentCandidateFile;
            else if (currentCandidateFile.IsOk())
                return currentCandidateFile.GetFullPath();
        }
    }
    if (candidateFile.IsOk())
        return candidateFile.GetFullPath();
    return wxEmptyString;
}

wxFileName ClangPlugin::FindSourceIn(const wxArrayString& candidateFilesArray,
                                     const wxFileName& activeFile, bool& isCandidate)
{
    bool extStartsWithCapital = wxIsupper(activeFile.GetExt()[0]);
    wxFileName candidateFile;
    for (size_t i = 0; i < candidateFilesArray.GetCount(); ++i)
    {
        wxFileName currentCandidateFile(candidateFilesArray[i]);
        if (IsSourceOf(currentCandidateFile, activeFile, isCandidate))
        {
            bool isUpper = wxIsupper(currentCandidateFile.GetExt()[0]);
            if (isUpper == extStartsWithCapital && !isCandidate)
                return currentCandidateFile;
            else
                candidateFile = currentCandidateFile;
        }
    }
    isCandidate = true;
    return candidateFile;
}

bool ClangPlugin::IsSourceOf(const wxFileName& candidateFile,
                             const wxFileName& activeFile, bool& isCandidate)
{
    if (candidateFile.GetName().CmpNoCase(activeFile.GetName()) == 0)
    {
        isCandidate = (candidateFile.GetName() != activeFile.GetName());
        if (FileTypeOf(candidateFile.GetFullName()) == ftSource)
        {
            if (candidateFile.GetPath() != activeFile.GetPath())
            {
                wxArrayString fileArray;
                wxDir::GetAllFiles(candidateFile.GetPath(wxPATH_GET_VOLUME), &fileArray,
                                   candidateFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
                for (size_t i = 0; i < fileArray.GetCount(); ++i)
                    if (wxFileName(fileArray[i]).GetFullName() == activeFile.GetFullName())
                        return false;
            }
            return candidateFile.FileExists();
        }
    }
    return false;
}
#endif

std::vector<std::string> ClangPlugin::GetCompileCommand(ProjectFile* pf, const wxString& filename)
{
    std::vector<std::string> compileCommand;
    m_UpdateCompileCommand++;
    if (m_UpdateCompileCommand > 1)
    {
        CCLogger::Get()->DebugLog( wxT("GetCompileCommand: skipping re-entry") );
        // Re-entry is not allowed
        m_UpdateCompileCommand--;
        return std::vector<std::string>();
    }
    ProjectBuildTarget* target = nullptr;
    Compiler* comp = nullptr;

    if (!pf)
    {
        Manager::Get()->GetProjectManager()->FindProjectForFile( filename, &pf, false, false );
    }
    cbProject* proj = (pf ? pf->GetParentProject() : nullptr);
    if (pf)
    {
        if (proj)
            target = proj->GetBuildTarget( proj->GetActiveBuildTarget() );
        if (target && (m_ProjectSettingsMap.find( target ) != m_ProjectSettingsMap.end())&&(m_ProjectSettingsMap[target].compileCommandSource == ProjectSetting::CompileCommandSource_jsonFile) )
        {
            m_UpdateCompileCommand--;
            wxString path = pf->file.GetPath();
            CXCompilationDatabase_Error err;
            CXCompilationDatabase db = clang_CompilationDatabase_fromDirectory(path.utf8_str(), &err);
            if (!db)
            {
                if (proj)
                {
                    path = proj->GetCommonTopLevelPath();
                    db = clang_CompilationDatabase_fromDirectory(path.utf8_str(), &err);
                }
            }
            if (!db)
            {
                Manager::Get()->GetLogManager()->LogError( wxT("Compilation database \"compile_commands.json\" not found in path \"")+path+wxT("\"") );
                return std::vector<std::string>();
            }
            CXCompileCommands lst = clang_CompilationDatabase_getCompileCommands(db, filename.utf8_str());
            if (clang_CompileCommands_getSize(lst) < 1)
            {
                Manager::Get()->GetLogManager()->LogError( wxT("Lookup of the compile command for ")+filename+wxT(" failed") );
                return std::vector<std::string>();
            }
            CXCompileCommand cmd = clang_CompileCommands_getCommand(lst,0);
            for (unsigned i = 1; i<clang_CompileCommand_getNumArgs(cmd); ++i)
            {
                CXString str = clang_CompileCommand_getArg(cmd,i);
                wxString cmd = wxString::FromUTF8( clang_getCString( str ) );
                clang_disposeString( str );
                cmd.Trim(wxString::both);
                if (cmd.Length() == 0)
                {
                    continue;
                }
                if (cmd == wxT("-c"))
                {
                    ++i; // skip argument too
                    continue;
                }
                if (cmd == wxT("-x"))
                {
                    ++i; // skip argument too
                    continue;
                }
                cmd.Replace(wxT("\""), wxT(""), true);
                compileCommand.push_back( cmd.ToUTF8().data() );

            }
            return compileCommand;
        }
        if (proj && !pf->GetBuildTargets().IsEmpty())
        {
            target = proj->GetBuildTarget(pf->GetBuildTargets()[0]);
            if (target)
                comp = CompilerFactory::GetCompiler(target->GetCompilerID());
        }
    }
    if ( (!comp) && proj)
        comp = CompilerFactory::GetCompiler(proj->GetCompilerID());
    if (!comp)
    {
        cbProject* tmpPrj = Manager::Get()->GetProjectManager()->GetActiveProject();
        if (tmpPrj)
            comp = CompilerFactory::GetCompiler(tmpPrj->GetCompilerID());
        if (!proj)
            proj = tmpPrj;
    }
    if (!comp)
    {
        comp = CompilerFactory::GetDefaultCompiler();
        if (!proj)
        {
            proj = Manager::Get()->GetProjectManager()->GetActiveProject();
        }
    }

    wxString compileCommandStr;
    if (pf && proj && (!pf->GetBuildTargets().IsEmpty()) )
    {
        target = proj->GetBuildTarget(pf->GetBuildTargets()[0]);

        if (pf->GetUseCustomBuildCommand(target->GetCompilerID() ))
            compileCommandStr = pf->GetCustomBuildCommand(target->GetCompilerID()).AfterFirst(wxT(' '));
    }

    if (compileCommandStr.IsEmpty())
        compileCommandStr = wxT("$options $includes");
    CompilerCommandGenerator* gen = NULL;
    if (proj)
        gen = comp->GetCommandGenerator(proj);
    if (gen)
        gen->GenerateCommandLine(compileCommandStr, target, pf, filename,
                                 g_InvalidStr, g_InvalidStr, g_InvalidStr );
    delete gen;

    wxStringTokenizer tokenizer(compileCommandStr);

    wxString pathStr;
    while (tokenizer.HasMoreTokens())
    {
        wxString flag = tokenizer.GetNextToken();
        // make all include paths absolute, so clang does not choke if Code::Blocks switches directories
        if (flag.StartsWith(wxT("-I"), &pathStr))
        {
            if (pathStr.StartsWith(wxT("\""), NULL))
            {
                pathStr = pathStr.Mid(1);
            }
            if (pathStr.EndsWith(wxT("\""), NULL))
            {
                pathStr = pathStr.RemoveLast(1);
            }
            wxFileName path(pathStr);
            if (path.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE))
                flag = wxT("-I") + path.GetFullPath();
        }
        compileCommand.push_back(flag.ToUTF8().data());
    }
    AddCompilerInclDirs(comp->GetID(), compileCommand);

    ConfigManager* cfg = Manager::Get()->GetConfigManager(CLANG_CONFIGMANAGER);

    if (cfg->ReadBool( _T("/cmdoption_wnoattributes") ))
        compileCommand.push_back( "-Wno-attributes " );
    else
        compileCommand.push_back( "-Wattributes " );

    if (cfg->ReadBool( _T("/cmdoption_wextratokens") ))
        compileCommand.push_back( "-Wextra-tokens " );
    else
        compileCommand.push_back( "-Wno-extra-tokens " );

    if (cfg->ReadBool( _T("/cmdoption_fparseallcomments") ))
        compileCommand.push_back( "-fparse-all-comments " );

    wxString extraOptions = cfg->Read(_T("/cmdoption_extra"));
    if (extraOptions.length() > 0)
    {
        extraOptions.Replace( wxT("\r"), wxT("\n") );
        wxStringTokenizer extraTokenizer(extraOptions, wxT("\n"));
        while (extraTokenizer.HasMoreTokens())
        {
            wxString opt = extraTokenizer.NextToken();
            opt.Trim(wxString::both);
            if (opt.Length() > 0)
                compileCommand.push_back( opt.ToUTF8().data() );
        }
    }
    m_UpdateCompileCommand--;
    return compileCommand;
}

/** \brief Update the cached compile command from CodeBlocks and ClangLib settings
 * Don't call this function from within the scope of:
 *      ClangPlugin::OnEditorHook
 *      ClangPlugin::OnTimer
 *
 * \param ed cbEditor*
 * \return 0 when the compile command has not changed or if another updateCompileCommand is busy (reentry)
 * \return not 0 when the compile command has changed
 *
 */
int ClangPlugin::UpdateCompileCommand(cbEditor* ed)
{
    std::vector<std::string> compileCommand = GetCompileCommand( ed->GetProjectFile(), ed->GetFilename() );

    if (compileCommand.empty())
        return 0;

    if (compileCommand != m_CompileCommand)
    {
        m_CompileCommand = compileCommand;
        return 1;
    }
    return 0;
}

/** \brief Event handler called when the Clang thread has finished creating a Translation Unit
 *
 * \param event wxEvent&
 *
 */
void ClangPlugin::OnClangCreateTUFinished( wxEvent& event )
{
    event.Skip();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;

    ClangProxy::CreateTranslationUnitJob* pJob = static_cast<ClangProxy::CreateTranslationUnitJob*>(event.GetEventObject());
    if (!pJob)
    {
        CCLogger::Get()->DebugLog( wxT("Wrong event detected") );
        return;
    }
    if (m_TranslUnitId == pJob->GetTranslationUnitId())
    {
        //CCLogger::Get()->DebugLog( _T("FIXME: Double OnClangCreateTUFinished detected") );
        return;
    }
    ClangProxy::UpdateTokenDatabaseJob updateDbJob(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangUpdateTokenDatabase, pJob->GetTranslationUnitId());
    m_Proxy.AppendPendingJob(updateDbJob);
    ClangEvent evt(clEVT_TRANSLATIONUNIT_CREATED, pJob->GetTranslationUnitId(), pJob->GetFile());
    evt.SetStartedTime( pJob->GetTimestamp() );
    ProcessEvent(evt);
    ClangEvent evt2(clEVT_REPARSE_FINISHED, pJob->GetTranslationUnitId(), pJob->GetFile());
    evt2.SetStartedTime( pJob->GetTimestamp() );
    ProcessEvent(evt2);
    if ( pJob->GetFile().GetFilename() != ed->GetFilename())
        return;

    m_TranslUnitId = pJob->GetTranslationUnitId();
    m_pLastEditor = ed;
}

void ClangPlugin::OnClangReparseFinished( wxEvent& event )
{
    event.Skip();
    m_ReparsingTranslUnitId = wxNOT_FOUND;
    bool bUpdate = true;
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (ed)
    {
        cbStyledTextCtrl* stc = ed->GetControl();
        if (stc&&stc->AutoCompActive())
            bUpdate = false;
    }

    ClangProxy::ReparseJob* pJob = static_cast<ClangProxy::ReparseJob*>(event.GetEventObject());
    if (bUpdate)
    {
        if (HasEventSink(clEVT_DIAGNOSTICS_UPDATED))
        {
            ClangProxy::GetDiagnosticsJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangGetDiagnostics, pJob->GetTranslationUnitId(), pJob->GetFile());
            m_Proxy.AppendPendingJob(job);
        }
        ClangProxy::UpdateTokenDatabaseJob updateDbJob(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangUpdateTokenDatabase, pJob->GetTranslationUnitId());
        m_Proxy.AppendPendingJob(updateDbJob);
    }
    ClangEvent evt(clEVT_REPARSE_FINISHED, pJob->GetTranslationUnitId(), pJob->GetFile());
    evt.SetStartedTime(pJob->GetTimestamp());
    ProcessEvent(evt);
}

void ClangPlugin::OnClangUpdateTokenDatabaseFinished(wxEvent& event)
{
    event.Skip();
    ClangProxy::UpdateTokenDatabaseJob* pJob = static_cast<ClangProxy::UpdateTokenDatabaseJob*>(event.GetEventObject());

    ClangEvent evt( clEVT_TOKENDATABASE_UPDATED, pJob->GetTranslationUnitId(),ClangFile(wxT("")) );
    evt.SetStartedTime(pJob->GetTimestamp());
    ProcessEvent(evt);
}

void ClangPlugin::OnEditorHook(cbEditor* ed, wxScintillaEvent& event)
{
    event.Skip();

    if (!IsProviderFor(ed))
        return;
    if (event.GetEventType() == wxEVT_SCI_MARGINCLICK)
    {
        m_Diagnostics.OnMarginClicked(ed, event);
        return;
    }
    if (m_ReparseTimer.IsRunning()&&(m_ReparseNeeded > 0))
    {
        m_ReparseTimer.Stop();
        RequestReparse();
        return;
    }
    if (event.GetModificationType() & (wxSCI_MOD_INSERTTEXT | wxSCI_MOD_DELETETEXT))
    {
        RequestReparse();
    }
}

void ClangPlugin::OnProjectLoadingHook(cbProject* project, TiXmlElement* elem, bool loading)
{
    ProjectSettingMap settmap = m_ProjectSettingsMap;
    if (loading)
    {
        settmap.clear();

        // Hook called when loading project file.
        TiXmlElement* conf = elem->FirstChildElement("LibClang");
        if (conf)
        {
            TiXmlElement* tElem = conf->FirstChildElement("TargetSettings");
            while (tElem)
            {
                wxString targetName = cbC2U(tElem->Attribute("name"));
                ProjectBuildTarget* bt = project->GetBuildTarget(targetName);
                ProjectSetting sett;
                TiXmlElement* opt = tElem->FirstChildElement("Options");
                while (opt)
                {
                    if (opt->Attribute("compile_command_source"))
                        sett.compileCommandSource = (ProjectSetting::CompileCommandSource)atol( opt->Attribute("compile_command_source") );

                    opt = opt->NextSiblingElement("Options");
                }
                settmap.insert(std::make_pair(bt, sett));

                tElem = tElem->NextSiblingElement("TargetSettings");
            }
        }
        m_ProjectSettingsMap = settmap;
    }
    else
    {
        // Hook called when saving project file.

        TiXmlElement* node = elem->FirstChildElement("LibClang");
        if (!node)
            node = elem->InsertEndChild(TiXmlElement("LibClang"))->ToElement();
        node->Clear();

        if (settmap.size())
        {
            for (ProjectSettingMap::iterator it = settmap.begin(); it != settmap.end(); ++it)
            {
                if (it->first)
                {
                    ProjectSetting& sett = it->second;
                    TiXmlElement* tnode = node->InsertEndChild(TiXmlElement("TargetSettings"))->ToElement();
                    tnode->SetAttribute("name", cbU2C(it->first->GetTitle()));
                    TiXmlElement* optnode = tnode->InsertEndChild(TiXmlElement("Options"))->ToElement();
                    optnode->SetAttribute("compile_command_source", (int)sett.compileCommandSource);
                }
            }
        }
    }
}


void ClangPlugin::OnClangGetDiagnosticsFinished( wxEvent& event )
{
    event.Skip();

    ClangProxy::GetDiagnosticsJob* pJob = static_cast<ClangProxy::GetDiagnosticsJob*>(event.GetEventObject());

    ClangEvent evt(clEVT_DIAGNOSTICS_UPDATED, pJob->GetTranslationUnitId(), pJob->GetFile(), ClTokenPosition(0,0), pJob->GetResults());
    evt.SetStartedTime(pJob->GetTimestamp());
    ProcessEvent(evt);
}

void ClangPlugin::OnClangGetOccurrencesFinished(wxEvent& event)
{
    event.Skip();

    ClangProxy::GetOccurrencesOfJob* pJob = dynamic_cast<ClangProxy::GetOccurrencesOfJob*>(event.GetEventObject());
    ClangEvent evt( clEVT_GETOCCURRENCES_FINISHED, pJob->GetTranslationUnitId(), pJob->GetFile(), pJob->GetPosition(), pJob->GetResults());
    evt.SetStartedTime(pJob->GetTimestamp());
    ProcessEvent(evt);
}

void ClangPlugin::OnClangSyncTaskFinished(wxEvent& event)
{
    event.Skip();
    ClangProxy::SyncJob* pJob = static_cast<ClangProxy::SyncJob*>(event.GetEventObject());

    if (event.GetId() == idClangCodeComplete)
    {
        ClangProxy::CodeCompleteAtJob* pCCJob = dynamic_cast<ClangProxy::CodeCompleteAtJob*>(pJob);
        if ((!m_pOutstandingCodeCompletion)||(*pCCJob == *m_pOutstandingCodeCompletion))
        {
            ClangEvent evt( clEVT_GETCODECOMPLETE_FINISHED, pCCJob->GetTranslationUnitId(), pCCJob->GetFile(), pCCJob->GetPosition(), pCCJob->GetResults());
            evt.SetStartedTime(pJob->GetTimestamp());
            ProcessEvent(evt);
            delete m_pOutstandingCodeCompletion;
            m_pOutstandingCodeCompletion = nullptr;
        }
    }
    else if (event.GetId() == idClangGetCCDocumentation)
    {
        //ClangProxy::DocumentCCTokenJob* pCCDocJob = dynamic_cast<ClangProxy::DocumentCCTokenJob*>(pJob);
        //ClangEvent evt( clEVT_GETOCCURRENCES_FINISHED, pCCDocJob->GetTranslationUnitId(), pCCDocJob->GetFilename(), pCCDocJob->GetLocation(), pCCDocJob->GetResult());
        //evt.SetStartedTime(pJob->GetTimestamp());
        //ProcessEvent(evt);
    }

    pJob->Finalize();
}

void ClangPlugin::OnClangReindexFinished(wxEvent& event)
{
    event.Skip();
    ClangProxy::ReindexFileJob* pJob = static_cast<ClangProxy::ReindexFileJob*>(event.GetEventObject());
    RequestStoreTokenIndexDB();
    if (HasEventSink( clEVT_REINDEXFILE_FINISHED ))
    {
        ClangEvent clEvt(clEVT_REINDEXFILE_FINISHED, wxID_ANY, pJob->GetFile());
        clEvt.SetString( wxString::FromUTF8(pJob->GetFilename().c_str()) );
        ProcessEvent( clEvt );
    }
}

bool ClangPlugin::IsProviderFor(cbEditor* ed)
{
    return cbCodeCompletionPlugin::IsProviderFor(ed);
}

void ClangPlugin::RequestReparse(int millisecs)
{
    if (millisecs == 0)
    {
        ConfigManager* cfg = Manager::Get()->GetConfigManager(CLANG_CONFIGMANAGER);
        millisecs = cfg->ReadInt( wxT("/reparse_idle_time") ) * 1000;
        if (millisecs < 1000)
            millisecs = 10000;
    }
    m_ReparseNeeded++;
    m_ReparseTimer.Stop();
    m_ReparseTimer.Start( millisecs, wxTIMER_ONE_SHOT);
    if (m_StoreIndexDBTimer.IsRunning())
    {
        RequestStoreTokenIndexDB();
    }
}

void ClangPlugin::RequestStoreTokenIndexDB()
{
    if (m_StoreIndexDBTimer.IsRunning())
    {
        m_StoreIndexDBTimer.Stop();
    }
    m_StoreIndexDBTimer.Start( 60000, wxTIMER_ONE_SHOT);
}

void ClangPlugin::FlushTranslationUnits()
{
    std::set<ClTranslUnitId> l;
    m_Proxy.GetAllTranslationUnitIds( l );
    for (std::set<ClTranslUnitId>::const_iterator it = l.begin(); it != l.end(); ++it)
    {
        ClangProxy::RemoveTranslationUnitJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangRemoveTU, *it);
        m_Proxy.AppendPendingJob(job);
    }
    m_TranslUnitId = wxNOT_FOUND;
    m_ReparseNeeded = 0;

    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (ed && ed->IsOK())
    {
        wxCommandEvent evt(cbEVT_COMMAND_CREATETU, idCreateTU);
        evt.SetString(ed->GetFilename());
        AddPendingEvent(evt);
    }
}

wxDateTime ClangPlugin::GetFileIndexingTimestamp(const ClangFile& file)
{
    ClTokenIndexDatabase* pDb = m_Proxy.GetTokenIndexDatabase(file.GetProject().ToUTF8().data());
    if (!pDb)
    {
        pDb = m_Proxy.LoadTokenIndexDatabase( file.GetProject().ToUTF8().data() );
    }
    if (!pDb)
    {
        return wxDateTime((time_t)0);
    }
    return pDb->GetFilenameTimestamp( pDb->GetFilenameId(file.GetFilename().ToUTF8().data()) );
}

void ClangPlugin::BeginReindexFile(const ClangFile& file)
{
    std::vector<std::string> compileCommand = GetCompileCommand( NULL, file.GetFilename() );
    ClangProxy::ReindexFileJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangReindex, file, compileCommand);
    m_Proxy.AppendPendingJob( job );
}

ClTranslUnitId ClangPlugin::GetTranslationUnitId(const ClangFile& file)
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (ed && ed->IsOK())
    {
        if ((ed->GetFilename() == file.GetFilename())&&(m_TranslUnitId != wxNOT_FOUND))
            return m_TranslUnitId;
    }
    return m_Proxy.GetTranslationUnitId(m_TranslUnitId, file);
}

void ClangPlugin::GetTokenScopes(const ClTranslUnitId id, const ClangFile& file, unsigned int tokenMask, std::vector<ClTokenScope>& out_Scopes)
{
    m_Proxy.ResolveTokenScopes(id, file.GetProject().ToUTF8().data(), file.GetFilename().ToUTF8().data(), tokenMask, out_Scopes);
}

void ClangPlugin::RequestOccurrencesOf(const ClTranslUnitId translUnitId, const ClangFile& file, const ClTokenPosition& loc)
{
    if ((translUnitId == m_TranslUnitId)&&(m_ReparseNeeded > 0))
    {
        RequestReparse( translUnitId, file );
    }
    ClangProxy::GetOccurrencesOfJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangGetOccurrences, file, loc, translUnitId);
    m_Proxy.AppendPendingJob(job);
}

wxCondError ClangPlugin::GetCodeCompletionAt(const ClTranslUnitId translUnitId, const ClangFile& file, const ClTokenPosition& loc,
                                             unsigned long timeout, const ClCodeCompleteOption complete_options, std::vector<ClToken>& out_tknResults)
{
    CCLogger::Get()->DebugLog(F(wxT("GetCodeCompletionAt %d,%d"), loc.line, loc.column));

    if ( m_pOutstandingCodeCompletion)
    {
        if (  (m_pOutstandingCodeCompletion->GetTranslationUnitId() == translUnitId)
            &&(m_pOutstandingCodeCompletion->GetFile() == file)
            &&(m_pOutstandingCodeCompletion->GetPosition() == loc)
           )
        {
            // duplicate request
            return wxCOND_TIMEOUT;
        }
        delete m_pOutstandingCodeCompletion;
        m_pOutstandingCodeCompletion = nullptr;
    }

    std::map<std::string, wxString> unsavedFiles;
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
    {
        cbEditor* ed = edMgr->GetBuiltinEditor(i);
        if (ed && ed->GetModified())
            unsavedFiles.insert(std::make_pair(ed->GetFilename().ToUTF8().data(), ed->GetControl()->GetText()));
    }
    m_pOutstandingCodeCompletion = new ClangProxy::CodeCompleteAtJob(cbEVT_CLANG_SYNCTASK_FINISHED, idClangCodeComplete, file, loc, translUnitId, unsavedFiles, complete_options);
    m_Proxy.AppendPendingJob(*m_pOutstandingCodeCompletion);
    if( timeout == 0 )
        return wxCOND_TIMEOUT;
    if (wxCOND_TIMEOUT == m_pOutstandingCodeCompletion->WaitCompletion(timeout))
        return wxCOND_TIMEOUT;
    out_tknResults = m_pOutstandingCodeCompletion->GetResults();

    delete m_pOutstandingCodeCompletion;
    m_pOutstandingCodeCompletion = nullptr;

    return wxCOND_NO_ERROR;
}

wxString ClangPlugin::GetCodeCompletionTokenDocumentation(const ClTranslUnitId id, const ClangFile& file, const ClTokenPosition& location, const ClTokenId tokenId)
{
    if (id < 0)
        return wxEmptyString;
    ClangProxy::DocumentCCTokenJob job(cbEVT_CLANG_SYNCTASK_FINISHED, idClangGetCCDocumentation, id, file, location, tokenId);
    m_Proxy.AppendPendingJob(job);
    if (wxCOND_TIMEOUT == job.WaitCompletion(40))
    {
        return wxEmptyString;
    }
    return job.GetResult();
}

wxString ClangPlugin::GetCodeCompletionInsertSuffix(const ClTranslUnitId translId, int tknId, const wxString& newLine, std::vector< std::pair<int, int> >& offsets)
{
    return m_Proxy.GetCCInsertSuffix(translId, tknId, false, newLine, offsets);
}

void ClangPlugin::RequestTokenDefinitionsAt(const ClTranslUnitId id, const ClangFile& file, const ClTokenPosition& loc)
{
    ClangProxy::LookupDefinitionJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangLookupDefinition, id, file, loc);
    m_Proxy.AppendPendingJob( job );
}

void ClangPlugin::OnClangLookupDefinitionFinished(wxEvent& event)
{
    event.Skip();

    ClangProxy::LookupDefinitionJob* pJob = static_cast<ClangProxy::LookupDefinitionJob*>(event.GetEventObject());
    std::vector< std::pair<std::string, ClTokenPosition> > Locations = pJob->GetResults();
    if (Locations.empty())
    {
        ClTokenIndexDatabase* db = m_Proxy.GetTokenIndexDatabase( pJob->GetProject() );
        if (!db)
            return;
        // Perform the request again, but now with loading of TU's so we need a compile command for that
        std::set<ClFileId> fileIdList = db->LookupTokenFileList( pJob->GetTokenIdentifier(), pJob->GetTokenUSR(), ClTokenType_DefGroup );
        for (std::set<ClFileId>::const_iterator it = fileIdList.begin(); it != fileIdList.end(); ++it)
        {
            ClTokenPosition pos(0,0);
            if (db->LookupTokenPosition( pJob->GetTokenIdentifier(), *it, pJob->GetTokenUSR(), ClTokenType_DefGroup, pos ) )
            {
                std::string fn = db->GetFilename( *it );
                Locations.push_back( std::make_pair( fn, pos ) );
            }
        }

        if (Locations.empty())
        {
            if (dynamic_cast<ClangProxy::LookupDefinitionInFilesJob*>(pJob)== nullptr)
            {
                std::vector< std::pair<std::string, std::vector<std::string> > > fileAndCompileCommands;
                EditorManager* edMgr = Manager::Get()->GetEditorManager();
                for (std::set<ClFileId>::const_iterator it = fileIdList.begin(); it != fileIdList.end(); ++it)
                {
                    std::string filename = db->GetFilename(*it);
                    std::vector<std::string> compileCommand;
                    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
                    {
                        cbEditor* ed = edMgr->GetBuiltinEditor(i);
                        if ( ed->GetFilename() == wxString::FromUTF8( filename.c_str() ) )
                        {
                            compileCommand = GetCompileCommand( ed->GetProjectFile(), ed->GetFilename() );
                            break;
                        }
                    }
                    fileAndCompileCommands.push_back( std::make_pair(filename,compileCommand) );
                }
                ClangProxy::LookupDefinitionInFilesJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangLookupDefinition, pJob->GetTranslationUnitId(), pJob->GetFile(), pJob->GetPosition(), fileAndCompileCommands);
                m_Proxy.AppendPendingJob( job );
                return;
            }
        }
    }

    ClangEvent evt(clEVT_GETDEFINITION_FINISHED, pJob->GetTranslationUnitId(), pJob->GetFile(), pJob->GetPosition(), Locations);
    evt.SetStartedTime(pJob->GetTimestamp());
    ProcessEvent(evt);
}

void ClangPlugin::OnClangStoreTokenIndexDB(wxEvent& /*event*/)
{
    if (m_StoreIndexDBJobCount)
        m_StoreIndexDBJobCount--;
}


bool ClangPlugin::ResolveTokenDeclarationAt(const ClTranslUnitId id, const ClangFile& file, const ClTokenPosition& loc, ClangFile& out_file, ClTokenPosition& out_loc)
{
    std::string filename = file.GetFilename().ToUTF8().data();
    if (m_Proxy.ResolveTokenDeclarationAt( id, filename, loc, out_loc ))
    {
        out_file = ClangFile(file, wxString::FromUTF8(filename.c_str()));
        return true;
    }
    return false;
}


void ClangPlugin::RequestReparse(const ClTranslUnitId translUnitId, const ClangFile& file)
{
    CCLogger::Get()->DebugLog(F(_T("RequestReparse %d ")+file.GetFilename(), translUnitId));
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (!ed)
        return;
    if (translUnitId == wxNOT_FOUND)
    {
        return;
    }
    if (translUnitId == m_TranslUnitId)
    {
        m_ReparseNeeded = 0;
        // RequestReparse can also be called from other contexts than reparse timer, so at this point, the timer should not fire anymore since something was faster than the timer
        m_ReparseTimer.Stop();
    }

    std::map<std::string, wxString> unsavedFiles;
    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
    {
        ed = edMgr->GetBuiltinEditor(i);
        if (ed && ed->GetModified())
            unsavedFiles.insert(std::make_pair(ed->GetFilename().ToUTF8().data(), ed->GetControl()->GetText()));
    }
    ClangProxy::ReparseJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangReparse, translUnitId, m_CompileCommand, file, unsavedFiles);
    m_Proxy.AppendPendingJob(job);
    m_ReparsingTranslUnitId = translUnitId;
}

void ClangPlugin::RequestTokenReferenceScopesAt(const ClTranslUnitId id, const ClangFile& file, const ClTokenPosition loc)
{
    ClangProxy::LookupTokenReferencesAtJob job(cbEVT_CLANG_ASYNCTASK_FINISHED, idClangLookupTokenReferences, id, file, loc);
    m_Proxy.AppendPendingJob( job );
}

void ClangPlugin::OnClangLookupTokenReferencesFinished(wxEvent& event)
{
    event.Skip();

    std::vector<ClTokenReference> refs;
    ClangProxy::LookupTokenReferencesAtJob* pJob = static_cast<ClangProxy::LookupTokenReferencesAtJob*>(event.GetEventObject());
    wxString tokenIdentifier = wxString::FromUTF8( pJob->GetTokenIdentifier().c_str() );
    wxString tokenDisplayName = pJob->GetTokenDisplayName();
    wxString tokenScopePath = pJob->GetTokenScopePath();

    std::set<ClangProxy::LookupTokenReferencesAtJob::TokenRef> Refs = pJob->GetResults();
    for (std::set<ClangProxy::LookupTokenReferencesAtJob::TokenRef>::const_iterator it = Refs.begin(); it != Refs.end(); ++it)
    {
        ClTokenScope scope;
        if (m_Proxy.ResolveTokenScopeAt(pJob->GetTranslationUnitId(), pJob->GetProject(), it->Filename, it->Range.beginLocation, scope))
        {
            ClTokenReference scopeRef( tokenIdentifier, tokenDisplayName, tokenScopePath, it->Range, it->Category, ClangFile(wxString::FromUTF8( pJob->GetProject().c_str()), wxString::FromUTF8( it->Filename.c_str() )), scope, it->Type );
            if (std::find( refs.begin(), refs.end(), scopeRef ) == refs.end())
            {
                refs.push_back( scopeRef );
            }
        }
    }
    ClangEvent evt(clEVT_GETREFERENCESCOPES_FINISHED, pJob->GetTranslationUnitId(), pJob->GetFile(), pJob->GetPosition(), refs);
    evt.SetStartedTime(pJob->GetTimestamp());
    ProcessEvent(evt);
}

void ClangPlugin::GetFileAndProject( ProjectFile& pf, wxString& out_project, wxString& out_filename)
{
    out_filename = pf.file.GetFullPath();
    out_project = wxT("");
    cbProject* proj = pf.GetParentProject();
    if (proj)
    {
        out_project = proj->GetFilename();
    }
}


void ClangPlugin::RegisterEventSink(const wxEventType eventType, IEventFunctorBase<ClangEvent>* functor)
{
    m_EventSinks[eventType].push_back(functor);
}

void ClangPlugin::RemoveAllEventSinksFor(void* owner)
{
    for (EventSinksMap::iterator mit = m_EventSinks.begin(); mit != m_EventSinks.end(); ++mit)
    {
        EventSinksArray::iterator it = mit->second.begin();
        bool endIsInvalid = false;
        while (!endIsInvalid && it != mit->second.end())
        {
            if ((*it) && (*it)->GetThis() == owner)
            {
                EventSinksArray::iterator it2 = it++;
                endIsInvalid = it == mit->second.end();
                delete (*it2);
                mit->second.erase(it2);
            }
            else
                ++it;
        }
    }
}

bool ClangPlugin::HasEventSink(wxEventType eventType)
{
    return (m_EventSinks.count(eventType) > 0);
}

bool ClangPlugin::ProcessEvent(ClangEvent& event)
{
    int id = event.GetId();
    EventSinksMap::iterator mit = m_EventSinks.find(id);
    if (mit != m_EventSinks.end())
    {
        for (EventSinksArray::iterator it = mit->second.begin(); it != mit->second.end(); ++it)
        {
            (*it)->Call(event);
        }
    }
    return true;
}

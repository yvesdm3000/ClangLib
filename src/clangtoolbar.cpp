#include "clangtoolbar.h"
#include <cbcolourmanager.h>
#include <cbstyledtextctrl.h>
#include <compilercommandgenerator.h>
#include "cclogger.h"
#include <editor_hooks.h>

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
#include <wx/tokenzr.h>
#include <wx/choice.h>
#include <wx/toolbar.h>
#include <wx/xrc/xmlres.h>
#endif // CB_PRECOMP


const int idToolbarUpdateSelection = wxNewId();
const int idToolbarUpdateContents = wxNewId();


DEFINE_EVENT_TYPE(clEVT_COMMAND_UPDATETOOLBARSELECTION)
DEFINE_EVENT_TYPE(clEVT_COMMAND_UPDATETOOLBARCONTENTS)

ClangToolbar::ClangToolbar() :
    ClangPluginComponent(),
    m_EditorHookId(-1),
    m_CurrentState(),
    m_pCurrentEditor(NULL),
    m_ToolBar(nullptr),
    m_Function(nullptr),
    m_Scope(nullptr)
{

}

ClangToolbar::~ClangToolbar()
{

}

void ClangToolbar::OnAttach(IClangPlugin* pClangPlugin)
{
    ClangPluginComponent::OnAttach(pClangPlugin);
    typedef cbEventFunctor<ClangToolbar, CodeBlocksEvent> ClToolbarEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new ClToolbarEvent(this, &ClangToolbar::OnEditorActivate));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_CLOSE,     new ClToolbarEvent(this, &ClangToolbar::OnEditorClose));

    typedef cbEventFunctor<ClangToolbar, ClangEvent> ClangToolbarEvent;
    pClangPlugin->RegisterEventSink(clEVT_TOKENDATABASE_UPDATED, new ClangToolbarEvent(this, &ClangToolbar::OnTokenDatabaseUpdated));

    Connect(idToolbarUpdateSelection,clEVT_COMMAND_UPDATETOOLBARSELECTION, wxCommandEventHandler(ClangToolbar::OnUpdateSelection), nullptr, this );
    Connect(idToolbarUpdateContents, clEVT_COMMAND_UPDATETOOLBARCONTENTS, wxCommandEventHandler(ClangToolbar::OnUpdateContents), nullptr, this );
    m_EditorHookId = EditorHooks::RegisterHook(new EditorHooks::HookFunctor<ClangToolbar>(this, &ClangToolbar::OnEditorHook));

    wxCommandEvent evt(clEVT_COMMAND_UPDATETOOLBARCONTENTS, idToolbarUpdateContents);
    AddPendingEvent(evt);
    CCLogger::Get()->DebugLog( wxT("OnAttach: Posting Update toolbar selection") );

    wxCommandEvent evt2(clEVT_COMMAND_UPDATETOOLBARSELECTION, idToolbarUpdateSelection);
    AddPendingEvent(evt2);
}

void ClangToolbar::OnRelease(IClangPlugin* pClangPlugin)
{
    Disconnect(idToolbarUpdateContents);
    Disconnect(idToolbarUpdateSelection);
    ClangPluginComponent::OnRelease(pClangPlugin);
    EditorHooks::UnregisterHook(m_EditorHookId);
    Manager::Get()->RemoveAllEventSinksFor(this);
}

void ClangToolbar::OnEditorActivate(CodeBlocksEvent& event)
{
    event.Skip();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed)
    {
        if (ed == m_pCurrentEditor)
            return;
        m_pCurrentEditor = ed;
        wxString fn = ed->GetFilename();

        ClTranslUnitId id = m_pClangPlugin->GetTranslationUnitId(fn);
        if ( (m_CurrentState.m_TranslUnitId != id)||(id == -1) )
        {
            m_CurrentState = CurrentState();
            EnableToolbarTools(false);
        }

        m_CurrentState.m_TranslUnitId = id;
        wxCommandEvent evt(clEVT_COMMAND_UPDATETOOLBARCONTENTS, idToolbarUpdateContents);
        AddPendingEvent(evt);
        CCLogger::Get()->DebugLog( wxT("ed activate: Posting Update toolbar selection") );
        wxCommandEvent evt2(clEVT_COMMAND_UPDATETOOLBARSELECTION, idToolbarUpdateSelection);
        AddPendingEvent(evt2);
    }
}

void ClangToolbar::OnEditorClose(CodeBlocksEvent& event)
{
    EditorManager* edm = Manager::Get()->GetEditorManager();
    if (!edm)
    {
        event.Skip();
        return;
    }

    // we need to clear CC toolbar only when we are closing last editor
    // in other situations OnEditorActivated does this job
    // If no editors were opened, or a non-buildin-editor was active, disable the CC toolbar
    if (edm->GetEditorsCount() == 0 || !edm->GetActiveEditor() || !edm->GetActiveEditor()->IsBuiltinEditor())
    {
        EnableToolbarTools(false);
    }
    event.Skip();
}

void ClangToolbar::OnEditorHook(cbEditor* ed, wxScintillaEvent& event)
{
    event.Skip();
    if (event.GetEventType() == wxEVT_SCI_UPDATEUI)
    {
        if (event.GetUpdated() & wxSCI_UPDATE_SELECTION)
        {
            cbStyledTextCtrl* stc = ed->GetControl();
            const int line = stc->GetCurrentLine();
            if (line != m_CurrentState.m_CurrentEditorLine )
            {
                m_CurrentState.m_CurrentEditorLine = line;
                if ( (ed->GetLastModificationTime() != m_CurrentState.m_CurrentEditorModificationTime)||(m_Function&&(m_Function->GetCount()==0)))
                {
                    wxCommandEvent evt(clEVT_COMMAND_UPDATETOOLBARCONTENTS, idToolbarUpdateContents);
                    AddPendingEvent(evt);
                }
                CCLogger::Get()->DebugLog( wxT("line change: Posting Update toolbar selection") );
                wxCommandEvent evt2(clEVT_COMMAND_UPDATETOOLBARSELECTION, idToolbarUpdateSelection);
                AddPendingEvent(evt2);
            }
        }
    }
}

void ClangToolbar::OnTokenDatabaseUpdated( ClangEvent& event )
{
    if (event.GetTranslationUnitId() != GetCurrentTranslationUnitId() )
        return;
    wxCommandEvent evt(clEVT_COMMAND_UPDATETOOLBARCONTENTS, idToolbarUpdateContents);
    AddPendingEvent(evt);
    CCLogger::Get()->DebugLog( wxT("db update: Posting Update toolbar selection") );
    wxCommandEvent evt2(clEVT_COMMAND_UPDATETOOLBARSELECTION, idToolbarUpdateSelection);
    AddPendingEvent(evt2);
}


void ClangToolbar::OnUpdateSelection( wxCommandEvent& event )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (ed)
    {
        if (!m_Function )
            return;
        if (m_Scope &&( m_Scope->IsEmpty() ) )
            OnUpdateContents(event);
        cbStyledTextCtrl* stc = ed->GetControl();
        int pos = stc->GetCurrentPos();
        int line = stc->LineFromPosition(pos);

        ClTokenScope tokScope;
        ClTokenPosition loc(line + 1, pos - stc->PositionFromLine(line) + 1);
        for (std::vector<ClTokenScope>::const_iterator it = m_CurrentState.m_TokenScopes.begin(); it != m_CurrentState.m_TokenScopes.end(); ++it)
        {
            if (it->range.InRange( loc ))
            {
                if (it->range.beginLocation.line > tokScope.range.beginLocation.line)
                {
                    tokScope = *it;
                }
            }
        }

        if (tokScope.tokenName.IsEmpty() )
        {
            EnableToolbarTools(false);
            return;
        }
        wxString scopeName = tokScope.scopeName;
        if (scopeName.IsEmpty())
        {
            scopeName = wxT("<global>");
        }
        EnableToolbarTools(true);
        if (m_Scope)
        {
            line = m_Scope->FindString(scopeName);
            if (line < 0 )
            {
                m_Scope->Append(scopeName);
                line = m_Scope->FindString(scopeName);
            }
            CCLogger::Get()->DebugLog( F(wxT("OnUpdateSelection: setting scope sel to %d"), line) );
            m_Scope->SetSelection(line);
        }
        line = m_Function->FindString( tokScope.tokenName );
        if (line < 0 )
        {
            m_Function->Append(tokScope.tokenName);
            line = m_Function->FindString(tokScope.tokenName);
        }
        m_Function->SetSelection(line);
    }
    else
    {
        EnableToolbarTools(false);
    }
}

static int SortByName( const ClTokenScope& first, const ClTokenScope& second )
{
    if (first.scopeName == second.scopeName)
    {
        return first.tokenName < second.tokenName;
    }
    return first.scopeName < second.scopeName;
}

void ClangToolbar::OnUpdateContents( wxCommandEvent& /*event*/ )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    if (!m_Function )
        return;
    ClangFile file(ed->GetFilename());
    if (ed->GetProjectFile())
    {
        file = ClangFile(*ed->GetProjectFile());
    }
    m_CurrentState.m_TokenScopes.clear();
    m_pClangPlugin->GetAllTokenScopes( m_CurrentState.m_TranslUnitId, file, m_CurrentState.m_TokenScopes );
    if (m_CurrentState.m_TokenScopes.empty())
    {
        EnableToolbarTools(false);
        return;
    }
    EnableToolbarTools(true);
    std::sort( m_CurrentState.m_TokenScopes.begin(), m_CurrentState.m_TokenScopes.end(), SortByName );
    wxString selScope;
    if (m_Scope)
    {
        int sel = m_Scope->GetSelection();
        selScope = m_Scope->GetString(sel);
        m_Scope->Freeze();
        m_Scope->Clear();
        for ( std::vector<ClTokenScope>::iterator it = m_CurrentState.m_TokenScopes.begin(); it != m_CurrentState.m_TokenScopes.end(); ++it )
        {
            class wxString scope = it->scopeName;
            if ( scope.IsEmpty() )
            {
                scope = wxT("<global>");
            }
            if (m_Scope->FindString(scope) < 0 )
            {
                m_Scope->Append(scope);
            }
        }

        UpdateFunctions(selScope);
        m_Scope->Thaw();
    }
    else
    {
        UpdateFunctions(selScope);
    }


    m_CurrentState.m_CurrentEditorModificationTime = ed->GetLastModificationTime();
}

void ClangToolbar::OnScope( wxCommandEvent& /*evt*/ )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    if (!m_Scope)
        return;
    int sel = m_Scope->GetSelection();
    if (sel == -1)
        return;
    wxString selStr = m_Scope->GetString(sel);
    CCLogger::Get()->DebugLog( wxT("OnScope sel=")+selStr );

    UpdateFunctions(selStr);
}

void ClangToolbar::OnFunction( wxCommandEvent& /*evt*/ )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    if (!m_Function )
        return;
    wxString scope;
    if (m_Scope)
    {
        scope = m_Scope->GetString( m_Scope->GetSelection() );
    }

    wxString func = m_Function->GetString(m_Function->GetSelection());
    for (std::vector< ClTokenScope >::const_iterator it = m_CurrentState.m_TokenScopes.begin(); it != m_CurrentState.m_TokenScopes.end(); ++it)
    {
        if ( (!m_Scope)||(it->scopeName == scope) )
        {
            if (it->tokenName == func)
            {
                ed->GetControl()->GotoLine(it->range.beginLocation.line-1);
                break;
            }
        }
    }
}

bool ClangToolbar::BuildToolBar(wxToolBar* toolBar)
{
    // load the toolbar resource
    Manager::Get()->AddonToolBar(toolBar,_T("clangcodecompletion_toolbar"));
    // get the wxChoice control pointers
    m_Function = XRCCTRL(*toolBar, "chcClangCodeCompletionFunction", wxChoice);
    m_Scope    = XRCCTRL(*toolBar, "chcClangCodeCompletionScope",    wxChoice);

    if (m_Function)
        Manager::Get()->GetAppWindow()->Connect(m_Function->GetId(), wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(ClangToolbar::OnFunction), nullptr, this);
    if (m_Scope)
        Manager::Get()->GetAppWindow()->Connect(m_Scope->GetId(), wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(ClangToolbar::OnScope), nullptr, this);
    m_ToolBar = toolBar;

    // set the wxChoice and best toolbar size
    UpdateToolBar();

    // disable the wxChoices
    EnableToolbarTools(false);


    return true;
}

void ClangToolbar::UpdateToolBar()
{
    bool showScope = Manager::Get()->GetConfigManager(CLANG_CONFIGMANAGER)->ReadBool(_T("/scope_filter"), true);
    if (!m_ToolBar)
        return;
    if (showScope && !m_Scope)
    {
        m_Scope = new wxChoice(m_ToolBar, wxNewId(), wxPoint(0, 0), wxSize(280, -1), 0, 0);
        m_ToolBar->InsertControl(0, m_Scope);
        m_Scope->Connect(m_Scope->GetId(), wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(ClangToolbar::OnScope), nullptr, this);
    }
    else if ((!showScope) && m_Scope)
    {
        m_ToolBar->DeleteTool(m_Scope->GetId());
        m_Scope = NULL;
    }
    else
        return;

    m_ToolBar->Realize();
    m_ToolBar->SetInitialSize();
}

void ClangToolbar::UpdateFunctions( const wxString& scopeItem )
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    if (!m_Function )
        return;

    wxString scopeName = scopeItem;
    if (scopeName == wxT("<global>"))
    {
        scopeName = wxT("");
    }
    //std::sort(funcList.begin(), funcList.end(), SortByFunctionName);
    m_Function->Freeze();
    m_Function->Clear();
    for ( std::vector<ClTokenScope>::const_iterator it = m_CurrentState.m_TokenScopes.begin(); it != m_CurrentState.m_TokenScopes.end(); ++it)
    {
        if (!it->tokenName.IsEmpty())
        {
            if ( (!m_Scope) || (it->scopeName == scopeName) )
            {
                m_Function->Append(it->tokenName);
            }
        }
    }

    m_Function->Thaw();
}

void ClangToolbar::EnableToolbarTools(bool enable)
{
    if (!enable)
    {
        if (m_Scope)
            m_Scope->Clear();
        if (m_Function)
            m_Function->Clear();
    }
    if (m_Scope)
        m_Scope->Enable(enable);
    if (m_Function)
        m_Function->Enable(enable);
}

ClTranslUnitId ClangToolbar::GetCurrentTranslationUnitId()
{
    if (m_CurrentState.m_TranslUnitId == -1 )
    {
        cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
        if (!ed)
        {
            return -1;
        }
        wxString filename = ed->GetFilename();
        m_CurrentState.m_TranslUnitId = m_pClangPlugin->GetTranslationUnitId( filename );
    }
    return m_CurrentState.m_TranslUnitId;
}

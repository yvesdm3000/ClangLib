#include "clangrefactoring.h"
#include <cbstyledtextctrl.h>
#include <editor_hooks.h>
#include <cbcolourmanager.h>
#include <infowindow.h>

//#ifndef CB_PRECOMP
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
#include <vector>
#include <wx/dir.h>
#include <wx/menu.h>
#include <wx/tokenzr.h>
#include <wx/choice.h>
#include <wx/choicdlg.h>
//#endif // CB_PRECOMP
#include "cclogger.h"

const int idHighlightTimer = wxNewId();
const int idGotoDefinition = wxNewId();

#define HIGHLIGHT_DELAY 700

const wxString ClangRefactoring::SettingName = _T("/refactoring");

ClangRefactoring::ClangRefactoring() :
    ClangPluginComponent(),
    m_TranslUnitId(-1),
    m_EditorHookId(-1),
    m_bShowOccurrences(false),
    m_HighlightTimer(this, idHighlightTimer)
{

}

ClangRefactoring::~ClangRefactoring()
{

}

void ClangRefactoring::OnAttach(IClangPlugin* pClangPlugin)
{
    ClangPluginComponent::OnAttach(pClangPlugin);

    Connect(idGotoDefinition,          wxEVT_COMMAND_MENU_SELECTED,    wxCommandEventHandler(ClangRefactoring::OnGotoDefinition),    nullptr, this);

    typedef cbEventFunctor<ClangRefactoring, CodeBlocksEvent> CBEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new CBEvent(this, &ClangRefactoring::OnEditorActivate));

    typedef cbEventFunctor<ClangRefactoring, ClangEvent> ClEvent;
    pClangPlugin->RegisterEventSink(clEVT_GETOCCURRENCES_FINISHED, new ClEvent(this, &ClangRefactoring::OnRequestOccurrencesFinished));
    pClangPlugin->RegisterEventSink(clEVT_GETDEFINITION_FINISHED,  new ClEvent(this, &ClangRefactoring::OnGetDefinitionFinished));
    ConfigurationChanged();
}

void ClangRefactoring::OnRelease(IClangPlugin* pClangPlugin)
{
    pClangPlugin->RemoveAllEventSinksFor(this);
    if (m_bShowOccurrences)
    {
        Disconnect(idHighlightTimer);
        EditorHooks::UnregisterHook(m_EditorHookId);
    }
    Manager::Get()->RemoveAllEventSinksFor(this);

    Disconnect( idGotoDefinition );

    ClangPluginComponent::OnRelease(pClangPlugin);
}

bool ClangRefactoring::ConfigurationChanged()
{
    bool bReloadEditor = false;
    ConfigManager* cfg = Manager::Get()->GetConfigManager(_T("ClangLib"));
    bool bShowOccurrences  = cfg->ReadBool(wxT("/occurrence_highlight"),   true);
    if (bShowOccurrences != m_bShowOccurrences)
    {
        if (bShowOccurrences)
        {
            Connect(idHighlightTimer, wxEVT_TIMER, wxTimerEventHandler(ClangRefactoring::OnTimer));
            m_EditorHookId = EditorHooks::RegisterHook(new EditorHooks::HookFunctor<ClangRefactoring>(this, &ClangRefactoring::OnEditorHook));
        }
        else
        {
            Disconnect(idHighlightTimer);
            EditorHooks::UnregisterHook(m_EditorHookId);
            bReloadEditor = true;
        }
        m_bShowOccurrences = bShowOccurrences;
    }
    return bReloadEditor;
}

void ClangRefactoring::OnEditorActivate(CodeBlocksEvent& event)
{
    event.Skip();
    if (!IsAttached())
        return;

    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed)
    {
        wxString fn = ed->GetFilename();

        ClTranslUnitId id = m_pClangPlugin->GetTranslationUnitId(fn);
        m_TranslUnitId = id;
        cbStyledTextCtrl* stc = ed->GetControl();
        const int theIndicator = 16;
        stc->SetIndicatorCurrent(theIndicator);
        stc->IndicatorClearRange(0, stc->GetLength());
    }
}

void ClangRefactoring::OnEditorHook(cbEditor* ed, wxScintillaEvent& event)
{
    event.Skip();
    if (!IsAttached())
        return;
    if (!m_bShowOccurrences)
        return;
    bool clearIndicator = false;

    //if (!m_pClangPlugin->IsProviderFor(ed))
    //    return;
    cbStyledTextCtrl* stc = ed->GetControl();
    if (event.GetEventType() == wxEVT_SCI_MODIFIED)
    {
        if (event.GetModificationType() & (wxSCI_MOD_INSERTTEXT | wxSCI_MOD_DELETETEXT))
        {
            m_HighlightTimer.Stop();
            clearIndicator = true;
        }
    }
    else if (event.GetEventType() == wxEVT_SCI_UPDATEUI)
    {
        if (event.GetUpdated() & wxSCI_UPDATE_SELECTION)
        {
            m_HighlightTimer.Stop();
            m_HighlightTimer.Start(HIGHLIGHT_DELAY, wxTIMER_ONE_SHOT);
            clearIndicator = true;
        }
    }
    else if (event.GetEventType() == wxEVT_SCI_CHANGE)
    {
        //fprintf(stdout,"wxEVT_SCI_CHANGE\n");
    }
    else if (event.GetEventType() == wxEVT_SCI_KEY)
    {
        //fprintf(stdout,"wxEVT_SCI_KEY\n");
    }
    if (clearIndicator)
    {
        const int theIndicator = 16;
        stc->SetIndicatorCurrent(theIndicator);
        stc->IndicatorClearRange(0, stc->GetLength());
    }
}

void ClangRefactoring::OnTimer(wxTimerEvent& event)
{
    if (!IsAttached())
        return;
    const int evId = event.GetId();
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;

    if (evId == idHighlightTimer)
        BeginHighlightOccurrences(ed);
    else
        event.Skip();
}

ClTranslUnitId ClangRefactoring::GetCurrentTranslationUnitId()
{
    if (m_TranslUnitId == wxNOT_FOUND)
    {
        cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
        if (!ed)
            return wxNOT_FOUND;
        wxString filename = ed->GetFilename();
        m_TranslUnitId = m_pClangPlugin->GetTranslationUnitId( filename );
    }
    return m_TranslUnitId;
}

void ClangRefactoring::BeginHighlightOccurrences(cbEditor* ed)
{
    ClTranslUnitId translId = GetCurrentTranslationUnitId();

    cbStyledTextCtrl* stc = ed->GetControl();
    int pos = stc->GetCurrentPos();
    const wxChar ch = stc->GetCharAt(pos);
    if (   pos > 0
        && (wxIsspace(ch) || (ch != wxT('_') && wxIspunct(ch)))
        && !wxIsspace(stc->GetCharAt(pos - 1)) )
    {
        --pos;
    }
    // chosen a high value for indicator, hoping not to interfere with the indicators used by some lexers
    // if they get updated from deprecated old style indicators someday.
    const int theIndicator = 16;
    stc->SetIndicatorCurrent(theIndicator);

    // Set Styling:
    // clear all style indications set in a previous run (is also done once after text gets unselected)
    stc->IndicatorClearRange(0, stc->GetLength());

    if (stc->GetTextRange(pos - 1, pos + 1).Strip().IsEmpty())
        return;

    const int line = stc->LineFromPosition(pos);
    ClTokenPosition loc(line + 1, pos - stc->PositionFromLine(line) + 1);

    m_pClangPlugin->RequestOccurrencesOf( translId,  ed->GetFilename(), loc );
}

void ClangRefactoring::OnRequestOccurrencesFinished(ClangEvent& event)
{
    event.Skip();
    if (event.GetTranslationUnitId() != m_TranslUnitId)
    {
        CCLogger::Get()->DebugLog( _T("Translation unit has switched") );
        return;
    }

    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinActiveEditor();
    if (!ed)
        return;
    cbStyledTextCtrl* stc = ed->GetControl();
    int pos = stc->GetCurrentPos();
    const wxChar ch = stc->GetCharAt(pos);
    if (   pos > 0
        && (wxIsspace(ch) || (ch != wxT('_') && wxIspunct(ch)))
        && !wxIsspace(stc->GetCharAt(pos - 1)) )
    {
        --pos;
    }
    const int line = stc->LineFromPosition(pos);
    ClTokenPosition loc(line + 1, pos - stc->PositionFromLine(line) + 1);

    if (event.GetPosition() != loc)
    {
        CCLogger::Get()->DebugLog( wxT("Location has changed since last GetOccurrences request") );
        return; // Location has changed since the request
    }

    // chosen a high value for indicator, hoping not to interfere with the indicators used by some lexers
    // if they get updated from deprecated old style indicators someday.
    const int theIndicator = 16;
    stc->SetIndicatorCurrent(theIndicator);

    // Set Styling:
    // clear all style indications set in a previous run (is also done once after text gets unselected)
    stc->IndicatorClearRange(0, stc->GetLength());
    // TODO: use independent key
    wxColour highlightColour(Manager::Get()->GetColourManager()->GetColour(wxT("editor_highlight_occurrence")));
    stc->IndicatorSetStyle(theIndicator, wxSCI_INDIC_HIGHLIGHT);
    stc->IndicatorSetForeground(theIndicator, highlightColour);
    stc->IndicatorSetUnder(theIndicator, true);

    const std::vector< std::pair<int, int> >& occurrences = event.GetOccurrencesResults();

    for (std::vector< std::pair<int, int> >::const_iterator tkn = occurrences.begin();
         tkn != occurrences.end(); ++tkn)
    {
        stc->IndicatorFillRange(tkn->first, tkn->second);
    }
}

void ClangRefactoring::BuildModuleMenu(const ModuleType type, wxMenu* menu,
                                  const FileTreeData* WXUNUSED(data))
{
    if (type != mtEditorManager)
        return;
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    cbStyledTextCtrl* stc = ed->GetControl();
    const int pos = stc->GetCurrentPos();
    if (stc->GetTextRange(pos - 1, pos + 1).Strip().IsEmpty())
        return;
    //menu->Insert(0, idGotoDeclaration,    _("Find declaration (clang)"));
    wxMenuItem* item = menu->Insert(0, idGotoDefinition, _("Goto definition (clang)"));
    if (GetCurrentTranslationUnitId() == wxNOT_FOUND)
    {
        item->Enable(false);
    }
}

void ClangRefactoring::OnGotoDefinition(wxCommandEvent& /*event*/)
{
    CCLogger::Get()->DebugLog( wxT("OnGotoDefinition") );
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    cbStyledTextCtrl* stc = ed->GetControl();
    const int pos = stc->GetCurrentPos();
    wxString filename = ed->GetFilename();
    int line = stc->LineFromPosition(pos);
    int column = pos - stc->PositionFromLine(line);
    if (stc->GetLine(line).StartsWith(wxT("#include")))
        column = 3;
    ClTokenPosition loc(line+1, column+1);
    ClTranslUnitId translId = GetCurrentTranslationUnitId();
    if(translId == wxNOT_FOUND)
        return;
    CCLogger::Get()->DebugLog( wxT("Calling RequestTokenDefinitions") );
    m_pClangPlugin->RequestTokenDefinitions( translId, filename, loc );
}

void ClangRefactoring::OnGetDefinitionFinished( ClangEvent &event )
{
    CCLogger::Get()->DebugLog( wxT("OnGetDefinitionFinished") );
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    cbStyledTextCtrl* stc = ed->GetControl();
    const std::vector< std::pair<wxString,ClTokenPosition> >& results = event.GetLocationResults();

    CCLogger::Get()->DebugLog( F(wxT("Received %d location results"), (int)results.size()) );

    wxString tokenFilename;
    ClTokenPosition tokenPosition(0,0);

    if (results.size() == 1)
    {
        tokenFilename = results.front().first;
        tokenPosition = results.front().second;
    }
    else if (results.size() > 0)
    {
        wxArrayString list;
        for (std::vector< std::pair<wxString,ClTokenPosition> >::const_iterator it = results.begin(); it != results.end(); ++it)
            list.Add(F(it->first+wxT(":%d"), it->second.line));
        int choice = wxGetSingleChoiceIndex(wxT("Please make your choice: "), wxT("Goto definition:"), list);
        if ((choice >= 0)&&(choice < (int)results.size()))
        {
            tokenFilename = results[choice].first;
            tokenPosition = results[choice].second;
        }
        else
            return;
    }
    else // Nothing found...
        return;

    cbEditor* newEd = Manager::Get()->GetEditorManager()->Open(results.front().first);
    if (newEd)
    {
        CCLogger::Get()->DebugLog( wxT("Going to file ")+results.front().first );
        newEd->GotoTokenPosition(results.front().second.line - 1, stc->GetTextRange(stc->WordStartPosition( stc->GetCurrentPos(), true), stc->WordEndPosition(stc->GetCurrentPos(), true)));
    }
}

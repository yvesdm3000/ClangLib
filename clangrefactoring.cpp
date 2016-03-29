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
#include <wx/tokenzr.h>
#include <wx/choice.h>
//#endif // CB_PRECOMP
#include "cclogger.h"

const int idHighlightTimer = wxNewId();

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

    typedef cbEventFunctor<ClangRefactoring, CodeBlocksEvent> CBEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new CBEvent(this, &ClangRefactoring::OnEditorActivate));

    typedef cbEventFunctor<ClangRefactoring, ClangEvent> ClEvent;
    pClangPlugin->RegisterEventSink(clEVT_GETOCCURRENCES_FINISHED, new ClEvent(this, &ClangRefactoring::OnRequestOccurrencesFinished));

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

    if (event.GetLocation() != loc)
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

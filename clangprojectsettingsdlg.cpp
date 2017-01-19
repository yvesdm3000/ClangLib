/*
 * This file is part of the Code::Blocks IDE and licensed under the GNU General Public License, version 3
 * http://www.gnu.org/licenses/gpl-3.0.html
 *
 * $Revision: 10473 $
 * $Id: ClangCodeCompletionSettingsDlg.cpp 10473 2015-08-30 23:29:27Z ollydbg $
 * $HeadURL: svn://svn.code.sf.net/p/codeblocks/code/trunk/src/plugins/codecompletion/ClangCodeCompletionSettingsDlg.cpp $
 */

#include <sdk.h>

#ifndef CB_PRECOMP
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/colordlg.h>
#include <wx/combobox.h>
#include <wx/intl.h>
#include <wx/listbox.h>
#include <wx/radiobut.h>
#include <wx/regex.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/treectrl.h>
#include <wx/xrc/xmlres.h>

#include <cbstyledtextctrl.h>
#include <configmanager.h>
#include <globals.h>
#include <logmanager.h>
#include <manager.h>
#endif
#include <vector>

#include <editpairdlg.h>

#include "clangprojectsettingsdlg.h"
#include "clangplugin.h"


BEGIN_EVENT_TABLE(ClangProjectSettingsDlg, wxPanel)
    EVT_LISTBOX(XRCID("lstTargets"),        ClangProjectSettingsDlg::OnTargetSel)
    EVT_UPDATE_UI(-1,                       ClangProjectSettingsDlg::OnUpdateUI)
END_EVENT_TABLE()

ClangProjectSettingsDlg::ClangProjectSettingsDlg(wxWindow* parent, cbProject* pProject, ClangPlugin* pPlugin )
    : m_LastTargetSel(-1), m_pProject(pProject), m_pPlugin(pPlugin)
{
    CCLogger::Get()->DebugLog( wxT("Creating ClangProjectSettingsDlg instance") );
    wxXmlResource::Get()->LoadPanel(this, parent, _T("dlgClangProjectSettings"));

    // -----------------------------------------------------------------------
    // Handle all options that are being directly applied from config
    // -----------------------------------------------------------------------

    // Page "Code Completion"
    //XRCCTRL(*this, "chkNoSemantic",         wxCheckBox)->SetValue(!cfg->ReadBool(_T("/semantic_keywords"),   false));
//XRCCTRL(*this, "chkCodeCompletion",     wxCheckBox)->SetValue(cfg->ReadBool(ClangCodeCompletion::SettingName,   true));
//    XRCCTRL(*this, "rdoOneParserPerWorkspace", wxRadioButton)->SetValue( m_NativeParser->IsParserPerWorkspace());
//    XRCCTRL(*this, "rdoOneParserPerProject",   wxRadioButton)->SetValue(!m_NativeParser->IsParserPerWorkspace());

    m_ProjectSettingMap = m_pPlugin->m_ProjectSettingsMap;

    wxListBox* control = XRCCTRL(*this, "lstTargets", wxListBox);
    control->Clear();
    for (int i = 0; i < pProject->GetBuildTargetsCount(); ++i)
    {
        control->Append(pProject->GetBuildTarget(i)->GetTitle());
    }
    control->SetSelection(-1);

    //LoadCurrentRemoteDebuggingRecord();
    LoadRecord();

    Manager::Get()->RegisterEventSink(cbEVT_BUILDTARGET_REMOVED, new cbEventFunctor<ClangProjectSettingsDlg, CodeBlocksEvent>(this, &ClangProjectSettingsDlg::OnBuildTargetRemoved));
    Manager::Get()->RegisterEventSink(cbEVT_BUILDTARGET_ADDED, new cbEventFunctor<ClangProjectSettingsDlg, CodeBlocksEvent>(this, &ClangProjectSettingsDlg::OnBuildTargetAdded));
    Manager::Get()->RegisterEventSink(cbEVT_BUILDTARGET_RENAMED, new cbEventFunctor<ClangProjectSettingsDlg, CodeBlocksEvent>(this, &ClangProjectSettingsDlg::OnBuildTargetRenamed));


}

ClangProjectSettingsDlg::~ClangProjectSettingsDlg()
{
}


void ClangProjectSettingsDlg::OnBuildTargetRemoved(CodeBlocksEvent& event)
{
    cbProject* project = event.GetProject();
    if(project != m_pProject)
    {
        return;
    }
    wxString theTarget = event.GetBuildTargetName();
    ProjectBuildTarget* bt = project->GetBuildTarget(theTarget);


    wxListBox* lstBox = XRCCTRL(*this, "lstTargets", wxListBox);
    int idx = lstBox->FindString(theTarget);
    if (idx > 0)
    {
        lstBox->Delete(idx);
    }
    if((size_t)idx >= lstBox->GetCount())
    {
        idx--;
    }
    lstBox->SetSelection(idx);
    // remove the target from the map to ensure that there are no dangling pointers in it.
    m_ProjectSettingMap.erase( bt );
    LoadRecord();
}

void ClangProjectSettingsDlg::OnBuildTargetAdded(CodeBlocksEvent& event)
{
    cbProject* project = event.GetProject();
    if(project != m_pProject)
    {
        return;
    }
    wxString newTarget = event.GetBuildTargetName();
    wxString oldTarget = event.GetOldBuildTargetName();
    ProjectBuildTarget* bt = m_pProject->GetBuildTarget(newTarget);

    if(!oldTarget.IsEmpty())
    {
        for (ProjectSettingMap::iterator it = m_ProjectSettingMap.begin(); it != m_ProjectSettingMap.end(); ++it)
        {
            // find our target
            if ( (!it->first) || (it->first->GetTitle() != oldTarget) )
                continue;
            if(bt)
                m_ProjectSettingMap.insert(m_ProjectSettingMap.end(), std::make_pair(bt, it->second));
            // if we inserted it, just break, there can only be one map per target
            break;
        }
    }
    else
    {
        if (bt)
            m_ProjectSettingMap.insert(m_ProjectSettingMap.end(), std::make_pair(bt, ProjectSetting()));
    }
    wxListBox* lstBox = XRCCTRL(*this, "lstTargets", wxListBox);
    int idx = lstBox->FindString(newTarget);
    if (idx == wxNOT_FOUND)
    {
        idx = lstBox->Append(newTarget);
    }
    lstBox->SetSelection(idx);
    LoadRecord();
}

void ClangProjectSettingsDlg::OnBuildTargetRenamed(CodeBlocksEvent& event)
{
    cbProject* project = event.GetProject();
    if(project != m_pProject)
    {
        return;
    }
    wxString newTarget = event.GetBuildTargetName();
    wxString oldTarget = event.GetOldBuildTargetName();

    for (ProjectSettingMap::iterator it = m_ProjectSettingMap.begin(); it != m_ProjectSettingMap.end(); ++it)
    {
        // find our target
        if ( (!it->first) || (it->first->GetTitle() != oldTarget) )
            continue;
        it->first->SetTitle(newTarget);
        // if we renamed it, just break, there can only be one map per target
        break;
    }

    wxListBox* lstBox = XRCCTRL(*this, "lstTargets", wxListBox);
    int idx = lstBox->FindString(oldTarget);
    if (idx == wxNOT_FOUND)
    {
        return;
    }
    lstBox->SetString(idx, newTarget);
    lstBox->SetSelection(idx);
    LoadRecord();
}

void ClangProjectSettingsDlg::OnApply()
{
    SaveRecord();
    m_pPlugin->m_ProjectSettingsMap = m_ProjectSettingMap;
    m_pPlugin->UpdateComponents();
}


void ClangProjectSettingsDlg::OnUpdateUI(cb_unused wxUpdateUIEvent& event)
{
    bool en = m_LastTargetSel != wxNOT_FOUND;

    XRCCTRL(*this, "rdoCompileCommandsProject", wxRadioButton)->Enable(en);
    XRCCTRL(*this, "rdoCompileCommandsJSON", wxRadioButton)->Enable(en);
}

void ClangProjectSettingsDlg::OnTargetSel(wxCommandEvent& WXUNUSED(event))
{
    // update remote debugging controls
    SaveRecord();
    LoadRecord();
}

void ClangProjectSettingsDlg::LoadRecord()
{
    m_LastTargetSel = XRCCTRL(*this, "lstTargets", wxListBox)->GetSelection();

    ProjectBuildTarget* bt = m_pProject->GetBuildTarget(m_LastTargetSel);
    ProjectSettingMap::iterator it = m_ProjectSettingMap.find(bt);
    if (it == m_ProjectSettingMap.end())
        it = m_ProjectSettingMap.insert(m_ProjectSettingMap.end(), std::make_pair(bt, ProjectSetting()));

    ProjectSetting& s = it->second;
    if (s.compileCommandSource == ProjectSetting::CompileCommandSource_project)
    {
        XRCCTRL( *this, "rdoCompileCommandsProject", wxRadioButton )->SetValue( true );
        XRCCTRL( *this, "rdoCompileCommandsJSON", wxRadioButton )->SetValue( false );
    }
    else
    {
        XRCCTRL( *this, "rdoCompileCommandsProject", wxRadioButton )->SetValue( false );
        XRCCTRL( *this, "rdoCompileCommandsJSON", wxRadioButton )->SetValue( true );
    }
}

void ClangProjectSettingsDlg::SaveRecord()
{
    ProjectBuildTarget* bt = m_pProject->GetBuildTarget(m_LastTargetSel);
//  if (!bt)
//      return;

    ProjectSettingMap::iterator it = m_ProjectSettingMap.find(bt);
    if (it == m_ProjectSettingMap.end())
        it = m_ProjectSettingMap.insert(m_ProjectSettingMap.end(), std::make_pair(bt, ProjectSetting()));

    ProjectSetting& s = it->second;

    if (XRCCTRL( *this, "rdoCompileCommandsProject", wxRadioButton)->GetValue())
    {
        s.compileCommandSource = ProjectSetting::CompileCommandSource_project;
    }
    else
    {
        s.compileCommandSource = ProjectSetting::CompileCommandSource_jsonFile;
    }

}

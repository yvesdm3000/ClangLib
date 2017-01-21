/*
 * This file is part of the Code::Blocks IDE and licensed under the GNU General Public License, version 3
 * http://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef ClangProjectSettingsDlg_H
#define ClangProjectSettingsDlg_H

#include <wx/intl.h>
#include "configurationpanel.h"
#include <settings.h>
#include "clangplugin.h"

class ClangPlugin;

class ClangProjectSettingsDlg : public cbConfigurationPanel
{
public:
    ClangProjectSettingsDlg(wxWindow* parent, cbProject* pProject, ClangPlugin* pPlugin);
    virtual ~ClangProjectSettingsDlg();

    virtual wxString GetTitle() const
    {
        return _("Clang Code assistance");
    }
    virtual wxString GetBitmapBaseName() const
    {
        return _T("codecompletion");
    }
    virtual void OnApply();
    virtual void OnCancel()
    {
        ;
    }

protected:
    void OnUpdateUI(wxUpdateUIEvent& event);
    void OnTargetSel(wxCommandEvent& WXUNUSED(event));

private:
    void OnBuildTargetRemoved(CodeBlocksEvent& event);
    void OnBuildTargetAdded(CodeBlocksEvent& event);
    void OnBuildTargetRenamed(CodeBlocksEvent& event);

    DECLARE_EVENT_TABLE()
private:
    void SaveRecord();
    void LoadRecord();
private:
    int m_LastTargetSel;
    cbProject* m_pProject;
    ClangPlugin* m_pPlugin;
    ProjectSettingMap m_ProjectSettingMap;
};

#endif // ClangCodeCompletionSettingsDlg_H

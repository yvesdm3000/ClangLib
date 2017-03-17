#ifndef CallHierarchyView_H
#define CallHierarchyView_H

#include "clangrefactoring.h"
#include <wx/panel.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/listctrl.h>

class ClangPlugin;
class ClangCallHierarchyView: public wxPanel
{
public:
    ClangCallHierarchyView(ClangRefactoring& controller);
    void AddViewToManager();
    void RemoveViewFromManager();
    void ActivateView();
    void AddReferences(const std::vector<ClTokenReference>& refs);
protected:
    DECLARE_EVENT_TABLE();

public:
    void OnTreeItemActivated(wxTreeEvent& evt);
    void OnTreeItemExpanded(wxTreeEvent& evt);
    void OnTreeItemSelected(wxTreeEvent &evt);
    void OnContextMenu(wxContextMenuEvent& evt);
    void OnClearContents(wxCommandEvent& evt);
public:
    void OnListItemActivated(wxListEvent& evt);

private:
    void FindReferences( const wxTreeItemId fromId, const ClangFile& file, const wxString& scopeName, const wxString& displayName, std::vector<wxTreeItemId>& out_ids);

private:
    ClangRefactoring& m_Controller;
    wxSplitterWindow* m_pSplitter;
    wxTreeCtrl* m_pTree;
    wxTreeItemId m_RootId;
    wxListView* m_pList;
};

#endif // CallHierarchyView_H


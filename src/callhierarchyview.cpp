#include "callhierarchyview.h"
#include "cclogger.h"

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
#include <wx/sizer.h>
#include <wx/wfstream.h>
#include <wx/txtstrm.h>
//#endif // CB_PRECOMP


const int idSplitterWindow = wxNewId();
const int idHierarchyTree = wxNewId();
const int idLocationList = wxNewId();
const int idClearContents = wxNewId();

/**
 * TODO: optimize with buffering
 */
class LineColumnInputStream : protected wxTextInputStream
{
    unsigned int m_CurrentLine;
    unsigned int m_CurrentColumn;
public:
    LineColumnInputStream(wxInputStream& input) :
        wxTextInputStream(input),
        m_CurrentLine( 0 ),
        m_CurrentColumn( 0 )
    {

    }
    void SeekLine(const unsigned int line)
    {
        if ( (m_CurrentLine == line)&&(m_CurrentColumn == 0) )
            return;
        if ( line <= m_CurrentLine )
        {
            m_CurrentLine = 0;
            m_CurrentColumn = 0;
            m_input.SeekI( 0 );
            for (unsigned i = 0; i < line; ++i)
            {
                if( m_input.Eof() )
                    return;
                ReadLine();
            }
            return;
        }
        if( m_input.Eof() )
            return;
        while( m_CurrentLine < line )
        {
            ReadLine();
            if( m_input.Eof() )
                return;
        }
    }
    void SeekPosition(const unsigned int line, const unsigned int column)
    {
        SeekLine( line );
        if( m_input.Eof() )
            return;
        for (unsigned int i=0; i<column;++i)
        {
            wxChar c = NextChar();
            if (c == wxEOT)
                return;
            if (EatEOL(c))
            {
                ++m_CurrentLine;
                return;
            }
            if( m_input.Eof() )
                return;
            ++m_CurrentColumn;
        }

    }
    wxString ReadLine()
    {
        if (m_input.Eof())
            return wxT("");
        ++m_CurrentLine;
        m_CurrentColumn = 0;
        return wxTextInputStream::ReadLine();
    }
    wxString ReadToPosition(const unsigned int line, const unsigned int column)
    {
        wxString str; // todo: optimize
        while(m_CurrentLine < line)
        {
            str<<ReadLine();
            if( m_input.Eof() )
                return str;
            str<<_T('\n');
        }
        for (unsigned int i=0; i<column;++i)
        {
            wxChar c = NextChar();
            if (c == wxEOT)
                return str;
            if (EatEOL(c))
            {
                str<<_T('\n');
                ++m_CurrentLine;
                m_CurrentColumn = 0;
                return str;
            }
            if( m_input.Eof() )
                return str;
            ++m_CurrentColumn;
            str<<c;
        }
        return str;
    }
};

class TreeItemData : public wxTreeItemData
{
public:
    TreeItemData(const ClangFile& file, const wxString& tokenIdentifier, const wxString& tokenName, const wxString& declScopeName, const ClTokenRange range ) :
        m_File(file),
        m_TokenIdentifier( tokenIdentifier ),
        m_TokenName( tokenName ),
        m_DeclScopeName( declScopeName ),
        m_Range( range ){}
    ClangFile m_File;
    wxString m_TokenIdentifier;
    wxString m_TokenName;
    wxString m_DeclScopeName;
    ClTokenRange m_Range;
    std::vector<ClTokenReference> m_References;
    wxString GetItemName() const
    {
        wxFileName fn(m_File.GetFilename());
        wxString scopeBase;
        if (!m_DeclScopeName.IsEmpty())
        {
            scopeBase = m_DeclScopeName + wxT("::");
        }
        return scopeBase+m_TokenName+wxT(" (")+fn.GetFullName()+wxT(":")+wxString::Format(wxT("%d"),m_Range.beginLocation.line)+wxT(")");
    }
};


ClangCallHierarchyView::ClangCallHierarchyView(ClangRefactoring& controller) :
    wxPanel(Manager::Get()->GetAppWindow()),
    m_Controller( controller ),
    m_pTree( nullptr )
{
    wxBoxSizer* pSizer = new wxBoxSizer(wxVERTICAL);
    m_pSplitter = new wxSplitterWindow(this,idSplitterWindow);
    pSizer->Add(m_pSplitter, 1, wxEXPAND, 0);

    wxPanel* pPanel1 = new wxPanel(m_pSplitter,wxID_ANY);
    m_pTree = new wxTreeCtrl(pPanel1, idHierarchyTree, wxDefaultPosition, wxSize(1,1), wxTR_HAS_BUTTONS|wxTR_LINES_AT_ROOT|wxTR_FULL_ROW_HIGHLIGHT|wxTR_HIDE_ROOT|wxTR_DEFAULT_STYLE|wxSUNKEN_BORDER);
    m_pTree->SetMinSize(wxSize(100, 100));
    m_RootId = m_pTree->AddRoot(wxEmptyString);
    wxBoxSizer* pSizer1 = new wxBoxSizer(wxVERTICAL);
    pPanel1->SetSizer( pSizer1 );
    pSizer1->Add( m_pTree, 1, wxEXPAND, 0 );

    wxPanel* pPanel2 = new wxPanel(m_pSplitter,wxID_ANY);
    m_pList = new wxListView(pPanel2, idLocationList);
    m_pList->InsertColumn(1,wxT("Line"));
    m_pList->InsertColumn(2,wxT("Call reference"), wxLIST_FORMAT_LEFT, wxLIST_AUTOSIZE_USEHEADER);

    wxBoxSizer* pSizer2 = new wxBoxSizer(wxVERTICAL);
    pPanel2->SetSizer( pSizer2 );
    pSizer2->Add( m_pList, 1, wxEXPAND, 0 );

    m_pSplitter->SetAutoLayout( true );
    m_pSplitter->SplitVertically( pPanel1, pPanel2 );
    //m_pTree->SetAutoLayout( true );
    SetAutoLayout( true );
    SetSizer( pSizer );
    pSizer->Fit( this );
    pSizer->SetSizeHints( this );
    m_pTree->Show();
    m_pList->Show();
    m_pSplitter->Show();
}

void ClangCallHierarchyView::AddViewToManager()
{
    // Creates log image
    wxString prefix = ConfigManager::GetDataFolder() + _T("/images/16x16/");
    wxBitmap * bmp = new wxBitmap(cbLoadBitmap(prefix + _T("findf.png"), wxBITMAP_TYPE_PNG));

    // Adds log to C::B Messages notebook
    CodeBlocksLogEvent evtShow(cbEVT_ADD_LOG_WINDOW, this, wxString(_T("Call hierarchy")), bmp);
    Manager::Get()->ProcessEvent(evtShow);
    Show();
}
void ClangCallHierarchyView::RemoveViewFromManager()
{
    // Removes ThreadSearch panel from C::B Messages notebook
    // Reparent call to avoid deletion
    CodeBlocksLogEvent evt(cbEVT_REMOVE_LOG_WINDOW, this);
    Manager::Get()->ProcessEvent(evt);
    Reparent(Manager::Get()->GetAppWindow());
    Show(false);
}
void ClangCallHierarchyView::ActivateView()
{
    CodeBlocksLogEvent evtSwitch(cbEVT_SWITCH_TO_LOG_WINDOW, this);
    Manager::Get()->ProcessEvent(evtSwitch);
}
void ClangCallHierarchyView::AddReferences(const std::vector<ClTokenReference>& refs)
{
    for (std::vector<ClTokenReference>::const_iterator it = refs.begin(); it != refs.end(); ++it)
    {
        std::vector<wxTreeItemId> ids;
        FindReferences( m_RootId, it->GetFile(), it->GetTokenDisplayName(), it->GetScopeName(), ids );
        if (ids.empty())
        {
            TreeItemData* data = new TreeItemData(it->GetFile(), it->GetTokenIdentifier(), it->GetTokenDisplayName(), it->GetScopeName(), it->GetTokenRange());
            wxTreeItemId id = m_pTree->InsertItem( m_RootId, 0, data->GetItemName() );
            m_pTree->SetItemData( id, data );
            data = new TreeItemData(it->GetFile(), it->GetReferenceScope().GetTokenIdentifier(), it->GetReferenceScope().GetTokenDisplayName(), it->GetReferenceScope().GetScopeName(), it->GetReferenceScope().GetTokenRange());
            wxTreeItemId childId = m_pTree->AppendItem( id, data->GetItemName() );
            m_pTree->SetItemData( childId, data );
            m_pTree->AppendItem( childId, wxT("") );
            m_pTree->CollapseAllChildren( childId );
            m_pTree->Expand( id );
            ids.push_back( id );
        }
        if (it->GetReferenceScope().GetTokenDisplayName().IsEmpty())
            continue;
        for (std::vector<wxTreeItemId>::const_iterator idIt = ids.begin(); idIt != ids.end(); ++idIt)
        {
            TreeItemData* data = static_cast<TreeItemData*>( m_pTree->GetItemData( *idIt ) );
            if (data)
            {
                data->m_References.push_back( *it );
            }
            std::vector<wxTreeItemId> scopeIds;
            FindReferences( *idIt, it->GetFile(), it->GetReferenceScope().GetTokenDisplayName(), it->GetReferenceScope().GetScopeName(), scopeIds );
            if (scopeIds.empty())
            {
                data = new TreeItemData(it->GetFile(), it->GetReferenceScope().GetTokenIdentifier(), it->GetReferenceScope().GetTokenDisplayName(), it->GetReferenceScope().GetScopeName(), it->GetReferenceScope().GetTokenRange());
                if (m_pTree->ItemHasChildren(*idIt))
                {
                    wxTreeItemIdValue cookie;
                    wxTreeItemId id = m_pTree->GetFirstChild(*idIt, cookie);
                    if(m_pTree->GetItemText( id ).IsEmpty())
                    {
                        m_pTree->Delete( id );
                    }
                }
                wxTreeItemId childId = m_pTree->AppendItem( *idIt, data->GetItemName() );
                m_pTree->SetItemData( childId, data );
                m_pTree->AppendItem( childId, wxT("") );
                m_pTree->CollapseAllChildren( childId );
                if (m_pTree->IsSelected( *idIt ))
                {
                    m_pTree->Expand( *idIt );
                }
            }
        }
    }
}
void ClangCallHierarchyView::FindReferences( const wxTreeItemId fromId, const ClangFile& file, const wxString& displayName, const wxString& declScopeName, std::vector<wxTreeItemId>& out_ids)
{
    if (displayName.IsEmpty())
        return;
    if (m_pTree->ItemHasChildren( fromId ))
    {
        wxTreeItemIdValue cookie;
        wxTreeItemId id = m_pTree->GetFirstChild(fromId, cookie);
        while (id.IsOk())
        {
            TreeItemData* data = static_cast<TreeItemData*>( m_pTree->GetItemData(id) );
            if (data)
            {
                if ( (data->m_TokenName == displayName)/*&&(data->m_File == file)*/&&(data->m_DeclScopeName == declScopeName))
                {
                    out_ids.push_back( id );
                }
            }
            FindReferences(id, file, displayName, declScopeName, out_ids);
            id = m_pTree->GetNextChild( fromId, cookie );
        }
    }
}

void ClangCallHierarchyView::OnTreeItemActivated(wxTreeEvent& evt )
{
    TreeItemData* data = static_cast<TreeItemData*>( m_pTree->GetItemData( evt.GetItem() ) );
    if (data)
    {
        cbEditor* ed = Manager::Get()->GetEditorManager()->Open(data->m_File.GetFilename());
        if (ed)
        {
            ed->GotoTokenPosition(data->m_Range.beginLocation.line - 1, data->m_TokenName);
        }
    }
}
void ClangCallHierarchyView::OnTreeItemExpanded( wxTreeEvent &evt )
{
    if (m_pTree->GetChildrenCount( evt.GetItem() ) == 1)
    {
        wxTreeItemIdValue cookie;
        if (m_pTree->GetItemText( m_pTree->GetFirstChild( evt.GetItem(), cookie ) ).IsEmpty())
        {
            TreeItemData* data = static_cast<TreeItemData*>( m_pTree->GetItemData(evt.GetItem()) );
            m_Controller.LookupCallHierarchy(data->m_File, data->m_Range.beginLocation);
        }
    }
}
void ClangCallHierarchyView::OnTreeItemSelected( wxTreeEvent &evt )
{
    m_pList->DeleteAllItems();
    wxTreeItemId id = evt.GetItem();
    if (!id.IsOk())
        return;
    TreeItemData* data = static_cast<TreeItemData*>( m_pTree->GetItemData( id ) );
    if (!data)
        return;
    wxTreeItemId parentId = m_pTree->GetItemParent(evt.GetItem());
    if (parentId.IsOk())
    {
        TreeItemData* parentData = static_cast<TreeItemData*>( m_pTree->GetItemData( parentId ) );
        if (parentData)
        {
            int ln = 0;
            wxFileInputStream fin(data->m_File.GetFilename());
            LineColumnInputStream in(fin);
            for ( std::vector<ClTokenReference>::const_iterator it = parentData->m_References.begin(); it != parentData->m_References.end(); ++it)
            {
                if (data->m_File == it->GetFile())
                {
                    if (data->m_Range.InRange( it->GetTokenRange().beginLocation ))
                    {
                        wxString lineStr = F(wxT("%d"), it->GetTokenRange().beginLocation.line);
                        long idx = m_pList->FindItem( 0, lineStr );
                        if (idx != wxNOT_FOUND)
                            continue;
                        m_pList->InsertItem( ln, lineStr );
                        in.SeekLine( it->GetTokenRange().beginLocation.line - 1 );
                        wxString str = in.ReadLine();
                        m_pList->SetItem( ln, 1, str.Trim().Trim(false) );
                        ++ln;
                    }
                }
            }
            const int w1 = m_pList->GetColumnWidth( 1 );
            m_pList->SetColumnWidth(1, wxLIST_AUTOSIZE);
            const int w2 = m_pList->GetColumnWidth( 1 );
            m_pList->SetColumnWidth( 1, std::max(w1,w2) );
        }
    }
}

void ClangCallHierarchyView::OnContextMenu(wxContextMenuEvent& )
{
    wxMenu menu;
    menu.Append(idClearContents, wxT("Clear contents"));
    PopupMenu(&menu);
}

void ClangCallHierarchyView::OnClearContents(wxCommandEvent& )
{
    m_pTree->DeleteChildren( m_RootId );
}

void ClangCallHierarchyView::OnListItemActivated(wxListEvent& evt)
{
    wxTreeItemId selId = m_pTree->GetSelection();
    if (selId.IsOk())
    {
        TreeItemData* data = static_cast<TreeItemData*>( m_pTree->GetItemData(selId) );
        if (data)
        {
            int ln = wxAtoi( evt.GetText() );

            cbEditor* ed = Manager::Get()->GetEditorManager()->Open(data->m_File.GetFilename());
            if (ed)
            {
                ed->GotoTokenPosition(ln - 1, data->m_TokenName);
            }
        }
    }
}


BEGIN_EVENT_TABLE(ClangCallHierarchyView, wxPanel)
EVT_TREE_ITEM_ACTIVATED( idHierarchyTree, ClangCallHierarchyView::OnTreeItemActivated )
EVT_TREE_ITEM_EXPANDED( idHierarchyTree, ClangCallHierarchyView::OnTreeItemExpanded )
EVT_TREE_SEL_CHANGED( idHierarchyTree, ClangCallHierarchyView::OnTreeItemSelected )
EVT_CONTEXT_MENU( ClangCallHierarchyView::OnContextMenu )
EVT_MENU(idClearContents, ClangCallHierarchyView::OnClearContents )
EVT_LIST_ITEM_ACTIVATED( idLocationList, ClangCallHierarchyView::OnListItemActivated )
END_EVENT_TABLE()

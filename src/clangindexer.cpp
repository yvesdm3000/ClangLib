#include "clangindexer.h"

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
#include <wx/dir.h>
#include <wx/menu.h>
//#endif // CB_PRECOMP

#include "cclogger.h"

ClangIndexer::ClangIndexer()
{

}

ClangIndexer::~ClangIndexer()
{

}

const wxString ClangIndexer::SettingName = _T("/indexer");

static const wxString IndexingDefault = _T("project");


void ClangIndexer::OnAttach(IClangPlugin* pClangPlugin)
{
    ClangPluginComponent::OnAttach(pClangPlugin);

    typedef cbEventFunctor<ClangIndexer, CodeBlocksEvent> CbEvent;
    Manager::Get()->RegisterEventSink(cbEVT_PROJECT_OPEN,        new CbEvent(this, &ClangIndexer::OnProjectOpen));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_OPEN,         new CbEvent(this, &ClangIndexer::OnEditorOpen));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_SAVE,         new CbEvent(this, &ClangIndexer::OnEditorSave));

    typedef cbEventFunctor<ClangIndexer, ClangEvent> ClIndexerEvent;
    pClangPlugin->RegisterEventSink(clEVT_REINDEXFILE_FINISHED, new ClIndexerEvent(this, &ClangIndexer::OnReindexFileFinished));
}

/** @brief Overloaded method that is called when this component (or plugin) is released
 *
 * @param pClangPlugin IClangPlugin*
 * @return void
 *
 */
void ClangIndexer::OnRelease(IClangPlugin* pClangPlugin)
{
    pClangPlugin->RemoveAllEventSinksFor(this);

    ClangPluginComponent::OnRelease(pClangPlugin);
}

/** @brief Event handler for the event that a project has been opened
 *
 * @param evt CodeBlocksEvent&
 * @return void
 *
 */
void ClangIndexer::OnProjectOpen(CodeBlocksEvent& evt)
{
    evt.Skip();
    ConfigManager* cfg = Manager::Get()->GetConfigManager(_T("ClangLib"));
    wxString indexingType = cfg->Read(wxT("/indexer_indexingtype"), IndexingDefault);
    if (indexingType == wxT("project"))
    {
        cbProject* proj = evt.GetProject();
        for (FilesList::iterator it = proj->GetFilesList().begin(); it != proj->GetFilesList().end(); ++it)
        {
            ProjectFile* f = *it;
            wxDateTime ts = m_pClangPlugin->GetFileIndexingTimestamp( ClangFile(*f) );
            if ( (!ts.IsValid()) ||((*it)->file.GetModificationTime() > ts ) )
                m_StagingFiles.insert( ClangFile(*f));
        }
    }
    if (!m_StagingFiles.empty())
    {
        ClangFile file = *m_StagingFiles.begin();
        m_pClangPlugin->BeginReindexFile( file );
    }
}

/** @brief Event handler for the event that a file is opened in the editor
 *
 * @param event CodeBlocksEvent&
 * @return void
 *
 */
void ClangIndexer::OnEditorOpen(CodeBlocksEvent& event)
{
    if (!Manager::Get()->IsAppStartedUp())
    {
        return;
    }
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed && ed->IsOK())
    {
        ConfigManager* cfg = Manager::Get()->GetConfigManager(CLANG_CONFIGMANAGER);
        wxString indexingType = cfg->Read(wxT("/indexer_indexingtype"), IndexingDefault);
        if (indexingType == wxT("fileopen"))
        {
            ClangFile file(ed->GetFilename());
            if (ed->GetProjectFile())
            {
                file = ClangFile(*ed->GetProjectFile());
            }
            else
            {
                cbProject* pProject = event.GetProject();
                if (!pProject)
                {
                    ProjectFile* pf = nullptr;
                    pProject = Manager::Get()->GetProjectManager()->FindProjectForFile(ed->GetFilename(), &pf, false, false);
                }
                file = ClangFile( pProject, ed->GetFilename());
            }
            if (file.GetProject().Length() == 0)
                return;
            wxDateTime ts = m_pClangPlugin->GetFileIndexingTimestamp( file );
            wxFileName fn(ed->GetFilename());
            if (fn.GetModificationTime() > ts)
            {
                m_StagingFiles.insert( file );
                m_pClangPlugin->BeginReindexFile( *m_StagingFiles.begin() );
            }
        }
    }
}

/** @brief Event handler for when the file in the editor has been saved
 *
 * @param evt CodeBlocksEvent&
 * @return void
 *
 */
void ClangIndexer::OnEditorSave(CodeBlocksEvent& evt)
{
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinEditor(evt.GetEditor());
    ClangFile file(ed->GetFilename());
    if (ed->GetProjectFile())
        file = ClangFile(*ed->GetProjectFile());
    std::set<ClangFile>::iterator it = m_StagingFiles.find( file );
    if (it != m_StagingFiles.end())
        m_StagingFiles.erase( it );
    m_pClangPlugin->BeginReindexFile( ed->GetFilename() );
}

/** @brief Event handler for when the reindexing of a file was finished
 *
 * @param evt ClangEvent&
 * @return void
 *
 */
void ClangIndexer::OnReindexFileFinished(ClangEvent& evt)
{
    CCLogger::Get()->DebugLog( wxT("OnReindexFileFinished ")+evt.GetFile().GetFilename() );
    if (!m_StagingFiles.empty())
    {
        if (m_StagingFiles.find( evt.GetFile() ) == m_StagingFiles.end())
        {
            // File not found... To make sure we don't have an endless loop we remove the first as this is the most likely one
            m_StagingFiles.erase( *m_StagingFiles.begin() );
        }
        else
        {
            m_StagingFiles.erase( evt.GetFile() );
        }
    }
    if (!m_StagingFiles.empty())
    {
        CCLogger::Get()->DebugLog( F(wxT("Reindexing next file ")+m_StagingFiles.begin()->GetFilename()+wxT(" on a total of %d"), (int)m_StagingFiles.size()));
        m_pClangPlugin->BeginReindexFile( *m_StagingFiles.begin() );
    }
}


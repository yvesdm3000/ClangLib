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
static const wxString IndexingDefault = _T("fileopen");
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

void ClangIndexer::OnRelease(IClangPlugin* pClangPlugin)
{
    pClangPlugin->RemoveAllEventSinksFor(this);

    ClangPluginComponent::OnRelease(pClangPlugin);
}

void ClangIndexer::OnProjectOpen(CodeBlocksEvent& evt)
{
    evt.Skip();
    ConfigManager* cfg = Manager::Get()->GetConfigManager(_T("ClangLib"));
    wxString indexingType = cfg->Read(wxT("/indexer_indexingtype"), IndexingDefault);
    if (indexingType == wxT("project"))
    {
        CCLogger::Get()->DebugLog( wxT("OnProjectOpen") );
        cbProject* proj = evt.GetProject();
        for (FilesList::iterator it = proj->GetFilesList().begin(); it != proj->GetFilesList().end(); ++it)
        {
            ProjectFile* f = *it;
            wxFileName fn = f->file;
            m_StagingFiles.insert( fn.GetFullPath() );
        }
    }
    if (!m_StagingFiles.empty())
        m_pClangPlugin->BeginReindexFile( *m_StagingFiles.begin() );
}

void ClangIndexer::OnEditorOpen(CodeBlocksEvent& event)
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed && ed->IsOK())
    {
        ConfigManager* cfg = Manager::Get()->GetConfigManager(_T("ClangLib"));
        wxString indexingType = cfg->Read(wxT("/indexer_indexingtype"), IndexingDefault);
        if (indexingType == wxT("fileopen"))
        {
            m_pClangPlugin->BeginReindexFile( ed->GetFilename() );
        }
    }
}

void ClangIndexer::OnEditorSave(CodeBlocksEvent& evt)
{
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    cbEditor* ed = edMgr->GetBuiltinEditor(evt.GetEditor());
    m_pClangPlugin->BeginReindexFile( ed->GetFilename() );
}

void ClangIndexer::OnReindexFileFinished(ClangEvent& evt)
{
    m_StagingFiles.erase( evt.GetFilename() );
    if (!m_StagingFiles.empty())
        m_pClangPlugin->BeginReindexFile( *m_StagingFiles.begin() );
}


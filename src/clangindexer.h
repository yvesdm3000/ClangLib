#ifndef CLANGINDEXER_H
#define CLANGINDEXER_H

#include "clangpluginapi.h"
#include <deque>

class ClangIndexer : public ClangPluginComponent
{
public:
    ClangIndexer();
    virtual ~ClangIndexer();

    static const wxString SettingName;

    void OnAttach(IClangPlugin* pClangPlugin);
    void OnRelease(IClangPlugin* pClangPlugin);

    void OnProjectOpen(CodeBlocksEvent& evt);
    void OnProjectClose(CodeBlocksEvent& evt);
    void OnEditorOpen(CodeBlocksEvent& evt);
    void OnEditorSave(CodeBlocksEvent& evt);

    void OnReindexFileFinished(ClangEvent& evt);

private:
    std::deque<ClangFile> m_StagingFiles;
};

#endif //CLANGINDEXER_H

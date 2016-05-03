#ifndef CLANGINDEXER_H
#define CLANGINDEXER_H

#include "clangpluginapi.h"

class ClangIndexer : public ClangPluginComponent
{
public:
    ClangIndexer();
    virtual ~ClangIndexer();

    static const wxString SettingName;

    void OnAttach(IClangPlugin* pClangPlugin);
    void OnRelease(IClangPlugin* pClangPlugin);

    void OnProjectOpen(CodeBlocksEvent& evt);
    void OnEditorOpen(CodeBlocksEvent& evt);
    void OnEditorSave(CodeBlocksEvent& evt);

    void OnReindexFileFinished(ClangEvent& evt);

private:
    std::set<wxString> m_StagingFiles;
};

#endif //CLANGINDEXER_H

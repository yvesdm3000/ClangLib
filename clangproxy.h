#ifndef CLANGPROXY_H
#define CLANGPROXY_H

#include <map>
#include <vector>
#include <list>
#include <wx/string.h>
#include <wx/wfstream.h>
#include <wx/filename.h>
#include <queue>
#include <backgroundthread.h>
#include "clangpluginapi.h"
#include "translationunit.h"
#include "persistency.h"

#undef CLANGPROXY_TRACE_FUNCTIONS

class ClTranslationUnit;
class ClTokenDatabase;
class ClangProxy;

typedef void* CXIndex;
typedef int ClFileId;

class ClangProxy
{
public:
    /*abstract */
    class ClangJob : public AbstractJob, public wxObject
    {
    public:
        enum JobType
        {
            CreateTranslationUnitType   = 1,
            RemoveTranslationUnitType   = 2,
            ReparseType                 = 3,
            GetDiagnosticsType          = 4,
            CodeCompleteAtType          = 5,
            DocumentCCTokenType         = 6,
            GetTokensAtType             = 7,
            GetCallTipsAtType           = 8,
            GetOccurrencesOfType        = 9,
            GetFunctionScopeAtType      = 10,

            IndexerGroupType            = 1<< 7, // Jobs that run on the indexer thread

            SerializeTokenDatabaseType  = 1 | IndexerGroupType,
            ReindexType                 = 2 | IndexerGroupType,
            ReindexListType             = 3 | IndexerGroupType,
        };
    protected:
        ClangJob( JobType JobType ) :
            AbstractJob(),
            wxObject(),
            m_JobType(JobType),
            m_pProxy(NULL)
        {
        }
        ClangJob( const ClangJob& other ) : AbstractJob(), wxObject()
        {
            m_JobType = other.m_JobType;
            m_pProxy = other.m_pProxy;
        }

    public:
        /// Returns a copy of this job on the heap to make sure the objects lifecycle is guaranteed across threads
        virtual ClangJob* Clone() const = 0;
        // Called on job thread
        virtual void Execute(ClangProxy& /*clangproxy*/) = 0;
        // Called on job thread
        virtual void Completed(ClangProxy& /*clangproxy*/) {}
        // Called on job thread
        void SetProxy( ClangProxy* pProxy )
        {
            m_pProxy = pProxy;
        }
        JobType GetJobType() const
        {
            return m_JobType;
        }
    public:
        void operator()()
        {
            assert( m_pProxy != NULL );
            Execute(*m_pProxy);
            Completed(*m_pProxy);
        }
    protected:
        JobType    m_JobType;
        ClangProxy* m_pProxy;
    };

    /**
     * @brief ClangJob that posts a wxEvent back when completed
     */
    /* abstract */
    class EventJob : public ClangJob
    {
    protected:
        EventJob( JobType JobType, wxEventType evtType, int evtId ) : ClangJob(JobType), m_EventType(evtType), m_EventId(evtId)
        {
        }
        EventJob( const EventJob& other ) : ClangJob(other.m_JobType)
        {
            m_EventType = other.m_EventType;
            m_EventId = other.m_EventId;
        }
    public:
        // Called on job thread
        virtual void Completed(ClangProxy& clangProxy)
        {
            if (clangProxy.m_pEventCallbackHandler&&(m_EventType != 0) )
            {
                ClangProxy::CallbackEvent evt( m_EventType, m_EventId, this);
                clangProxy.m_pEventCallbackHandler->AddPendingEvent( evt );
            }
        }
    private:
        wxEventType m_EventType;
        int         m_EventId;
    };

    /* final */
    class CreateTranslationUnitJob : public EventJob
    {
    public:
        CreateTranslationUnitJob( wxEventType evtType, int evtId, const wxString& filename, const wxString& commands, const std::map<wxString, wxString>& unsavedFiles ) :
            EventJob(CreateTranslationUnitType, evtType, evtId),
            m_Filename(filename),
            m_Commands(commands),
            m_TranslationUnitId(-1),
            m_UnsavedFiles(unsavedFiles) {}
        ClangJob* Clone() const
        {
            CreateTranslationUnitJob* job = new CreateTranslationUnitJob(*this);
            return static_cast<ClangJob*>(job);
        }
        void Execute(ClangProxy& clangproxy)
        {
            m_TranslationUnitId = clangproxy.GetTranslationUnitId(m_TranslationUnitId, m_Filename);
            if (m_TranslationUnitId == wxNOT_FOUND )
            {
                ClTokenDatabase db(clangproxy.m_Database.GetFileDB());
                clangproxy.CreateTranslationUnit(m_Filename, m_Commands, m_UnsavedFiles, m_TranslationUnitId, db);
                clangproxy.m_Database.Update( db.GetFilenameId(m_Filename), db);
            }
            m_UnsavedFiles.clear();
        }
        ClTranslUnitId GetTranslationUnitId()
        {
            return m_TranslationUnitId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
    protected:
        CreateTranslationUnitJob(const CreateTranslationUnitJob& other):
            EventJob(other),
            m_Filename(other.m_Filename.c_str()),
            m_Commands(other.m_Commands.c_str()),
            m_TranslationUnitId(other.m_TranslationUnitId),
            m_UnsavedFiles()
        {
            /* deep copy */
            for( std::map<wxString, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }
        }
    public:
        wxString m_Filename;
        wxString m_Commands;
        int m_TranslationUnitId; // Returned value
        std::map<wxString, wxString> m_UnsavedFiles;
    };

    /* final */
    class RemoveTranslationUnitJob : public EventJob
    {
    public:
        RemoveTranslationUnitJob( wxEventType evtType, int evtId, int TranslUnitId ) :
            EventJob(RemoveTranslationUnitType, evtType, evtId),
            m_TranslationUnitId(TranslUnitId) {}
        ClangJob* Clone() const
        {
            RemoveTranslationUnitJob* job = new RemoveTranslationUnitJob(*this);
            return static_cast<ClangJob*>(job);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.RemoveTranslationUnit(m_TranslationUnitId);
        }
    protected:
        RemoveTranslationUnitJob(const RemoveTranslationUnitJob& other):
            EventJob(other),
            m_TranslationUnitId(other.m_TranslationUnitId) {}
        int m_TranslationUnitId;
    };

    /* final */
    class ReparseJob : public EventJob
    {
    public:
        ReparseJob( wxEventType evtType, int evtId, int translId, const wxString& compileCommand, const wxString& filename, const std::map<wxString, wxString>& unsavedFiles, bool parents = false )
            : EventJob(ReparseType, evtType, evtId),
              m_TranslId(translId),
              m_UnsavedFiles(unsavedFiles),
              m_CompileCommand(compileCommand),
              m_Filename(filename),
              m_Parents(parents)
        {
        }
        ClangJob* Clone() const
        {
            return new ReparseJob(*this);
        }
        void Execute(ClangProxy& clangproxy);
#if 0
        virtual void Completed(ClangProxy& /*clangProxy*/)
        {
            return; // Override: the event will be posted after AsyncReparse
        }
        virtual void ReparseCompleted( ClangProxy& clangProxy )
        {
            EventJob::Completed(clangProxy);
        }
#endif
        ClTranslUnitId GetTranslationUnitId()
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
    private:
        ReparseJob(const ReparseJob& other )
            : EventJob(other),
              m_TranslId(other.m_TranslId),
              m_UnsavedFiles(),
              m_CompileCommand(other.m_CompileCommand.c_str()),
              m_Filename(other.m_Filename.c_str()),
              m_Parents(other.m_Parents)
        {
            /* deep copy */
            for( std::map<wxString, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }
        }

    public:
        int m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
        wxString m_CompileCommand;
        wxString m_Filename;
        bool m_Parents; // If the parents also need to be reparsed
    };

    /* final */
    class GetDiagnosticsJob : public EventJob
    {
    public:
        GetDiagnosticsJob( wxEventType evtType, int evtId, int translId, const wxString& filename ):
            EventJob(GetDiagnosticsType, evtType, evtId),
            m_TranslId(translId),
            m_Filename( filename )
        {}
        ClangJob* Clone() const
        {
            GetDiagnosticsJob* pJob = new GetDiagnosticsJob(*this);
            pJob->m_Results = m_Results;
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetDiagnostics(m_TranslId, m_Filename, m_Results);
        }
        ClTranslUnitId GetTranslationUnitId()
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const std::vector<ClDiagnostic>& GetResults() const
        {
            return m_Results;
        }

    protected:
        GetDiagnosticsJob( const GetDiagnosticsJob& other ) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Results(other.m_Results) {}
    public:
        int m_TranslId;
        wxString m_Filename;
        std::vector<ClDiagnostic> m_Results; // Returned value
    };

    class GetFunctionScopeAtJob : public EventJob
    {
    public:
        GetFunctionScopeAtJob( wxEventType evtType, int evtId, int translId, const wxString& filename, const ClTokenPosition& location) :
            EventJob(GetFunctionScopeAtType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename),
            m_Location(location) {}
        ClangJob* Clone() const
        {
            GetFunctionScopeAtJob* pJob = new GetFunctionScopeAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetFunctionScopeAt(m_TranslId, m_Filename, m_Location, m_ScopeName, m_MethodName);
        }

    protected:
        GetFunctionScopeAtJob( const GetFunctionScopeAtJob& other ) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Location(other.m_Location),
            m_ScopeName(other.m_ScopeName.c_str()),
            m_MethodName(other.m_MethodName.c_str())
        {
        }
    public:
        int m_TranslId;
        wxString m_Filename;
        ClTokenPosition m_Location;
        wxString m_ScopeName;
        wxString m_MethodName;
    };

    class ReindexJob : public ClangJob
    {
    public:
        ReindexJob(const wxString& pchFilename, ClFileId fileId, const wxString& compileCommand) :
            ClangJob(ReindexType),
            m_PchFilename( pchFilename ),
            m_FileId(fileId),
            m_CompileCommand(compileCommand)
        {}

        ClangJob* Clone() const
        {
            ReindexJob* pJob = new ReindexJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute( ClangProxy& clangproxy )
        {
            CXIndex index = clang_createIndex(0, 0);
            ClTranslationUnit tu(0, index);
            ClTokenDatabase db(clangproxy.m_Database.GetFileDB());

            wxString filename = clangproxy.m_Database.GetFilename( m_FileId );

            std::vector<wxCharBuffer> argsBuffer;
            clangproxy.BuildCompileArgs( filename, m_CompileCommand, argsBuffer );
            std::vector<const char*> args;
            for( std::vector<wxCharBuffer>::iterator it = argsBuffer.begin(); it != argsBuffer.end(); ++it)
            {
                args.push_back( it->data() );
            }

            std::map<wxString, wxString> unsavedFiles;
            tu.Parse( filename, m_FileId, args, unsavedFiles, &db );
            if ( m_PchFilename.Len() > 0 )
                tu.Store( m_PchFilename );

            clang_disposeIndex( index );
            clangproxy.m_Database.Update( m_FileId, db );
        }
    protected:
        ReindexJob(const ReindexJob& other) :
            ClangJob(other),
            m_PchFilename(other.m_PchFilename.c_str()),
            m_FileId(other.m_FileId),
            m_CompileCommand(other.m_CompileCommand.c_str())
        {}
        wxString m_PchFilename;
        ClFileId m_FileId;
        wxString m_CompileCommand;
    };

    class ReindexListJob : public ClangJob
    {
    public:
        ReindexListJob(const std::vector<ClFileId>& fileIds, const wxString& compileCommand, bool generatePCH) :
            ClangJob(ReindexListType),
            m_FileIds(fileIds),
            m_CompileCommand(compileCommand),
            m_CurrentIndex(0),
            m_ReindexCount(0),
            m_GeneratePCH(generatePCH)
        {}

        ClangJob* Clone() const
        {
            ReindexListJob* pJob = new ReindexListJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute( ClangProxy& clangproxy )
        {
            if (m_CurrentIndex >= m_FileIds.size() )
            {
                if (m_ReindexCount > 0)
                {
                    wxString dbfn = clangproxy.GetTokenDatabaseFilename();
                    SerializeTokenDatabaseJob job( clangproxy.m_Database, dbfn );
                    clangproxy.AppendPendingJob(job);
                }
                return;
            }
            ClFileId fId = m_FileIds[m_CurrentIndex];
            wxString filename = clangproxy.m_Database.GetFilename( fId );
            wxString pchFilename;
            if( m_GeneratePCH )
                pchFilename = clangproxy.GetPchFilename( filename );
            wxFileName f1(filename);
            if( f1.FileExists() )
            {
                wxFileName f2(pchFilename);
                if( (!f2.FileExists())||(f1.GetModificationTime() > f2.GetModificationTime()) )
                {
                    ReindexJob job(pchFilename, fId, m_CompileCommand);
                    clangproxy.AppendPendingJob(job);
                    m_ReindexCount++;
                }
                else
                {
                    wxDateTime mtime = clangproxy.m_Database.GetFilenameTimestamp( fId );
                    wxDateTime fmtime = f1.GetModificationTime();
                    if( mtime != fmtime )
                    {
                        ReindexJob job(pchFilename, fId, m_CompileCommand);
                        clangproxy.AppendPendingJob(job);
                        m_ReindexCount++;
                    }
                }
            }
            m_CurrentIndex++;
            clangproxy.AppendPendingJob(*this);
        }
    protected:
        ReindexListJob(const ReindexListJob& other) :
            ClangJob(other),
            m_FileIds(other.m_FileIds),
            m_CompileCommand(other.m_CompileCommand.c_str()),
            m_CurrentIndex(other.m_CurrentIndex),
            m_ReindexCount(other.m_ReindexCount),
            m_GeneratePCH(other.m_GeneratePCH)
        {}
        std::vector<ClFileId> m_FileIds;
        wxString m_CompileCommand;
        size_t m_CurrentIndex;
        size_t m_ReindexCount; // The amount of files that really have been indexed
        bool m_GeneratePCH;
    };

    /// Job designed to be run synchronous
    /*abstract */
    class SyncJob : public EventJob
    {
    protected:
        SyncJob(JobType JobType, wxEventType evtType, int evtId) :
            EventJob(JobType, evtType, evtId),
            m_bCompleted(false),
            m_pMutex(new wxMutex()),
            m_pCond(new wxCondition(*m_pMutex))
        {
        }
        SyncJob(JobType JobType, wxEventType evtType, int evtId, wxMutex* pMutex, wxCondition* pCond) :
            EventJob(JobType, evtType, evtId),
            m_bCompleted(false),
            m_pMutex(pMutex),
            m_pCond(pCond) {}
    public:
        // Called on Job thread
        virtual void Completed( ClangProxy& clangproxy )
        {
            {
                wxMutexLocker lock(*m_pMutex);
                m_bCompleted = true;
                m_pCond->Signal();
            }
            EventJob::Completed(clangproxy);
        }
        /// Called on main thread to wait for completion of this job.
        wxCondError WaitCompletion( unsigned long milliseconds )
        {
            wxMutexLocker lock(*m_pMutex);
            if (m_bCompleted )
            {
                return wxCOND_NO_ERROR;
            }
            return m_pCond->WaitTimeout(milliseconds);
        }
        /// Called on main thread when the last/final copy of this object will be destroyed.
        virtual void Finalize()
        {
            m_pMutex->Unlock();
            delete m_pMutex;
            m_pMutex = NULL;
            delete m_pCond;
            m_pCond = NULL;
        }
    protected:
        bool m_bCompleted;
        mutable wxMutex* m_pMutex;
        mutable wxCondition* m_pCond;
    };

    /* final */
    class CodeCompleteAtJob : public SyncJob
    {
    public:
        CodeCompleteAtJob( wxEventType evtType, const int evtId, const bool isAuto,
                           const wxString& filename, const ClTokenPosition& location,
                           const ClTranslUnitId translId, const std::map<wxString, wxString>& unsavedFiles ):
            SyncJob(CodeCompleteAtType, evtType, evtId),
            m_IsAuto(isAuto),
            m_Filename(filename.c_str()),
            m_Location(location),
            m_TranslId(translId),
            m_UnsavedFiles(unsavedFiles),
            m_pResults(new std::vector<ClToken>())
        {
        }

        ClangJob* Clone() const
        {
            CodeCompleteAtJob* pJob = new CodeCompleteAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            std::vector<ClToken> results;
            clangproxy.CodeCompleteAt( m_TranslId, m_Filename, m_Location, m_IsAuto, m_UnsavedFiles, results, m_Diagnostics);
            for (std::vector<ClToken>::iterator tknIt = results.begin(); tknIt != results.end(); ++tknIt)
            {
                switch (tknIt->category)
                {
                case tcClass:
                case tcCtorPublic:
                case tcDtorPublic:
                case tcFuncPublic:
                case tcVarPublic:
                case tcEnum:
                case tcTypedef:
                    clangproxy.RefineTokenType(m_TranslId, tknIt->id, tknIt->category);
                    break;
                default:
                    break;
                }
            }

            m_pResults->swap(results);
            // Get rid of some copied memory we no longer need
            m_UnsavedFiles.clear();
        }
        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResults;
        }
        ClTranslUnitId GetTranslationUnitId()
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetLocation() const
        {
            return m_Location;
        }
        const std::vector<ClToken>& GetResults() const
        {
            return *m_pResults;
        }
        const std::vector<ClDiagnostic>& GetDiagnostics() const
        {
            return m_Diagnostics;
        }
    protected:
        CodeCompleteAtJob( const CodeCompleteAtJob& other ) :
            SyncJob(other),
            m_IsAuto(other.m_IsAuto),
            m_Filename(other.m_Filename.c_str()),
            m_Location(other.m_Location),
            m_TranslId(other.m_TranslId),
            m_UnsavedFiles(),
            m_pResults(other.m_pResults),
            m_Diagnostics(other.m_Diagnostics)
        {
            for( std::map<wxString, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }
        }
        bool m_IsAuto;
        wxString m_Filename;
        ClTokenPosition m_Location;
        ClTranslUnitId m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
        std::vector<ClToken>* m_pResults; // Returned value
        std::vector<ClDiagnostic> m_Diagnostics;
    };

    /* final */
    class DocumentCCTokenJob : public SyncJob
    {
    public:
        DocumentCCTokenJob( wxEventType evtType, const int evtId, ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location, ClTokenId tknId ):
            SyncJob(DocumentCCTokenType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename),
            m_Location(location),
            m_TokenId(tknId),
            m_pResult(new wxString())
        {
        }

        ClangJob* Clone() const
        {
            DocumentCCTokenJob* pJob = new DocumentCCTokenJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            wxString str = clangproxy.DocumentCCToken( m_TranslId, m_TokenId );
            *m_pResult = str;
        }
        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResult;
        }
        ClTranslUnitId GetTranslationUnitId()
        {
            return m_TranslId;
        }
        const wxString& GetFilename()
        {
            return m_Filename;
        }
        const ClTokenPosition& GetLocation()
        {
            return m_Location;
        }
        const wxString& GetResult()
        {
            return *m_pResult;
        }
    private:
        DocumentCCTokenJob( const DocumentCCTokenJob& other ) :
            SyncJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Location(other.m_Location),
            m_TokenId(other.m_TokenId),
            m_pResult(other.m_pResult) {}
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        ClTokenPosition m_Location;
        ClTokenId m_TokenId;
        wxString* m_pResult;
    };
    /* final */
    class GetTokensAtJob : public SyncJob
    {
    public:
        GetTokensAtJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, int translId ):
            SyncJob(GetTokensAtType, evtType, evtId),
            m_Filename(filename),
            m_Location(location),
            m_TranslId(translId)
        {
            m_pResults = new wxStringVec();
        }

        ClangJob* Clone() const
        {
            GetTokensAtJob* pJob = new GetTokensAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }

        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetTokensAt( m_TranslId, m_Filename, m_Location, *m_pResults);
        }

        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResults;
        }

        const wxStringVec& GetResults()
        {
            return *m_pResults;
        }
    protected:
        GetTokensAtJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, int translId,
                        wxMutex* pMutex, wxCondition* pCond,
                        wxStringVec* pResults ):
            SyncJob(GetTokensAtType, evtType, evtId, pMutex, pCond),
            m_Filename(filename.c_str()),
            m_Location(location),
            m_TranslId(translId),
            m_pResults(pResults) {}
        wxString m_Filename;
        ClTokenPosition m_Location;
        int m_TranslId;
        wxStringVec* m_pResults;
    };

    /* final */
    class GetCallTipsAtJob : public SyncJob
    {
    public:
        GetCallTipsAtJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, int translId, const wxString& tokenStr ):
            SyncJob( GetCallTipsAtType, evtType, evtId),
            m_Filename(filename),
            m_Location(location),
            m_TranslId(translId),
            m_TokenStr(tokenStr)
        {
            m_pResults = new std::vector<wxStringVec>();
        }
        ClangJob* Clone() const
        {
            GetCallTipsAtJob* pJob = new GetCallTipsAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetCallTipsAt( m_TranslId, m_Filename, m_Location, m_TokenStr, *m_pResults);
        }

        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResults;
        }

        const std::vector<wxStringVec>& GetResults()
        {
            return *m_pResults;
        }
    protected:
        GetCallTipsAtJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, int translId, const wxString& tokenStr,
                          wxMutex* pMutex, wxCondition* pCond,
                          std::vector<wxStringVec>* pResults ):
            SyncJob( GetCallTipsAtType, evtType, evtId, pMutex, pCond),
            m_Filename(filename.c_str()),
            m_Location(location),
            m_TranslId(translId),
            m_TokenStr(tokenStr),
            m_pResults(pResults) {}
        wxString m_Filename;
        ClTokenPosition m_Location;
        int m_TranslId;
        wxString m_TokenStr;
        std::vector<wxStringVec>* m_pResults;
    };

    /* final */
    class GetOccurrencesOfJob : public SyncJob
    {
    public:
        GetOccurrencesOfJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, ClTranslUnitId translId ):
            SyncJob( GetOccurrencesOfType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename),
            m_Location(location)
        {
            m_pResults = new std::vector< std::pair<int, int> >();
        }
        ClangJob* Clone() const
        {
            GetOccurrencesOfJob* pJob = new GetOccurrencesOfJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetOccurrencesOf( m_TranslId, m_Filename, m_Location, *m_pResults);
        }

        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResults;
        }

        ClTranslUnitId GetTranslationUnitId()
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetLocation() const
        {
            return m_Location;
        }
        const std::vector< std::pair<int, int> >& GetResults() const
        {
            return *m_pResults;
        }
    protected:
        GetOccurrencesOfJob( wxEventType evtType, int evtId, const wxString& filename, const ClTokenPosition& location, int translId,
                             wxMutex* pMutex, wxCondition* pCond,
                             std::vector< std::pair<int, int> >* pResults ):
            SyncJob(GetOccurrencesOfType, evtType, evtId, pMutex, pCond),
            m_TranslId(translId),
            m_Filename(filename.c_str()),
            m_Location(location),
            m_pResults(pResults) {}
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        ClTokenPosition m_Location;
        std::vector< std::pair<int, int> >* m_pResults;
    };

    /**
     * Helper class that manages the lifecycle of the Get/SetEventObject() object when passing threads
     */
    class CallbackEvent : public wxEvent
    {
    public:
        CallbackEvent( wxEventType evtType, int evtId, ClangJob* job ) : wxEvent( evtType, evtId )
        {
            SetEventObject(job);
        }
        CallbackEvent( const CallbackEvent& other ) : wxEvent(other)
        {
            ClangProxy::ClangJob* pJob = static_cast<ClangProxy::ClangJob*>(other.GetEventObject());
            if (pJob)
                SetEventObject( pJob->Clone() );
        }
        ~CallbackEvent()
        {
            wxObject* obj = GetEventObject();
            delete obj;
        }
        wxEvent* Clone() const
        {
            ClangProxy::ClangJob* pJob = static_cast<ClangProxy::ClangJob*>(GetEventObject());
            if (pJob )
                pJob = pJob->Clone();
            return new CallbackEvent( m_eventType, m_id, pJob );
        }
    };

    class SerializeTokenDatabaseJob : public ClangJob
    {
    public:
        SerializeTokenDatabaseJob( const ClTokenDatabase& db, const wxString& filename ) :
            ClangJob(SerializeTokenDatabaseType),
            m_TokenDatabase(db),
            m_Filename( filename )
        {}
        ClangJob* Clone() const
        {
            SerializeTokenDatabaseJob* pJob = new SerializeTokenDatabaseJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& /*clangproxy*/)
        {
            wxFileOutputStream out(m_Filename);
            if (out.IsOk())
                ClTokenDatabase::WriteOut( m_TokenDatabase, out );
        }
    private:
        SerializeTokenDatabaseJob( const SerializeTokenDatabaseJob& other ) :
            ClangJob(other),
            m_TokenDatabase(other.m_TokenDatabase),
            m_Filename( other.m_Filename.c_str() ) {}
        ClTokenDatabase m_TokenDatabase;
        wxString m_Filename;
    };

    class LoadTokenDatabaseJob : public ClangJob
    {
    public:
        LoadTokenDatabaseJob( const ClTokenDatabase& db, const wxString& filename ) :
            ClangJob(SerializeTokenDatabaseType),
            m_TokenDatabase(db),
            m_Filename( filename )
        {}
        ClangJob* Clone() const
        {
            LoadTokenDatabaseJob* pJob = new LoadTokenDatabaseJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& /*clangproxy*/)
        {
            wxFileInputStream in(m_Filename);
            if (in.IsOk())
                ClTokenDatabase::ReadIn( m_TokenDatabase, in );
        }
    private:
        LoadTokenDatabaseJob( const LoadTokenDatabaseJob& other ) :
            ClangJob(other),
            m_TokenDatabase(other.m_TokenDatabase),
            m_Filename( other.m_Filename.c_str() ) {}
        ClTokenDatabase m_TokenDatabase;
        wxString m_Filename;
    };

public:
    ClangProxy( wxEvtHandler* pEvtHandler, ClTokenDatabase& database, PersistencyManager& persistency, const std::vector<wxString>& cppKeywords);
    ~ClangProxy();

    /** Append a job to the end of the queue */
    void AppendPendingJob( ClangProxy::ClangJob& job );
    //void PrependPendingJob( ClangProxy::ClangJob& job );

    ClTranslUnitId GetTranslationUnitId(ClTranslUnitId CtxTranslUnitId, ClFileId fId);
    ClTranslUnitId GetTranslationUnitId(ClTranslUnitId CtxTranslUnitId, const wxString& filename);

    void SetPersistency( PersistencyManager& persistency )
    {
        m_Persistency = persistency;
    }
    wxString GetTokenDatabaseFilename();
    wxString GetPchFilename( const wxString& filename );

    void SetExtraCompileArgs( const wxString& cmds );
    void BuildCompileArgs( const wxString& filename, const wxString& cmd, std::vector<wxCharBuffer>& out_args );
protected: // jobs that are run only on the thread
    void CreateTranslationUnit(const wxString& filename, const wxString& compileCommand,  const std::map<wxString, wxString>& unsavedFiles, ClTranslUnitId& out_TranslId, ClTokenDatabase& tokenDatabase);
    void RemoveTranslationUnit(ClTranslUnitId TranslUnitId);
    /** Reparse translation id
     *
     * @param unsavedFiles reference to the unsaved files data. This function takes the data and this list will be empty after this call
     */
    void Reparse(         ClTranslUnitId translId, const wxString& compileCommand, const std::map<wxString, wxString>& unsavedFiles, ClTokenDatabase &tokenDatabase);
    void GetDiagnostics(  ClTranslUnitId translId, const wxString& filename, std::vector<ClDiagnostic>& diagnostics);
    void CodeCompleteAt(  ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location,
                          bool isAuto, const std::map<wxString, wxString>& unsavedFiles, std::vector<ClToken>& results, std::vector<ClDiagnostic>& diagnostics);
    wxString DocumentCCToken( ClTranslUnitId translId, int tknId );
    void GetTokensAt(     ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location, std::vector<wxString>& results);
    void GetCallTipsAt(   ClTranslUnitId translId,const wxString& filename, const ClTokenPosition& location,
                          const wxString& tokenStr, std::vector<wxStringVec>& results);
    void GetOccurrencesOf(ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location,
                          std::vector< std::pair<int, int> >& results);
    void RefineTokenType( ClTranslUnitId translId, int tknId, int& tknType); // TODO: cache TokenId (if resolved) for DocumentCCToken()

public:
    wxString GetCCInsertSuffix( ClTranslUnitId translId, int tknId, const wxString& newLine, std::vector< std::pair<int, int> >& offsets );
    bool ResolveDeclTokenAt( const ClTranslUnitId translId, const wxString& filename, wxString& out_filename, ClTokenPosition& out_location);
    bool ResolveDefinitionTokenAt( const ClTranslUnitId translUnitId, const wxString& filename, wxString& out_filename, ClTokenPosition& inout_location);

    void GetFunctionScopeAt( ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location, wxString &out_ClassName, wxString &out_FunctionName );
    std::vector<std::pair<wxString, wxString> > GetFunctionScopes( ClTranslUnitId, const wxString& filename );


private:
    mutable wxMutex m_Mutex;
    //wxString m_PersistencyDirectory;
    PersistencyManager& m_Persistency;
    ClTokenDatabase& m_Database;
    const std::vector<wxString>& m_CppKeywords;
    std::vector<ClTranslationUnit> m_TranslUnits;
    CXIndex m_ClIndex;
    wxString m_ExtraCompileCommands;
private: // Thread
    wxEvtHandler* m_pEventCallbackHandler;
    BackgroundThread* m_pThread;
    BackgroundThread* m_pIndexerThread;
};

#endif // CLANGPROXY_H

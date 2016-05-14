#ifndef CLANGPROXY_H
#define CLANGPROXY_H

#include "clangpluginapi.h"
#include "translationunit.h"
#include "cclogger.h"

#include <map>
#include <vector>
#include <list>
#include <wx/string.h>
#include <queue>
#include <backgroundthread.h>


#undef CLANGPROXY_TRACE_FUNCTIONS

class ClTranslationUnit;
class ClTokenDatabase;
class ClangProxy;

typedef void* CXIndex;
typedef int ClFileId;

class ClangProxy
{
public:
    /** @brief Base class for a Clang job.
     *
     *  This class is designed to be subclassed and the Execute() call be overridden.
     */
    /*abstract */
    class ClangJob : public AbstractJob, public wxObject
    {
    public:
        enum JobType
        {
            CreateTranslationUnitType,
            RemoveTranslationUnitType,
            ReparseType,
            UpdateTokenDatabaseType,
            GetDiagnosticsType,
            CodeCompleteAtType,
            DocumentCCTokenType,
            GetTokensAtType,
            GetCallTipsAtType,
            GetOccurrencesOfType,
            GetFunctionScopeAtType,
            ReindexFileType,
            LookupDefinitionType
        };
    protected:
        ClangJob(JobType jt) :
            AbstractJob(),
            wxObject(),
            m_JobType(jt),
            m_pProxy(nullptr),
            m_Timestamp( wxDateTime::Now() )
        {
        }
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         */
        ClangJob( const ClangJob& other ) :
            AbstractJob(),
            wxObject(),
            m_JobType( other.m_JobType),
            m_pProxy( other.m_pProxy ),
            m_Timestamp( other.m_Timestamp)
        {
        }

    public:
        /// Returns a copy of this job on the heap to make sure the objects lifecycle is guaranteed across threads
        virtual ClangJob* Clone() const = 0;
        // Called on job thread
        virtual void Execute(ClangProxy& WXUNUSED(clangproxy)) = 0;
        // Called on job thread
        virtual void Completed(ClangProxy& WXUNUSED(clangproxy)) {}
        // Called on job thread
        void SetProxy(ClangProxy* pProxy)
        {
            m_pProxy = pProxy;
        }
        JobType GetJobType() const
        {
            return m_JobType;
        }
        const wxDateTime& GetTimestamp() const
        {
            return m_Timestamp;
        }
    public:
        void operator()()
        {
            assert(m_pProxy != nullptr);
            Execute(*m_pProxy);
            Completed(*m_pProxy);
        }
    protected:
        JobType     m_JobType;
        ClangProxy* m_pProxy;
        wxDateTime  m_Timestamp;
    };

    /**
     * @brief ClangJob that posts a wxEvent back when completed
     */
    /* abstract */
    class EventJob : public ClangJob
    {
    protected:
        /** @brief Constructor
         *
         * @param jt JobType from the enum.
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        EventJob(const JobType jt, const wxEventType evtType, const int evtId) :
            ClangJob(jt),
            m_EventType(evtType),
            m_EventId(evtId),
            m_CreationTime(wxDateTime::GetTimeNow())
        {
        }
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         */
        EventJob( const EventJob& other ) :
            ClangJob(other.m_JobType),
            m_EventType( other.m_EventType ),
            m_EventId( other.m_EventId ),
            m_CreationTime( other.m_CreationTime )
        {}
    public:
        // Called on job thread
        /** @brief Function that is called on the job thread when the job is complete.
         *
         * @param clangProxy ClangProxy&
         * @return virtual void
         *
         *  This virtual base function will send a wxEvent with the wxEventType
         *  and id taken from the constructor and passes the job to the main UI
         *  event handler. The job will also be destroyed on the main UI.
         */
        virtual void Completed(ClangProxy& clangProxy)
        {
            if (clangProxy.m_pEventCallbackHandler && (m_EventType != 0))
            {
                ClangProxy::JobCompleteEvent evt(m_EventType, m_EventId, this);
                clangProxy.m_pEventCallbackHandler->AddPendingEvent(evt);
            }
        }
        const wxDateTime& GetCreationTime() const
        {
            return m_CreationTime;
        }
    private:
        const wxEventType m_EventType;
        const int         m_EventId;
        wxDateTime        m_CreationTime;
    };

    /* final */
    class CreateTranslationUnitJob : public EventJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        CreateTranslationUnitJob( const wxEventType evtType, const int evtId, const wxString& filename, const wxString& commands, const std::map<wxString, wxString>& unsavedFiles ) :
            EventJob(CreateTranslationUnitType, evtType, evtId),
            m_Filename(filename),
            m_Commands(commands),
            m_TranslationUnitId(-1),
            m_UnsavedFiles(unsavedFiles)
        {
        }
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
                clangproxy.CreateTranslationUnit(m_Filename, m_Commands, m_UnsavedFiles, m_TranslationUnitId);
            }
            m_UnsavedFiles.clear();
        }
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslationUnitId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
    protected:
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        CreateTranslationUnitJob(const CreateTranslationUnitJob& other):
            EventJob(other),
            m_Filename(other.m_Filename.c_str()),
            m_Commands(other.m_Commands.c_str()),
            m_TranslationUnitId(other.m_TranslationUnitId),
            m_UnsavedFiles()
        {
            /* deep copy */
            for ( std::map<wxString, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }
        }
    public:
        wxString m_Filename;
        wxString m_Commands;
        ClTranslUnitId m_TranslationUnitId; // Returned value
        std::map<wxString, wxString> m_UnsavedFiles;
    };

    /** @brief Remove a translation unit from memory
     */
    /* final */
    class RemoveTranslationUnitJob : public EventJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        RemoveTranslationUnitJob( const wxEventType evtType, const int evtId, int TranslUnitId ) :
            EventJob(RemoveTranslationUnitType, evtType, evtId),
            m_TranslUnitId(TranslUnitId) {}
        ClangJob* Clone() const
        {
            RemoveTranslationUnitJob* job = new RemoveTranslationUnitJob(*this);
            return static_cast<ClangJob*>(job);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.RemoveTranslationUnit(m_TranslUnitId);
        }
    protected:
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        RemoveTranslationUnitJob(const RemoveTranslationUnitJob& other):
            EventJob(other),
            m_TranslUnitId(other.m_TranslUnitId) {}
        ClTranslUnitId m_TranslUnitId;
    };

    /* final */
    /** @brief Reparse a translation unit job.
     */
    class ReparseJob : public EventJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        ReparseJob( const wxEventType evtType, const int evtId, ClTranslUnitId translId, const wxString& compileCommand, const wxString& filename, const std::map<wxString, wxString>& unsavedFiles, bool parents = false )
            : EventJob(ReparseType, evtType, evtId),
              m_TranslId(translId),
              m_UnsavedFiles(unsavedFiles),
              m_CompileCommand(compileCommand.c_str()),
              m_Filename(filename.c_str()),
              m_Parents(parents)
        {
        }
        ClangJob* Clone() const
        {
            return new ReparseJob(*this);
        }
        void Execute(ClangProxy& clangproxy);
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
    private:
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        ReparseJob( const ReparseJob& other )
            : EventJob(other),
              m_TranslId(other.m_TranslId),
              m_UnsavedFiles(),
              m_CompileCommand(other.m_CompileCommand.c_str()),
              m_Filename(other.m_Filename.c_str()),
              m_Parents(other.m_Parents)
        {
            /* deep copy */
            for (std::map<wxString, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }
        }
    public:
        ClTranslUnitId m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
        wxString m_CompileCommand;
        wxString m_Filename;
        bool m_Parents; // If the parents also need to be reparsed
    };

    /* final */
    /** @brief Update the tokendatabase with tokens from a translation unit job
     */
    class UpdateTokenDatabaseJob : public EventJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        UpdateTokenDatabaseJob( const wxEventType evtType, const int evtId, int translId ) :
            EventJob(UpdateTokenDatabaseType, evtType, evtId),
            m_TranslId(translId)
        {
        }
        ClangJob* Clone() const
        {
            return new UpdateTokenDatabaseJob(*this);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.UpdateTokenDatabase(m_TranslId);
        }
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }

    private:
        ClTranslUnitId m_TranslId;
    };

    /* final */
    /** @brief Request diagnostics job.
     */
    class GetDiagnosticsJob : public EventJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        GetDiagnosticsJob( const wxEventType evtType, const int evtId, int translId, const wxString& filename ):
            EventJob(GetDiagnosticsType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename)
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
        ClTranslUnitId GetTranslationUnitId() const
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
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        GetDiagnosticsJob( const GetDiagnosticsJob& other ) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Results(other.m_Results) {}
    public:
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        std::vector<ClDiagnostic> m_Results; // Returned value
    };

    /** @brief Find the function scope of a code position job
     */
    class GetFunctionScopeAtJob : public EventJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        GetFunctionScopeAtJob( const wxEventType evtType, const int evtId, int translId, const wxString& filename, const ClTokenPosition& position) :
            EventJob(GetFunctionScopeAtType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename),
            m_Position(position) {}
        ClangJob* Clone() const
        {
            GetFunctionScopeAtJob* pJob = new GetFunctionScopeAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetFunctionScopeAt(m_TranslId, m_Filename, m_Position, m_ScopeName, m_MethodName);
        }

    protected:
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        GetFunctionScopeAtJob( const GetFunctionScopeAtJob& other ) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Position(other.m_Position),
            m_ScopeName(other.m_ScopeName.c_str()),
            m_MethodName(other.m_MethodName.c_str())
        {
        }
    public:
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        ClTokenPosition m_Position;
        wxString m_ScopeName;
        wxString m_MethodName;
    };

    class LookupDefinitionJob : public EventJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        LookupDefinitionJob( const wxEventType evtType, const int evtId, int translId, const wxString& filename, const ClTokenPosition& position) :
            EventJob(LookupDefinitionType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename),
            m_Position(position) {}
        ClangJob* Clone() const
        {
            LookupDefinitionJob* pJob = new LookupDefinitionJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            CCLogger::Get()->DebugLog(wxT("LookupDefinition"));
            ClTokenPosition pos(0,0);
            if (clangproxy.ResolveDefinitionTokenAt( m_TranslId, m_Filename, m_Position, pos))
            {
                CCLogger::Get()->DebugLog(wxT("Token definition found in own TU"));
                m_Locations.push_back( std::make_pair(m_Filename, pos) );
                return;
            }
            if (clangproxy.GetTokenAt( m_TranslId, m_Filename, m_Position, m_TokenIdentifier, m_TokenUSR ))
            {
                CCLogger::Get()->DebugLog(wxT("TokenIdentifier='")+m_TokenIdentifier+wxT("' USR=")+m_TokenUSR);
                std::set<ClFileId> fileIdList = clangproxy.m_Database.LookupTokenFileList( m_TokenIdentifier, ClTokenType_DefGroup );
                std::set<ClFileId> unhandledFileIdList;
                CCLogger::Get()->DebugLog(F(wxT("LookupTokenFileList returned %d results"), fileIdList.size()));
                for ( std::set<ClFileId>::const_iterator it = fileIdList.begin(); it != fileIdList.end(); ++it)
                {
                    ClTokenPosition pos(0,0);
                    if (clangproxy.LookupTokenDefinition(*it, m_TokenIdentifier, m_TokenUSR, pos) )
                    {
                        wxString filename = clangproxy.m_Database.GetFilename( *it );
                        m_Locations.push_back( std::make_pair( filename, pos ) );
                    }
                    else
                    {
                        unhandledFileIdList.insert( *it );
                    }
                }
                if (m_Locations.size() > 0)
                {
                    CCLogger::Get()->DebugLog( wxT("Locations found in loaded TU") );
                    return;
                }
                CCLogger::Get()->DebugLog( F(wxT("Found %d definitions"), (int)m_Locations.size()) );
            }
        }
        int GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetPosition() const
        {
            return m_Position;
        }
        const std::vector< std::pair<wxString, ClTokenPosition> >& GetResults() const
        {
            return m_Locations;
        }
        const wxString& GetTokenIdentifier() const
        {
            return m_TokenIdentifier;
        }
        const wxString& GetTokenUSR() const
        {
            return m_TokenUSR;
        }
    protected:
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        LookupDefinitionJob( const LookupDefinitionJob& other ) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Position(other.m_Position),
            m_Locations(other.m_Locations),
            m_TokenIdentifier(other.m_TokenIdentifier),
            m_TokenUSR(other.m_TokenUSR)
        {
        }
    protected:
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        ClTokenPosition m_Position;
        std::vector< std::pair<wxString, ClTokenPosition> > m_Locations;
        wxString m_TokenIdentifier;
        wxString m_TokenUSR;
    };


    class LookupDefinitionInFilesJob : public LookupDefinitionJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        LookupDefinitionInFilesJob( const wxEventType evtType, const int evtId, int translId, const wxString& filename, const ClTokenPosition& position, const std::vector< std::pair<wxString,wxString> >& fileAndCompileCommands ) :
            LookupDefinitionJob(evtType, evtId, translId, filename, position),
            m_fileAndCompileCommands(fileAndCompileCommands){}
        ClangJob* Clone() const
        {
            LookupDefinitionInFilesJob* pJob = new LookupDefinitionInFilesJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            CCLogger::Get()->DebugLog(wxT("LookupDefinition"));
            wxString tokenIdentifier;
            wxString usr;
            if (clangproxy.GetTokenAt( m_TranslId, m_Filename, m_Position, tokenIdentifier, usr ))
            {
                //CCLogger::Get()->DebugLog(wxT("TokenIdentifier='")+tokenIdentifier+wxT("' USR=")+usr);
                //std::set<ClFileId> fileIdList = clangproxy.m_Database.LookupTokenFileList( tokenIdentifier, ClTokenType_DefGroup );
                CCLogger::Get()->DebugLog( wxT("Finding definition in non-loaded TUs") );
                CXIndex clangIndex = clang_createIndex(0,0);

                for (std::vector< std::pair<wxString,wxString> >::const_iterator it = m_fileAndCompileCommands.begin(); it != m_fileAndCompileCommands.end(); ++it)
                {
                    std::vector<wxCharBuffer> argsBuffer;
                    std::vector<const char*> args;
                    clangproxy.BuildCompileArgs( it->first, it->second, argsBuffer, args );
                    ClFileId destFileId = clangproxy.m_Database.GetFilenameId(it->first);
                    {
                        ClTranslationUnit tu(clangproxy.m_Database, 127, clangIndex);
                        const std::map<wxString, wxString> unsavedFiles; // No unsaved files for reindex...
                        CCLogger::Get()->DebugLog( wxT("Parsing file ")+it->first);
                        if (!tu.Parse( it->first, destFileId, args, unsavedFiles, false ))
                            CCLogger::Get()->DebugLog(wxT("Could not parse file ")+it->first);
                        else
                        {
                            CCLogger::Get()->DebugLog(wxT("Building tokendatabase from TU"));
                            ClTokenDatabase db(clangproxy.m_Database);
                            if (tu.ProcessAllTokens( NULL, NULL, &db ))
                            {
                                ClTokenPosition pos(0,0);
                                CCLogger::Get()->DebugLog(wxT("Looking up token definition in db"));
                                if (db.LookupTokenDefinition( destFileId, tokenIdentifier, usr, pos ))
                                {
                                    m_Locations.push_back( std::make_pair( it->first, pos ) );
                                }
                            }
                        }
                    }
                }
                clang_disposeIndex(clangIndex);
                CCLogger::Get()->DebugLog( F(wxT("Found %d definitions"), (int)m_Locations.size()) );
            }
        }
    protected:
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        LookupDefinitionInFilesJob( const LookupDefinitionInFilesJob& other ) :
            LookupDefinitionJob(other),
            m_fileAndCompileCommands(other.m_fileAndCompileCommands)
        {
        }
    private:
        std::vector< std::pair<wxString,wxString> > m_fileAndCompileCommands;
    };

    /*abstract */
    /** @brief Base class job designed to be run (partially) synchronous.
     *
     *  When the job is posted, the user can wait for completion of this job (with timout).
     */
    class SyncJob : public EventJob
    {
    protected:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        SyncJob(JobType jt, const wxEventType evtType, const int evtId) :
            EventJob(jt, evtType, evtId),
            m_bCompleted(false),
            m_pMutex(new wxMutex()),
            m_pCond(new wxCondition(*m_pMutex))
        {
        }
        SyncJob(JobType jt, const wxEventType evtType, const int evtId, wxMutex* pMutex, wxCondition* pCond) :
            EventJob(jt, evtType, evtId),
            m_bCompleted(false),
            m_pMutex(pMutex),
            m_pCond(pCond) {}
    public:
        // Called on Job thread
        virtual void Completed(ClangProxy& clangproxy)
        {
            {
                wxMutexLocker lock(*m_pMutex);
                m_bCompleted = true;
                m_pCond->Signal();
            }
            EventJob::Completed(clangproxy);
        }
        /// Called on main thread to wait for completion of this job.
        wxCondError WaitCompletion(unsigned long milliseconds)
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
        static unsigned int s_SerialNo;
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        CodeCompleteAtJob( const wxEventType evtType, const int evtId,
                           const wxString& filename, const ClTokenPosition& position,
                           const ClTranslUnitId translId, const std::map<wxString, wxString>& unsavedFiles,
                           bool includeCtors ):
            SyncJob(CodeCompleteAtType, evtType, evtId),
            m_SerialNo( ++s_SerialNo ),
            m_Filename(filename),
            m_Position(position),
            m_TranslId(translId),
            m_UnsavedFiles(unsavedFiles),
            m_IncludeCtors(includeCtors),
            m_pResults(new std::vector<ClToken>()),
            m_Diagnostics()
        {
        }
        bool operator==(CodeCompleteAtJob& other)const
        {
            if (m_SerialNo == other.m_SerialNo)
                return true;
            return false;
        }

        ClangJob* Clone() const
        {
            CodeCompleteAtJob* pJob = new CodeCompleteAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            std::vector<ClToken> results;
            clangproxy.CodeCompleteAt(m_TranslId, m_Filename, m_Position, m_UnsavedFiles, results, m_Diagnostics);
            for (std::vector<ClToken>::iterator tknIt = results.begin(); tknIt != results.end(); ++tknIt)
            {
                switch (tknIt->category)
                {
                case tcCtorPublic:
                case tcDtorPublic:
                    if ( !m_IncludeCtors )
                        continue;
                case tcClass:
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

            // Get rid of some copied memory we no longer need
            m_UnsavedFiles.clear();

            m_pResults->swap(results);
        }
        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResults;
        }
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetPosition() const
        {
            return m_Position;
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
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        CodeCompleteAtJob( const CodeCompleteAtJob& other ) :
            SyncJob(other),
            m_SerialNo(other.m_SerialNo),
            m_Filename(other.m_Filename.c_str()),
            m_Position(other.m_Position),
            m_TranslId(other.m_TranslId),
            m_IncludeCtors(other.m_IncludeCtors),
            m_pResults(other.m_pResults),
            m_Diagnostics(other.m_Diagnostics)
        {
            for ( std::map<wxString, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( wxString(it->first.c_str()), wxString(it->second.c_str()) ) );
            }
        }
        unsigned int m_SerialNo;
        wxString m_Filename;
        ClTokenPosition m_Position;
        ClTranslUnitId m_TranslId;
        std::map<wxString, wxString> m_UnsavedFiles;
        bool m_IncludeCtors;
        std::vector<ClToken>* m_pResults; // Returned value
        std::vector<ClDiagnostic> m_Diagnostics;
    };

    /* final */
    class DocumentCCTokenJob : public SyncJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        DocumentCCTokenJob( const wxEventType evtType, const int evtId, ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& position, ClTokenId tknId ):
            SyncJob(DocumentCCTokenType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename),
            m_Position(position),
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
            wxString str = clangproxy.DocumentCCToken(m_TranslId, m_TokenId);
            *m_pResult = str;
        }
        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResult;
        }
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetPosition() const
        {
            return m_Position;
        }
        const wxString& GetResult()
        {
            return *m_pResult;
        }
    protected:
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        DocumentCCTokenJob( const DocumentCCTokenJob& other ) :
            SyncJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Position(other.m_Position),
            m_TokenId(other.m_TokenId),
            m_pResult(other.m_pResult) {}
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        ClTokenPosition m_Position;
        ClTokenId m_TokenId;
        wxString* m_pResult;
    };
    /* final */
    class GetTokensAtJob : public SyncJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        GetTokensAtJob( const wxEventType evtType, const int evtId, const wxString& filename, const ClTokenPosition& position, int translId ):
            SyncJob(GetTokensAtType, evtType, evtId),
            m_Filename(filename),
            m_Position(position),
            m_TranslId(translId),
            m_pResults(new wxStringVec())
        {
        }

        ClangJob* Clone() const
        {
            GetTokensAtJob* pJob = new GetTokensAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }

        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetTokensAt(m_TranslId, m_Filename, m_Position, *m_pResults);
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
        GetTokensAtJob( const wxEventType evtType, const int evtId, const wxString& filename, const ClTokenPosition& position, int translId,
                        wxMutex* pMutex, wxCondition* pCond,
                        wxStringVec* pResults ):
            SyncJob(GetTokensAtType, evtType, evtId, pMutex, pCond),
            m_Filename(filename.c_str()),
            m_Position(position),
            m_TranslId(translId),
            m_pResults(pResults) {}
        wxString m_Filename;
        ClTokenPosition m_Position;
        ClTranslUnitId m_TranslId;
        wxStringVec* m_pResults;
    };

    /* final */
    class GetCallTipsAtJob : public SyncJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        GetCallTipsAtJob( const wxEventType evtType, const int evtId, const wxString& filename,
                          const ClTokenPosition& position, int translId, const wxString& tokenStr ):
            SyncJob( GetCallTipsAtType, evtType, evtId),
            m_Filename(filename),
            m_Position(position),
            m_TranslId(translId),
            m_TokenStr(tokenStr),
            m_pResults(new std::vector<wxStringVec>())
        {
        }
        ClangJob* Clone() const
        {
            GetCallTipsAtJob* pJob = new GetCallTipsAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetCallTipsAt( m_TranslId, m_Filename, m_Position, m_TokenStr, *m_pResults);
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
        GetCallTipsAtJob( const wxEventType evtType, const int evtId, const wxString& filename, const ClTokenPosition& position, int translId, const wxString& tokenStr,
                          wxMutex* pMutex, wxCondition* pCond,
                          std::vector<wxStringVec>* pResults ):
            SyncJob(GetCallTipsAtType, evtType, evtId, pMutex, pCond),
            m_Filename(filename.c_str()),
            m_Position(position),
            m_TranslId(translId),
            m_TokenStr(tokenStr.c_str()),
            m_pResults(pResults) {}
        wxString m_Filename;
        ClTokenPosition m_Position;
        ClTranslUnitId m_TranslId;
        wxString m_TokenStr;
        std::vector<wxStringVec>* m_pResults;
    };

    /* final */
    class GetOccurrencesOfJob : public EventJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        GetOccurrencesOfJob( const wxEventType evtType, const int evtId, const wxString& filename,
                             const ClTokenPosition& position, ClTranslUnitId translId ):
            EventJob( GetOccurrencesOfType, evtType, evtId),
            m_TranslId(translId),
            m_Filename(filename),
            m_Position(position)
        {
        }
        ClangJob* Clone() const
        {
            GetOccurrencesOfJob* pJob = new GetOccurrencesOfJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetOccurrencesOf(m_TranslId, m_Filename, m_Position, m_Results);
        }

        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        const wxString& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetPosition() const
        {
            return m_Position;
        }
        const std::vector< std::pair<int, int> >& GetResults() const
        {
            return m_Results;
        }
    protected:
        GetOccurrencesOfJob( const GetOccurrencesOfJob& other) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Filename(other.m_Filename.c_str()),
            m_Position(other.m_Position),
            m_Results(other.m_Results){}
        ClTranslUnitId m_TranslId;
        wxString m_Filename;
        ClTokenPosition m_Position;
        std::vector< std::pair<int, int> > m_Results;
    };

   class ReindexFileJob : public EventJob
    {
    public:
        ReindexFileJob( const wxEventType evtType, const int evtId, const wxString& filename, const wxString& commands ):
            EventJob(ReindexFileType, evtType, evtId),
            m_Filename(filename),
            m_Commands(commands)
        {
        }
        ClangJob* Clone() const
        {
            ReindexFileJob* pJob = new ReindexFileJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            CXIndex clangIndex = clang_createIndex(0,0);
            { // Scope for TU
                ClTranslationUnit tu(clangproxy.m_Database, 127, clangIndex);
                ClFileId fileId = clangproxy.m_Database.GetFilenameId(m_Filename);
                std::vector<wxCharBuffer> argsBuffer;
                std::vector<const char*> args;
                clangproxy.BuildCompileArgs( m_Filename, m_Commands, argsBuffer, args );
                const std::map<wxString, wxString> unsavedFiles; // No unsaved files for reindex...
                if (!tu.Parse( m_Filename, fileId, args, unsavedFiles, false ))
                {
                    CCLogger::Get()->DebugLog(wxT("Reindex: Parsing failed for ")+m_Filename);
                }
                else
                {
                    std::vector<ClFileId> includeFileList;
                    ClFunctionScopeMap functionScopes;
                    ClTokenDatabase tokenDatabase(clangproxy.m_Database);
                    tu.ProcessAllTokens( &includeFileList, &functionScopes, &tokenDatabase );
                    tokenDatabase.StoreIndexes();
                }
            }
            clang_disposeIndex(clangIndex);
        }

        const wxString& GetFilename() const
        {
            return m_Filename;
        }
    protected:
        ReindexFileJob( const ReindexFileJob& other) :
            EventJob(other),
            m_Filename(other.m_Filename.c_str()),
            m_Commands(other.m_Commands.c_str()){}
        wxString m_Filename;
        wxString m_Commands;
    };

    /**
     * @brief Helper class that manages the lifecycle of the Get/SetEventObject() object when passing threads
     */
    class JobCompleteEvent : public wxEvent
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to indicate that the job is completed
         * @param evtId Event ID to indicate that the job is completed
         *
         */
        JobCompleteEvent( const wxEventType evtType, const int evtId, ClangJob* job ) : wxEvent( evtType, evtId )
        {
            SetEventObject(job);
        }
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         */
        JobCompleteEvent( const JobCompleteEvent& other ) : wxEvent(other)
        {
            ClangProxy::ClangJob* pJob = static_cast<ClangProxy::ClangJob*>(other.GetEventObject());
            if (pJob)
                SetEventObject( pJob->Clone() );
        }
        ~JobCompleteEvent()
        {
            wxObject* obj = GetEventObject();
            delete obj;
        }
        wxEvent* Clone() const
        {
            ClangProxy::ClangJob* pJob = static_cast<ClangProxy::ClangJob*>(GetEventObject());
            if (pJob)
                pJob = pJob->Clone();
            return new JobCompleteEvent(m_eventType, m_id, pJob);
        }
    };

public:
    ClangProxy(wxEvtHandler* pEvtHandler, ClTokenIndexDatabase& database, const std::vector<wxString>& cppKeywords);
    ~ClangProxy();

    /** Append a job to the end of the queue */
    void AppendPendingJob( ClangProxy::ClangJob& job );
    //void PrependPendingJob( ClangProxy::ClangJob& job );

    ClTranslUnitId GetTranslationUnitId( const ClTranslUnitId CtxTranslUnitId, ClFileId fId);
    ClTranslUnitId GetTranslationUnitId( const ClTranslUnitId CtxTranslUnitId, const wxString& filename);
    void GetAllTranslationUnitIds( std::set<ClTranslUnitId>& out_list ) const;

protected: // jobs that are run only on the thread
    void CreateTranslationUnit( const wxString& filename, const wxString& compileCommand,  const std::map<wxString, wxString>& unsavedFiles, ClTranslUnitId& out_TranslId);
    void RemoveTranslationUnit( const ClTranslUnitId TranslUnitId );
    /** Reparse translation id
     *
     * @param unsavedFiles reference to the unsaved files data. This function takes the data and this list will be empty after this call
     */
    void Reparse(         const ClTranslUnitId translId, const wxString& compileCommand, const std::map<wxString, wxString>& unsavedFiles);

    /** Update token database with all tokens from the passed translation unit id
     * @param translId The ID of the intended translation unit
     */
    void UpdateTokenDatabase( const ClTranslUnitId translId );
    void GetDiagnostics(  const ClTranslUnitId translId, const wxString& filename, std::vector<ClDiagnostic>& diagnostics);
    void CodeCompleteAt(  const ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& location,
                          const std::map<wxString, wxString>& unsavedFiles, std::vector<ClToken>& results, std::vector<ClDiagnostic>& diagnostics);
    wxString DocumentCCToken( ClTranslUnitId translId, int tknId );
    void GetTokensAt(     const ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& position, std::vector<wxString>& results);
    void GetCallTipsAt(   const ClTranslUnitId translId,const wxString& filename, const ClTokenPosition& position,
                          const wxString& tokenStr, std::vector<wxStringVec>& results);
    void GetOccurrencesOf(const ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& position,
                          std::vector< std::pair<int, int> >& results);
    void RefineTokenType( const ClTranslUnitId translId, int tknId, ClTokenCategory& out_tknType); // TODO: cache TokenId (if resolved) for DocumentCCToken()
    bool GetTokenAt( const ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& position, wxString& out_Identifier, wxString& out_USR );
    bool LookupTokenDefinition( const ClFileId fileId, const wxString& identifier, const wxString& usr, ClTokenPosition& out_position);

public: // Tokens
    wxString GetCCInsertSuffix( const  ClTranslUnitId translId, int tknId, bool isDecl, const wxString& newLine, std::vector< std::pair<int, int> >& offsets );
    bool ResolveDeclTokenAt( const ClTranslUnitId translId, wxString& filename, const ClTokenPosition& position, ClTokenPosition& out_Position);
    bool ResolveDefinitionTokenAt( const ClTranslUnitId translUnitId, wxString& filename, const ClTokenPosition& position, ClTokenPosition& out_Position);

public: // Function scopes
    void GetFunctionScopeAt( const ClTranslUnitId translId, const wxString& filename, const ClTokenPosition& position, wxString &out_ClassName, wxString &out_FunctionName );
    void GetFunctionScopes( const ClTranslUnitId, const wxString& filename, std::vector<std::pair<wxString, wxString> >& out_Scopes  );
    void GetFunctionScopePosition( const ClTranslUnitId id, const wxString& filename, const wxString& scopeName, const wxString& functionName, ClTokenPosition& out_Position);

private: // Utility functions
    void BuildCompileArgs(const wxString& filename, const wxString& commands, std::vector<wxCharBuffer>& argsBuffer, std::vector<const char*>& out_args) const;

private:
    mutable wxMutex m_Mutex;
    ClTokenIndexDatabase& m_Database;
    const std::vector<wxString>& m_CppKeywords;
    std::vector<ClTranslationUnit> m_TranslUnits;
    CXIndex m_ClIndex;
private: // Thread
    wxEvtHandler* m_pEventCallbackHandler;
    BackgroundThread* m_pThread;
    BackgroundThread* m_pReindexThread;
};

#endif // CLANGPROXY_H

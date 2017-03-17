#ifndef CLANGPROXY_H
#define CLANGPROXY_H

#include "clangpluginapi.h"
#include "translationunit.h"
#include "cclogger.h"

#include <map>
#include <vector>
#include <list>
#include <wx/string.h>
#include <wx/wfstream.h>
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
            LookupDefinitionType,
            StoreTokenIndexDBType,
            LookupTokenUsersAtType
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
        CreateTranslationUnitJob( const wxEventType evtType, const int evtId, const ClangFile& file, const std::vector<std::string>& commands, const std::map<std::string, wxString>& unsavedFiles ) :
            EventJob(CreateTranslationUnitType, evtType, evtId),
            m_Project(file.GetProject().ToUTF8().data()),
            m_Filename(file.GetFilename().ToUTF8().data()),
            m_CompileCommand(commands),
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
            m_TranslationUnitId = clangproxy.GetTranslationUnitId(m_TranslationUnitId, GetFile() );
            if (m_TranslationUnitId == wxNOT_FOUND )
            {
                clangproxy.CreateTranslationUnit( m_Project, m_Filename, m_CompileCommand, m_UnsavedFiles, m_TranslationUnitId);
            }
            m_UnsavedFiles.clear();
        }
        ClTranslUnitId GetTranslationUnitId() const
        {
            return m_TranslationUnitId;
        }
        ClangFile GetFile() const
        {
            return ClangFile(wxString::FromUTF8(m_Project.c_str()), wxString::FromUTF8(m_Filename.c_str()));
        }
        const std::string& GetProject() const
        {
            return m_Project;
        }
        const std::string& GetFilename() const
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
        CreateTranslationUnitJob(const CreateTranslationUnitJob& other):
            EventJob(other),
            m_Project(other.m_Project),
            m_Filename(other.m_Filename),
            m_CompileCommand(other.m_CompileCommand),
            m_TranslationUnitId(other.m_TranslationUnitId),
            m_UnsavedFiles()
        {
            /* deep copy */
            for ( std::map<std::string, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( it->first, wxString(it->second.c_str()) ) );
            }
        }
    public:
        std::string m_Project;
        std::string m_Filename;
        std::vector<std::string> m_CompileCommand;
        ClTranslUnitId m_TranslationUnitId; // Returned value
        std::map<std::string, wxString> m_UnsavedFiles;
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
    private:
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
        ReparseJob( const wxEventType evtType, const int evtId, ClTranslUnitId translId, const std::vector<std::string>& compileCommand, const ClangFile& file, const std::map<std::string, wxString>& unsavedFiles, bool parents = false )
            : EventJob(ReparseType, evtType, evtId),
              m_TranslId(translId),
              m_UnsavedFiles(unsavedFiles),
              m_CompileCommand(compileCommand),
              m_Project(file.GetProject().ToUTF8().data()),
              m_Filename(file.GetFilename().ToUTF8().data()),
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
        ClangFile GetFile() const
        {
            return ClangFile(wxString::FromUTF8( m_Project.c_str() ), wxString::FromUTF8( m_Filename.c_str() ) );
        }
        const std::string& GetProject() const
        {
            return m_Project;
        }
        const std::string& GetFilename() const
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
              m_CompileCommand(other.m_CompileCommand),
              m_Project(other.m_Project),
              m_Filename(other.m_Filename),
              m_Parents(other.m_Parents)
        {
            /* deep copy */
            for (std::map<std::string, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( it->first, wxString(it->second.c_str()) ) );
            }
        }
    public:
        ClTranslUnitId m_TranslId;
        std::map<std::string, wxString> m_UnsavedFiles;
        std::vector<std::string> m_CompileCommand;
        std::string m_Project;
        std::string m_Filename;
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
        GetDiagnosticsJob( const wxEventType evtType, const int evtId, int translId, const ClangFile& file ):
            EventJob(GetDiagnosticsType, evtType, evtId),
            m_TranslId(translId),
            m_Project(file.GetProject().ToUTF8().data()),
            m_Filename(file.GetFilename().ToUTF8().data())
        {}

        /** @brief Make a deep copy of this class on the heap
         *
         * @return ClangJob*
         *
         */
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
        ClangFile GetFile() const
        {
            return ClangFile(wxString::FromUTF8(m_Project.c_str()), wxString::FromUTF8(m_Filename.c_str()));
        }
        const std::string& GetProject() const
        {
            return m_Project;
        }
        const std::string& GetFilename() const
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
            m_Project(other.m_Project),
            m_Filename(other.m_Filename),
            m_Results(other.m_Results) {}
    public:
        ClTranslUnitId m_TranslId;
        std::string m_Project;
        std::string m_Filename;
        std::vector<ClDiagnostic> m_Results; // Returned value
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
        LookupDefinitionJob( const wxEventType evtType, const int evtId, int translId, const ClangFile& file, const ClTokenPosition& position) :
            EventJob(LookupDefinitionType, evtType, evtId),
            m_TranslId(translId),
            m_Project(file.GetProject().ToUTF8().data()),
            m_Filename(file.GetFilename().ToUTF8().data()),
            m_Position(position) {}
        ClangJob* Clone() const
        {
            LookupDefinitionJob* pJob = new LookupDefinitionJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            ClTokenPosition pos(0,0);
            if (clangproxy.ResolveTokenDefinitionAt( m_TranslId, m_Filename, m_Position, pos))
            {
                m_Locations.push_back( std::make_pair(m_Filename, pos) );
                return;
            }
            if (clangproxy.GetTokenAt( m_TranslId, m_Filename, m_Position, m_TokenIdentifier, m_TokenUSR, m_TokenDisplayName ))
            {
                ClTokenIndexDatabase* db = clangproxy.LoadTokenIndexDatabase( m_Project );
                if (!db)
                    return;
                std::set<ClFileId> fileIdList = db->LookupTokenFileList( m_TokenIdentifier, m_TokenUSR, ClTokenType_DefGroup );
                for ( std::set<ClFileId>::const_iterator it = fileIdList.begin(); it != fileIdList.end(); ++it)
                {
                    ClTokenPosition pos(0,0);
                    if (clangproxy.LookupTokenDefinition(*it, m_TokenIdentifier, m_TokenUSR, pos) )
                    {
                        std::string fn = db->GetFilename( *it );
                        if (std::find( m_Locations.begin(), m_Locations.end(), std::make_pair( fn, pos ) ) == m_Locations.end())
                            m_Locations.push_back( std::make_pair( fn, pos ) );
                    }
                }
                if (m_Locations.size() > 0)
                {
                    return;
                }
                // Find token in subclasses
                std::vector<ClUSRString> USRList;
                clangproxy.GetTokenOverridesAt( m_TranslId, m_Filename, m_Position, USRList);
                for (std::vector<std::string>::const_iterator it = USRList.begin(); it != USRList.end(); ++it)
                {
                    const ClUSRString& USR = *it;
                    std::set<ClFileId> fileIdList = db->LookupTokenFileList( m_TokenIdentifier, USR, ClTokenType_Unknown );
                    for ( std::set<ClFileId>::const_iterator it = fileIdList.begin(); it != fileIdList.end(); ++it)
                    {
                        ClTokenPosition pos(0,0);

                        if (clangproxy.LookupTokenDefinition(*it, m_TokenIdentifier, USR, pos) )
                        {
                            std::string fn = db->GetFilename( *it );
                            m_Locations.push_back( std::make_pair( fn, pos ) );
                        }
                        else if (db->LookupTokenPosition( m_TokenIdentifier, *it, USR, ClTokenType_DefGroup, pos ))
                        {
                            std::string fn = db->GetFilename( *it );
                            m_Locations.push_back( std::make_pair( fn, pos ) );
                        }
                    }
                }
                if (m_Locations.size() > 0)
                {
                    return;
                }
            }
        }
        int GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        ClangFile GetFile() const
        {
            return ClangFile(wxString::FromUTF8(m_Project.c_str()), wxString::FromUTF8(m_Filename.c_str()));
        }
        const std::string& GetProject() const
        {
            return m_Project;
        }
        const std::string& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetPosition() const
        {
            return m_Position;
        }
        const std::vector< std::pair<std::string, ClTokenPosition> >& GetResults() const
        {
            return m_Locations;
        }
        const ClIdentifierString& GetTokenIdentifier() const
        {
            return m_TokenIdentifier;
        }
        const ClUSRString& GetTokenUSR() const
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
            m_Project(other.m_Project),
            m_Filename(other.m_Filename),
            m_Position(other.m_Position),
            m_Locations(other.m_Locations),
            m_TokenIdentifier(other.m_TokenIdentifier),
            m_TokenUSR(other.m_TokenUSR),
            m_TokenDisplayName(other.m_TokenDisplayName)
        {
        }
    protected:
        ClTranslUnitId m_TranslId;
        std::string m_Project;
        std::string m_Filename;
        ClTokenPosition m_Position;
        std::vector< std::pair<std::string, ClTokenPosition> > m_Locations;
        ClIdentifierString m_TokenIdentifier;
        ClUSRString m_TokenUSR;
        ClIdentifierString m_TokenDisplayName;
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
        LookupDefinitionInFilesJob( const wxEventType evtType, const int evtId, int translId, const ClangFile& file, const ClTokenPosition& position, const std::vector< std::pair<std::string,std::vector<std::string> > >& fileAndCompileCommands ) :
            LookupDefinitionJob(evtType, evtId, translId, file, position),
            m_fileAndCompileCommands(fileAndCompileCommands){}
        ClangJob* Clone() const
        {
            LookupDefinitionInFilesJob* pJob = new LookupDefinitionInFilesJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            ClIdentifierString tokenIdentifier;
            ClUSRString usr;
            ClIdentifierString tokenDisplayName;
            if (clangproxy.GetTokenAt( m_TranslId, m_Filename, m_Position, tokenIdentifier, usr, tokenDisplayName ))
            {
                CXIndex clangIndex = clang_createIndex(0,0);

                for (std::vector< std::pair<std::string,std::vector<std::string> > >::const_iterator it = m_fileAndCompileCommands.begin(); it != m_fileAndCompileCommands.end(); ++it)
                {
                    std::vector<std::string> argsBuffer;
                    std::vector<const char*> args;
                    clangproxy.BuildCompileArgs( it->first, it->second, argsBuffer, args );
                    ClTokenIndexDatabase* indexdb = clangproxy.LoadTokenIndexDatabase( m_Project );
                    if (!indexdb)
                        continue;
                    ClFileId destFileId = indexdb->GetFilenameId(it->first);
                    {
                        ClTranslationUnit tu(indexdb, 127, clangIndex);
                        const std::map<std::string, wxString> unsavedFiles; // No unsaved files for reindex...
                        ClFileId fileId = tu.GetTokenDatabase().GetFilenameId( it->first );
                        if (!tu.Parse( it->first, fileId, args, unsavedFiles, false ))
                            CCLogger::Get()->DebugLog(wxT("Could not parse file ")+wxString::FromUTF8(it->first.c_str()));
                        else
                        {
                            ClTokenDatabase db(indexdb);
                            if (tu.ProcessAllTokens( NULL, &db ))
                            {
                                ClTokenPosition pos(0,0);
                                if (db.LookupTokenDefinition( destFileId, tokenIdentifier, usr, pos ))
                                {
                                    m_Locations.push_back( std::make_pair( it->first, pos ) );
                                }
                            }
                        }
                    }
                }
                clang_disposeIndex(clangIndex);
            }
        }
    private:
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
        std::vector< std::pair<std::string,std::vector<std::string> > > m_fileAndCompileCommands;
    };

    class LookupTokenReferencesAtJob : public EventJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        LookupTokenReferencesAtJob( const wxEventType evtType, const int evtId, int translId, const ClangFile& file, const ClTokenPosition& position) :
            EventJob(LookupTokenUsersAtType, evtType, evtId),
            m_TranslId(translId),
            m_Project(file.GetProject().ToUTF8().data()),
            m_Filename(file.GetFilename().ToUTF8().data()),
            m_Position(position),
            m_TokenIdentifier(),
            m_USR(),
            m_TokenDisplayName()
            {}
        ClangJob* Clone() const
        {
            LookupTokenReferencesAtJob* pJob = new LookupTokenReferencesAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            if (clangproxy.GetTokenAt( m_TranslId, GetFilename(), m_Position, m_TokenIdentifier, m_USR, m_TokenDisplayName ))
            {
                // TODO: ask tu for references

                ClTokenIndexDatabase* db = clangproxy.LoadTokenIndexDatabase( GetProject() );
                if (!db)
                {
                    CCLogger::Get()->DebugLog( wxT("TokenIndexDB not found") );
                    return;
                }
                std::set<ClFileId> fileIdSet = db->LookupTokenFileList( m_TokenIdentifier, m_USR, ClTokenType_RefGroup );
                for (std::set<ClFileId>::const_iterator it = fileIdSet.begin(); it != fileIdSet.end(); ++it)
                {
                    std::string filename = db->GetFilename( *it );
                    std::vector<ClIndexToken> tokens;
                    db->GetFileTokens( *it, ClTokenType_RefGroup, m_USR, tokens );
                    for (std::vector<ClIndexToken>::const_iterator itToken = tokens.begin(); itToken != tokens.end(); ++itToken)
                    {
                        for (std::vector<ClIndexTokenLocation>::const_iterator itLoc = itToken->locationList.begin(); itLoc != itToken->locationList.end(); ++itLoc)
                        {
                            if ((itLoc->fileId == *it)&&(itLoc->tokenType&ClTokenType_RefGroup))
                            {
                                m_Locations.insert( std::make_pair( filename, itLoc->range ) );
                            }
                        }
                    }
                }
            }
        }
        int GetTranslationUnitId() const
        {
            return m_TranslId;
        }
        ClangFile GetFile() const
        {
            return ClangFile(wxString::FromUTF8(m_Project.c_str()), wxString::FromUTF8(m_Filename.c_str()));
        }
        const std::string& GetProject() const
        {
            return m_Project;
        }
        const std::string& GetFilename() const
        {
            return m_Filename;
        }
        const ClTokenPosition& GetPosition() const
        {
            return m_Position;
        }
        const ClIdentifierString& GetTokenIdentifier() const
        {
            return m_TokenIdentifier;
        }
        wxString GetTokenDisplayName() const
        {
            return wxString::FromUTF8(m_TokenDisplayName.c_str());
        }
        wxString GetTokenScopePath() const
        {
            return wxString::FromUTF8(m_TokenScopePath.c_str());
        }
        const std::set< std::pair<std::string, ClTokenRange> >& GetResults() const
        {
            return m_Locations;
        }
    protected:
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        LookupTokenReferencesAtJob( const LookupTokenReferencesAtJob& other ) :
            EventJob(other),
            m_TranslId(other.m_TranslId),
            m_Project(other.m_Project),
            m_Filename(other.m_Filename),
            m_Position(other.m_Position),
            m_TokenIdentifier( other.m_TokenIdentifier ),
            m_USR( other.m_USR ),
            m_TokenDisplayName( other.m_TokenDisplayName ),
            m_TokenScopePath( other.m_TokenScopePath ),
            m_Locations(other.m_Locations)
        {
        }
    protected:
        ClTranslUnitId m_TranslId;
        std::string m_Project;
        std::string m_Filename;
        ClTokenPosition m_Position;
        ClIdentifierString m_TokenIdentifier;
        ClUSRString m_USR;
        ClIdentifierString m_TokenDisplayName;
        ClIdentifierString m_TokenScopePath; // Scope of declaration
        std::set<std::pair<std::string, ClTokenRange> > m_Locations;
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
                           const ClangFile& file, const ClTokenPosition& position,
                           const ClTranslUnitId translId, const std::map<std::string, wxString>& unsavedFiles,
                           const ClCodeCompleteOption complete_options):
            SyncJob(CodeCompleteAtType, evtType, evtId),
            m_SerialNo( ++s_SerialNo ),
            m_Project(file.GetProject().ToUTF8().data()),
            m_Filename(file.GetFilename().ToUTF8().data()),
            m_Position(position),
            m_TranslId(translId),
            m_UnsavedFiles(unsavedFiles),
            m_IncludeCtors(complete_options & ClCodeCompleteOption_IncludeCTors),
            m_pResults(new std::vector<ClToken>()),
            m_Diagnostics(),
            m_Options(0)
        {
            if (complete_options&ClCodeCompleteOption_IncludeCodePatterns)
                m_Options |= CXCodeComplete_IncludeCodePatterns;
            if (complete_options&ClCodeCompleteOption_IncludeBriefComments)
                m_Options |= CXCodeComplete_IncludeBriefComments;
            if (complete_options&ClCodeCompleteOption_IncludeMacros)
                m_Options |= CXCodeComplete_IncludeMacros;
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
            clangproxy.CodeCompleteAt(m_TranslId, m_Filename, m_Position, m_Options, m_UnsavedFiles, results, m_Diagnostics);
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
        ClangFile GetFile() const
        {
            return ClangFile(wxString::FromUTF8( m_Project.c_str() ), wxString::FromUTF8( m_Filename.c_str() ));
        }
        const std::string& GetProject() const
        {
            return m_Project;
        }
        const std::string& GetFilename() const
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
    private:
        /** @brief Copy constructor
         *
         * @param other To copy from
         *
         *  Performs a deep copy for multi-threaded use
         */
        CodeCompleteAtJob( const CodeCompleteAtJob& other ) :
            SyncJob(other),
            m_SerialNo(other.m_SerialNo),
            m_Project(other.m_Project),
            m_Filename(other.m_Filename),
            m_Position(other.m_Position),
            m_TranslId(other.m_TranslId),
            m_IncludeCtors(other.m_IncludeCtors),
            m_pResults(other.m_pResults),
            m_Diagnostics(other.m_Diagnostics),
            m_Options(other.m_Options)
        {
            for ( std::map<std::string, wxString>::const_iterator it = other.m_UnsavedFiles.begin(); it != other.m_UnsavedFiles.end(); ++it)
            {
                m_UnsavedFiles.insert( std::make_pair( it->first, wxString(it->second.c_str()) ) );
            }
        }
        unsigned int m_SerialNo;
        std::string m_Project;
        std::string m_Filename;
        ClTokenPosition m_Position;
        ClTranslUnitId m_TranslId;
        std::map<std::string, wxString> m_UnsavedFiles;
        bool m_IncludeCtors;
        std::vector<ClToken>* m_pResults; // Returned value
        std::vector<ClDiagnostic> m_Diagnostics;
        unsigned m_Options;
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
        DocumentCCTokenJob( const wxEventType evtType, const int evtId, ClTranslUnitId translId, const ClangFile&, const ClTokenPosition& position, ClTokenId tknId ):
            SyncJob(DocumentCCTokenType, evtType, evtId),
            m_TranslId(translId),
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
            m_Position(other.m_Position),
            m_TokenId(other.m_TokenId),
            m_pResult(other.m_pResult) {}
        ClTranslUnitId m_TranslId;
        ClTokenPosition m_Position;
        ClTokenId m_TokenId;
        wxString* m_pResult;
    };
    /* final */
    class GetTokenCompletionAtJob : public SyncJob
    {
    public:
        /** @brief Constructor
         *
         * @param evtType wxEventType to use when the job is completed
         * @param evtId Event ID to use when the job is completed
         *
         */
        GetTokenCompletionAtJob( const wxEventType evtType, const int evtId, const ClangFile& file, const ClTokenPosition& position, int translId ):
            SyncJob(GetTokensAtType, evtType, evtId),
            m_Filename(file.GetFilename().ToUTF8().data()),
            m_Position(position),
            m_TranslId(translId),
            m_pResult(new ClIdentifierString())
        {
        }

        ClangJob* Clone() const
        {
            GetTokenCompletionAtJob* pJob = new GetTokenCompletionAtJob(*this);
            return static_cast<ClangJob*>(pJob);
        }

        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.GetTokenCompletionAt(m_TranslId, m_Filename, m_Position, *m_pResult);
        }

        virtual void Finalize()
        {
            SyncJob::Finalize();
            delete m_pResult;
        }

        const ClIdentifierString& GetResult()
        {
            return *m_pResult;
        }
    protected:
        GetTokenCompletionAtJob( const wxEventType evtType, const int evtId, const ClangFile& file, const ClTokenPosition& position, int translId,
                        wxMutex* pMutex, wxCondition* pCond,
                        ClIdentifierString* pResult ):
            SyncJob(GetTokensAtType, evtType, evtId, pMutex, pCond),
            m_Filename(file.GetFilename().ToUTF8().data()),
            m_Position(position),
            m_TranslId(translId),
            m_pResult(pResult) {}
        std::string m_Filename;
        ClTokenPosition m_Position;
        ClTranslUnitId m_TranslId;
        ClIdentifierString* m_pResult;
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
        GetCallTipsAtJob( const wxEventType evtType, const int evtId, const ClangFile& file,
                          const ClTokenPosition& position, int translId, const ClIdentifierString& identifier ):
            SyncJob( GetCallTipsAtType, evtType, evtId),
            m_Filename(file.GetFilename().ToUTF8().data()),
            m_Position(position),
            m_TranslId(translId),
            m_Identifier(identifier),
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
            clangproxy.GetCallTipsAt( m_TranslId, m_Filename, m_Position, m_Identifier, *m_pResults);
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
        GetCallTipsAtJob( const wxEventType evtType, const int evtId, const ClangFile& file, const ClTokenPosition& position, int translId, const ClIdentifierString& identifier,
                          wxMutex* pMutex, wxCondition* pCond,
                          std::vector<wxStringVec>* pResults ):
            SyncJob(GetCallTipsAtType, evtType, evtId, pMutex, pCond),
            m_Filename(file.GetFilename().ToUTF8().data()),
            m_Position(position),
            m_TranslId(translId),
            m_Identifier(identifier),
            m_pResults(pResults) {}
        std::string m_Filename;
        ClTokenPosition m_Position;
        ClTranslUnitId m_TranslId;
        ClIdentifierString m_Identifier;
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
        GetOccurrencesOfJob( const wxEventType evtType, const int evtId, const ClangFile& file,
                             const ClTokenPosition& position, ClTranslUnitId translId ):
            EventJob( GetOccurrencesOfType, evtType, evtId),
            m_TranslId(translId),
            m_Project(file.GetProject().ToUTF8().data()),
            m_Filename(file.GetFilename().ToUTF8().data()),
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
        ClangFile GetFile() const
        {
            return ClangFile(wxString::FromUTF8(m_Project.c_str()), wxString::FromUTF8(m_Filename.c_str()));
        }
        const std::string& GetProject() const
        {
            return m_Project;
        }
        const std::string& GetFilename() const
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
            m_Project(other.m_Project),
            m_Filename(other.m_Filename),
            m_Position(other.m_Position),
            m_Results(other.m_Results){}
        ClTranslUnitId m_TranslId;
        std::string m_Project;
        std::string m_Filename;
        ClTokenPosition m_Position;
        std::vector< std::pair<int, int> > m_Results;
    };

    class ReindexFileJob : public EventJob
    {
    public:
        ReindexFileJob( const wxEventType evtType, const int evtId, const ClangFile& file, const std::vector<std::string>& commands ):
            EventJob(ReindexFileType, evtType, evtId),
            m_Project(file.GetProject().ToUTF8().data()),
            m_Filename(file.GetFilename().ToUTF8().data()),
            m_CompileCommand(commands) {}
        ClangJob* Clone() const
        {
            ReindexFileJob* pJob = new ReindexFileJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy);

        ClangFile GetFile() const
        {
            return ClangFile(wxString::FromUTF8(m_Project.c_str()), wxString::FromUTF8(m_Filename.c_str()));
        }
        const std::string& GetProject() const
        {
            return m_Project;
        }
        const std::string& GetFilename() const
        {
            return m_Filename;
        }

    protected:
        ReindexFileJob( const ReindexFileJob& other) :
            EventJob(other),
            m_Project(other.m_Project),
            m_Filename(other.m_Filename),
            m_CompileCommand(other.m_CompileCommand){}
        std::string m_Project;
        std::string m_Filename;
        std::vector<std::string> m_CompileCommand;
    };

    class StoreTokenIndexDBJob : public EventJob
    {
    public:
        StoreTokenIndexDBJob( const wxEventType evtType, const int evtId, const std::string& project):
            EventJob(StoreTokenIndexDBType, evtType, evtId),
            m_Project(project)
        {
        }
        ClangJob* Clone() const
        {
            StoreTokenIndexDBJob* pJob = new StoreTokenIndexDBJob(*this);
            return static_cast<ClangJob*>(pJob);
        }
        void Execute(ClangProxy& clangproxy)
        {
            clangproxy.StoreTokenIndexDatabase( m_Project );
        }

    protected:
        StoreTokenIndexDBJob( const StoreTokenIndexDBJob& other) :
            EventJob(other),
            m_Project(other.m_Project){}
        std::string m_Project;
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

    static wxString GetTokenIndexDatabaseFilename( const std::string& project );

public:
    ClangProxy(wxEvtHandler* pEvtHandler, const std::vector<wxString>& cppKeywords);
    ~ClangProxy();

    /** Append a job to the end of the queue */
    void AppendPendingJob( ClangProxy::ClangJob& job );

    ClTranslUnitId GetTranslationUnitId( const ClTranslUnitId CtxTranslUnitId, const ClangFile& file) const;
    void GetAllTranslationUnitIds( std::set<ClTranslUnitId>& out_list ) const;
    void SetMaxTranslationUnits( unsigned int Max );

public: // TokenIndexDatabase functions
    void GetLoadedTokenIndexDatabases( std::set<std::string>& out_projectFileNamesSet ) const;
    ClTokenIndexDatabase* GetTokenIndexDatabase( const std::string& projectFileName );
    const ClTokenIndexDatabase* GetTokenIndexDatabase( const std::string& projectFileName ) const;
    ClTokenIndexDatabase* LoadTokenIndexDatabase( const std::string& projectFileName );

protected: // jobs that are run only on the thread
    void CreateTranslationUnit( const std::string& project, const std::string& filename, const std::vector<std::string>& compileCommand,  const std::map<std::string, wxString>& unsavedFiles, ClTranslUnitId& out_TranslId);
    void RemoveTranslationUnit( const ClTranslUnitId TranslUnitId );
    /** Reparse translation id
     *
     * @param unsavedFiles reference to the unsaved files data. This function takes the data and this list will be empty after this call
     */
    void Reparse( const ClTranslUnitId translId, const std::vector<std::string>& compileCommand, const std::map<std::string, wxString>& unsavedFiles);

    /** Update token database with all tokens from the passed translation unit id
     * @param translId The ID of the intended translation unit
     */
    void UpdateTokenDatabase( const ClTranslUnitId translId );
    void GetDiagnostics(  const ClTranslUnitId translId, const std::string& filename, std::vector<ClDiagnostic>& diagnostics);
    void CodeCompleteAt(  const ClTranslUnitId translId, const std::string& filename, const ClTokenPosition& location, unsigned cc_options,
                          const std::map<std::string, wxString>& unsavedFiles, std::vector<ClToken>& results, std::vector<ClDiagnostic>& diagnostics);
    wxString DocumentCCToken( ClTranslUnitId translId, int tknId );
    void GetTokenCompletionAt( const ClTranslUnitId translId, const std::string& filename, const ClTokenPosition& position,
                          ClIdentifierString& out_result);
    void GetCallTipsAt(   const ClTranslUnitId translId, const std::string& filename, const ClTokenPosition& position,
                          const ClIdentifierString& tokenStr, std::vector<wxStringVec>& results);
    void GetOccurrencesOf(const ClTranslUnitId translId, const std::string& filename, const ClTokenPosition& position,
                          std::vector< std::pair<int, int> >& results);
    void RefineTokenType( const ClTranslUnitId translId, int tknId, ClTokenCategory& out_tknType); // TODO: cache TokenId (if resolved) for DocumentCCToken()
    bool GetTokenAt( const ClTranslUnitId translId, const std::string& filename, const ClTokenPosition& position, ClIdentifierString& out_Identifier, ClUSRString& out_USR, ClIdentifierString& out_DisplayName );
    void GetTokenOverridesAt( const ClTranslUnitId translUnitId, const std::string& filename, const ClTokenPosition& position, std::vector<ClUSRString>& out_USRList);

    bool LookupTokenDefinition( const ClFileId fileId, const ClIdentifierString& identifier, const ClUSRString& usr, ClTokenPosition& out_position);
    void StoreTokenIndexDatabase( const std::string& projectFileName ) const;

public: // Tokens
    wxString GetCCInsertSuffix( const  ClTranslUnitId translId, int tknId, bool isDecl, const wxString& newLine, std::vector< std::pair<int, int> >& offsets );
    bool ResolveTokenDeclarationAt( const ClTranslUnitId translId, std::string& inout_filename, const ClTokenPosition& position, ClTokenPosition& out_Position);
    bool ResolveTokenDefinitionAt( const ClTranslUnitId translUnitId, std::string& inout_filename, const ClTokenPosition& position, ClTokenPosition& out_Position);
public: // Function scopes
    void ResolveTokenScopes(const ClTranslUnitId id, const std::string& project, const std::string& filename, unsigned int tokenMask, std::vector<ClTokenScope>& out_Scopes);
    bool ResolveTokenScopeAt(const ClTranslUnitId id, const std::string& project, const std::string& filename, const ClTokenPosition& position, ClTokenScope& out_Scope );


private: // Utility functions
    void BuildCompileArgs(const std::string& filename, const std::vector<std::string>& compileCommands, std::vector<std::string>& out_argsBuffer, std::vector<const char*>& out_args) const;

private:
    mutable wxMutex m_Mutex;
    ClTokenIndexDatabaseMap_t m_DatabaseMap;
    const std::vector<wxString>& m_CppKeywords;
    std::vector<ClTranslationUnit> m_TranslUnits;
    unsigned int m_MaxTranslUnits;
    CXIndex m_ClIndex;
private: // Thread
    wxEvtHandler* m_pEventCallbackHandler;
    BackgroundThread* m_pThread;
    BackgroundThread* m_pReindexThread;
};

#endif // CLANGPROXY_H

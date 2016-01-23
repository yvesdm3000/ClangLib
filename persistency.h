#ifndef PERSISTENCY_H
#define PERSISTENCY_H

#include <wx/string.h>
#include <wx/archive.h>
#include <wx/filename.h>

/* abstract */
class PersistencyManager
{
protected:
    PersistencyManager(){}
    ~PersistencyManager(){}
public:
    virtual wxString GetTokenDatabaseFilename() = 0;
    virtual wxString GetPchFilename( const wxString& filename ) = 0;
};

/**
 * Implementation of a PersistencyManager to store everything in a single directory
 */
class SinglePathPersistencyManager : public PersistencyManager
{
public:
    SinglePathPersistencyManager( const wxString& persistencyDirectory )
        : PersistencyManager()
        , m_PersistencyDirectory(persistencyDirectory)
    {
        if ( persistencyDirectory.Len() == 0 )
        {
            m_PersistencyDirectory = wxFileName::GetTempDir();
        }
    }
    virtual wxString GetTokenDatabaseFilename()
    {
        wxFileName name = wxFileName::DirName( m_PersistencyDirectory );
        name.SetFullName(wxT("tokendatabase.dat"));
        return name.GetFullPath();
    }
    virtual wxString GetPchFilename( const wxString& filename )
    {
        wxString fn(filename.c_str() );
        for( wxString::iterator it = fn.begin(); it != fn.end(); ++it)
        {
            if( *it < '0' )
            {
                *it = '_';
            }
            else if( (*it > '9')&&(*it < 'A') )
            {
                *it = '_';
            }
            else if( (*it > 'Z')&&(*it < 'a'))
            {
                *it = '_';
            }
            else if( *it > 'z' )
            {
                *it = '_';
            }
        }

        wxFileName name = wxFileName::DirName( m_PersistencyDirectory );
        name.SetFullName( fn );
        name.SetExt( wxT("pch") );
        return name.GetFullPath();
    }

private:
    wxString m_PersistencyDirectory;
};

#endif

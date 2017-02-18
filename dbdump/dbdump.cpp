#include "dbdump.h"

#include "../src/tokendatabase.h"

#include <wx/cmdline.h>
#include <wx/wfstream.h>
#include "../src/cclogger.h"

#ifndef WX_PRECOMP
       #include <wx/wx.h>
#endif


std::ostream& operator<<(std::ostream& o, ClTokenType val)
{
    switch(val)
    {
    case ClTokenType_FuncDecl:
        o<<"Function declaration";
        break;
    case ClTokenType_VarDecl:
        o<<"Variable declaration";
        break;
    case ClTokenType_MemberDecl:
        o<<"Member declaration";
        break;
    case ClTokenType_ParmDecl:
        o<<"Parameter declaration";
        break;
    case ClTokenType_ScopeDecl:
        o<<"Scope declaration";
        break;
    case ClTokenType_FuncDef:
        o<<"Function definition";
        break;
    case ClTokenType_VarDef:
        o<<"Variable definition";
        break;
    case ClTokenType_ParmDef:
        o<<"Parameter definition";
        break;
    case ClTokenType_ScopeDef:
        o<<"Scope definition";
        break;
    case ClTokenType_FuncRef:
        o<<"Function reference";
        break;
    case ClTokenType_VarRef:
        o<<"Variable reference";
        break;
    case ClTokenType_MemberRef:
        o<<"Member reference";
        break;
    case ClTokenType_ParmRef:
        o<<"Parameter reference";
        break;
    case ClTokenType_ScopeRef:
        o<<"Scope reference";
        break;
    }
    return o;
};

class ListSymbols : public IPersistentTokenIndexDatabase
{
public:
    IPersistentFilenameDatabase* GetFilenameDatabase()
    {
        return &m_FilenameDatabase;
    }

    void OutputIdent(unsigned ident)
    {
        for (unsigned i = 0; i<ident;++i)
        {
            std::cout<<"  ";
        }
    }
    void OutputToken(unsigned ident, const ClIndexToken& token)
    {
        ClFileId fileFilterId = wxNOT_FOUND;
        if (!m_FilenameFilter.IsEmpty())
        {
            fileFilterId = m_FilenameDatabase.GetFilenameId( m_FilenameFilter );
            if (fileFilterId == wxNOT_FOUND)
                return;
            bool found = false;
            for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it)
            {
                if (it->fileId == fileFilterId)
                {
                    found = true;
                    break;
                }
            }
        }
        OutputIdent( ident );
        std::cout<<"Token:                "<<token.identifier.utf8_str()<<std::endl;
        OutputIdent( ident );
        std::cout<<"  Display Name:       "<<token.displayName.utf8_str()<<std::endl;
        OutputIdent( ident );
        std::cout<<"  USR:                "<<token.USR.utf8_str()<<std::endl;
        if (!token.scope.first.IsEmpty())
        {
            OutputIdent( ident );
            std::cout<<"  Scope identifier:   "<<token.scope.first.utf8_str()<<std::endl;
            OutputIdent( ident );
            std::cout<<"        USR:          "<<token.scope.second.utf8_str()<<std::endl;
        }
        if (!token.parentTokenList.empty())
        {
            OutputIdent( ident );
            std::cout<<"  Parents:"<<std::endl;
        }
        for (std::vector<std::pair<wxString,wxString> >::const_iterator it = token.parentTokenList.begin(); it != token.parentTokenList.end(); ++it)
        {
            OutputIdent( ident );
            std::cout<<"    Identifier:       "<<it->first.utf8_str()<<std::endl;
            OutputIdent( ident );
            std::cout<<"    USR:              "<<it->second.utf8_str()<<std::endl;
        }
        OutputIdent( ident );
        std::cout<<"  Token locations:"<<std::endl;
        for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it)
        {
            if (fileFilterId != wxNOT_FOUND)
            {
                if (it->fileId != fileFilterId)
                    continue;
            }
            OutputIdent( ident );
            std::cout<<"    TokenType:        "<<it->tokenType<<std::endl;
            OutputIdent( ident );
            std::cout<<"    Range:            "<<it->range.beginLocation.line<<", "<<it->range.beginLocation.column<<" - "<<it->range.endLocation.line<<", "<<it->range.endLocation.column<<std::endl;
            OutputIdent( ident );
            std::cout<<"    File:             "<<m_FilenameDatabase.GetFilename( it->fileId ).utf8_str()<<std::endl;
        }
    }
    void AddToken(const ClIndexToken& token)
    {
        OutputToken( 0, token );
    }
    const IPersistentFilenameDatabase* GetFilenameDatabase() const
    {
        return &m_FilenameDatabase;
    }
    std::vector<ClIndexToken> GetTokens() const
    {
        std::vector<ClIndexToken> ret;
        return ret;
    }

    ClFilenameDatabase m_FilenameDatabase;
    wxString m_FilenameFilter;
};

static const wxCmdLineEntryDesc cmdLineDesc[] =
{
    { wxCMD_LINE_SWITCH, wxT("v"), wxT("verbose"), wxT("be verbose"), wxCMD_LINE_VAL_NONE, 0 },
    { wxCMD_LINE_SWITCH, wxT("f"), wxT("file"), wxT("Show tokens in file"), wxCMD_LINE_VAL_STRING, 0 },
    { wxCMD_LINE_PARAM,  NULL, NULL, wxT("cmd"), wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_MULTIPLE },
    { wxCMD_LINE_PARAM,  NULL, NULL, wxT("input file"), wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_MULTIPLE },
    { wxCMD_LINE_NONE, NULL, NULL, NULL, wxCMD_LINE_VAL_NONE, 0 }
};

int main(int argc, char* argv[])
{
    wxCmdLineParser parser;
    parser.SetDesc( cmdLineDesc );
    parser.SetCmdLine( argc, argv );
    parser.Parse();
    if (parser.GetParamCount() < 2)
    {
        parser.Usage();
        std::cout<<" Not enough parameters: "<<parser.GetParamCount()<<std::endl;
        std::cout<<"Usage: "<<argv[0]<<" listsymbols [indexdb file]"<<std::endl;
        return -1;
    }
    wxString cmd = parser.GetParam(0);
    wxString dbfile = parser.GetParam(1);
    wxFileInputStream in(dbfile);
    if (!in.IsOk())
    {
        parser.Usage();
        return -1;
    }
    wxString fileFilter;
    if (!parser.Found( wxT("file"), &fileFilter ))
        fileFilter.Empty();

    if (cmd == wxT("listsymbols"))
    {
        ListSymbols l;
        l.m_FilenameFilter = fileFilter;
        if (!CTokenIndexDatabasePersistence::ReadIn( l, in ))
        {
            std::cout<<"Token database parsing stopped prematurely"<<std::endl;
            return 1;
        }
    }
    else
    {
        parser.Usage();
    }
    return 0;
}

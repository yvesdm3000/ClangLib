#include "dbdump.h"

#include "../src/tokendatabase.h"

#include <wx/cmdline.h>
#include <wx/wfstream.h>
#include "../src/cclogger.h"

#ifndef WX_PRECOMP
       #include <wx/wx.h>
#endif


std::ostream& operator<<(std::ostream& o, const ClTokenType val)
{
    int valInt = val;
    switch(val)
    {
    case ClTokenType_FuncDecl:
        o<<"Function declaration 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_VarDecl:
        o<<"Variable declaration 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_ParmDecl:
        o<<"Parameter declaration 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_ScopeDecl:
        o<<"Scope declaration 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_FuncDef:
        o<<"Function definition 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_VarDef:
        o<<"Variable definition 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_ParmDef:
        o<<"Parameter definition 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_ScopeDef:
        o<<"Scope definition 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_FuncRef:
        o<<"Function reference 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_VarRef:
        o<<"Variable reference 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_ParmRef:
        o<<"Parameter reference 0x"<<std::hex<<valInt;
        break;
    case ClTokenType_ScopeRef:
        o<<"Scope reference 0x"<<std::hex<<valInt;
        break;
    default:
        o<<"0x"<<std::hex<<valInt;
        break;
    }
    return o;
};

std::ostream& operator<<(std::ostream& o, const ClTokenCategory val)
{
    switch (val)
    {
        case tcClassFolder:
            o<<"Class folder";
            break;
    case tcClass:
        o<<"Class";
        break;
    case tcClassPrivate:
        o<<"Private class";
        break;
    case tcClassProtected:
        o<<"Protected class";
        break;
    case tcClassPublic:
        o<<"Public class";
        break;
    case tcCtorPrivate:
        o<<"Private constructor";
        break;
    case tcCtorProtected:
        o<<"Protected constructor";
        break;
    case tcCtorPublic:
        o<<"Public constructor";
        break;
    case tcDtorPrivate:
        o<<"Private destructor";
        break;
    case tcDtorProtected:
        o<<"Protected destructor";
        break;
    case tcDtorPublic:
        o<<"Public destructor";
        break;
    case tcFuncPrivate:
        o<<"Private function";
        break;
    case tcFuncProtected:
        o<<"Protected function";
        break;
    case tcFuncPublic:
        o<<"Public function";
        break;
    case tcVarPrivate:
        o<<"Private variable";
        break;
    case tcVarProtected:
        o<<"Protected variable";
        break;
    case tcVarPublic:
        o<<"Public variable";
        break;
    case tcMacroDef:
        o<<"Macro definition";
        break;
    case tcEnum:
        o<<"Enum";
        break;
    case tcEnumPrivate:
        o<<"Private enum";
        break;
    case tcEnumProtected:
        o<<"Protected enum";
        break;
    case tcEnumPublic:
        o<<"Public enum";
        break;
    case tcEnumerator:
        o<<"Enumerator";
        break;
    case tcNamespace:
        o<<"Namespace";
        break;
    case tcTypedef:
        o<<"Typedef";
        break;
    case tcTypedefPrivate:
        o<<"Private private";
        break;
    case tcTypedefProtected:
        o<<"Protected typedef";
        break;
    case tcTypedefPublic:
        o<<"Public typedef";
        break;
    case tcSymbolsFolder:
        o<<"Symbols folder";
        break;
    case tcVarsFolder:
        o<<"Vars folder";
        break;
    case tcFuncsFolder:
        o<<"Funcs folder";
        break;
    case tcEnumsFolder:
        o<<"Enums folder";
        break;
    case tcPreprocFolder:
        o<<"Preproc folder";
        break;
    case tcOthersFolder:
        o<<"Others folder";
        break;
    case tcTypedefFolder:
        o<<"Typedef folder";
        break;
    case tcMacroUse:
        o<<"Macro use";
        break;
    case tcMacroPrivate:
        o<<"Private macro";
        break;
    case tcMacroProtected:
        o<<"Protected macro";
        break;
    case tcMacroPublic:
        o<<"Public macro";
        break;
    case tcMacroFolder:
        o<<"Macro folder";
        break;
    case tcLangKeyword: // added
        o<<"Language keyword";
        break;
    default:
    case tcNone: ;
        o<<"None";
        break;
    }
    return o;
}

ClFilenameDatabase g_FilenameDatabase;

class SymbolDB : public IPersistentTokenIndexDatabase
{
public:
    IPersistentFilenameDatabase* GetFilenameDatabase()
    {
        return &g_FilenameDatabase;
    }
    void AddToken(const ClIndexToken& token)
    {
        m_TokenList.push_back( token );
        m_TokenMap.insert( std::make_pair( std::make_pair( token.identifier, token.USR ), &m_TokenList.back() ) );
    }
    const IPersistentFilenameDatabase* GetFilenameDatabase() const
    {
        return &g_FilenameDatabase;
    }
    std::vector<ClIndexToken> GetTokens() const
    {
        std::vector<ClIndexToken> ret;
        return ret;
    }
    std::vector<ClIndexToken> m_TokenList;
    std::map<std::pair<ClIdentifierString,ClUSRString>, ClIndexToken*> m_TokenMap;
};

class ListSymbols : public IPersistentTokenIndexDatabase
{
public:
    ListSymbols() : m_pSymbolDB( NULL ) {}
    IPersistentFilenameDatabase* GetFilenameDatabase()
    {
        return &g_FilenameDatabase;
    }

    void OutputIndent(unsigned indent)
    {
        for (unsigned i = 0; i<indent;++i)
        {
            std::cout<<"  ";
        }
    }
    void OutputToken(unsigned indent, const ClIndexToken& token)
    {
        unsigned count = 0;
        ClFileId fileFilterId = wxNOT_FOUND;
        if (!m_FilenameFilter.empty())
        {
            fileFilterId = g_FilenameDatabase.GetFilenameId( m_FilenameFilter );
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
        OutputIndent( indent );
        std::cout<<"Token:                "<<token.identifier<<std::endl;
        OutputIndent( indent );
        std::cout<<"  Display Name:       "<<token.displayName.ToUTF8().data()<<std::endl;
        OutputIndent( indent );
        std::cout<<"  USR:                "<<token.USR<<std::endl;
        OutputIndent( indent );
        std::cout<<"  Category:           "<<token.category<<std::endl;
        if (!token.semanticScope.first.empty())
        {
            OutputIndent( indent );
            std::cout<<"  Scope identifier:   "<<token.semanticScope.first<<std::endl;
            OutputIndent( indent );
            std::cout<<"        USR:          "<<token.semanticScope.second<<std::endl;
            if (m_bResolveScope&&m_pSymbolDB)
            {
                std::map<std::pair<ClIdentifierString,ClUSRString>, ClIndexToken*>::const_iterator scopeIt = m_pSymbolDB->m_TokenMap.find( std::make_pair( token.semanticScope.first, token.semanticScope.second ) );
                if (scopeIt != m_pSymbolDB->m_TokenMap.end())
                {
                    OutputToken( indent+4, *scopeIt->second );
                }
            }
        }
        if (!token.parentTokenList.empty())
        {
            OutputIndent( indent );
            std::cout<<"  Parents:"<<std::endl;
            for (std::vector<std::pair<ClIdentifierString,ClUSRString> >::const_iterator it = token.parentTokenList.begin(); it != token.parentTokenList.end(); ++it)
            {
                OutputIndent( indent );
                std::cout<<"    Identifier:       "<<it->first<<std::endl;
                OutputIndent( indent );
                std::cout<<"    USR:              "<<it->second<<std::endl;
                if (m_bResolveParents&&m_pSymbolDB)
                {
                    std::map<std::pair<ClIdentifierString,ClUSRString>, ClIndexToken*>::const_iterator parentIt = m_pSymbolDB->m_TokenMap.find( std::make_pair( it->first, it->second ) );
                    if (parentIt != m_pSymbolDB->m_TokenMap.end())
                    {
                        OutputToken(indent+4, *parentIt->second);
                    }
                    else
                    {
                        OutputIndent( indent );
                        std::cout<<"Token could not be resolved"<<std::endl;
                    }
                }
            }
        }

        count = 0;
        for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it)
        {
            if (fileFilterId != wxNOT_FOUND)
            {
                if (it->fileId != fileFilterId)
                    continue;
            }
            count++;
        }
        OutputIndent( indent );
        std::cout<<"  Types:              0x"<<std::hex<<(int)token.tokenTypeMask<<std::endl;
        if (count)
        {
            OutputIndent( indent );
            std::cout<<"  Locations:"<<std::endl;
            for (std::vector<ClIndexTokenLocation>::const_iterator it = token.locationList.begin(); it != token.locationList.end(); ++it)
            {
                if (fileFilterId != wxNOT_FOUND)
                {
                    if (it->fileId != fileFilterId)
                        continue;
                }
                OutputIndent( indent );
                std::cout<<"    TokenType:        "<<it->tokenType<<std::endl;
                OutputIndent( indent );
                std::cout<<"    Range:            "<<std::dec<<it->range.beginLocation.line<<", "<<it->range.beginLocation.column<<" - "<<it->range.endLocation.line<<", "<<it->range.endLocation.column<<std::endl;
                OutputIndent( indent );
                std::cout<<"    File:             "<<g_FilenameDatabase.GetFilename( it->fileId )<<std::endl;
            }
        }
    }
    void AddToken(const ClIndexToken& token)
    {
        OutputToken( 0, token );
    }
    const IPersistentFilenameDatabase* GetFilenameDatabase() const
    {
        return &g_FilenameDatabase;
    }
    std::vector<ClIndexToken> GetTokens() const
    {
        std::vector<ClIndexToken> ret;
        return ret;
    }
    void OutputSymbolDB(const SymbolDB& db)
    {
        m_pSymbolDB = &db;
        for (std::vector< ClIndexToken >::const_iterator it = db.m_TokenList.begin(); it != db.m_TokenList.end(); ++it)
        {
            OutputToken( 0, *it );
        }
    }

    std::string m_FilenameFilter;
    const SymbolDB* m_pSymbolDB;
    bool m_bResolveParents;
    bool m_bResolveScope;
};

#if wxCHECK_VERSION(3,0,0)

static const wxCmdLineEntryDesc cmdLineDesc[] =
{
    { wxCMD_LINE_SWITCH, ("v"),  ("verbose"),        ("Be verbose"),                wxCMD_LINE_VAL_NONE, 0 },
    { wxCMD_LINE_SWITCH, ("rs"), ("resolvescope"),   ("Resolve scope"),             wxCMD_LINE_VAL_NONE, 0 },
    { wxCMD_LINE_SWITCH, ("rp"), ("resolveparents"), ("Resolve parents"),           wxCMD_LINE_VAL_NONE, 0 },
    { wxCMD_LINE_SWITCH, ("ra"), ("resolveall"),     ("Resolve scope and parents"), wxCMD_LINE_VAL_NONE, 0 },
    { wxCMD_LINE_SWITCH, ("f"),  ("file"),           ("Show tokens in file"),       wxCMD_LINE_VAL_STRING, 0 },
    { wxCMD_LINE_PARAM,   NULL,  NULL,               ("cmd"),                       wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_MULTIPLE },
    { wxCMD_LINE_PARAM,   NULL,  NULL,               ("input file"),                wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_MULTIPLE },
    { wxCMD_LINE_NONE, NULL, NULL, NULL, wxCMD_LINE_VAL_NONE, 0 }
};

#else

static const wxCmdLineEntryDesc cmdLineDesc[] =
{
    { wxCMD_LINE_SWITCH, wxT("v"), wxT("verbose"), wxT("Be verbose"), wxCMD_LINE_VAL_NONE, 0 },
    { wxCMD_LINE_SWITCH, wxT("rs"), wxT("resolvescope"), wxT("Resolve scope"), wxCMD_LINE_VAL_NONE, 0 },
    { wxCMD_LINE_SWITCH, wxT("rp"), wxT("resolveparents"), wxT("Resolve parents"), wxCMD_LINE_VAL_NONE, 0 },
    { wxCMD_LINE_SWITCH, wxT("ra"), wxT("resolveall"), wxT("Resolve scope and parents"), wxCMD_LINE_VAL_NONE, 0 },
    { wxCMD_LINE_SWITCH, wxT("f"), wxT("file"), wxT("Show tokens in file"), wxCMD_LINE_VAL_STRING, 0 },
    { wxCMD_LINE_PARAM,  NULL, NULL, wxT("cmd"), wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_MULTIPLE },
    { wxCMD_LINE_PARAM,  NULL, NULL, wxT("input file"), wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_MULTIPLE },
    { wxCMD_LINE_NONE, NULL, NULL, NULL, wxCMD_LINE_VAL_NONE, 0 }
};

#endif // wxCHECK_VERSION


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
        std::cout<<"Usage: "<<argv[0]<<" [-rs] [-rp] [-ra] [--file=/path/ro/file] listsymbols indexdb_file"<<std::endl;
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
        bool resolve = false;
        ListSymbols l;
        l.m_FilenameFilter = fileFilter.ToUTF8().data();

        if (parser.Found( wxT("resolveall") ))
        {
            l.m_bResolveScope = true;
            l.m_bResolveParents = true;
            resolve = true;
        }

        if (parser.Found( wxT("resolvescope") ))
        {
            l.m_bResolveScope = true;
            resolve = true;
        }
        if (parser.Found( wxT("resolveparents") ))
        {
            l.m_bResolveParents = true;
            resolve = true;
        }
        bool ok = true;
        if (resolve)
        {
            SymbolDB db;
            ok = CTokenIndexDatabasePersistence::ReadIn( db, in );
            l.OutputSymbolDB( db );
        }
        else
        {
            ok = CTokenIndexDatabasePersistence::ReadIn( l, in );
        }
        if (!ok)
        {
            std::cout<<"Token database parsing stopped prematurely"<<std::endl;
            return 1;
        };
    }
    else
    {
        parser.Usage();
    }
    return 0;
}









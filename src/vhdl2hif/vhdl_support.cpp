/// @file vhdl_support.cpp
/// @brief
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <set>

#include <hif/hif.hpp>

#include "vhdl2hif/vhdl_support.hpp"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
#endif

/*
 * Global definitions used by vhdl_support, parser and lexer
 * --------------------------------------------------------------------- */
std::string yyfilename;
int yycolumno = 1;

using std::endl;
using std::string;
using namespace hif;

namespace
{

std::string oct2bits(char oct)
{
    switch (oct) {
    case '0':
        return "000";
    case '1':
        return "001";
    case '2':
        return "010";
    case '3':
        return "011";
    case '4':
        return "100";
    case '5':
        return "101";
    case '6':
        return "110";
    case '7':
        return "111";
    default:
        break;
    }
    return "";
}

std::string hex2bits(char hex)
{
    switch (hex) {
    case '0':
        return "0000";
    case '1':
        return "0001";
    case '2':
        return "0010";
    case '3':
        return "0011";
    case '4':
        return "0100";
    case '5':
        return "0101";
    case '6':
        return "0110";
    case '7':
        return "0111";
    case '8':
        return "1000";
    case '9':
        return "1001";
    case 'a':
    case 'A':
        return "1010";
    case 'b':
    case 'B':
        return "1011";
    case 'c':
    case 'C':
        return "1100";
    case 'd':
    case 'D':
        return "1101";
    case 'e':
    case 'E':
        return "1110";
    case 'f':
    case 'F':
        return "1111";
    default:
        break;
    }
    return "";
}

std::string dec2bits(std::string dec)
{
    int i;
    int orig = atoi(dec.c_str());
    std::string result;
    while (orig > 0) {
        i = orig % 2;
        result += static_cast<char>(i + '0');
        orig = orig / 2;
    }
    return result;
}

} // namespace

//
//  Parser Output functions
//
#ifdef HALT_ON_UNSUPPORTED_RULES
void yyerror(char const *msg, Object *o)
{
    assert(msg != nullptr);
    (*errorStream) << " -- ERROR: " << msg << endl;
    (*errorStream) << "    File: " << yyfilename.c_str() << " At line " << yylineno << ", column " << yycolumno << endl;

    if (o != nullptr) {
        hif::writeFile(*errorStream, o, false);
        *errorStream << std::endl;
    }
    assert(false);
    exit(1);
}
#else
void yyerror(char const *msg, Object *o)
{
    if (std::strncmp(msg, "syntax error", 12) == 0 || std::strncmp(msg, "File not found: ", 16) == 0 ||
        std::strncmp(msg, "Unsupported directive", 21) == 0) {
        (*errorStream) << " -- ERROR: " << msg << endl;
        (*errorStream) << "    File: " << yyfilename.c_str() << " At line " << yylineno << ", column " << yycolumno
                       << endl;
        if (o != nullptr) {
            hif::writeFile(*errorStream, o, false);
            *errorStream << std::endl;
        }
        assert(false);
        exit(1);
    }
}
#endif

void yyerror(VhdlParser *, const char *msg) { yyerror(msg, nullptr); }

void yywarning(char const *msg, Object *o)
{
    assert(msg != nullptr);
    (*errorStream) << " -- WARNING: " << msg << endl;
    (*errorStream) << "    File: " << yyfilename.c_str() << " At line " << yylineno << ", column " << yycolumno << endl;

    if (o != nullptr) {
        hif::writeFile(*errorStream, o, false);
        *errorStream << std::endl;
    }
}

void yydebug(char const *msg, Object *o)
{
    assert(msg != nullptr);
    (*debugStream) << " -- DEBUG: " << msg << " At line " << yylineno << ", column " << yycolumno << endl;

    if (o != nullptr) {
        hif::writeFile(*debugStream, o, false);
        *debugStream << std::endl;
    }
}

std::string toBits(const char *value, int base)
{
    std::string result = string("");

    for (unsigned int i = 0; i < strlen(value); i++) {
        if (base == 2) {
            result += value[i];
        } else if (base == 8) {
            result += oct2bits(value[i]);
        } else if (base == 10) {
            return dec2bits(value);
        } else if (base == 16) {
            result += hex2bits(value[i]);
        } else {
            yyerror("Base format not supported.");
        }
    }
    return result;
}

// IntValue * based_integer_to_intval( std::string base_lit )
// {
//     std::string::size_type s = base_lit.find( '_' );
//     while ( s != std::string::npos )
//     {
//         base_lit.replace( s, 1, "" );
//         s = base_lit.find( '_' );
//     }
//     atoi( base_lit );
// }

std::string fro2string(Value *vo)
{
    FieldReference *fieldref_o = dynamic_cast<FieldReference *>(vo);
    Identifier *identifier_o   = dynamic_cast<Identifier *>(vo);

    if (fieldref_o != nullptr) {
        string name = string(".") + string(fieldref_o->getName());
        return fro2string(fieldref_o->getPrefix()) + name;
    }
    if (identifier_o != nullptr) {
        return identifier_o->getName();
    }
    return "";
}

int bit2decimal(char *value)
{
    int result = 0;

    for (unsigned int i = 0; i < strlen(value); i++) {
        result = 2 * result + (value[i] - '0');
    }

    return result;
}

std::string stringToLower(std::string strToConvert)
{
    //change each element of the string to lower case
    for (unsigned int i = 0; i < strToConvert.length(); i++) {
        strToConvert[i] = static_cast<char>(tolower(strToConvert[i]));
    }
    return strToConvert;
}

std::string stringToUpper(std::string strToConvert)
{
    //change each element of the string to lower case
    for (unsigned int i = 0; i < strToConvert.length(); i++) {
        strToConvert[i] = static_cast<char>(toupper(strToConvert[i]));
    }
    return strToConvert; //return the converted string
}

std::string str_replace(char const *old_value, char const *new_value, std::string s)
{
    while (s.find(old_value) != std::string::npos) {
        s = s.replace(s.find(old_value), strlen(old_value), new_value);
    }

    return s;
}

Library *resolveLibraryType(Value *prefix, const bool skipNotFound)
{
    if (prefix == nullptr)
        return nullptr;

    Library *reference = nullptr;
    Library *ret       = nullptr;

    do {
        Identifier *id      = dynamic_cast<Identifier *>(prefix);
        FieldReference *ffr = dynamic_cast<FieldReference *>(prefix);
        Instance *inst      = dynamic_cast<Instance *>(prefix);
        if (skipNotFound && id == nullptr && ffr == nullptr && inst == nullptr) {
            return nullptr;
        }

        messageAssert(id != nullptr || ffr != nullptr || inst != nullptr, "Unexpected prefix (1)", prefix, nullptr);

        if (id != nullptr) {
            if (id->getName() != "work") {
                Library *lib = new Library();
                //setCodeInfo(lib);
                lib->setName(id->getName());
                if (skipNotFound) {
                    hif::semantics::DeclarationOptions dopt;
                    dopt.location = prefix;
                    LibraryDef *ld =
                        hif::semantics::getDeclaration(lib, hif::semantics::VHDLSemantics::getInstance(), dopt);
                    if (ld == nullptr) {
                        delete lib;
                        return nullptr;
                    }
                }

                if (reference != nullptr)
                    reference->setInstance(lib);
                reference = lib;
            }
            prefix = nullptr;
        } else if (inst != nullptr) {
            Library *lib = dynamic_cast<Library *>(inst->getReferencedType());
            messageAssert(lib != nullptr, "Unexpected prefix (2)", prefix, nullptr);
            inst->setReferencedType(nullptr);
            if (reference != nullptr)
                reference->setInstance(lib);
            reference = lib;
            prefix    = nullptr;
        } else // ffr != nullptr
        {
            if (ffr->getName() != "all") {
                Library *lib = new Library();
                //setCodeInfo(lib);
                lib->setName(ffr->getName());
                if (reference != nullptr)
                    reference->setInstance(lib);
                reference = lib;
            }
            prefix = ffr->getPrefix();
        }

        if (ret == nullptr)
            ret = reference;
    } while (prefix != nullptr);

    return ret;
}

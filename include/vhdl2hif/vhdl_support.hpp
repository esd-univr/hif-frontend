/// @file vhdl_support.hpp
/// @brief Header file that contains all the support functions used by the
/// VHDL parser to convert the VHDL code into the HIF format.
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#pragma once

#include "vhdl_parser.hpp"
#include "vhdl_parser_struct.hpp"
#include <hif/hif.hpp>
#include <string>

#define HALT_ON_UNSUPPORTED_RULES

#define BLOCK_STATEMENT_PROPERTY       "block_statement"
#define AGGREGREGATE_INIDICES_PROPERTY "AGGREGREGATE_INIDICES"

#define RECOGNIZED_FCALL_PROPERTY "RECOGNIZED_FUNCTION_CALL"
#define HIF_CONCURRENT_ASSERTION  "HIF_CONCURRENT_ASSERTION"

extern int yylineno;              // defined in vhdlParser.cc
extern int yycolumno;             // defined in vhdl_support.cc
extern std::string yyfilename;    // defined in vhdl_support.cc
extern std::ostream *msgStream;   // defined in vhdl2hif.cc
extern std::ostream *errorStream; // defined in vhdl2hif.cc
extern std::ostream *debugStream; // defined in vhdl2hif.cc

///
/// Output and debug functions
///
void yyerror [[noreturn]] (VhdlParser *, const char *msg);
void yyerror [[noreturn]] (const char *msg, hif::Object *o = nullptr);
void yywarning(const char *msg, hif::Object *o = nullptr);
void yydebug(const char *msg, hif::Object *o = nullptr);

template <typename T> hif::BList<T> *initBList(T *p)
{
    hif::BList<T> *ret = new hif::BList<T>();
    ret->push_back(p);
    return ret;
}

///
/// @brief Function use to convert value to bit representation; supported conversion
/// are:
/// <ul>
/// <li>bits to bits</li>
/// <li>octal to bits</li>
/// <li>decimal to bits</li>
/// <li>hex to bits</li>
/// </ul>
///
/// @param value char* representation of number that we want convert
/// @param base base information of value
/// @return string that represent bit representation of number
///
std::string toBits(const char *value, int base);

// //////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////

/// @brief Function used to translate a FieldReference
/// in a string dot notation
///
/// @param vo FielfRefObject to translate
/// @return String notation
///
std::string fro2string(hif::Value *vo);

///
/// @brief Function that starts from char* representation of bit value
/// and return it decimal representation.
///
/// @param value char* representation if bit value
/// @return integer value that represent bit string
///
int bit2decimal(char *value);

///
/// @brief Function that give an input string return toLower representation
/// of input string
///
/// @param strToConvert that we want manipulate
/// @return toLower representation of input string
///
std::string stringToLower(std::string strToConvert);

///
/// @brief Function that give an input string return toupper representation
/// of input string
///
/// @param strToConvert that we want manipulate
/// @return toLower representation of input string
///
std::string stringToUpper(std::string strToConvert);

/// @brief Replaces old_value with new_value inside given string.
std::string str_replace(char const *old_value, char const *new_value, std::string s);

/// @brief Given a value, translate it as nested libraries.
///
/// @param prefix The value.
/// @param skipNotFound Not found returns nullptr, instead of going into error.
/// @return The nested libraries.
///
hif::Library *resolveLibraryType(hif::Value *prefix, const bool skipNotFound = false);

// //////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////

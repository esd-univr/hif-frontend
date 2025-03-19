/// @file support.hpp
/// @brief Support for Verilog to HIF conversion.
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#pragma once

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

// HIF library
#include <hif/hif.hpp>

#include "verilog_parser.hpp"
#include "parser_struct.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#define HALT_ON_UNSUPPORTED_RULES

extern int yylineno;              // defined in verilogParser.cc
extern int yycolumno;             // defined in verilog_support.cc
extern std::string yyfilename;    // defined in verilog_support.cc
extern std::ostream *msgStream;   // defined in verilog2hif.cc
extern std::ostream *errorStream; // defined in verilog2hif.cc
extern std::ostream *debugStream; // defined in verilog2hif.cc

/////////////////////////////////////////////////////////////////
// Properties used by fix description.
/////////////////////////////////////////////////////////////////
extern const char *PROPERTY_SENSITIVE_POS;
extern const char *PROPERTY_SENSITIVE_NEG;
extern const char *PROPERTY_TASK_NOT_AUTOMATIC;
extern const char *PROPERTY_GENVAR;

// Property related to Assign. Default case (if not set): blocking assignment,
// else (if set): non-blocking assignment.
extern const char *NONBLOCKING_ASSIGNMENT;
extern const char *IS_VARIABLE_TYPE;
extern const char *HIF_ALL_SENSITIVITY;
extern const char *INITIAL_STATEMENT;

/////////////////////////////////////////////////////////////////
// Functions.
/////////////////////////////////////////////////////////////////

/// This function build an Int that respects the verilog semantics.
hif::Bitvector *makeVerilogIntegerType();

/// This function builds a Bit that respects the verilog semantics.
hif::Bit *makeVerilogBitType();

/// This function builds an Array that represent a verilog reg. It is
/// an array unsigned, packed with type bit logic. The range given is
/// not copied.
/// @param range the range of the array
hif::Bitvector *makeVerilogRegisterType(hif::Range *range = nullptr);

/// This function builds an Array that represent a verilog reg signed.
/// It is an array unsigned, packed with type bit logic. The range given
/// is not copied.
/// @param range the range of the array
hif::Bitvector *makeVerilogSignedRegisterType(hif::Range *range = nullptr);

/// @brief This function simplify a Value also if the given
/// object is not inserted in an HIF tree. This is necessary because
/// the replacement fails.
/// @param o the object to simplify.
/// @param simplify_names set it to true if you want names and template
///  parameter simplified
// void _simplify( hif::Value* o, bool simplify_names = false );

/// This function builds a concatenation for all the values inside the given
/// list. The list must be not empty. The values provided are copied
hif::Value *_concat(hif::BList<hif::Value> &values);

hif::Type *getSemanticType(hif::Range *ro, bool is_signed = false);

std::string convertToBinary(std::string number, int numbit);
std::string convertToBinary(int number, int numbit);

// Given a Switchalt of a casez or casex, transforms z & x into dontcare.
void clean_bitvalues(hif::SwitchAlt *c, const bool x);

template <typename Child_t, typename Parent_t> hif::BList<Child_t> *blist_scast(hif::BList<Parent_t> *p)
{
    // Parent_t * tmp_p = nullptr;
    // Child_t * tmp_c = nullptr;
    // tmp_c = static_cast<Child_t *>( tmp_p );
    return reinterpret_cast<hif::BList<Child_t> *>(p);
}

///
/// Output and debug functions
///
void yyerror(VerilogParser *, const char *msg);
void yyerror(const char *msg, hif::Object *o = nullptr);
void yywarning(const char *msg, hif::Object *o = nullptr);
void yydebug(const char *msg, hif::Object *o = nullptr);
void yymessage(const char *str);

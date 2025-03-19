/// @file vhdl_post_parsing_methods.hpp
/// @brief
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#pragma once

#include <hif/hif.hpp>

void performRangeRefinements(hif::System *o, const bool use_int_32, hif::semantics::VHDLSemantics *sem);

void performStep1Refinements(hif::System *o, hif::semantics::ILanguageSemantics *sem);

void performStep2Refinements(hif::System *o, hif::semantics::ILanguageSemantics *sem);

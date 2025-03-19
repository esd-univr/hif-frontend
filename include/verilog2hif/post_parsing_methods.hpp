/// @file post_parsing_methods.hpp
/// @brief Contains the declarations of the functions that perform the post-parsing refinements on the HIF AST.
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#pragma once

void performStep1Refinements(hif::System *o, hif::semantics::ILanguageSemantics *sem);

void performStep2Refinements(hif::System *o, hif::semantics::ILanguageSemantics *sem);

void performStep3Refinements(hif::System *o, hif::semantics::ILanguageSemantics *sem, const bool preserveStructure);

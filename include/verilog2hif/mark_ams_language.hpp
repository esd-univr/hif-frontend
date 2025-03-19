/// @file mark_ams_language.hpp
/// @brief Marks units with the AMS language tag.
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#pragma once

#include <hif/hif.hpp>

/// @brief Marks units with the AMS language tag.
/// @details
/// In fact, parser cannot directly mark design units and libraries as AMS.
/// Thus, this visitor refines from RTL to AMS language.
/// @param o The object to mark.
/// @param sem The semantics object.
void markAmsLanguage(hif::Object *o, hif::semantics::ILanguageSemantics *sem);

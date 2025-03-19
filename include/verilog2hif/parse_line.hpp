/// @file parse_line.hpp
/// @brief Contains the command line parser for the Verilog2hif application.
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#pragma once

#include <hif/hif.hpp>

/// @brief Class to parse the command line arguments for the Verilog2hif application.
class Verilog2hifParseLine : public hif::application_utils::CommandLineParser
{
public:

    /// @brief Constructor for the Verilog2hifParseLine class.
    /// @param argc number of arguments.
    /// @param argv array of arguments.
    Verilog2hifParseLine(int argc, char *argv[]);

    /// @brief Destructor for the Verilog2hifParseLine class.
    virtual ~Verilog2hifParseLine();

    /// @brief Function to get the Verilog-AMS source file.
    /// @return Files object containing the Verilog-AMS source file.
    hif::Files getAmsFiles();

    /// @brief If the user whants to consider ternary operators without 'X' and
    /// 'Z' cases.
    /// @return <tt>true</tt> if the user wants to consider ternary operators
    /// without 'X' and 'Z' cases, <tt>false</tt> otherwise.
    bool getTernary() const;

    /// @brief If the user whants to preserve design structure even when this
    /// could lead to non-equivalent translation.
    /// @return <tt>true</tt> if the user wants to preserve design structure
    /// even when this could lead to non-equivalent translation, <tt>false</tt>
    /// otherwise.
    bool getStructure() const;

protected:

    /// @brief Validates and configures the arguments.
    void _validateArguments();

    /// @brief Function to check a Verilog source file.
    /// The correct shape of the Verilog source file is : file_name.v.
    /// @param fileName string name of the file.
    /// @return <tt>true</tt> if the file_name is a correct Verilog source file, <tt>false</tt> otherwise.
    bool _checkVerilogFile(const std::string &fileName);

    /// @brief Function to check a Verilog-AMS source file.
    /// The correct shape of the Verilog-AMS source file is : file_name.va
    /// @param fileName string name of the file.
    /// @return <tt>true</tt> if the file_name is a correct Verilog-AMS source file, <tt>false</tt> otherwise.
    bool _checkVerilogAmsFile(const std::string &fileName);

    /// @brief Function to extract the name of the verilog source file.
    /// @param fileName string name of the file.
    /// @return string containing only the name of the file. If <tt>file_name</tt> is a path
    /// (e.g.: ../dir1/dir2/foo.v), the function returns the file name (foo.v).
    std::string _cleanFileName(const std::string &fileName);

    /// @brief Function to extract the path of the verilog source file.
    hif::Files _amsFiles;
};

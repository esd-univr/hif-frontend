/// @file verilog2hif_parse_line.cpp
/// @brief
/// Copyright (c) 2024-2025, Electronic Systems Design (ESD) Group,
/// Univeristy of Verona.
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#include "verilog2hif/parse_line.hpp"

static inline bool __checkExtension(const std::string &file_name, const std::string &extension)
{
    std::string::size_type size, index;
    size  = file_name.size();
    index = file_name.find(extension, 0);

    return ((index == std::string::npos) || (size - index != extension.size())) ? false : true;
}

Verilog2hifParseLine::Verilog2hifParseLine(int argc, char *argv[])
    : CommandLineParser()
    , _amsFiles()
{
    addToolInfos(
        // Tool name.
        "verilog2hif",
        // Copyright.
        "Copyright (c) 2024-2025, Electronic Systems Design (ESD) Group, Univeristy of Verona."
        "This file is distributed under the BSD 2-Clause License.",
        // description
        "Generates HIF from a Verilog/Verilog-AMS description.",
        // synopsys
        "verilog2hif [OPTIONS] <VERILOG FILES>",
        // notes
        "- The default output file name is out.hif.\n"
        "- The output file specified via '-o' can have no extension.\n"
        "\n"
        "Email: enrico.fraccaroli@univr.it    Site: Coming Soon");

    addHelp();
    addVersion();
    addVerbose();
    addOutputFile();
    addPrintOnly();
    addParseOnly();
    addWriteParsing();
    addOption('t', "ternary", false, true, "Consider ternary operators without 'X' and 'Z' cases.");
    addOption(
        's', "structure", false, true,
        "Preserve design structure even when this could lead to "
        "non-equivalent translation.");

    parse(argc, argv);

    _validateArguments();
}

Verilog2hifParseLine::~Verilog2hifParseLine()
{
    // ntd
}

bool Verilog2hifParseLine::getTernary() const { return isOptionFlagSet('t'); }

bool Verilog2hifParseLine::getStructure() const { return isOptionFlagSet('s'); }

void Verilog2hifParseLine::_validateArguments()
{
    if (!_options['h'].value.empty())
        printHelp();
    if (!_options['v'].value.empty())
        printVersion();

    if (_files.empty()) {
        messageError(
            "Verilog input file missing.\n"
            "Try 'verilog2hif --help' for more information",
            nullptr, nullptr);
    }

    // Validate input file list
    for (Files::iterator i = _files.begin(); i != _files.end();) {
        std::string cleanFile = _cleanFileName(*i);
        if (_checkVerilogFile(cleanFile)) {
            // OK
            ++i;
        } else if (_checkVerilogAmsFile(cleanFile)) {
            _amsFiles.push_back(*i);
            i = _files.erase(i);
        } else {
            messageError("Unrecognized file format:" + cleanFile, nullptr, nullptr);
        }
    }

    // Establish output file name
    std::string out = _options['o'].value;
    if (out == "") {
        out = "out.hif.xml";
    } else {
        std::string::size_type ix = out.find(".hif.xml");
        if (ix == std::string::npos)
            out += ".hif.xml";
    }
    _options['o'].value = out;
}

bool Verilog2hifParseLine::_checkVerilogFile(const std::string &fileName) { return __checkExtension(fileName, ".v"); }

bool Verilog2hifParseLine::_checkVerilogAmsFile(const std::string &fileName)
{
    return __checkExtension(fileName, ".va") || __checkExtension(fileName, ".vams");
}

std::string Verilog2hifParseLine::_cleanFileName(const std::string &fileName)
{
    std::string cleanFile;
    size_t found = fileName.find_last_of("/");
    if (found != std::string::npos)
        cleanFile = fileName.substr(found + 1);
    else
        cleanFile = fileName;

    return cleanFile;
}

std::vector<std::string> Verilog2hifParseLine::getAmsFiles() { return _amsFiles; }

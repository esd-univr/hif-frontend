/// @file verilog_parser.hpp
/// @brief Header file for the VerilogParser class.
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#pragma once

#include <cstdlib>

#include <hif/hif.hpp>

#include "parser_struct.hpp"

class VerilogParser;
#include "parse_line.hpp"
#include "verilog_parser.hpp"

class VerilogParser
{
public:
    VerilogParser(std::string fileName, const Verilog2hifParseLine &cLine);
    ~VerilogParser();

    static hif::System *buildSystemObject();
    static void setVerilogAms(const bool b);
    static bool isVerilogAms();

    /*
     * Public functions
     * --------------------------------------------------------------------- */
    bool parse(bool parseOnly);
    bool isParseOnly();
    void setCodeInfo(hif::Object *o, const bool recursive = false);
    void setCodeInfo(hif::Object *o, keyword_data_t &keyword);
    void setCurrentBlockCodeInfo(keyword_data_t &keyword);
    void setCurrentBlockCodeInfo(hif::Object *other);
    void setCodeInfoFromCurrentBlock(hif::Object *o);
    void SetTimescale(hif::TimeValue *unit, hif::TimeValue *precision);
    hif::HifFactory *getFactory();

    /*
     * VERILOG-Grammar related functions
     * --------------------------------------------------------------------- */

    hif::Identifier *parse_Identifier(char *identifier);

    /* -----------------------------------------------------------------------
     *  VERILOG SOURCE TEXT
     * -----------------------------------------------------------------------
     */
    hif::DesignUnit *parse_ModuleDeclarationStart(char *identifier);

    void parse_ModuleDeclaration(
        hif::DesignUnit *designUnit,
        hif::BList<hif::Declaration> *paramList,
        hif::BList<hif::Port> *list_of_ports,
        bool referencePortList,
        std::list<module_item_t *> *module_item_list);

    void parse_ModuleDeclaration(
        hif::DesignUnit *du,
        hif::BList<hif::Declaration> *paramList,
        std::list<non_port_module_item_t *> *non_port_module_item_list);

    /* -----------------------------------------------------------------------
     *  PROCEDURAL BLOCKS AND ASSIGNMENTS
     * -----------------------------------------------------------------------
     */
    hif::Assign *parse_AnalogVariableAssignment(hif::Value *lvalue, hif::Value *expression);

    hif::Contents *parse_InitialConstruct(statement_t *statement);

    hif::Assign *
    parse_BlockingAssignment(hif::Value *variable_lvalue, hif::Value *expression, hif::Value *delay_or_event_control);

    hif::Assign *parse_NonBlockingAssignment(
        hif::Value *variable_lvalue,
        hif::Value *expression,
        hif::Value *delay_or_event_control);

    hif::StateTable *parse_AlwaysConstruct(statement_t *statement);

    hif::StateTable *parse_AnalogConstruct(analog_statement_t *statement);

    hif::Assign *parse_Assignment(hif::Value *lvalue, hif::Value *expression);

    hif::BList<hif::Assign> *
    parse_ContinuousAssign(hif::Value *delay3_opt, hif::BList<hif::Assign> *list_of_net_assignments);

    /* -----------------------------------------------------------------------
     *  PORT_DECLARATIONS
     * -----------------------------------------------------------------------
     */
    hif::Port *
    parse_InoutDeclaration(discipline_and_modifiers_t *discipline_and_modifiers, hif::Identifier *identifier);

    hif::BList<hif::Port> *parse_InoutDeclaration(
        discipline_and_modifiers_t *discipline_and_modifiers,
        hif::BList<hif::Identifier> *list_of_identifiers);

    hif::Port *
    parse_InputDeclaration(discipline_and_modifiers_t *discipline_and_modifiers, hif::Identifier *identifier);

    hif::BList<hif::Port> *parse_InputDeclaration(
        discipline_and_modifiers_t *discipline_and_modifiers,
        hif::BList<hif::Identifier> *list_of_identifiers);

    hif::Port *
    parse_OutputDeclaration(discipline_and_modifiers_t *discipline_and_modifiers, hif::Identifier *identifier);

    hif::Port *
    parse_OutputDeclaration(bool k_signed, hif::Range *range, char *identifier, hif::Value *initVal, const bool isReg);

    hif::Port *parse_OutputDeclaration(hif::Type *output_variable_type, char *identifier, hif::Value *initVal);

    hif::BList<hif::Port> *parse_OutputDeclaration(
        discipline_and_modifiers_t *discipline_and_modifiers,
        hif::BList<hif::Identifier> *list_of_identifiers);

    hif::BList<hif::Port> *
    parse_OutputDeclaration(hif::Type *output_variable_type, hif::BList<hif::Port> *list_of_variable_port_identifiers);

    hif::BList<hif::Port> *parse_OutputDeclaration(
        bool k_signed,
        hif::Range *range,
        hif::BList<hif::Port> *list_of_variable_port_identifiers,
        const bool isReg);

    hif::Port *parse_VariablePortIdentifier(char *identifier, hif::Value *initVal);

    hif::Variable *parse_EventIdentifier(char *identifier, hif::BList<hif::Range> *dimension_list);

    hif::Port *parse_PortReference(char *identifier);
    //hif::Value * parse_PortReference( hif::Identifier * identifier, /* RANGE_EXPRESSION */ );

    /* -----------------------------------------------------------------------
     *  TYPE DECLARATIONS
     * -----------------------------------------------------------------------
     */

    hif::BList<hif::Declaration> *parse_BranchDeclaration(
        hif::BList<hif::Value> *branch_terminal_list,
        hif::BList<hif::Identifier> *list_of_identifiers);
    hif::Member *parse_BranchTerminal(char *identifier, hif::Value *range_expression);

    /* -----------------------------------------------------------------------
     *  MODULE PARAMETER DECLARATIONS
     * -----------------------------------------------------------------------
     */
    hif::BList<hif::ValueTP> *parse_ParameterDeclaration(
        bool k_signed,
        hif::Range *range,
        hif::BList<hif::Assign> *list_of_param_assignments,
        hif::Type *parameter_type);

    hif::BList<hif::Const> *parse_LocalParameterDeclaration(
        bool K_signed_opt,
        hif::Range *range_opt,
        hif::BList<hif::Assign> *list_of_param_assignments,
        hif::Type *parameter_type);

    /* -----------------------------------------------------------------------
     *  DECLARATION ASSIGNMENTS
     * -----------------------------------------------------------------------
     */
    hif::Assign *parse_ParamAssignment(
        char *identifier,
        hif::Value *expr,
        hif::Range *range_opt                          = nullptr,
        hif::BList<hif::Value *> *value_range_list_opt = nullptr);

    /* -----------------------------------------------------------------------
     *  DECLARATION RANGES
     * -----------------------------------------------------------------------
     */
    hif::Range *parse_Range(hif::Value *lbound, hif::Value *rbound);

    /* -----------------------------------------------------------------------
     *  EXPRESSIONS - NUMBERS
     * -----------------------------------------------------------------------
     */
    hif::Value *parse_UnsignedNumber(number_t &DEC_NUMBER);
    hif::Value *parse_BasedNumber(number_t &BASED_NUMBER);
    hif::Value *parse_DecNumber(number_t &BASED_NUMBER);
    hif::Value *parse_DecBasedNumber(number_t &DEC_NUMBER, number_t &BASED_NUMBER);
    hif::Value *parse_RealTime(real_number_t &REALTIME);

    /* -----------------------------------------------------------------------
     *  EXPRESSIONS
     * -----------------------------------------------------------------------
     */
    hif::Value *parse_ExpressionUnaryOperator(hif::Operator unary_op, hif::Value *primary, const bool negate = false);

    hif::Value *parse_ExpressionBinaryOperator(
        hif::Value *expression1,
        hif::Operator binary_op,
        hif::Value *expression2,
        const bool negate = false);

    hif::Value *
    parse_ExpressionTernaryOperator(hif::Value *expression1, hif::Value *expression2, hif::Value *expression3);

    hif::Value *parse_ExpressionNorOperator(hif::Value *primary);

    hif::Value *parse_RangeExpression(hif::Value *lbound, hif::Value *rbound);

    hif::Value *parse_RangeExpressionPO_POS(hif::Value *lbound, hif::Value *rbound);

    hif::Value *parse_RangeExpressionPO_NEG(hif::Value *lbound, hif::Value *rbound);

    /* -----------------------------------------------------------------------
     *  EVENT STATEMENTS
     * -----------------------------------------------------------------------
     */
    hif::ValueStatement *
    parse_EventTrigger(hif::Value *hierarchical_identifier, hif::BList<hif::Value> *bracket_expression_list);

    /* -----------------------------------------------------------------------
     *  EXPRESSIONS - PRIMARIES
     * -----------------------------------------------------------------------
     */
    hif::Value *parse_PrimaryListOfMemberOrSlice(char *identifier, hif::BList<hif::Value> *range_expression_list);

    /* -----------------------------------------------------------------------
     *  EXPRESSIONS - CONCATENATIONS
     * -----------------------------------------------------------------------
     */
    hif::Value *parse_Concatenation(hif::BList<hif::Value> *value_list);

    hif::Value *parse_MultipleConcatenation(hif::Value *expression, hif::Value *concatenation);

    hif::Value *parse_ArrayInitialization(hif::BList<hif::Value> *value_list);

    /* -----------------------------------------------------------------------
     *  GENERAL - IDENTIFIERS
     * -----------------------------------------------------------------------
     */
    hif::Value *
    parse_HierarchicalIdentifier(hif::Value *hierarchical_identifier, hif::Value *hierarchical_identifier_item);

    hif::Value *parse_HierarchicalIdentifierItem(char *identifier);

    /* -----------------------------------------------------------------------
     *  TYPE_DECLARATIONS
     * -----------------------------------------------------------------------
     */

    hif::Signal *parse_Type(char *identifier, hif::BList<hif::Range> *non_empty_dimension_list);

    hif::Signal *parse_Type(char *identifier, hif::Value *expression);

    hif::Signal *parse_Type(char *identifier);

    hif::BList<hif::Declaration> *parse_EventDeclaration(hif::BList<hif::Variable> *list_of_variable_identifiers);

    hif::BList<hif::Declaration> *parse_IntegerDeclaration(hif::BList<hif::Signal> *list_of_variable_identifiers);

    hif::BList<hif::Declaration> *parse_RealDeclaration(hif::BList<hif::Signal> *list_of_real_identifiers);

    hif::BList<hif::Declaration> *parse_RegDeclaration(
        discipline_identifier_signed_range_t *discipline_identifier_signed_range,
        hif::BList<hif::Signal> *list_of_variable_identifiers);

    hif::BList<hif::Declaration> *parse_TimeDeclaration(hif::BList<hif::Signal> *list_of_variable_identifiers);

    hif::BList<hif::Declaration> *parse_NetDeclaration(
        bool is_signed,
        hif::Range *range,
        std::list<net_ams_decl_identifier_assignment_t *> *identifiers_or_assign,
        hif::Type *explicitType = nullptr);

    /* -----------------------------------------------------------------------
     *  EXPRESSIONS - FUNCTION CALLS
     * -----------------------------------------------------------------------
     */

    hif::FunctionCall *parse_InitialOrFinalStep(const std::string &name, std::list<std::string> *string_list);

    hif::Value *parse_FunctionCall(hif::Value *hierarchical_identifier, hif::BList<hif::Value> *expression_list);

    /* -----------------------------------------------------------------------
     *  EXPRESSIONS - NATURE ATTRIBUTE REFERENCE
     * -----------------------------------------------------------------------
     */
    hif::Value *parse_NatureAttributeReference(
        hif::Value *hierarchical_identifier,
        const std::string &fieldName,
        hif::Value *nature_attribute_identifier);

    hif::Value *parse_AmsFlowOfPort(hif::Value *hierarchical_identifier, hif::Value *expression_list);

    /* -----------------------------------------------------------------------
     *  CASE STATEMENTS
     * -----------------------------------------------------------------------
     */
    hif::Switch *parse_CaseStatement(
        hif::Value *expression,
        hif::BList<hif::SwitchAlt> *case_item_list,
        const hif::CaseSemantics caseSem);

    hif::SwitchAlt *parse_CaseItem(hif::BList<hif::Value> *expression_list, statement_t *statement_or_null);

    hif::SwitchAlt *
    parse_AnalogCaseItem(hif::BList<hif::Value> *expression_list, analog_statement_t *analog_statement_or_null);

    /* -----------------------------------------------------------------------
     *  CONDITIONAL STATEMENTS
     * -----------------------------------------------------------------------
     */

    hif::If *parse_ConditionalStatement(hif::If *ifStatement);

    hif::If *parse_ConditionalStatement(
        hif::Value *expression,
        statement_t *statement_or_null,
        statement_t *else_statement_or_null);

    hif::If *parse_AnalogConditionalStatement(
        hif::Value *expression,
        analog_statement_t *analog_statement_or_null,
        analog_statement_t *else_analog_statement_or_null);

    hif::BList<hif::IfAlt> *parse_ElseIfStatementOrNullList(
        hif::BList<hif::IfAlt> *elseif_statement_or_null_list,
        hif::Value *expression,
        statement_t *statement_or_null);

    /* -----------------------------------------------------------------------
     *  GENERATE STATEMENTS
     * -----------------------------------------------------------------------
     */

    hif::BList<hif::Generate> *parse_IfGenerateConstruct(
        hif::Value *expression,
        generate_block_t *generate_block_or_null_if,
        generate_block_t *generate_block_or_null_else);

    hif::BList<hif::Generate> *parse_LoopGenerateConstruct(
        hif::Assign *genvar_initialization,
        hif::Value *expression,
        hif::Assign *genvar_iteration,
        generate_block_t *generate_block);

    /* -----------------------------------------------------------------------
     *  TIMING CONTROL STATEMENTS
     * -----------------------------------------------------------------------
     */

    statement_t *parse_ProceduralTimingControlStatement(
        procedural_timing_control_t *procedural_timing_control,
        statement_t *stat_or_null);

    analog_statement_t *
    parse_AnalogEventControlStatement(analog_event_control_t *event_control, analog_statement_t *stat_or_null);

    statement_t *parse_WaitStatement(hif::Value *expression, statement_t *statement_or_null);

    analog_event_expression_t *
    parseOrAnalogEventExpression(analog_event_expression_t *e1, analog_event_expression_t *e2);

    /* -----------------------------------------------------------------------
     *  LOOPING STATEMENTS
     * -----------------------------------------------------------------------
     */
    template <typename T> hif::While *parse_LoopStatementWhile(hif::Value *expression, T *statement);
    template <typename T>
    hif::For *parse_LoopStatementFor(
        hif::Assign *init_variable_assign,
        hif::Value *expression,
        hif::Assign *step_variable_assign,
        T *statement);
    template <typename T> hif::For *parse_LoopStatementRepeat(hif::Value *expression, T *statement);
    hif::While *parse_LoopStatementForever(statement_t *statement);

    /* -----------------------------------------------------------------------
     *  TASK DECLARATIONS
     * -----------------------------------------------------------------------
     */
    hif::Procedure *parse_TaskDeclaration(
        bool isAutomatic,
        char *identifier,
        std::list<task_item_declaration_t *> *task_item_declaration_list,
        statement_t *statement_or_null);

    //    hif::Procedure * parse_TaskDeclaration( hif::Identifier * identifier,
    //            hif::BList<hif::Port> * task_port_list,
    //            hif::BList<block_item_declaration_t> * block_item_declaration_list,
    //            hif::BList<hif::Action> * statements);

    hif::Port *parse_TfDeclaration(
        hif::PortDirection dir,
        bool isSigned,
        hif::Range *range_opt,
        hif::Identifier *identifier,
        hif::Type *task_port_type);

    hif::BList<hif::Port> *parse_TfDeclaration(
        hif::PortDirection dir,
        bool isSigned,
        hif::Range *range_opt,
        hif::BList<hif::Identifier> *list_of_identifiers,
        hif::Type *task_port_type);

    hif::BList<hif::Signal> *parse_BlockItemDeclaration_Reg(
        bool signed_opt,
        hif::Range *range_opt,
        hif::BList<hif::Signal> *list_of_block_variable_identifiers);

    hif::BList<hif::Signal> *
    parse_BlockItemDeclaration_Integer(hif::BList<hif::Signal> *list_of_block_variable_identifiers);

    hif::Signal *parse_BlockVariableType(char *identifier, hif::BList<hif::Range> *dimension_list);

    hif::StateTable *parse_AnalogSeqBlock(
        const char *block_identifier,
        hif::BList<hif::Declaration> *analog_block_item_declaration_list,
        hif::StateTable *analog_statement_no_empty_list);

    hif::Break *parse_DisableStatement(hif::Value *hierarchical_identifier);

    /* -----------------------------------------------------------------------
     *  FUNCTION DECLARATIONS
     * -----------------------------------------------------------------------
     */
    hif::Function *parse_FunctionDeclaration(
        hif::Type *function_range_or_type,
        char *identifier,
        std::list<function_item_declaration_t *> *function_item_declaration_list,
        statement_t *statements);

    hif::Function *parse_FunctionDeclaration(
        hif::Type *function_range_or_type,
        char *identifier,
        hif::BList<hif::Port> *function_port_list,
        std::list<block_item_declaration_t *> *block_item_declaration_list,
        statement_t *statements);

    /* -----------------------------------------------------------------------
     *  FUNCTION/PROCEDURE CALLS
     * -----------------------------------------------------------------------
     */
    hif::FunctionCall *parse_BranchProbeFunctionCall(
        hif::Value *nature_attribute_identifier,
        hif::Value *hierarchical_identifier1,
        hif::Value *hierarchical_identifier2);

    hif::FunctionCall *parse_AnalogDifferentialFunctionCall(
        const char *function_name,
        hif::Value *expression1,
        hif::Value *expression2 = nullptr,
        hif::Value *expression3 = nullptr,
        hif::Value *expression4 = nullptr);

    hif::ProcedureCall *parse_ContributionStatement(hif::Value *branch_probe_function_call, hif::Value *expression);

    hif::ProcedureCall *parse_IndirectContributionStatement(
        hif::Value *branch_probe_function_call,
        hif::Value *indirect_expression,
        hif::Value *expression);

    hif::Value *parse_AnalogBuiltInFunctionCall(
        hif::Identifier *analog_built_in_function_name,
        hif::Value *analog_expression1,
        hif::Value *analog_expression2);

    hif::FunctionCall *parse_analysisFunctionCall(std::list<std::string> *string_list);

    hif::FunctionCall *parse_AnalogFilterFunctionCall(
        const char *function_name,
        hif::Value *expression1,
        hif::Value *expression2,
        hif::Value *expression3,
        hif::Value *expression4,
        hif::Value *expression5,
        hif::Value *expression6);

    hif::FunctionCall *parse_AnalogFilterFunctionCallArg(
        const char *function_name,
        hif::Value *expression1,
        analog_filter_function_arg_t *expression2,
        analog_filter_function_arg_t *expression3,
        hif::Value *expression4,
        hif::Value *expression5,
        hif::Value *expression6);

    hif::FunctionCall *parse_AnalogSmallSignalFunctionCall(
        const char *function_name,
        hif::Value *expression1,
        hif::Value *expression2,
        hif::Value *expression3);

    hif::FunctionCall *parse_analogEventFunction(
        const char *function_name,
        hif::Value *expression1,
        hif::Value *expression2 = nullptr,
        hif::Value *expression3 = nullptr,
        hif::Value *expression4 = nullptr,
        hif::Value *expression5 = nullptr);

    event_expression_t *parse_EventExpressionDriverUpdate(hif::Value *expression);

    hif::ProcedureCall *parse_SystemTaskEnable(
        const char *systemIdentifier,
        hif::Value *expression_opt,
        hif::BList<hif::Value> *expression_comma_list);

    hif::ProcedureCall *
    parse_TaskEnable(hif::Value *hierarchical_identifier, hif::BList<hif::Value> *comma_expression_list);

    /* -----------------------------------------------------------------------
     *  MODULE INSTATIATION
     * -----------------------------------------------------------------------
     */
    hif::PortAssign *parse_NamedPortConnectionList(char *identifier, hif::Value *expression_opt);

    module_instance_and_net_ams_decl_identifier_assignment_t *
    parse_ModuleInstance(std::list<net_ams_decl_identifier_assignment_t *> *net_ams_decl_identifier_assignment_list);

    module_instance_and_net_ams_decl_identifier_assignment_t *parse_ModuleInstance(
        hif::Identifier *name_of_module_instance,
        list_of_port_connections_t *list_of_port_connections_opt);

    std::list<module_instance_and_net_ams_decl_identifier_assignment_t *> *parse_ModuleInstantiation(
        char *identifier,
        std::list<module_instance_and_net_ams_decl_identifier_assignment_t *> *module_instance_list);

    std::list<module_instance_and_net_ams_decl_identifier_assignment_t *> *parse_ModuleInstantiation(
        char *identifier,
        hif::BList<hif::ValueTPAssign> *parameter_value_assignment,
        std::list<module_instance_and_net_ams_decl_identifier_assignment_t *> *module_instance_list);

    std::list<module_instance_and_net_ams_decl_identifier_assignment_t *> *parse_ModuleInstantiation(
        char *identifier,
        hif::Range *range_opt,
        std::list<module_instance_and_net_ams_decl_identifier_assignment_t *> *module_instance_list);

    hif::BList<hif::ValueTPAssign> *
    parse_ParameterValueAssignment(list_of_parameter_assignment_t *list_of_parameter_assignment);

    hif::ValueTPAssign *parse_NamedParameterAssignment(char *identifier, hif::Value *mintypmax_expression_opt);

    module_or_generate_item_t *parse_ModuleOrGenerateItem(
        std::list<module_instance_and_net_ams_decl_identifier_assignment_t *> *module_instantiation);

    hif::Variable *parse_NatureBinding(char *identifier, const bool isPotential);
    void parse_DisciplineDeclaration(char *identifier, hif::BList<hif::Variable> *discipline_item_list);

    hif::Function *parse_AnalogFunctionDeclaration(
        const bool isInteger,
        char *identifier,
        analog_function_item_declaration_t *analog_function_item_declaration_list,
        analog_statement_t *analog_function_statement);

    hif::BList<hif::Declaration> *parse_GenvarDeclaration(hif::BList<hif::Identifier> *list_of_identifiers);

    /* -----------------------------------------------------------------------
     *  PARALLEL AND SEQUENTIAL BLOCKS
     * -----------------------------------------------------------------------
     */
    statement_t *parse_SeqBlock(
        char *identifier,
        std::list<block_item_declaration_t *> *declarations,
        std::list<statement_t *> *statements);

    analog_statement_t *parse_AnalogSeqBlock(
        char *identifier,
        hif::BList<hif::Declaration> *declarations,
        std::list<analog_statement_t *> *statements);

    /* -----------------------------------------------------------------------
     *  SPECIFY SECTION
     *
     *  SPECIFY BLOCK DECLARATION
     * -----------------------------------------------------------------------
     */

    std::list<specify_item_t *> *parse_SpecifyBlock(std::list<specify_item_t *> *items);

    /* -----------------------------------------------------------------------
     *  SYSTEM TIMING CHECK COMMANDS
     * -----------------------------------------------------------------------
     */

    hif::Value *parse_TimingCheckEvent(
        timing_check_event_control_t *timing_check_event_control_opt,
        specify_terminal_descriptor_t *specify_terminal_descriptor);

    hif::ProcedureCall *parse_TimingCheck(
        const char *name,
        hif::Value *dataEvent,
        hif::Value *referenceEvent,
        hif::Value *timingCheckLimit,
        hif::Identifier *notifier);

    specify_terminal_descriptor_t *parse_SpecifyTerminalDescriptor(char *identifier, hif::Value *range_expression);

private:
    VerilogParser(const VerilogParser &);
    VerilogParser &operator=(const VerilogParser &);

    std::string _fileName;
    unsigned int _tmpCustomLineNumber;
    unsigned int _tmpCustomColumnNumber;
    bool _parseOnly;
    hif::TimeValue *_unit;
    hif::TimeValue *_precision;
    hif::semantics::ILanguageSemantics *_sem;
    hif::HifFactory _factory;

    static hif::BList<hif::DesignUnit> *_designUnits;
    static bool _isVerilogAms;

    const Verilog2hifParseLine &_cLine;

    void _fillBaseContentsFromModuleOrGenerateItem(
        hif::BaseContents *contents_o,
        module_or_generate_item_t *mod_item,
        hif::DesignUnit *designUnit = nullptr);

    void _buildActionList(
        statement_t *statement,
        hif::BList<hif::Action> &actionList,
        hif::BList<hif::Declaration> *declList = nullptr);

    void _buildActionList(analog_statement_t *statement, hif::BList<hif::Action> &actionList);

    void _buildActionListFromStatement(
        statement_t *statement,
        hif::BList<hif::Action> &actionList,
        hif::BList<hif::Declaration> *declList = nullptr);

    void _buildActionListFromAnalogStatement(analog_statement_t *statement, hif::BList<hif::Action> &actionList);

    void _buildSensitivityFromEventControl(event_control_t *ect, hif::BList<hif::Value> &sensitivity, bool &allSignals);

    void _buildSensitivityFromAnalogEventControl(
        analog_event_control_t *ect,
        hif::BList<hif::Value> &sensitivity,
        bool &allSignals);

    void _manageModuleItemDeclarations(
        hif::BaseContents *contents_o,
        hif::DesignUnit *du,
        module_or_generate_item_declaration_t *decls);

    hif::Type *_composeAmsType(hif::Type *portType, hif::Type *declarationType);

    hif::Value *_makeValueFromFilter(analog_filter_function_arg_t *arg);
};

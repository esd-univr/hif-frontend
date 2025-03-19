/// @file vhdl_parser.hpp
/// @brief Header file for the VhdlParser class.
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#pragma once

#include <hif/hif.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>

#include "vhdl_parser_struct.hpp"

class VhdlParser;

// Bison generated parser
#include <vhdlParser.hpp>

class VhdlParser
{
public:
    VhdlParser(std::string fileName);
    ~VhdlParser();

    /// @brief For any instance, store the entity/configuration to use.
    typedef std::map<hif::Instance *, binding_indication_t *> component_configuration_map_t;

    /// @brief For any configuration, store the indications for each components.
    typedef std::map<configuration_map_key_t, component_configuration_map_t> configuration_map_t;

    typedef std::map<std::string, std::set<std::string>> LibraryFunctions_t;
    typedef std::map<std::string, std::string> Attributes_t;
    typedef std::set<std::string> VhdlTypes_t;
    typedef std::map<std::string, std::string> OperatorOverloading_t;

    // @brief Enumeration used to set the current parsing context.
    // Parser can be in VHDL or PSL mode, according to the keywords and
    // the statements encountered during the parsing stage.
    enum parser_context_enum_t { VHDL_ctx, PSL_ctx };

    /*
         * Static declarations
         * --------------------------------------------------------------------- */

    // Lists of declarations (entities and libraries)
    static hif::BList<hif::DesignUnit> *du_declarations;
    static hif::BList<hif::LibraryDef> *lib_declarations;
    // Lists of definitions (architectures and packages)
    static std::list<architecture_body_t *> *du_definitions;
    static hif::BList<hif::LibraryDef> *lib_definitions;

    static hif::System *buildSystemObject();

    /*
         * Public functions
         * --------------------------------------------------------------------- */

    /// @brief Parser entry point
    bool parse(bool parseOnly);
    void setCurrentBlockCodeInfo(keyword_data_t keyword);
    void setCurrentBlockCodeInfo(hif::Object *other);
    void setCodeInfo(hif::Object *o);
    void setCodeInfoFromCurrentBlock(hif::Object *o);
    void setContextVhdl();
    void setContextPsl();
    parser_context_enum_t getContext();

    void setGlobalScope(bool s);

    bool isGlobalScope();
    bool isParseOnly();

    bool isPslMixed();

    /// @brief Collects all the libraries used by the design
    void addLibrary(hif::BList<hif::Library> *lib);

    /*
         * VHDL-Grammar related functions
         * --------------------------------------------------------------------- */

    hif::BList<hif::ParameterAssign> *parse_ActualParameterPart(hif::BList<hif::PortAssign> *association_list);

    hif::Alias *
    parse_AliasDeclaration(hif::Identifier *designator, subtype_indication_t *subtype_indication, hif::Value *name);

    hif::Value *parse_Allocator(subtype_indication_t *subtype_indication);
    hif::Value *parse_Allocator(hif::Cast *qualified_expression);

    hif::PortAssign *parse_AssociationElement(hif::Value *formal_part, hif::Value *actual_part);

    hif::PortAssign *parse_AssociationElement(hif::Value *actual_part);

    hif::FunctionCall *parse_AttributeName(hif::Value *prefix, hif::Value *name);

    hif::FunctionCall *parse_AttributeName(hif::Value *prefix, hif::Value *nn, hif::Value *expression);

    architecture_body_t *parse_ArchitectureBody(
        hif::Value *identifier,
        hif::Value *name,
        std::list<block_declarative_item_t *> *architecture_declarative_part,
        std::list<concurrent_statement_t *> *concurrent_statement_list);

    hif::ProcedureCall *parse_Assertion(assert_directive_t *a);

    hif::Aggregate *parse_Aggregate(hif::BList<hif::AggregateAlt> *element_association_list);

    hif::IntValue *parse_BasedLiteral(std::string basedLit);

    hif::BitvectorValue *parse_BitStringLiteral(identifier_data_t bitString);

    block_configuration_t *parse_BlockConfiguration(
        block_specification_t *block_specification,
        hif::BList<hif::Library> *use_clause_list,
        std::list<configuration_item_t *> *configuration_item_list);

    hif::View *parse_BlockStatement(
        hif::Value *identifier,
        hif::Value *guard_expression,
        block_header_t *block_header,
        std::list<block_declarative_item_t *> *block_declarative_item_list,
        std::list<concurrent_statement_t *> *concurrent_statement_list);

    hif::Value *parse_CharacterLiteral(const char *c);

    hif::StateTable *parse_ConcurrentAssertionStatement(hif::ProcedureCall *assertion);

    component_specification_t *parse_ComponentSpecification(instantiation_list_t *instantiation_list, hif::Value *name);

    hif::BList<hif::Assign> *
    parse_ConditionalSignalAssignment(hif::Value *target, hif::BList<hif::Assign> *conditional_waveforms);

    hif::BList<hif::Assign> *parse_ConditionalWaveforms(
        hif::When *conditional_waveforms_when_else_list,
        hif::BList<hif::Assign> *waveform,
        hif::Value *condition);

    hif::When *parse_ConditionalWaveformsWhen(
        hif::When *conditional_waveforms_when_else_list,
        hif::BList<hif::Assign> *waveform,
        hif::Value *condition);

    void parse_ConfigurationDeclaration(
        hif::Value *identifier,
        hif::Value *name,
        block_configuration_t *block_configuration);

    component_configuration_t *parse_ConfigurationSpecification(
        component_specification_t *component_specification,
        binding_indication_t *binding_indication);

    hif::Switch *
    parse_CaseStatement(hif::Value *expression, hif::BList<hif::SwitchAlt> *case_statement_alternative_list);

    hif::SwitchAlt *
    parse_CaseStatementAlternative(hif::BList<hif::Value> *choices, hif::BList<hif::Action> *statements);

    component_configuration_t *parse_ComponentConfiguration(
        component_specification_t *component_specification,
        binding_indication_t *binding_indication_opt,
        block_configuration_t *block_configuration_opt);

    hif::DesignUnit *parse_ComponentDeclaration(
        hif::Value *identifier,
        hif::BList<hif::Declaration> *generic_clause_opt,
        hif::BList<hif::Port> *port_clause_opt);

    hif::Instance *parse_ComponentInstantiationStatement(
        hif::Value *identifier,
        hif::ViewReference *instantiated_unit,
        hif::BList<hif::TPAssign> *generic_map_aspect_opt,
        hif::BList<hif::PortAssign> *port_map_aspect_opt);

    hif::BList<hif::Declaration> *parse_ConstantDeclaration(
        hif::BList<hif::Identifier> *identifier_list,
        subtype_indication_t *subtype_indication,
        hif::Value *expression);

    hif::Array *parse_ConstrainedArrayDefinition(
        hif::BList<hif::Range> *index_constraint,
        subtype_indication_t *subtype_indication);

    hif::Value *parser_DecimalLiteral(char *abstractLit);

    hif::Identifier *parse_Designator(hif::Value *identifier);

    void parse_DesignUnit(std::list<context_item_t *> *context_clause, library_unit_t *library_unit);

    hif::Range *parse_DiscreteRange(subtype_indication_t *subtype_indication);

    hif::Value *parse_DiscreteRange(hif::Value *expression);

    hif::AggregateAlt *parse_ElementAssociation(hif::BList<hif::Value> *choices, hif::Value *expression);

    hif::AggregateAlt *parse_ElementAssociation(hif::Value *expression);

    hif::BList<hif::Field> *
    parse_ElementDeclaration(hif::BList<hif::Identifier> *identifier_list, hif::Type *element_subtype_definition);

    hif::Type *parse_ElementSubtypeDefinition(subtype_indication_t *subtype_indication);

    entity_aspect_t *parse_EntityAspect(hif::Value *name, bool entity, bool configuration);

    hif::DesignUnit *parse_EntityDeclaration(
        hif::Value *identifier,
        hif::View *entity_header,
        std::list<entity_declarative_item_t *> *entity_declarative_part);

    hif::View *parse_EntityHeader(hif::BList<hif::Declaration> *generic_clause, hif::BList<hif::Port> *port_clause);

    hif::EnumValue *parse_EnumerationLiteral(char *characterLit);

    hif::EnumValue *parse_EnumerationLiteral(hif::Value *identifier);

    hif::Break *parse_ExitStatement(hif::Identifier *identifier_colon_opt, hif::Identifier *identifier_opt);

    hif::Value *parse_Expression(hif::Value *left_relation, hif::Value *right_relation, hif::Operator op_type);

    hif::Value *parse_ExpressionAND(hif::Value *left_relation, hif::Value *right_relation);

    hif::Value *parse_ExpressionXNOR(hif::Value *left_relation, hif::Value *right_relation);

    hif::Value *parse_ExpressionNOR(hif::Value *left_relation, hif::Value *right_relation);

    hif::Value *parse_ExpressionNAND(hif::Value *left_relation, hif::Value *right_relation);

    hif::BList<hif::Declaration> *parse_FileDeclaration(
        hif::BList<hif::Identifier> *identifier_list,
        subtype_indication_t *subtype_indication,
        hif::Value *file_open_information);

    hif::Value *parse_FileOpenInformation(hif::Value *expression, hif::Value *file_logical_name);

    hif::File *parse_FileTypeDefinition(hif::Value *name);

    hif::Type *parse_FloatingOrIntegerTypeDefinition(hif::Range *range_constraint);

    hif::TypeDef *parse_FullTypeDeclaration(hif::Value *id, hif::Type *type_definition);

    hif::Value *parse_FunctionCall(hif::Value *name, hif::BList<hif::PortAssign> *passign_list);

    hif::Value *parse_SequenceInstance(hif::Value *name, hif::BList<hif::Value> *passign_list);

    hif::Generate *parse_GenerateStatement(
        hif::Value *identifier,
        hif::Generate *generation_scheme,
        std::list<block_declarative_item_t *> *block_declarative_item_list,
        std::list<concurrent_statement_t *> *concurrent_statement_list);

    hif::Generate *parse_GenerationScheme(hif::For *for_scheme);

    hif::Generate *parse_GenerationScheme(hif::Value *contdition);

    hif::BList<hif::Declaration> *parse_GenericClause(std::list<interface_declaration_t *> *generic_list);

    hif::BList<hif::TPAssign> *parse_GenericMapAspect(hif::BList<hif::PortAssign> *association_list);

    hif::ViewReference *parse_HdlModuleName(hif::Value *entityIdentifier, hif::Value *viewIdentifier = nullptr);

    hif::Type *parse_HdlVariableType(subtype_indication_t *subtype_indication);

    hif::Value *parse_Identifier(char *identifier);

    hif::Range *parse_IndexConstraint(hif::Value *discrete_range);

    hif::Action *parse_IfStatement(
        hif::Value *condition,
        hif::BList<hif::Action> *sequence_of_statements_then,
        hif::BList<hif::IfAlt> *if_statement_elseif_list,
        hif::BList<hif::Action> *sequence_of_statements_other);

    hif::Action *parse_IfStatement(
        hif::Value *condition,
        hif::BList<hif::Action> *sequence_of_statements,
        hif::BList<hif::IfAlt> *if_statement_elseif_list);

    hif::Action *parse_IfStatement(
        hif::Value *condition,
        hif::BList<hif::Action> *sequance_of_statements_then,
        hif::BList<hif::Action> *sequance_of_statements_else);

    hif::Action *parse_IfStatement(hif::Value *condition, hif::BList<hif::Action> *sequence_of_statements);

    hif::BList<hif::IfAlt> *
    parse_IfStatementElseifList(hif::Value *condition, hif::BList<hif::Action> *sequence_of_statements);

    hif::BList<hif::IfAlt> *parse_IfStatementElseifList(
        hif::BList<hif::IfAlt> *if_statement_elseif_list,
        hif::Value *condition,
        hif::BList<hif::Action> *sequence_of_statements);

    hif::Range *parse_IndexSubtypeDefinition(hif::Value *name);

    hif::ViewReference *parse_InstantiatedUnit(bool component, bool entity, hif::Value *name);

    instantiation_list_t *parse_InstantiationList(hif::BList<hif::Identifier> *identifier_list);

    instantiation_list_t *parse_InstantiationList(bool all);

    hif::BList<hif::Port> *parse_InterfaceConstantDeclaration(
        hif::BList<hif::Identifier> *identifier_list,
        bool in_opt,
        subtype_indication_t *subtype_indication,
        hif::Value *expression);

    hif::BList<hif::Port> *parse_InterfaceDeclaration(
        hif::BList<hif::Identifier> *identifier_list,
        hif::PortDirection mode_opt,
        subtype_indication_t *subtype_indication);

    hif::BList<hif::Port> *parse_InterfaceSignalDeclaration(
        hif::BList<hif::Identifier> *identifier_list,
        hif::PortDirection mode_opt,
        subtype_indication_t *subtype_indication,
        hif::Value *expression);

    hif::BList<hif::Port> *parse_InterfaceVariableDeclaration(
        hif::BList<hif::Identifier> *identifier_list,
        hif::PortDirection mode_opt,
        subtype_indication_t *subtype_indication,
        hif::Value *expression);

    hif::Action *parse_IterationScheme(hif::Value *condition);

    hif::Action *parse_IterationScheme(hif::BList<hif::Value> *parameter_specification);

    hif::BList<hif::Library> *parse_LibraryClause(hif::BList<hif::Identifier> *logical_name_list);

    hif::Value *parse_Literal_null();

    hif::Action *parse_LoopStatement(
        hif::Identifier *identifier_colon_opt,
        hif::Action *iteration_scheme_opt,
        hif::BList<hif::Action> *sequence_of_statements);

    hif::Action *parse_NextStatement(hif::Identifier *identifier_opt, hif::Value *condition);

    hif::Action *parse_NextStatement(hif::Identifier *identifier_opt);

    hif::Value *parse_NumericLiteral(hif::Value *num, hif::Value *unit);

    hif::LibraryDef *parse_PackageBody(hif::Value *id, hif::BList<hif::Declaration> *package_body_declarative_part);

    hif::LibraryDef *parse_PackageDeclaration(hif::Value *id, hif::BList<hif::Declaration> *package_declarative_part);

    hif::For *parse_ParameterSpecification(hif::Value *id, hif::Range *discrete_range);

    hif::BList<hif::Port> *parse_PortList(std::list<interface_declaration_t *> *interface_list);

    hif::Value *parse_Primary(hif::Aggregate *aggregate);

    hif::ProcedureCall *parse_ProcedureCall(hif::Value *name);

    hif::StateTable *parse_ProcessStatement(
        hif::Identifier *identifier_colon_opt,
        hif::BList<hif::Value> *sensitivity_list_paren_opt,
        hif::BList<hif::Declaration> *process_declarative_part,
        hif::BList<hif::Action> *process_statement_part,
        hif::Identifier *identifier_opt);

    hif::Cast *parse_QualifiedExpression(hif::Value *name, hif::Aggregate *aggregate);

    hif::Range *parse_Range(hif::Value *attribute_name);

    hif::Range *
    parse_Range(hif::Value *simple_expression_left, hif::RangeDirection direction, hif::Value *simple_espression_right);

    hif::Record *parse_RecordTypeDefinition(hif::BList<hif::Field> *element_declaration_list);

    hif::Return *parse_ReturnStatement(hif::Value *expression_opt);

    hif::Type *parse_ScalarTypeDefinition(hif::BList<hif::EnumValue> *enumeration_type_definition);

    hif::FieldReference *parse_SelectedName(hif::Value *name, hif::Value *suffix);

    hif::BList<hif::Assign> *parse_SignalAssignmentStatement(
        hif::Identifier *identifier_colon_opt,
        hif::Value *target,
        hif::BList<hif::Assign> *waveform);

    hif::Assign *parse_SelectedSignalAssignment(
        hif::Value *expression,
        hif::Value *target,
        hif::BList<hif::WithAlt> *selected_waveforms);

    hif::WithAlt *parse_SelectedWaveformsWhen(hif::BList<hif::Assign> *waveform, hif::BList<hif::Value> *choices);

    hif::BList<hif::Declaration> *parse_SignalDeclaration(
        hif::BList<hif::Identifier> *identifier_list,
        subtype_indication_t *subtype_indication,
        hif::Value *expression);

    hif::BList<hif::Declaration> *
    parse_SignalDeclaration(hif::BList<hif::Identifier> *identifier_list, subtype_indication_t *subtype_indication);

    hif::Value *parse_StringLiteral(identifier_data_t stringLit);

    hif::Declaration *parse_SubprogramBody(
        hif::SubProgram *subprogram_specification,
        hif::BList<hif::Declaration> *subprogram_declarative_part,
        hif::BList<hif::Action> *suprogram_statement_part);

    hif::SubProgram *parse_SubprogramSpecification(
        hif::Identifier *designator,
        std::list<interface_declaration_t *> *formal_parameter_list_paren_opt);

    hif::SubProgram *parse_SubprogramSpecification(
        hif::Identifier *designator,
        std::list<interface_declaration_t *> *formal_parameter_list_paren_opt,
        hif::Value *name);

    hif::TypeDef *parse_SubtypeDeclaration(hif::Value *id, subtype_indication_t *subtype_indication);

    subtype_indication_t *parse_SubtypeIndication(hif::Value *name, constraint_t *constraint_opt);

    hif::Array *parse_UnconstrainedArrayDefinition(
        hif::BList<hif::Range> *index_subtype_definition_list,
        subtype_indication_t *subtype_indication);

    hif::BList<hif::Library> *parse_UseClause(hif::BList<hif::FieldReference> *selected_name_list);

    hif::Assign *parse_VariableAssignmentStatement(hif::Value *targer, hif::Value *expression);

    hif::BList<hif::Declaration> *parse_VariableDeclaration(
        hif::BList<hif::Identifier> *identifier_list,
        subtype_indication_t *subtype_indication,
        hif::Value *expression);

    hif::BList<hif::Declaration> *
    parse_VariableDeclaration(hif::BList<hif::Identifier> *identifier_list, subtype_indication_t *subtype_indication);

    hif::Wait *parse_WaitStatement(
        hif::BList<hif::Value> *sensitivity_clause_opt,
        hif::Value *condition_clause_opt,
        hif::Value *timeout_clause_opt);

    hif::Assign *parse_WaveformElement(hif::Value *expression, hif::Value *afterExpression);

    static hif::Type *resolveType(
        std::string type_ref,
        hif::BList<hif::Value> *opt_arg,
        hif::Range *ro,
        hif::semantics::ILanguageSemantics *sem,
        bool mandatory);

    /*
         * PSL-Grammar related functions
         * --------------------------------------------------------------------- */

    hif::Range *parse_RangePsl(hif::Value *lower, hif::Value *upper);

    assert_directive_t *parse_AssertDirective(hif::Value *property, hif::Value *report, hif::Value *severity);

    hif::Value *parse_FLProperty(hif::Value *hdl_or_psl_expression);

    hif::Value *parse_FLProperty(hif::Value *fl_property, hif::Value *clock_expression);

    hif::Value *parse_FLProperty(hif::Operator bool_op, hif::Value *property1, hif::Value *property2);

    hif::Value *parse_FLProperty(const char *op, hif::Value *fl_property);

    hif::Value *parse_FLPropertyCycles(const char *op, hif::Value *fl_property, hif::Value *cycles);

    hif::Value *parse_FLPropertyRange(const char *op, hif::Value *fl_property, hif::Range *range);

    hif::Value *parse_FLPropertyOccurrence(const char *op, hif::Value *fl_property, hif::Value *occurrence);

    hif::Value *parse_FLPropertyOccurrenceCycles(
        const char *op,
        hif::Value *fl_property,
        hif::Value *occurrence,
        hif::Value *cycles);

    hif::Value *
    parse_FLPropertyOccurrenceRange(const char *op, hif::Value *fl_property, hif::Value *occurrence, hif::Range *range);

    hif::Value *parse_FLProperty(const char *op, hif::Value *fl_property1, hif::Value *fl_property2);

    hif::ProcedureCall *parse_PslDirective(verification_directive_t *v);

    hif::DesignUnit *parse_VerificationUnit(hif::Value *name, std::list<vunit_item_t *> *items);

    void parse_VerificationUnit(hif::Value *name, hif::ViewReference *context_spec, std::list<vunit_item_t *> *items);

private:
    VhdlParser(const VhdlParser &);
    VhdlParser &operator=(const VhdlParser &);

    // Current parsing file
    std::string _fileName;
    bool _parseOnly;
    // The current parsing context
    parser_context_enum_t _parserContext;
    // Flag set to True if we are in VHDL global scope, and set to False
    // if we are inside a VHDL global statement (architecture, entity ...)
    bool _globalScope;
    // Flag set to True if the vhdl module containts psl verification units
    bool _pslMixed;

    unsigned int _tmpCustomLineNumber;
    unsigned int _tmpCustomColumnNumber;

    hif::semantics::ILanguageSemantics *_sem;
    hif::HifFactory _factory;

    hif::NameTable *_name_table;
    hif::BList<hif::Library> *_library_list;

    LibraryFunctions_t _library_function;
    VhdlTypes_t *_is_vhdl_type;
    OperatorOverloading_t *_is_operator_overloading;

    configuration_map_t _configurationMap;

    void _initStandardLibraries();

    /// @brief Populates Contents object starting from the list of concurrent statements.
    /// The function also cleans the concurrent statements list.
    ///
    /// @param contents_o   the Contents object to be populated
    /// @param concurrent_statement_list    the list of concurrent statements
    ///
    void _populateContents(hif::Contents *contents_o, std::list<concurrent_statement_t *> *concurrent_statement_list);

    void _populateConfigurationMap(configuration_item_t *configuration_item, component_configuration_map_t *config_map);

    /// Returns hif representation of a VHDL type
    ///
    /// @param type_ref String representation of vhdl type.
    /// @param opt_arg List of constraints.
    /// @param ro The range.
    /// @return Type representation of vhdl type.
    ///
    hif::Type *_resolveType(std::string type_ref, hif::BList<hif::Value> *opt_arg = nullptr, hif::Range *ro = nullptr);

    /// @brief Given a field reference, translates it as a typereference
    /// with nested libraries.
    ///
    /// @param fr The fieldreference.
    /// @return The matched type.
    ///
    hif::ReferencedType *_resolveFieldReferenceType(hif::FieldReference *fr);

    /// @brief Given a component, fix all instances with default values.
    void _fixIntancesWithComponent(hif::Contents *contents, hif::View *component);
};

/// @file parser_struct.hpp
/// @brief Contains the definition of the Verilog parser structure.
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#pragma once

#include <hif/hif.hpp>
#include <list>

struct module_or_generate_item_declaration_t;
struct module_or_generate_item_t;
struct non_port_module_item_t;
struct module_item_t;
struct list_of_port_connections_t;
struct list_of_parameter_assignment_t;
struct block_item_declaration_t;
struct function_item_declaration_t;
struct task_item_declaration_t;
struct architecture_body_t;
struct statement_t;
struct analog_statement_t;
struct event_control_t;
struct event_expression_t;
struct procedural_timing_control_t;
struct net_ams_decl_identifier_assignment_t;
struct discipline_and_modifiers_t;
struct discipline_identifier_signed_range_t;
struct analog_filter_function_arg_t;
struct specify_item_t;
struct specify_terminal_descriptor_t;
struct timing_check_event_control_t;

/// @brief Data about a identifier.
typedef struct {
    int line;   ///< The source code line number.
    int column; ///< The source code column number.
    int len;    ///< The length of the token.
    char *name; ///< The name of the token.
} identifier_data_t;

/// @brief Data about a keyword.
typedef struct {
    int line;   ///< The source code line number.
    int column; ///< The source code column number.
} keyword_data_t;

/// @brief Data about a number.
typedef struct {
    int bits;    ///< The number of bits.
    char *value; ///< The value of the number.
    char type;   ///< The type of the number.
    bool sign;   ///< The sign of the number.
} number_t;

/// @brief Data about a real number.
typedef struct {
    bool e;      ///< The presence of the exponent.
    char *value; ///< The value of the number.
    char *exp;   ///< The exponent of the number.
} real_number_t;

/// @brief Data about generate procedures.
struct module_or_generate_item_declaration_t {
    /// @brief Constructor.
    module_or_generate_item_declaration_t();

    /// @brief Destructor.
    ~module_or_generate_item_declaration_t();

    /// @brief Copy constructor.
    /// @param o The object to copy.
    module_or_generate_item_declaration_t(const module_or_generate_item_declaration_t &o);

    /// @brief Assignment operator.
    /// @param o The object to copy.
    /// @return Reference to the current object.
    module_or_generate_item_declaration_t &operator=(const module_or_generate_item_declaration_t &o);

    hif::BList<hif::Declaration> *net_declaration;      ///< List of net declarations.
    hif::BList<hif::Declaration> *reg_declaration;      ///< List of register declarations.
    hif::BList<hif::Declaration> *integer_declaration;  ///< List of integer declarations.
    hif::BList<hif::Declaration> *real_declaration;     ///< List of real declarations.
    hif::BList<hif::Declaration> *time_declaration;     ///< List of time declarations.
    hif::BList<hif::Declaration> *realtime_declaration; ///< List of realtime declarations.
    hif::BList<hif::Declaration> *event_declaration;    ///< List of event declarations.
    hif::BList<hif::Declaration> *genvar_declaration;   ///< List of genvar declarations.
    hif::BList<hif::Declaration> *branch_declaration;   ///< List of branch declarations.
    hif::Procedure *task_declaration;                   ///< Task declaration.
    hif::Function *function_declaration;                ///< Function declaration.
    hif::BList<hif::Generate> *generate_declaration;    ///< List of generate declarations.
};

/// @brief Data about a module or generate item.
struct module_or_generate_item_t {
    /// @brief Constructor.
    module_or_generate_item_t();

    /// @brief Destructor.
    ~module_or_generate_item_t();

    /// @brief Copy constructor.
    /// @param o The object to copy.
    module_or_generate_item_t(const module_or_generate_item_t &o);

    /// @brief Assignment operator.
    /// @param o The object to copy.
    /// @return Reference to the current object.
    module_or_generate_item_t &operator=(const module_or_generate_item_t &o);

    hif::Contents *initial_construct;                    ///< Pointer to the initial construct.
    hif::BList<hif::Const> *local_parameter_declaration; ///< List of local parameter declarations.
    hif::StateTable *always_construct;                   ///< Pointer to the always construct.
    hif::StateTable *analog_construct;                   ///< Pointer to the analog construct.
    hif::BList<hif::Assign> *continuous_assign;          ///< List of continuous assignments.
    hif::BList<hif::Instance> *module_instantiation;     ///< List of module instantiations.

    /// @brief The module item.
    module_or_generate_item_declaration_t *module_or_generate_item_declaration;
};

/// @brief Data about a non-port module item.
/// @todo Add: generate_region, specparam_declaration.
struct non_port_module_item_t {

    hif::BList<hif::ValueTP> *parameter_declaration; ///< List of parameter declarations.
    std::list<specify_item_t *> *specify_block;      ///< List of specify blocks.

    module_or_generate_item_t *module_or_generate_item; ///< Pointer to the module or generate item.
};

/// @brief Data about a module item.
struct module_item_t {
    hif::BList<hif::Port> *port_declaration_identifiers; ///< List of port declaration identifiers.

    non_port_module_item_t *non_port_module_item; ///< Pointer to the non-port module item.
};

/// @brief Data about a list of port connections.
struct list_of_port_connections_t {
    hif::BList<hif::Value> *ordered_port_connection_list;    ///< List of ordered port connections.
    hif::BList<hif::PortAssign> *named_port_connection_list; ///< List of named port connections.
};

/// @brief Data about a list of parameter assignments.
struct list_of_parameter_assignment_t {
    hif::BList<hif::Value> *ordered_parameter_assignment_list;       ///< List of ordered parameter assignments.
    hif::BList<hif::ValueTPAssign> *named_parameter_assignment_list; ///< List of named parameter assignments.
};

/// @brief Data about a block item declaration.
/// @todo Add: parameter_declaration, event_declaration.
struct block_item_declaration_t {
    hif::BList<hif::Const> *local_parameter_declaration;   ///< List of local parameter declarations.
    hif::BList<hif::Signal> *variable_declaration;         ///< List of variable declarations.
    hif::BList<hif::Signal> *reg_variable_declaration;     ///< List of reg variable declarations.
    hif::BList<hif::Signal> *integer_variable_declaration; ///< List of integer variable declarations.

    /// @brief Get the first object.
    /// @return a pointer to the first object.
    hif::Object *getFirstObject();
};

struct analog_block_item_declaration_t {
    hif::BList<hif::Declaration> *real_declaration;
    hif::BList<hif::Declaration> *integer_declaration;
    hif::BList<hif::ValueTP> *parameter_declaration_identifiers;
};

struct function_item_declaration_t {
    block_item_declaration_t *block_item_declaration;
    hif::BList<hif::Port> *tf_input_declaration;
};

struct task_item_declaration_t {
    hif::BList<hif::Port> *tf_declaration;
    block_item_declaration_t *block_item_declaration;
};

struct architecture_body_t {
    ~architecture_body_t()
    {
        delete entity_name;
        // do not delete 'contents'
    }

    hif::Contents *contents;
    hif::Identifier *entity_name;
};

struct event_control_t {
    event_control_t();

    virtual ~event_control_t();

    event_control_t(const event_control_t &o);

    event_control_t &operator=(const event_control_t &o);

    hif::Value *event_identifier;
    std::list<event_expression_t *> *event_expression_list;
    bool event_all;
    // TODO: * , (*)

    hif::Object *getFirstObject();
};

struct event_expression_t {
    event_expression_t();

    event_expression_t(const event_expression_t &e);

    event_expression_t &operator=(const event_expression_t &e);

    virtual ~event_expression_t();

    hif::Value *expression;
    hif::Value *posedgeExpression;
    hif::Value *negedgeExpression;

    hif::Object *getFirstObject();
};

struct analog_event_expression_t : public event_expression_t {
    analog_event_expression_t();

    analog_event_expression_t(const analog_event_expression_t &a);

    analog_event_expression_t &operator=(const analog_event_expression_t &a);

    virtual ~analog_event_expression_t();

    std::list<std::string> *analysis_identifier_list;
    hif::BList<hif::Value> *or_analog_event_expression;
    // analog_event_functions
    // analog_event_expression_1
    // analog_event_expression_2

    hif::Object *getFirstObject();
};

struct analog_event_control_t : public event_control_t {
    analog_event_control_t();
    virtual ~analog_event_control_t();

    analog_event_control_t(const analog_event_control_t &o);

    analog_event_control_t &operator=(const analog_event_control_t &o);

    analog_event_expression_t *analog_event_expression;

    hif::Object *getFirstObject();
};

struct statement_t {
    statement_t();
    statement_t(const statement_t &e);
    statement_t &operator=(const statement_t &e);

    // ** Mutually exclusive fields **

    hif::Assign *blocking_assignment;
    hif::Switch *case_statement;
    hif::If *conditional_statement;
    hif::Break *disable_statement;
    hif::ValueStatement *event_trigger;
    hif::Action *loop_statement;
    hif::Assign *nonblocking_assignment;
    // par_block;
    // procedural_continuous_assignments;
    std::list<statement_t *> *seq_block_actions;
    hif::ProcedureCall *system_task_enable;
    hif::ProcedureCall *task_enable;
    // task_enable;
    hif::Wait *wait_statement;
    bool skipped;
    // TODO: sensitivity list

    // ** No mutually exclusive fields **

    // Timing control informantions
    procedural_timing_control_t *procedural_timing_control;
    // The name (if any) of the block that contains this statement
    std::string blockName;
    hif::BList<hif::Declaration> *seq_block_declarations;

    hif::Object *getFirstObject();
};

struct analog_statement_t {
    analog_statement_t();
    analog_statement_t(const analog_statement_t &e);
    analog_statement_t &operator=(const analog_statement_t &e);

    // ** Mutually exclusive fields **

    hif::Action *analog_loop_statement;
    hif::Switch *analog_case_statement;
    hif::If *analog_conditional_statement;
    hif::Assign *analog_procedural_assignment;
    std::list<analog_statement_t *> *analog_seq_block_actions;
    hif::ProcedureCall *system_task_enable;
    hif::ProcedureCall *contribution_statement;
    hif::ProcedureCall *indirect_contribution_statement;
    // disable_statement
    bool skipped;

    // ** No mutually exclusive fields **

    // The name (if any) of the block that contains this statement
    std::string blockName;
    hif::BList<hif::Declaration> *analog_seq_block_declarations;
    analog_event_control_t *event_control;
};

struct procedural_timing_control_t {
    procedural_timing_control_t()
        : delay_control(nullptr)
        , event_control(nullptr)
    {
    }

    hif::Value *delay_control;
    event_control_t *event_control;

    hif::Object *getFirstObject();
};

struct net_ams_decl_identifier_assignment_t {
    net_ams_decl_identifier_assignment_t()
        : identifier(nullptr)
        , init_expression(nullptr)
        , dimension_list(nullptr)
    {
    }

    hif::Value *identifier;

    // mutually exclusive fields combined with the first one
    hif::Value *init_expression;
    hif::BList<hif::Range> *dimension_list;
};

struct discipline_and_modifiers_t {
    discipline_and_modifiers_t()
        : discipline_identifier(nullptr)
        , net_type(-1)
        , k_signed(false)
        , k_wreal(false)
        , range(nullptr)
    {
    }

    discipline_and_modifiers_t(const discipline_and_modifiers_t &a)
        : discipline_identifier(hif::copy(a.discipline_identifier))
        , net_type(a.net_type)
        , k_signed(a.k_signed)
        , k_wreal(a.k_wreal)
        , range(hif::copy(a.range))
    {
    }

    hif::Identifier *discipline_identifier;
    int net_type;
    bool k_signed;
    bool k_wreal;
    hif::Range *range;
};

struct discipline_identifier_signed_range_t {
    discipline_identifier_signed_range_t()
        : discipline_identifier(nullptr)
        , k_signed(false)
        , range(nullptr)
    {
    }

    discipline_identifier_signed_range_t(const discipline_identifier_signed_range_t &a)
        : discipline_identifier(hif::copy(a.discipline_identifier))
        , k_signed(a.k_signed)
        , range(hif::copy(a.range))
    {
    }

    hif::Identifier *discipline_identifier;
    bool k_signed;
    hif::Range *range;
};

struct module_instance_and_net_ams_decl_identifier_assignment_t {
    module_instance_and_net_ams_decl_identifier_assignment_t()
        : net_ams_decl_identifier_assignment_list(nullptr)
        , name_of_module_instance(nullptr)
        , ams_created_variables(nullptr)
    {
    }

    module_instance_and_net_ams_decl_identifier_assignment_t(
        const module_instance_and_net_ams_decl_identifier_assignment_t &a)
        : net_ams_decl_identifier_assignment_list(a.net_ams_decl_identifier_assignment_list)
        , name_of_module_instance(a.name_of_module_instance)
        , ams_created_variables(a.ams_created_variables)
    {
    }

    std::list<net_ams_decl_identifier_assignment_t *> *net_ams_decl_identifier_assignment_list;
    hif::Instance *name_of_module_instance;

    hif::BList<hif::Signal> *ams_created_variables;
};

struct analog_function_item_declaration_t {
    analog_function_item_declaration_t();
    analog_function_item_declaration_t(const analog_function_item_declaration_t &a);
    ~analog_function_item_declaration_t();
    hif::BList<hif::Port> input_declaration_identifiers;
    hif::BList<hif::Port> output_declaration_identifiers;
    hif::BList<hif::Port> inout_declaration_identifiers;
    hif::BList<hif::Declaration> analog_block_item_declaration;
};

struct analog_filter_function_arg_t {
    hif::Identifier *identifier;
    // identifier [ expr : expr ]
    hif::BList<hif::Value> *constant_optional_arrayinit;
};

struct specify_item_t {
    // specparam_declaration
    // pulsestyle_declaration
    // showcancelled_declaration
    hif::ProcedureCall *system_timing_check;
};

struct specify_terminal_descriptor_t {
    hif::Identifier *identifier;
    hif::Value *range_expression;
};

struct timing_check_event_control_t {
    bool pos_edge;
    bool neg_edge;
    // TODO
    // edge_control_specifier
};

struct generate_block_t {
    generate_block_t();
    ~generate_block_t();
    generate_block_t(const generate_block_t &a);
    generate_block_t &operator=(const generate_block_t &a);
    std::string generate_block_identifier_opt;
    std::list<module_or_generate_item_t *> *module_or_generate_item_list;
};
/// @file verilog_parser_struct.cpp
/// @brief
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#include "verilog2hif/parser_struct.hpp"

statement_t::statement_t()
    : blocking_assignment(nullptr)
    , case_statement(nullptr)
    , conditional_statement(nullptr)
    , disable_statement(nullptr)
    , event_trigger(nullptr)
    , loop_statement(nullptr)
    , nonblocking_assignment(nullptr)
    , seq_block_actions(nullptr)
    , system_task_enable(nullptr)
    , task_enable(nullptr)
    , wait_statement(nullptr)
    , skipped(false)
    , procedural_timing_control(nullptr)
    , seq_block_declarations(nullptr)
{
    // ntd
}

statement_t::statement_t(const statement_t &e)
    : blocking_assignment(e.blocking_assignment)
    , case_statement(e.case_statement)
    , conditional_statement(e.conditional_statement)
    , disable_statement(e.disable_statement)
    , event_trigger(e.event_trigger)
    , loop_statement(e.loop_statement)
    , nonblocking_assignment(e.nonblocking_assignment)
    , seq_block_actions(e.seq_block_actions)
    , system_task_enable(e.system_task_enable)
    , task_enable(e.task_enable)
    , wait_statement(e.wait_statement)
    , skipped(e.skipped)
    , procedural_timing_control(e.procedural_timing_control)
    , blockName(e.blockName)
    , seq_block_declarations(e.seq_block_declarations)
{
    // ntd
}

auto statement_t::operator=(const statement_t &e) -> statement_t &
{
    if (this == &e) {
        return *this;
    }

    blocking_assignment       = e.blocking_assignment;
    case_statement            = e.case_statement;
    conditional_statement     = e.conditional_statement;
    disable_statement         = e.disable_statement;
    event_trigger             = e.event_trigger;
    loop_statement            = e.loop_statement;
    nonblocking_assignment    = e.nonblocking_assignment;
    seq_block_actions         = e.seq_block_actions;
    system_task_enable        = e.system_task_enable;
    task_enable               = e.task_enable;
    wait_statement            = e.wait_statement;
    skipped                   = e.skipped;
    procedural_timing_control = e.procedural_timing_control;
    blockName                 = e.blockName;
    seq_block_declarations    = e.seq_block_declarations;

    return *this;
}

auto statement_t::getFirstObject() const -> hif::Object *
{
    if (blocking_assignment != nullptr) {
        return blocking_assignment;
    }
    if (case_statement != nullptr) {
        return case_statement;
    }
    if (conditional_statement != nullptr) {
        return conditional_statement;
    }
    if (disable_statement != nullptr) {
        return disable_statement;
    }
    if (event_trigger != nullptr) {
        return event_trigger;
    }
    if (loop_statement != nullptr) {
        return loop_statement;
    }
    if (nonblocking_assignment != nullptr) {
        return nonblocking_assignment;
    }
    if (system_task_enable != nullptr) {
        return system_task_enable;
    }
    if (task_enable != nullptr) {
        return task_enable;
    }
    if (wait_statement != nullptr) {
        return wait_statement;
    }

    if (seq_block_actions != nullptr) {
        for (auto *c : *seq_block_actions) {
            if (c != nullptr && c->getFirstObject() != nullptr) {
                return c->getFirstObject();
            }
        }
    }

    if (procedural_timing_control != nullptr && procedural_timing_control->getFirstObject() != nullptr) {
        return procedural_timing_control->getFirstObject();
    }
    if (seq_block_declarations != nullptr && !seq_block_declarations->empty()) {
        return seq_block_declarations->front();
    }

    return nullptr;
}

analog_statement_t::analog_statement_t()
    : analog_loop_statement(nullptr)
    , analog_case_statement(nullptr)
    , analog_conditional_statement(nullptr)
    , analog_procedural_assignment(nullptr)
    , analog_seq_block_actions(nullptr)
    , system_task_enable(nullptr)
    , contribution_statement(nullptr)
    , indirect_contribution_statement(nullptr)
    , skipped(false)
    , analog_seq_block_declarations(nullptr)
    , event_control(nullptr)
{
}

analog_statement_t::analog_statement_t(const analog_statement_t &e)

    = default;

auto analog_statement_t::operator=(const analog_statement_t &e) -> analog_statement_t &
{
    if (this == &e) {
        return *this;
    }

    analog_loop_statement           = e.analog_loop_statement;
    analog_case_statement           = e.analog_case_statement;
    analog_conditional_statement    = e.analog_conditional_statement;
    analog_procedural_assignment    = e.analog_procedural_assignment;
    analog_seq_block_actions        = e.analog_seq_block_actions;
    system_task_enable              = e.system_task_enable;
    contribution_statement          = e.contribution_statement;
    indirect_contribution_statement = e.indirect_contribution_statement;
    skipped                         = e.skipped;
    blockName                       = e.blockName;
    analog_seq_block_declarations   = e.analog_seq_block_declarations;
    event_control                   = e.event_control;

    return *this;
}

analog_function_item_declaration_t::analog_function_item_declaration_t()
    : input_declaration_identifiers()
    , output_declaration_identifiers()
    , inout_declaration_identifiers()
    , analog_block_item_declaration()
{
}

analog_function_item_declaration_t::analog_function_item_declaration_t(const analog_function_item_declaration_t &a)
    : input_declaration_identifiers()
    , output_declaration_identifiers()
    , inout_declaration_identifiers()
    , analog_block_item_declaration()
{
    hif::copy(a.input_declaration_identifiers, input_declaration_identifiers);
    hif::copy(a.output_declaration_identifiers, output_declaration_identifiers);
    hif::copy(a.inout_declaration_identifiers, inout_declaration_identifiers);
    hif::copy(a.analog_block_item_declaration, analog_block_item_declaration);
}

analog_function_item_declaration_t::~analog_function_item_declaration_t() = default;

auto block_item_declaration_t::getFirstObject() const -> hif::Object *
{
    if (local_parameter_declaration != nullptr && !local_parameter_declaration->empty()) {
        return local_parameter_declaration->front();
    }
    if (variable_declaration != nullptr && !variable_declaration->empty()) {
        return variable_declaration->front();
    }
    if (reg_variable_declaration != nullptr && !reg_variable_declaration->empty()) {
        return reg_variable_declaration->front();
    }
    if (integer_variable_declaration != nullptr && !integer_variable_declaration->empty()) {
        return integer_variable_declaration->front();
    }

    return nullptr;
}

auto procedural_timing_control_t::getFirstObject() const -> hif::Object *
{
    if (delay_control != nullptr) {
        return delay_control;
    }
    if (event_control != nullptr && event_control->getFirstObject() != nullptr) {
        return event_control->getFirstObject();
    }

    return nullptr;
}

event_control_t::event_control_t()
    : event_identifier(nullptr)
    , event_expression_list(nullptr)
    , event_all(false)
{
}

event_control_t::~event_control_t() = default;

event_control_t::event_control_t(const event_control_t &o)

    = default;

auto event_control_t::operator=(const event_control_t &o) -> event_control_t &
{
    if (this == &o) {
        return *this;
    }

    event_identifier      = o.event_identifier;
    event_expression_list = o.event_expression_list;
    event_all             = o.event_all;

    return *this;
}

auto event_control_t::getFirstObject() const -> hif::Object *
{
    if (event_identifier != nullptr) {
        return event_identifier;
    }

    if (event_expression_list != nullptr) {
        for (auto &i : *event_expression_list) {
            if (i != nullptr && i->getFirstObject() != nullptr) {
                return i->getFirstObject();
            }
        }
    }

    return nullptr;
}

event_expression_t::event_expression_t()
    : expression(nullptr)
    , posedgeExpression(nullptr)
    , negedgeExpression(nullptr)
{
}

event_expression_t::event_expression_t(const event_expression_t &e)

    = default;

auto event_expression_t::operator=(const event_expression_t &e) -> event_expression_t &
{
    if (this == &e) {
        return *this;
    }
    expression        = e.expression;
    posedgeExpression = e.posedgeExpression;
    negedgeExpression = e.negedgeExpression;

    return *this;
}

event_expression_t::~event_expression_t() = default;

auto event_expression_t::getFirstObject() const -> hif::Object *
{
    if (expression != nullptr) {
        return expression;
    }
    if (posedgeExpression != nullptr) {
        return posedgeExpression;
    }
    if (negedgeExpression != nullptr) {
        return negedgeExpression;
    }

    return nullptr;
}

analog_event_expression_t::analog_event_expression_t()
    : event_expression_t()
    , analysis_identifier_list(nullptr)
    , or_analog_event_expression(nullptr)
{
}

analog_event_expression_t::analog_event_expression_t(const analog_event_expression_t &a)

    = default;

auto analog_event_expression_t::operator=(const analog_event_expression_t &a) -> analog_event_expression_t &
{
    if (this == &a) {
        return *this;
    }

    event_expression_t::operator=(a);
    analysis_identifier_list   = a.analysis_identifier_list;
    or_analog_event_expression = a.or_analog_event_expression;

    return *this;
}

analog_event_expression_t::~analog_event_expression_t() = default;

auto analog_event_expression_t::getFirstObject() const -> hif::Object *
{
    if (or_analog_event_expression != nullptr && !or_analog_event_expression->empty()) {
        return or_analog_event_expression->front();
    }
    if (analysis_identifier_list != nullptr) {
        messageError("Unsupported case", nullptr, nullptr);
    }

    return event_expression_t::getFirstObject();
}

generate_block_t::generate_block_t()
    : generate_block_identifier_opt()
    , module_or_generate_item_list(nullptr)
{
    // ntd
}

generate_block_t::~generate_block_t()
{
    // ntd
}

generate_block_t::generate_block_t(const generate_block_t &a)
    : generate_block_identifier_opt(a.generate_block_identifier_opt)
    , module_or_generate_item_list(a.module_or_generate_item_list)
{
    // ntd
}

auto generate_block_t::operator=(const generate_block_t &a) -> generate_block_t &
{
    if (this == &a) {
        return *this;
    }
    generate_block_identifier_opt = a.generate_block_identifier_opt;
    module_or_generate_item_list  = a.module_or_generate_item_list;
    return *this;
}

module_or_generate_item_t::module_or_generate_item_t()
    : initial_construct(nullptr)
    , local_parameter_declaration(nullptr)
    , always_construct(nullptr)
    , analog_construct(nullptr)
    , continuous_assign(nullptr)
    , module_instantiation(nullptr)
    , module_or_generate_item_declaration(nullptr)
{
    // ntd
}

module_or_generate_item_t::~module_or_generate_item_t()
{
    // ntd
}

module_or_generate_item_t::module_or_generate_item_t(const module_or_generate_item_t &o)
    : initial_construct(o.initial_construct)
    , local_parameter_declaration(o.local_parameter_declaration)
    , always_construct(o.always_construct)
    , analog_construct(o.analog_construct)
    , continuous_assign(o.continuous_assign)
    , module_instantiation(o.module_instantiation)
    , module_or_generate_item_declaration(o.module_or_generate_item_declaration)
{
    // ntd
}

auto module_or_generate_item_t::operator=(const module_or_generate_item_t &o) -> module_or_generate_item_t &
{
    if (this == &o) {
        return *this;
    }

    initial_construct                   = o.initial_construct;
    local_parameter_declaration         = o.local_parameter_declaration;
    always_construct                    = o.always_construct;
    analog_construct                    = o.analog_construct;
    continuous_assign                   = o.continuous_assign;
    module_instantiation                = o.module_instantiation;
    module_or_generate_item_declaration = o.module_or_generate_item_declaration;

    return *this;
}

module_or_generate_item_declaration_t::module_or_generate_item_declaration_t()
    : net_declaration(nullptr)
    , reg_declaration(nullptr)
    , integer_declaration(nullptr)
    , real_declaration(nullptr)
    , time_declaration(nullptr)
    , realtime_declaration(nullptr)
    , event_declaration(nullptr)
    , genvar_declaration(nullptr)
    , branch_declaration(nullptr)
    , task_declaration(nullptr)
    , function_declaration(nullptr)
    , generate_declaration(nullptr)
{
    // ntd
}

module_or_generate_item_declaration_t::~module_or_generate_item_declaration_t()
{
    // ntd
}

module_or_generate_item_declaration_t::module_or_generate_item_declaration_t(
    const module_or_generate_item_declaration_t &o)
    : net_declaration(o.net_declaration)
    , reg_declaration(o.reg_declaration)
    , integer_declaration(o.integer_declaration)
    , real_declaration(o.real_declaration)
    , time_declaration(o.time_declaration)
    , realtime_declaration(o.realtime_declaration)
    , event_declaration(o.event_declaration)
    , genvar_declaration(o.genvar_declaration)
    , branch_declaration(o.branch_declaration)
    , task_declaration(o.task_declaration)
    , function_declaration(o.function_declaration)
    , generate_declaration(o.generate_declaration)
{
    // ntd
}

auto module_or_generate_item_declaration_t::operator=(const module_or_generate_item_declaration_t &o)
    -> module_or_generate_item_declaration_t &
{
    if (this == &o) {
        return *this;
    }
    net_declaration      = o.net_declaration;
    reg_declaration      = o.reg_declaration;
    integer_declaration  = o.integer_declaration;
    real_declaration     = o.real_declaration;
    time_declaration     = o.time_declaration;
    realtime_declaration = o.realtime_declaration;
    event_declaration    = o.event_declaration;
    genvar_declaration   = o.genvar_declaration;
    branch_declaration   = o.branch_declaration;
    task_declaration     = o.task_declaration;
    function_declaration = o.function_declaration;
    generate_declaration = o.generate_declaration;
    return *this;
}

analog_event_control_t::analog_event_control_t()
    : analog_event_expression(nullptr)
{
}

analog_event_control_t::~analog_event_control_t() = default;

analog_event_control_t::analog_event_control_t(const analog_event_control_t &o)

    = default;

auto analog_event_control_t::operator=(const analog_event_control_t &o) -> analog_event_control_t &
{
    if (this == &o) {
        return *this;
    }
    event_control_t::operator=(o);
    analog_event_expression = o.analog_event_expression;
    return *this;
}

auto analog_event_control_t::getFirstObject() const -> hif::Object *
{
    if (analog_event_expression != nullptr && analog_event_expression->getFirstObject() != nullptr) {
        return analog_event_expression->getFirstObject();
    }
    return event_control_t::getFirstObject();
}
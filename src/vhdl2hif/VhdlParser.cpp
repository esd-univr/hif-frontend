/// @file VhdlParser.cpp
/// @brief
/// Copyright (c) 2024-2025, Electronic Systems Design (ESD) Group,
/// Univeristy of Verona.
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#include <cmath>

#include "vhdl2hif/vhdl_parser.hpp"
#include "vhdl2hif/vhdl_support.hpp"

using namespace hif;
using std::list;
using std::map;
using std::pair;
using std::string;

#if (defined _MSC_VER)
#else
#pragma GCC diagnostic ignored "-Wredundant-decls"
#endif

/*
 * Initialization of static fields
 * --------------------------------------------------------------------- */
BList<DesignUnit> *VhdlParser::du_declarations          = new BList<DesignUnit>();
BList<LibraryDef> *VhdlParser::lib_declarations         = new BList<LibraryDef>();
list<architecture_body_t *> *VhdlParser::du_definitions = new list<architecture_body_t *>();
BList<LibraryDef> *VhdlParser::lib_definitions          = new BList<LibraryDef>();

extern FILE *yyin;  // defined in vhdlParser.cc
extern FILE *yyout; // defined in vhdlParser.cc

/*
 * Bison forward declarations
 * --------------------------------------------------------------------- */
int yylex_destroy();
int yyparse(VhdlParser *parser);

VhdlParser::VhdlParser(string fileName)
    : _fileName(fileName)
    , _parseOnly(false)
    , _parserContext(VHDL_ctx)
    , _globalScope(true)
    , _pslMixed(false)
    , _tmpCustomLineNumber(0)
    , _tmpCustomColumnNumber(0)
    , _sem(hif::semantics::VHDLSemantics::getInstance())
    , _factory()
    , _name_table(NameTable::getInstance())
    , _library_list(new BList<Library>())
    , _library_function()
    , _is_vhdl_type()
    , _is_operator_overloading()
    , _configurationMap()
{
    this->_initStandardLibraries();
    _factory.setSemantics(_sem);

    yyfilename = fileName;
}

VhdlParser::~VhdlParser()
{
    delete _is_vhdl_type;
    delete _is_operator_overloading;
    delete _library_list;

    for (VhdlParser::configuration_map_t::iterator i(_configurationMap.begin()); i != _configurationMap.end(); ++i) {
        for (component_configuration_map_t::iterator j(i->second.begin()); j != i->second.end(); ++j) {
            delete j->first;
            delete j->second->entity_aspect.entity;
            delete j->second;
        }
    }
}

bool VhdlParser::parse(bool parseOnly)
{
    std::ostringstream os;

    _parseOnly = parseOnly;

    yyin = fopen(_fileName.c_str(), "r");

    if (!yyin) {
        os.str("");
        os << "Could not open VHDL file " << _fileName;
        messageError(os.str(), nullptr, nullptr);
    }

    os.str("");
    os << "Input VHDL source file: " << _fileName;
    messageInfo(os.str());

    // Reset position info
    yylineno  = 1;
    yycolumno = 1;

    // Parser entry-point
    yyparse(this);

    fclose(yyin);
    // Destroy of lexer. Not called by Bison.
    yylex_destroy();

    return true;
}

void VhdlParser::setCurrentBlockCodeInfo(keyword_data_t keyword)
{
    _tmpCustomLineNumber   = static_cast<unsigned int>(keyword.line);
    _tmpCustomColumnNumber = static_cast<unsigned int>(keyword.column);
}

void VhdlParser::setCurrentBlockCodeInfo(Object *other)
{
    if (other == nullptr)
        return;

    _tmpCustomLineNumber   = other->getSourceLineNumber();
    _tmpCustomColumnNumber = other->getSourceColumnNumber();
}

void VhdlParser::setCodeInfo(Object *o)
{
    if (o == nullptr)
        return;

    o->setSourceFileName(_fileName);
    o->setSourceLineNumber(static_cast<unsigned int>(yylineno));
    o->setSourceColumnNumber(static_cast<unsigned int>(yycolumno));
}

void VhdlParser::setCodeInfoFromCurrentBlock(Object *o)
{
    if (o == nullptr)
        return;

    o->setSourceFileName(_fileName);
    o->setSourceLineNumber(_tmpCustomLineNumber);
    o->setSourceColumnNumber(_tmpCustomColumnNumber);
}

void VhdlParser::setContextVhdl()
{
    _parserContext = VHDL_ctx;
    yydebug("** Switching in VHDL-MODE **");
}

void VhdlParser::setContextPsl()
{
    _parserContext = PSL_ctx;
    yydebug("** Switching in PSL-MODE **");
    setGlobalScope(false);
    _pslMixed = true;
}

VhdlParser::parser_context_enum_t VhdlParser::getContext() { return _parserContext; }

void VhdlParser::setGlobalScope(bool s) { _globalScope = s; }

bool VhdlParser::isGlobalScope() { return _globalScope; }

bool VhdlParser::isParseOnly() { return _parseOnly; }

bool VhdlParser::isPslMixed() { return _pslMixed; }

void VhdlParser::addLibrary(BList<Library> *lib)
{
    if (lib == nullptr)
        return;

    for (BList<Library>::iterator i = lib->begin(); i != lib->end(); ++i) {
        _library_list->push_back(hif::copy(*i));
    }
}

/*
 * VHDL-Grammar related functions
 * --------------------------------------------------------------------- */

architecture_body_t *VhdlParser::parse_ArchitectureBody(
    Value *id,
    Value *name,
    list<block_declarative_item_t *> *architecture_declarative_part,
    std::list<concurrent_statement_t *> *concurrent_statement_list)
{
    // view name
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    Contents *contents = new Contents();
    setCodeInfoFromCurrentBlock(contents);

    GlobalAction *global = new GlobalAction();
    setCodeInfoFromCurrentBlock(global);

    contents->setName(identifier->getName());
    contents->setGlobalAction(global);

    // design unit name
    Identifier *entityName = dynamic_cast<Identifier *>(name);
    if (entityName == nullptr) {
        yyerror("Entity name not supported.", name);
    }

    // ** STEP 1 **
    for (list<block_declarative_item_t *>::iterator i = architecture_declarative_part->begin();
         i != architecture_declarative_part->end();) {
        if ((*i)->declarations != nullptr) {
            contents->declarations.merge(*((*i)->declarations));
            delete (*i)->declarations;
            delete *i;
            i = architecture_declarative_part->erase(i);
        } else if ((*i)->use_clause != nullptr) {
            contents->libraries.merge(*(*i)->use_clause);
            delete (*i)->use_clause;
            delete *i;
            i = architecture_declarative_part->erase(i);
        } else if ((*i)->configuration_specification != nullptr) {
            component_configuration_map_t comp_config_map;
            configuration_item_t *config_item    = new configuration_item_t();
            config_item->component_configuration = (*i)->configuration_specification;

            _populateConfigurationMap(config_item, &comp_config_map);

            configuration_map_key_t conf_map_key;
            conf_map_key.configuration = _name_table->getFreshName();
            conf_map_key.design_unit   = entityName->getName();
            conf_map_key.view          = identifier->getName();

            _configurationMap.insert(make_pair(conf_map_key, comp_config_map));

            delete config_item;
            delete (*i)->configuration_specification;
            delete *i;
            i = architecture_declarative_part->erase(i);
        } else if ((*i)->component_declaration != nullptr) {
            // do nothing, see STEP 2
            ++i;
        } else if ((*i)->isSkipped) {
            // ntd
            ++i;
        } else {
            messageError("Unexpected case", nullptr, _sem);
        }
    }

    // ** STEP 2 **
    // Populate the Contents object (processes, instances, actions, generates ...)
    //
    _populateContents(contents, concurrent_statement_list);

    // ** STEP 3 **
    // For each Component (DesignUnit object) in 'architecture_declarative_part',
    // set the list of libraries inherited from the architecture
    //
    architecture_body_t *ret = new architecture_body_t();
    for (list<block_declarative_item_t *>::iterator i = architecture_declarative_part->begin();
         i != architecture_declarative_part->end(); ++i) {
        if ((*i)->component_declaration == nullptr)
            continue;

        DesignUnit *comp = (*i)->component_declaration;
        for (BList<View>::iterator v = comp->views.begin(); v != comp->views.end(); ++v) {
            _fixIntancesWithComponent(contents, *v);
        }

        // TODO
        //contents->declarations.push_back( comp );
        ret->components.push_back(comp);

        delete *i;
    }

    delete architecture_declarative_part;
    delete concurrent_statement_list;
    delete identifier;

    ret->contents    = contents;
    ret->entity_name = entityName;

    return ret;
}

ProcedureCall *VhdlParser::parse_Assertion(assert_directive_t *a)
{
    ProcedureCall *ret = new ProcedureCall();
    setCodeInfo(ret);
    ret->setName("assert");
    Instance *inst = _factory.libraryInstance("standard", false, true);
    ret->setInstance(inst);

    ParameterAssign *p1 = new ParameterAssign();
    setCodeInfo(p1);
    p1->setName("condition");
    if (a->property != nullptr)
        p1->setValue(a->property);
    else
        p1->setValue(_factory.boolval(false));
    ret->parameterAssigns.push_back(p1);

    if (a->report != nullptr) {
        ParameterAssign *p2 = new ParameterAssign();
        setCodeInfo(p2);
        p2->setName("report");
        p2->setValue(a->report);
        ret->parameterAssigns.push_back(p2);
    }

    if (a->severity != nullptr) {
        ParameterAssign *p3 = new ParameterAssign();
        setCodeInfo(p3);
        p3->setName("severity");
        p3->setValue(a->severity);
        ret->parameterAssigns.push_back(p3);
    }

    delete a;

    return ret;
}

Aggregate *VhdlParser::parse_Aggregate(BList<AggregateAlt> *element_association_list)
{
    Aggregate *ret = new Aggregate();
    setCodeInfo(ret);

    unsigned count = 0;
    // Managing the "OTHERS" case
    // Search for an AggregateAlt with "OTHERS" in the list of indices
    for (BList<AggregateAlt>::iterator alt = element_association_list->begin(); alt != element_association_list->end();
         ++alt) {
        Identifier *no = dynamic_cast<Identifier *>((*alt)->indices.back());
        if (no != nullptr && no->getName() == "HIF_OTHERS") {
            // set the OTHERS field of the Aggregate
            ret->setOthers(hif::copy((*alt)->getValue()));
            continue;
        } else {
            if ((*alt)->indices.empty() && element_association_list->size() > 1) {
                IntValue *intv = new IntValue(count++);
                (*alt)->indices.push_back(intv);
                setCodeInfo(intv);
            }
            ret->alts.push_back(hif::copy(*alt));
        }
    }

    if (count > 0) {
        // Indices could be not correct. Add property and fixing in step 2
        // ref design vhdl/openCores/corproc
        ret->addProperty(AGGREGREGATE_INIDICES_PROPERTY);
    }

    // VHDL semantics forbids empty list:
    //assert( !ret->alts.empty() );

    delete element_association_list;
    return ret;
}

block_configuration_t *VhdlParser::parse_BlockConfiguration(
    block_specification_t *block_specification,
    BList<Library> *use_clause_list,
    list<configuration_item_t *> *configuration_item_list)
{
    if (!use_clause_list->empty()) {
        delete use_clause_list;
        yyerror("block_configuration: t_FOR block_specification use_clause_list "
                "configuration_item_list t_END t_FOR t_Semicolon. \n"
                "'use_clause_list' is not supported");
    }

    map<Instance *, binding_indication_t *> *conf_map = new map<Instance *, binding_indication_t *>();

    messageDebugAssert(block_specification->block_name != nullptr, "Unexpected case", nullptr, _sem);

    // for each configuration item
    for (list<configuration_item_t *>::iterator i = configuration_item_list->begin();
         i != configuration_item_list->end(); ++i) {
        _populateConfigurationMap(*i, conf_map);
        delete *i;
    }

    block_configuration_t *ret       = new block_configuration_t();
    ret->component_configuration_map = conf_map;
    ret->block_specification         = block_specification;

    delete use_clause_list;
    delete configuration_item_list;

    return ret;
}

View *VhdlParser::parse_BlockStatement(
    Value *identifier,
    Value *guard_expression,
    block_header_t *block_header,
    std::list<block_declarative_item_t *> *block_declarative_item_list,
    std::list<concurrent_statement_t *> *concurrent_statement_list)
{
    if (guard_expression != nullptr) {
        messageWarning("Guard expression of block statement is not supported", guard_expression, _sem);

        delete guard_expression;
    }

    View *ret = parse_EntityHeader(block_header->block_header_generic_part, block_header->block_header_port_part);

    ret->addProperty(BLOCK_STATEMENT_PROPERTY);

    // view name
    Identifier *viewId = dynamic_cast<Identifier *>(identifier);
    messageAssert(viewId != nullptr, "Expected identifier", identifier, _sem);

    Contents *contents_o = new Contents();
    setCodeInfo(contents_o);

    GlobalAction *global = new GlobalAction();
    setCodeInfo(global);

    contents_o->setName(viewId->getName());
    contents_o->setGlobalAction(global);

    // ** STEP 1 **
    for (list<block_declarative_item_t *>::iterator i = block_declarative_item_list->begin();
         i != block_declarative_item_list->end();) {
        if ((*i)->declarations != nullptr) {
            contents_o->declarations.merge(*((*i)->declarations));
            delete (*i)->declarations;
            delete *i;
            i = block_declarative_item_list->erase(i);
        } else if ((*i)->use_clause != nullptr) {
            contents_o->libraries.merge(*(*i)->use_clause);
            delete (*i)->use_clause;
            delete *i;
            i = block_declarative_item_list->erase(i);
        } else if ((*i)->configuration_specification != nullptr) {
            yyerror("block_statement: configuration-specifications inside a "
                    "block are not supported.");

            //delete (*i)->configuration_specification->component_specification->component_name;
            //delete (*i)->configuration_specification->component_specification->instantiation_list->identifier_list;
            //delete (*i)->configuration_specification->component_specification->instantiation_list;
            //delete (*i)->configuration_specification->component_specification;
            //delete (*i)->configuration_specification->binding_indication->entity_aspect.configuration;
            //delete (*i)->configuration_specification->binding_indication->entity_aspect.entity;
            //delete (*i)->configuration_specification->binding_indication->generic_map_aspect;
            //delete (*i)->configuration_specification->binding_indication->port_map_aspect;
            //delete (*i)->configuration_specification->binding_indication;
            //delete (*i)->configuration_specification;
            //
            //delete *i;
            //i = block_declarative_item_list->erase( i );
            // ---------------------------------------------------------------------
            //            component_configuration_map_t comp_config_map;
            //            configuration_item_t * config_item = new configuration_item_t();
            //            config_item->component_configuration = (*i)->configuration_specification;

            //            _populateConfigurationMap( config_item, &comp_config_map );

            //            configuration_map_key_t conf_map_key;
            //            conf_map_key.configuration = _name_table->none();
            //            conf_map_key.design_unit = entityName->getName();
            //            conf_map_key.view = viewId->getName();

            //            _configurationMap.insert( make_pair( conf_map_key, comp_config_map ) );

            //            delete config_item;
            //            delete (*i)->configuration_specification;
            //            delete *i;
            //            i = block_declarative_item_list->erase( i );
        } else if ((*i)->component_declaration != nullptr) {
            // do nothing, see STEP 2
            ++i;
        } else {
            messageError("Unexpected case", nullptr, _sem);
        }
    }

    // ** STEP 2 **
    // Populate the Contents object (processes, instances, actions, generates ...)
    //
    _populateContents(contents_o, concurrent_statement_list);

    // ** STEP 3 **
    // For each Component (DesignUnit object) in 'architecture_declarative_part',
    // set the list of libraries inherited from the architecture
    //
    for (list<block_declarative_item_t *>::iterator i = block_declarative_item_list->begin();
         i != block_declarative_item_list->end(); ++i) {
        if ((*i)->component_declaration == nullptr)
            continue;

        DesignUnit *comp = (*i)->component_declaration;
        for (BList<View>::iterator v = comp->views.begin(); v != comp->views.end(); ++v) {
            _fixIntancesWithComponent(contents_o, *v);
        }

        // TODO
        //contents_o->declarations.push_back( comp );

        delete comp;
        delete *i;
    }

    delete block_declarative_item_list;
    delete concurrent_statement_list;
    delete identifier;
    delete block_header;

    ret->setContents(contents_o);
    ret->setStandard(false);

    return ret;
}

IntValue *VhdlParser::parse_BasedLiteral(std::string base_lit)
{
    //base
    std::string base = base_lit.substr(0, base_lit.find_first_of("#", 0));
    int b            = atoi(base.c_str());

    //value in spevified base
    std::string value = base_lit.substr(base_lit.find_first_of("#") + 1);
    value             = value.substr(0, value.find_first_of("#"));

    //exponent
    std::string exp = base_lit.substr(base_lit.find_last_of("#E") + 1);
    int exponent    = (exp == "") ? 1 : atoi(exp.c_str());

    //cout << "Base: " << base << " Value:" << value << " Exp:" << exp << endl;

    std::string result = toBits(const_cast<char *>(value.c_str()), b);
    //cout << "To bits: " << result << endl;
    int int_v          = bit2decimal(const_cast<char *>(result.c_str()));

    //cout << "\tBit2Decimal: " << int_v << endl;

    int_v = static_cast<int>(pow(static_cast<double>(int_v), static_cast<double>(exponent)));

    //cout << "\tResult: " << int_v << "\n\n";

    IntValue *ret = new IntValue(int_v);
    setCodeInfo(ret);
    return ret;
}

hif::BitvectorValue *VhdlParser::parse_BitStringLiteral(identifier_data_t bitString)
{
    hif::BitvectorValue *ret = nullptr;

    std::string name(bitString.name);
    std::string::size_type i;

    while ((i = name.find('"')) != std::string::npos)
        name.erase(i, 1);

    while ((i = name.find('B')) != std::string::npos)
        name.erase(i, 1);

    while ((i = name.find('b')) != std::string::npos)
        name.erase(i, 1);

    // FROM STANDARD :
    // "An underline character inserted between adjacent digits
    // of a bit string literal does not affect the value of this literal."
    while ((i = name.find('_')) != std::string::npos)
        name.erase(i, 1);

    ret = new BitvectorValue(name);
    setCodeInfo(ret);

    free(bitString.name);

    return ret;
}

Value *VhdlParser::parse_CharacterLiteral(const char *c)
{
    Value *ret = nullptr;

    if (strcmp(c, "\'1\'") == 0) {
        BitValue *b = new BitValue();
        b->setValue(bit_one);
        ret = b;
    } else if (strcmp(c, "\'0\'") == 0) {
        BitValue *b = new BitValue();
        b->setValue(bit_zero);
        ret = b;
    } else if (strcmp(c, "\'-\'") == 0) {
        BitValue *b = new BitValue();
        b->setValue(bit_dontcare);
        ret = b;
    } else if (strcmp(c, "\'X\'") == 0 || strcmp(c, "\'x\'") == 0) {
        BitValue *b = new BitValue();
        b->setValue(bit_x);
        ret = b;
    } else if (strcmp(c, "\'Z\'") == 0 || strcmp(c, "\'z\'") == 0) {
        BitValue *b = new BitValue();
        b->setValue(bit_z);
        ret = b;
    } else if (strcmp(c, "\'L\'") == 0 || strcmp(c, "\'l\'") == 0) {
        BitValue *b = new BitValue();
        b->setValue(bit_l);
        ret = b;
    } else if (strcmp(c, "\'H\'") == 0 || strcmp(c, "\'h\'") == 0) {
        BitValue *b = new BitValue();
        b->setValue(bit_h);
        ret = b;
    } else if (strcmp(c, "\'W\'") == 0 || strcmp(c, "\'w\'") == 0) {
        BitValue *b = new BitValue();
        b->setValue(bit_w);
        ret = b;
    } else if (strcmp(c, "\'U\'") == 0 || strcmp(c, "\'u\'") == 0) {
        BitValue *b = new BitValue();
        b->setValue(bit_u);
        ret = b;
    } else {
        // General character:
        CharValue *ch = new CharValue(c[1]);
        ret           = ch;
    }

    setCodeInfo(ret);
    return ret;
}

StateTable *VhdlParser::parse_ConcurrentAssertionStatement(ProcedureCall *assertion)
{
    messageAssert(assertion != nullptr, "Missing assertion", nullptr, nullptr);
    StateTable *ret = new StateTable();
    ret->setName(NameTable::getInstance()->getFreshName("concurrent_assertion"));
    State *s = new State();
    s->setName(ret->getName());
    ret->states.push_back(s);
    s->actions.push_back(assertion);

    ret->addProperty(HIF_CONCURRENT_ASSERTION);
    return ret;
}

BList<Assign> *
VhdlParser::parse_ConditionalSignalAssignment(Value *target, hif::BList<hif::Assign> *conditional_waveforms)
{
    for (BList<Assign>::iterator i = conditional_waveforms->begin(); i != conditional_waveforms->end(); ++i) {
        (*i)->setLeftHandSide(hif::copy(target));
    }

    delete target;
    return conditional_waveforms;
}

hif::BList<hif::Assign> *VhdlParser::parse_ConditionalWaveforms(
    hif::When *conditional_waveforms_when_else_list,
    hif::BList<hif::Assign> *waveform,
    hif::Value *condition)
{
    if (condition != nullptr) {
        // This is a when for sure!
        hif::BList<hif::Assign> *ret = new hif::BList<hif::Assign>;
        When *newWhen = this->parse_ConditionalWaveformsWhen(conditional_waveforms_when_else_list, waveform, condition);
        Assign *ass   = new Assign;
        setCodeInfo(ass);
        ass->setRightHandSide(newWhen);
        ret->push_back(ass);
        return ret;
    }

    if (conditional_waveforms_when_else_list == nullptr || conditional_waveforms_when_else_list->alts.empty()) {
        // Normal assign, without after or when.
        delete conditional_waveforms_when_else_list;
        return waveform;
    }

    // Assign with when and after?
    messageAssert(
        waveform->size() == 1 && waveform->front()->getDelay() == nullptr, "Unsupported case.",
        conditional_waveforms_when_else_list, _sem);

    // Assign with when
    Value *def = waveform->front()->setRightHandSide(conditional_waveforms_when_else_list);
    conditional_waveforms_when_else_list->setDefault(def);
    return waveform;
}

When *VhdlParser::parse_ConditionalWaveformsWhen(
    When *conditional_waveforms_when_else_list,
    BList<Assign> *waveform,
    Value *condition)
{
    messageAssert(
        waveform->size() == 1 && waveform->front()->getDelay() == nullptr, "Unsupported case.", condition, _sem);
    When *when_o = conditional_waveforms_when_else_list;
    if (when_o == nullptr) {
        when_o = new When();
        setCodeInfo(when_o);
    }
    WhenAlt *whenalt_o = new WhenAlt();
    setCodeInfo(whenalt_o);
    whenalt_o->setCondition(condition);
    whenalt_o->setValue(waveform->back()->setRightHandSide(nullptr));
    when_o->alts.push_back(whenalt_o);

    delete waveform;

    return when_o;
}

void VhdlParser::parse_ConfigurationDeclaration(
    Value *identifier,
    Value *name,
    block_configuration_t *block_configuration)
{
    Identifier *config_name = dynamic_cast<Identifier *>(identifier);
    Identifier *design_unit = dynamic_cast<Identifier *>(name);

    if (config_name == nullptr || design_unit == nullptr) {
        yyerror("t_CONFIGURATION identifier t_OF name t_IS "
                "configuration_declarative_part "
                "block_configuration t_END t_CONFIGURATION_opt identifier_opt "
                "t_Semicolon."
                "\n Configuration name or design unit name not valid");
    }

    messageDebugAssert(
        block_configuration->block_specification->block_name != nullptr, "Unexpected case", nullptr, _sem);

    if (block_configuration->component_configuration_map->empty()) {
        delete identifier;
        delete name;
        delete block_configuration;

        return;
    }

    configuration_map_key_t conf_map_key;
    conf_map_key.configuration = config_name->getName();
    conf_map_key.design_unit   = design_unit->getName();
    conf_map_key.view          = block_configuration->block_specification->block_name->getName();

    _configurationMap[conf_map_key] = *block_configuration->component_configuration_map;

    delete block_configuration->component_configuration_map;
    delete block_configuration->block_specification->block_name;
    delete identifier;
    delete name;
    delete block_configuration;
}

component_configuration_t *VhdlParser::parse_ConfigurationSpecification(
    component_specification_t *component_specification,
    binding_indication_t *binding_indication)
{
    component_configuration_t *ret = new component_configuration_t();

    ret->component_specification = component_specification;
    ret->binding_indication      = binding_indication;

    return ret;
}

component_specification_t *
VhdlParser::parse_ComponentSpecification(instantiation_list_t *instantiation_list, hif::Value *name)
{
    Identifier *entity_name = dynamic_cast<Identifier *>(name);
    messageAssert(entity_name != nullptr, "Expected identifier", name, _sem);

    component_specification_t *ret = new component_specification_t();
    ret->instantiation_list        = instantiation_list;
    ret->component_name            = entity_name;

    return ret;
}

Switch *VhdlParser::parse_CaseStatement(Value *expression, BList<SwitchAlt> *case_statement_alternative_list)
{
    Switch *ret = new Switch();
    setCodeInfoFromCurrentBlock(ret);
    ret->setCondition(expression);
    ret->alts.merge(*case_statement_alternative_list);
    // VHDL semantics forbids empty list:
    messageAssert(!ret->alts.empty(), "Unexpected empty switch alt list", ret, _sem);
    // Managing the "others" case:
    SwitchAlt *sao = ret->alts.back();
    Identifier *no = dynamic_cast<Identifier *>(sao->conditions.back());
    if (no != nullptr && no->getName() == "HIF_OTHERS") {
        ret->defaults.merge(sao->actions);
        BList<SwitchAlt>::iterator i(sao);
        i.erase();
        if (ret->defaults.empty()) {
            Null *n = new Null();
            setCodeInfoFromCurrentBlock(n);
            ret->defaults.push_back(n);
        }
    }

    delete case_statement_alternative_list;
    return ret;
}

BList<Declaration> *VhdlParser::parse_ConstantDeclaration(
    BList<Identifier> *identifier_list,
    subtype_indication_t *subtype_indication,
    Value *expression)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    BList<Declaration> *ret = new BList<Declaration>();

    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Const *c = new Const();
        setCodeInfo(c);
        c->setName((*i)->getName());
        c->setType(hif::copy(type_o));
        if (expression != nullptr) {
            c->setValue(hif::copy(expression));
            setCodeInfo(c->getValue());
        }

        ret->push_back(c);
    }

    delete identifier_list;
    delete type_o;
    delete subtype_indication;

    if (expression != nullptr) {
        delete expression;
    }

    return ret;
}

Array *
VhdlParser::parse_ConstrainedArrayDefinition(BList<Range> *index_constraint, subtype_indication_t *subtype_indication)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    Array *array_o = nullptr;
    messageDebugAssert(!index_constraint->empty(), "Unexpected case", nullptr, _sem);
    for (BList<Range>::iterator i = index_constraint->rbegin(); i != index_constraint->rend(); --i) {
        Array *arr = new Array();
        setCodeInfo(arr);
        arr->setSpan(hif::copy(*i));
        if (i == index_constraint->rbegin()) {
            arr->setType(type_o);
        } else {
            arr->setType(array_o);
        }
        array_o = arr;
    }

    setCodeInfo(array_o);

    delete index_constraint;
    delete subtype_indication;
    return array_o;
}

Value *VhdlParser::parser_DecimalLiteral(char *abstractLit)
{
    Value *ret = nullptr;
    // An abstract literal shoul be IntValue or RealValue
    std::string abstract_val(abstractLit);
    if (abstract_val.find(".") != std::string::npos) {
        // RealValue
        ret = new RealValue(atof(abstractLit));
    } else {
        // IntValue
        ret = new IntValue(atoi(abstractLit));
    }

    free(abstractLit);
    setCodeInfo(ret);
    return ret;
}

Identifier *VhdlParser::parse_Designator(Value *id)
{
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    if (identifier == nullptr) {
        StringValue *s     = dynamic_cast<StringValue *>(id);
        BitvectorValue *bv = dynamic_cast<BitvectorValue *>(id);
        if (s == nullptr && bv == nullptr) {
            messageError("Unexpected object", id, nullptr);
        }

        std::string val;
        if (s != nullptr)
            val = s->getValue();
        else /*bv != nullptr*/
            val = bv->getValue();
        std::map<string, string>::iterator i = _is_operator_overloading->find(val);
        if (i == _is_operator_overloading->end()) {
            messageError("Unexpected case", nullptr, _sem);
        }

        identifier = new Identifier();
        identifier->setName(i->second);
        delete id;
    }

    setCodeInfo(identifier);
    return identifier;
}

void VhdlParser::parse_DesignUnit(list<context_item_t *> *context_clause, library_unit_t *library_unit)
{
    //
    //  Primary Unit:
    //  -   entity declaration
    //  -   package declaration
    //
    if (library_unit->primary_unit != nullptr) {
        primary_unit_t *p = library_unit->primary_unit;

        // new DesignUnit
        if (p->entity_declaration != nullptr) {
            DesignUnit *design_unit = p->entity_declaration;
            // for each view of the DesignUnit ...
            for (BList<View>::iterator i = design_unit->views.begin(); i != design_unit->views.end(); ++i) {
                View *view = *i;
                for (list<context_item_t *>::iterator j = context_clause->begin(); j != context_clause->end(); ++j) {
                    context_item_t *cItem = *j;

                    messageDebugAssert(cItem->library_clause == nullptr, "Unexpected case", nullptr, _sem);

                    if (cItem->use_clause != nullptr) {
                        view->libraries.merge(*(cItem->use_clause));
                        delete cItem->use_clause;
                    }

                    delete cItem;
                }
            }
            du_declarations->push_back(design_unit);
        }
        // new Library
        else if (p->package_declaration != nullptr) {
            LibraryDef *lib_def = p->package_declaration;
            for (list<context_item_t *>::iterator i = context_clause->begin(); i != context_clause->end(); ++i) {
                context_item_t *cItem = *i;

                messageDebugAssert(cItem->library_clause == nullptr, "Unexpected case", nullptr, _sem);
                if (cItem->use_clause != nullptr) {
                    lib_def->libraries.merge(*(cItem->use_clause));
                    delete cItem->use_clause;
                }
                delete cItem;
            }
            lib_declarations->push_back(lib_def);
        }

        delete library_unit->primary_unit;
    } else if (library_unit->secondary_unit != nullptr) {
        //
        //  Secondary Unit:
        //  -   architecture body   (contents of a view of the design unit)
        //  -   package body        (library def)
        //

        secondary_unit_t *s = library_unit->secondary_unit;

        // new View of a DesignUnit
        if (s->architecture_body != nullptr) {
            du_definitions->push_back(s->architecture_body);
        }
        // new LibraryDef
        else if (s->package_body != nullptr) {
            lib_definitions->push_back(s->package_body);
        }

        delete library_unit->secondary_unit;

        for (list<context_item_t *>::iterator i = context_clause->begin(); i != context_clause->end(); ++i) {
            context_item_t *cItem = *i;

            messageDebugAssert(cItem->library_clause == nullptr, "Unexpected library clause", nullptr, _sem);
            if (cItem->use_clause != nullptr) {
                delete cItem->use_clause;
            }
            delete cItem;
        }
    }

    delete context_clause;
    delete library_unit;
}

EnumValue *VhdlParser::parse_EnumerationLiteral(Value *id)
{
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    EnumValue *ret = new EnumValue();
    setCodeInfo(ret);

    ret->setName(identifier->getName());

    delete identifier;
    return ret;
}

EnumValue *VhdlParser::parse_EnumerationLiteral(char *characterLit)
{
    EnumValue *ret = new EnumValue();
    setCodeInfo(ret);

    ret->setName(characterLit);

    free(characterLit);
    return ret;
}

entity_aspect_t *VhdlParser::parse_EntityAspect(Value *name, bool entity, bool configuration)
{
    entity_aspect_t *ret = nullptr;
    if (entity) {
        // 'name' must be a Member or a FieldReference
        // (representing which architecture is implemented)
        FunctionCall *fc_o         = dynamic_cast<FunctionCall *>(name);
        FieldReference *fieldref_o = dynamic_cast<FieldReference *>(name);
        if (fc_o != nullptr) {
            ret         = new entity_aspect_t();
            ret->entity = new ViewReference();

            messageAssert(fc_o->getInstance() != nullptr, "Unexpected fcall", fc_o, _sem);
            ret->entity->setDesignUnit(fc_o->getName());

            messageAssert(fc_o->parameterAssigns.size() == 1u, "Unexpected name of assigns", fc_o, _sem);
            ParameterAssign *pass = fc_o->parameterAssigns.back();
            Identifier *id        = dynamic_cast<Identifier *>(pass->getValue());
            messageAssert(id != nullptr, "Expected identifier", pass->getValue(), _sem);
            ret->entity->setName(id->getName());
        } else if (fieldref_o != nullptr) {
            ret         = new entity_aspect_t();
            ret->entity = new ViewReference();
            ret->entity->setDesignUnit(fieldref_o->getName());
        } else {
            yyerror("entity_aspect: t_ENTITY name. Invalid architecture.", name);
        }
    } else if (configuration) {
        FieldReference *fieldref_o = dynamic_cast<FieldReference *>(name);
        if (fieldref_o != nullptr) {
            ret                = new entity_aspect_t();
            Identifier *i      = new Identifier();
            ret->configuration = i;
            ret->configuration->setName(fieldref_o->getName());
        } else {
            yyerror("entity_aspect: t_CONFIGURATION /*configuration_*/ mark. "
                    "Invalid configuration.");
        }
    }

    delete name;
    return ret;
}

View *VhdlParser::parse_EntityHeader(BList<hif::Declaration> *generic_clause, BList<Port> *port_clause)
{
    View *ret = new View();
    setCodeInfo(ret);

    Entity *iface_o = new Entity();
    setCodeInfo(iface_o);
    ret->setEntity(iface_o);

    if (generic_clause != nullptr) {
        for (BList<Declaration>::iterator i = generic_clause->begin(); i != generic_clause->end(); ++i) {
            ValueTP *valuetp_o = dynamic_cast<ValueTP *>(*i);
            if (valuetp_o == nullptr)
                continue;

            ret->templateParameters.push_back(hif::copy(valuetp_o));
        }
    }

    if (port_clause != nullptr) {
        iface_o->ports.merge(*port_clause);
    }

    delete generic_clause;
    delete port_clause;
    return ret;
}

Break *VhdlParser::parse_ExitStatement(Identifier *identifier_colon_opt, Identifier *identifier_opt)
{
    Break *ret = new Break();
    setCodeInfo(ret);

    if (identifier_opt != nullptr) {
        ret->setName(identifier_opt->getName());
        delete identifier_opt;
    }

    if (identifier_colon_opt != nullptr) {
        delete identifier_colon_opt;
    }

    return ret;
}

Value *VhdlParser::parse_Expression(Value *left_relation, Value *right_relation, Operator op_type)
{
    Expression *v = _factory.expression(left_relation, op_type, right_relation);
    setCodeInfo(v);
    return v;
}

Value *VhdlParser::parse_ExpressionAND(Value *left_relation, Value *right_relation)
{
    messageAssert(
        right_relation != nullptr, "Unexpected nullptr second operand in binary expression.", left_relation, nullptr);

    Expression *v = _factory.expression(left_relation, op_and, right_relation);
    setCodeInfo(v);
    return v;
}

Value *VhdlParser::parse_ExpressionXNOR(Value *left_relation, Value *right_relation)
{
    Expression *v = _factory.expression(left_relation, op_xor, right_relation);
    setCodeInfo(v);
    Expression *v2 = _factory.expression(op_not, v);
    setCodeInfo(v2);
    return v2;
}

Value *VhdlParser::parse_ExpressionNOR(Value *left_relation, Value *right_relation)
{
    Expression *v = _factory.expression(left_relation, op_or, right_relation);
    setCodeInfo(v);
    Expression *v2 = _factory.expression(op_not, v);
    setCodeInfo(v2);
    return v2;
}

Value *VhdlParser::parse_ExpressionNAND(Value *left_relation, Value *right_relation)
{
    Expression *v = _factory.expression(left_relation, op_and, right_relation);
    setCodeInfo(v);
    Expression *v2 = _factory.expression(op_not, v);
    setCodeInfo(v2);
    return v2;
}

hif::BList<hif::Declaration> *VhdlParser::parse_FileDeclaration(
    hif::BList<hif::Identifier> *identifier_list,
    subtype_indication_t *subtype_indication,
    hif::Value *file_open_information)
{
    hif::BList<hif::Declaration> *ret = new hif::BList<hif::Declaration>();

    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Variable *v = _factory.variable(
            /*_factory.file(*/ hif::copy(subtype_indication->type) /*)*/, (*i)->getName(),
            hif::copy(file_open_information));
        setCodeInfo(v);
        _factory.codeInfo(v, v->getSourceFileName(), v->getSourceLineNumber());

        ret->push_back(v);
    }

    delete identifier_list;
    delete subtype_indication->type;
    delete subtype_indication;
    delete file_open_information;
    return ret;
}

Value *VhdlParser::parse_FileOpenInformation(Value *expression, Value *file_logical_name)
{
    FunctionCall *fco = _factory.functionCall(
        "file_open", _factory.libraryInstance("std_textio", false, true), _factory.noTemplateArguments(),
        (_factory.parameterArgument("param1", file_logical_name), _factory.parameterArgument("param2", expression)));

    setCodeInfo(fco);
    _factory.codeInfo(fco, fco->getSourceFileName(), fco->getSourceLineNumber());

    return fco;
}

File *VhdlParser::parse_FileTypeDefinition(Value *name)
{
    File *ret = new File();
    setCodeInfo(ret);

    Identifier *id = dynamic_cast<Identifier *>(name);
    messageAssert(id != nullptr, "Unable to retrieve type", name, _sem);

    Type *t = resolveType(id->getName(), nullptr, nullptr, _sem, true);
    messageAssert(t != nullptr, "Unable to recognize specified type: " + std::string(id->getName()), nullptr, _sem);
    ret->setType(t);
    setCodeInfo(t);

    delete name;
    return ret;
}

Type *VhdlParser::parse_FloatingOrIntegerTypeDefinition(Range *range_constraint)
{
    RealValue *lbound = dynamic_cast<RealValue *>(range_constraint->getLeftBound());
    RealValue *rbound = dynamic_cast<RealValue *>(range_constraint->getRightBound());

    if (lbound != nullptr || rbound != nullptr) {
        Real *ro = new Real();
        ro->setSpan(range_constraint);
        setCodeInfo(ro);
        return ro;
    } else {
        Int *io = new Int();
        io->setSpan(range_constraint);
        setCodeInfo(io);
        return io;
    }
}

TypeDef *VhdlParser::parse_FullTypeDeclaration(Value *id, Type *type_definition)
{
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    TypeDef *ret = new TypeDef();
    setCodeInfo(ret);
    ret->setName(identifier->getName());
    ret->setType(type_definition);
    ret->setOpaque(true);

    delete identifier;
    return ret;
}

Value *VhdlParser::parse_FunctionCall(Value *name, BList<PortAssign> *passign_list)
{
    FunctionCall *fc = dynamic_cast<FunctionCall *>(name);

    messageAssert(passign_list != nullptr, "Expected param assign list", name, _sem);
    if (fc != nullptr && !passign_list->empty()) {
        Value *range_v = passign_list->back()->getValue();
        Range *range_o = dynamic_cast<Range *>(range_v);
        if (range_o != nullptr) {
            /*
             * name(value) ( [discrete_range] )
             */
            Slice *slice_o = new Slice();
            setCodeInfo(slice_o);

            slice_o->setPrefix(fc);
            slice_o->setSpan(hif::copy(range_o));

            delete passign_list;
            return slice_o;
        } else {
            /*
             * name(value) ( [value] )
             */
            Member *ret_member_o = new Member();
            setCodeInfo(ret_member_o);
            ret_member_o->setPrefix(fc);
            ret_member_o->setIndex(hif::copy(passign_list->back()->getValue()));

            delete passign_list;
            return ret_member_o;
        }
    }

    FieldReference *fieldref_o = dynamic_cast<FieldReference *>(name);
    if (fieldref_o != nullptr) {
        /*
         *  name.field ( [ list_of_values ] )
         */
        FunctionCall *ret_o = new FunctionCall();
        setCodeInfo(ret_o);
        ret_o->setName(fieldref_o->getName());
        ret_o->setInstance(fieldref_o->setPrefix(nullptr));
        for (BList<PortAssign>::iterator i = passign_list->begin(); i != passign_list->end(); ++i) {
            PortAssign *portass = (*i);
            ParameterAssign *pa = new ParameterAssign();
            setCodeInfo(pa);
            pa->setName(portass->getName());
            pa->setValue(hif::copy(portass->getValue()));
            ret_o->parameterAssigns.push_back(pa);
        }

        delete name;
        delete passign_list;
        return ret_o;
    }

    Identifier *id = dynamic_cast<Identifier *>(name);
    if (id == nullptr) {
        /*
         * other_object ( [ list_of_values ] )
         *
         * For instance:
         * conv_std_logic_vector(Tx_nRx,1)(0)
         * -------------------------------
         *               |
         *               ----------> is translated as a cast
         *
         * In this case we have 'cast_object ( list_of_values )'
         *
         */

        FieldReference *fr = dynamic_cast<FieldReference *>(name);
        messageAssert(fr != nullptr, "Unexpected non-field reference", name, _sem);

        FunctionCall *ret_o = new FunctionCall();
        setCodeInfo(ret_o);
        ret_o->setName(fr->getName());
        ret_o->setInstance(fr->setPrefix(nullptr));
        for (BList<PortAssign>::iterator i = passign_list->begin(); i != passign_list->end(); ++i) {
            PortAssign *portass = (*i);
            ParameterAssign *pa = new ParameterAssign();
            setCodeInfo(pa);
            pa->setName(portass->getName());
            pa->setValue(portass->setValue(nullptr));
            ret_o->parameterAssigns.push_back(pa);
        }

        delete fr;
        delete passign_list;
        return ret_o;
    }

    Value *ret       = nullptr;
    string id_string = stringToLower(id->getName());

    if (_is_vhdl_type->find(id_string) != _is_vhdl_type->end() && passign_list->size() == 1 &&
        dynamic_cast<Range *>(passign_list->back()->getValue()) != nullptr) {
        /*
         * Standard VHDL types - translated as Cast object
         *
         *      vhdl_type ( [discrete_range] )
         */
        BList<Value> val_list;
        val_list.push_back(hif::copy(static_cast<Range *>(passign_list->back()->getValue())));

        Cast *co = new Cast();
        setCodeInfo(co);
        Type *to = _resolveType(id->getName(), &val_list, nullptr);
        messageAssert(to != nullptr, "Cannot resolve type", nullptr, _sem);
        setCodeInfo(to);

        co->setType(to);
        ret = co;
        delete name;
    } else if (_is_vhdl_type->find(id_string) != _is_vhdl_type->end() && passign_list->size() == 1) {
        /*
         * Standard VHDL types - translated as Cast object
         *
         *      vhdl_type ( [value] )
         */
        messageAssert(passign_list->size() == 1, "Unexpxcted list size", nullptr, _sem);

        Cast *co = new Cast();
        setCodeInfo(co);
        Type *to = _resolveType(id->getName());
        messageAssert(to != nullptr, "Cannot resolve type", id, _sem);
        setCodeInfo(to);
        Value *op = passign_list->front()->getValue();
        setCodeInfo(op);

        co->setValue(hif::copy(op));
        co->setType(to);
        ret = co;
        delete name;
    } else if (passign_list->size() == 1 && dynamic_cast<Range *>(passign_list->back()->getValue()) != nullptr) {
        /*
         * A slice object:
         *
         * name ( [discrete_range] )
         */

        Slice *slice_o = new Slice();
        setCodeInfo(slice_o);
        slice_o->setPrefix(hif::copy(id));
        Range *range_o = hif::copy(static_cast<Range *>(passign_list->back()->getValue()));
        setCodeInfo(range_o);
        slice_o->setSpan(range_o);
        ret = slice_o;
        delete name;
    } else if (!passign_list->empty()) {
        /*
         * None of the above.
         *
         */

        /*
         * From Language Reference Manual
         *
         * "Named associations can be given in any order, but if both positional and
         * named associations appear in the same association list, then all positional
         * associations must occur first at their normal position. Hence once a
         * named association is used, the rest of the association list must use only
         * named associations."
         */

        /*
         * For each parameter the parser builds a PortAssign object.
         * Named association:
         * - PortAssignObject->name = formal parameter,
         * - PortAssignObject->value = actual parameter
         *
         * Positional association:
         * - PortAssignObject->name = (no name)
         * - PortAssignObject->value = actual parameter
         */
        bool is_function = false;
        for (BList<PortAssign>::iterator i = passign_list->begin(); i != passign_list->end(); ++i) {
            PortAssign *pass = (*i);
            if (pass->getName() != _name_table->none()) {
                is_function = true;
                break;
            }
        }

        /*
         * If at least one of the PortAssigns specifies the formal parameter,
         * the name certainly identifies a function, so we build a FunctionCall object
         *
         *  function_name ( [formal -> actual, formal -> actual, ... ] )
         *  function_name ( [ actual, actual, ... ] [formal -> actual, formal -> actual, ... ] )
         *
         */
        if (is_function) {
            Identifier *n     = static_cast<Identifier *>(name);
            FunctionCall *fco = new FunctionCall();
            setCodeInfo(fco);
            fco->setName(n->getName());

            for (BList<PortAssign>::iterator i = passign_list->begin(); i != passign_list->end(); ++i) {
                PortAssign *port = *i;
                setCodeInfo(port);
                ParameterAssign *param = new ParameterAssign();
                setCodeInfo(param);
                param->setValue(hif::copy(port->getValue()));
                param->setName(port->getName());
                fco->parameterAssigns.push_back(param);
            }

            ret = fco;
            fco->addProperty(RECOGNIZED_FCALL_PROPERTY);
            delete name;
        } else {
            /*
             * None of the above.
             *
             *      ?identifier? ( [list_of_values] )
             *
             * At this point we cannot determine if the name identifies a function declared
             * by this (or another) design unit.
             *
             * To be fixed in the post-parsing stage
             * ( VisitMember in PostParsingVisitor_step1 )
             *
             * Collect the informations in a Member object and append it to the HIF Tree
             */
            FunctionCall *fco = new FunctionCall();
            setCodeInfo(fco);
            fco->setName(id->getName());
            for (BList<PortAssign>::iterator i = passign_list->begin(); i != passign_list->end(); ++i) {
                PortAssign *portass = (*i);
                ParameterAssign *pa = new ParameterAssign();
                setCodeInfo(pa);
                pa->setName(portass->getName());
                pa->setValue(hif::copy(portass->getValue()));
                fco->parameterAssigns.push_back(pa);
            }
            ret = fco;
            delete name;
        }
    } else {
        messageDebugAssert(false, "Unexpected case", nullptr, _sem);
    }

    delete passign_list;
    setCodeInfo(ret);
    return ret;
}

Value *VhdlParser::parse_SequenceInstance(Value *name, hif::BList<Value> *passign_list)
{
    BList<PortAssign> *pass = new BList<PortAssign>();
    for (BList<Value>::iterator i = passign_list->begin(); i != passign_list->end();) {
        Value *v       = *i;
        i              = i.remove();
        PortAssign *pa = new PortAssign();
        setCodeInfo(pa);
        pa->setValue(v);
        pass->push_back(pa);
    }

    delete passign_list;
    return parse_FunctionCall(name, pass);
}

Generate *VhdlParser::parse_GenerateStatement(
    Value *id,
    Generate *generation_scheme,
    list<block_declarative_item_t *> *block_declarative_item_list,
    list<concurrent_statement_t *> *concurrent_statement_list)
{
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    generation_scheme->setName(identifier->getName());

    GlobalAction *go = new GlobalAction();
    setCodeInfoFromCurrentBlock(go);

    //
    // ** STEP 1 **
    // Process the list of concurrent statements
    //
    for (list<concurrent_statement_t *>::iterator i = concurrent_statement_list->begin();
         i != concurrent_statement_list->end(); ++i) {
        if ((*i)->signal_assignment != nullptr) {
            go->actions.merge(*(*i)->signal_assignment);
        } else if ((*i)->instantiation != nullptr) {
            Instance *io = (*i)->instantiation;
            generation_scheme->instances.push_back(io);
        } else if ((*i)->process != nullptr) {
            StateTable *sto = (*i)->process;
            generation_scheme->stateTables.push_back(sto);
        } else if ((*i)->generate != nullptr) {
            Generate *genObj = (*i)->generate;
            generation_scheme->generates.push_back(genObj);
        } else if ((*i)->component_instantiation != nullptr) {
            Instance *io = (*i)->component_instantiation;
            generation_scheme->instances.push_back(io);
        } else if ((*i)->block != nullptr) {
            // do nothing
            View *block = (*i)->block;
            generation_scheme->declarations.merge(block->declarations);
            Contents *blockContents = block->getContents();
            if (blockContents != nullptr) {
                generation_scheme->declarations.merge(blockContents->declarations);
                generation_scheme->stateTables.merge(blockContents->stateTables);
                generation_scheme->instances.merge(blockContents->instances);
                generation_scheme->generates.merge(blockContents->generates);

                GlobalAction *blockGlobact = blockContents->getGlobalAction();
                if (blockGlobact != nullptr) {
                    go->actions.merge(blockGlobact->actions);
                }
            }

            yywarning("Found a block statement in generate statement. It will "
                      "be merged into parent scope.");
            //yyerror( "A block statement in generate statement is not supported" );
        } else {
            messageDebugAssert(false, "Unexpected case", nullptr, _sem);
        }

        delete *i;
    }

    generation_scheme->setGlobalAction(go);

    //
    // ** STEP 2 **
    // Process the list of declarations and configurations
    //
    for (list<block_declarative_item_t *>::iterator i = block_declarative_item_list->begin();
         i != block_declarative_item_list->end(); ++i) {
        if ((*i)->declarations != nullptr) {
            generation_scheme->declarations.merge(*(*i)->declarations);
            delete (*i)->declarations;
        } else if ((*i)->use_clause != nullptr) {
            // do nothing
            delete (*i)->use_clause;
        } else if ((*i)->configuration_specification != nullptr) {
            // do nothing
            delete (*i)->configuration_specification->component_specification->component_name;
            delete (*i)->configuration_specification->component_specification->instantiation_list->identifier_list;
            delete (*i)->configuration_specification->component_specification->instantiation_list;
            delete (*i)->configuration_specification->component_specification;
            delete (*i)->configuration_specification->binding_indication->entity_aspect.configuration;
            delete (*i)->configuration_specification->binding_indication->entity_aspect.entity;
            delete (*i)->configuration_specification->binding_indication->generic_map_aspect;
            delete (*i)->configuration_specification->binding_indication->port_map_aspect;
            delete (*i)->configuration_specification->binding_indication;
            delete (*i)->configuration_specification;
        } else if ((*i)->component_declaration != nullptr) {
            // do nothing
            delete (*i)->component_declaration;
        } else {
            messageDebugAssert(false, "Unexpected case", nullptr, _sem);
        }

        delete *i;
    }

    delete identifier;
    delete concurrent_statement_list;
    delete block_declarative_item_list;

    return generation_scheme;
}

Value *VhdlParser::parse_Identifier(char *identifier)
{
    Value *ret = nullptr;
    string id  = stringToLower(identifier);

    if (strcmp("true", id.c_str()) == 0) {
        ret = new BoolValue(true);
    } else if (strcmp("false", id.c_str()) == 0) {
        ret = new BoolValue(false);
    } else {
        ret = new Identifier(identifier);
    }

    setCodeInfo(ret);
    free(identifier);
    return ret;
}

Range *VhdlParser::parse_IndexConstraint(Value *discrete_range)
{
    Range *range_o = dynamic_cast<Range *>(discrete_range);

    Identifier *id = dynamic_cast<Identifier *>(discrete_range);
    messageAssert(range_o != nullptr || id != nullptr, "Unexpected index constraint", discrete_range, _sem);

    if (id != nullptr) {
        // typed range
        range_o = new Range();
        Type *t = _resolveType(id->getName(), nullptr, nullptr);
        messageAssert(t != nullptr, "Unexpected index constraint identifier", id, _sem);
        range_o->setType(t);
    }

    return range_o;
}

Action *VhdlParser::parse_IfStatement(
    Value *condition,
    BList<Action> *sequence_of_statements_then,
    BList<IfAlt> *if_statement_elseif_list,
    BList<Action> *sequence_of_statements_other)
{
    IfAlt *cao = new IfAlt();
    setCodeInfoFromCurrentBlock(cao);
    cao->setCondition(condition);
    cao->actions.merge(*sequence_of_statements_then);

    If *co = new If();
    setCodeInfoFromCurrentBlock(co);
    co->alts.push_back(cao);
    co->alts.merge(*if_statement_elseif_list);
    co->defaults.merge(*sequence_of_statements_other);

    delete sequence_of_statements_then;
    delete if_statement_elseif_list;
    delete sequence_of_statements_other;
    return co;
}

Action *VhdlParser::parse_IfStatement(
    Value *condition,
    BList<Action> *sequence_of_statements,
    BList<IfAlt> *if_statement_elseif_list)
{
    IfAlt *cao = new IfAlt();
    setCodeInfoFromCurrentBlock(cao);
    cao->setCondition(condition);
    cao->actions.merge(*sequence_of_statements);

    If *co = new If();
    setCodeInfoFromCurrentBlock(co);
    co->alts.push_back(cao);
    co->alts.merge(*if_statement_elseif_list);

    delete sequence_of_statements;
    delete if_statement_elseif_list;
    return co;
}

Action *VhdlParser::parse_IfStatement(
    Value *condition,
    BList<Action> *sequence_of_statements_then,
    BList<Action> *sequence_of_statemens_else)
{
    IfAlt *cao = new IfAlt();
    setCodeInfoFromCurrentBlock(cao);
    cao->setCondition(condition);
    cao->actions.merge(*sequence_of_statements_then);

    If *co = new If();
    setCodeInfoFromCurrentBlock(co);
    co->alts.push_back(cao);
    co->defaults.merge(*sequence_of_statemens_else);

    delete sequence_of_statements_then;
    delete sequence_of_statemens_else;
    return co;
}

Action *VhdlParser::parse_IfStatement(Value *condition, BList<Action> *sequance_of_statements)
{
    IfAlt *cao = new IfAlt();
    setCodeInfoFromCurrentBlock(cao);
    cao->setCondition(condition);
    cao->actions.merge(*sequance_of_statements);

    If *co = new If();
    setCodeInfoFromCurrentBlock(co);
    co->alts.push_back(cao);

    delete sequance_of_statements;
    return co;
}

BList<IfAlt> *VhdlParser::parse_IfStatementElseifList(Value *condition, BList<Action> *sequence_of_statements)
{
    BList<IfAlt> *ret = new BList<IfAlt>();
    IfAlt *cao        = new IfAlt();
    setCodeInfoFromCurrentBlock(cao);
    cao->setCondition(condition);
    cao->actions.merge(*sequence_of_statements);

    delete sequence_of_statements;
    ret->push_back(cao);
    return ret;
}

BList<IfAlt> *VhdlParser::parse_IfStatementElseifList(
    BList<IfAlt> *if_statement_elseif_list,
    Value *condition,
    BList<Action> *sequence_of_statements)
{
    IfAlt *cao = new IfAlt();
    setCodeInfoFromCurrentBlock(cao);
    cao->setCondition(condition);
    cao->actions.merge(*sequence_of_statements);

    delete sequence_of_statements;
    if_statement_elseif_list->push_back(cao);
    return if_statement_elseif_list;
}

Range *VhdlParser::parse_IndexSubtypeDefinition(Value *name)
{
    Range *range_o = new Range();
    setCodeInfo(range_o);
    Type *rangeType = nullptr;
    setCodeInfo(rangeType);

    Identifier *id = dynamic_cast<Identifier *>(name);
    if (id == nullptr) {
        yyerror("index_subtype_definition: invalid range type");
    }

    if (id->getName() == "natural") {
        Int *ret = new Int();
        setCodeInfo(ret);
        ret->setSigned(false);
        rangeType = ret;
    } else if (id->getName() == "integer") {
        Int *ret = new Int();
        setCodeInfo(ret);
        ret->setSigned(true);
        rangeType = ret;
    } else if (id->getName() == "positive") {
        Int *ret = new Int();
        setCodeInfo(ret);
        ret->setSigned(false);
        rangeType = ret;
    }

    if (rangeType != nullptr) {
        // Range is typed.
        range_o->setType(rangeType);
    }

    delete name;
    return range_o;
}

instantiation_list_t *VhdlParser::parse_InstantiationList(hif::BList<hif::Identifier> *identifier_list)
{
    instantiation_list_t *ret = new instantiation_list_t();
    ret->identifier_list      = identifier_list;

    return ret;
}

instantiation_list_t *VhdlParser::parse_InstantiationList(bool all)
{
    instantiation_list_t *ret = new instantiation_list_t();

    if (all) {
        ret->all    = true;
        ret->others = false;
    } else {
        ret->others = true;
        ret->all    = false;
    }

    return ret;
}

ViewReference *VhdlParser::parse_InstantiatedUnit(bool component, bool entity, Value *name)
{
    //
    //  The non-terminal 'name' recognizes the information about design-unit
    //  and architecture as follow:
    //
    //  - 'instance_id : COMPONENT designUnit'                          as Identifier
    //  - 'instance_id : ENTITY designUnit'                             as Identifier
    //  - 'instance_id : CONFIGURATION designUnit'                      as Identifier
    //  - 'instance_id : ENTITY library.designUnit ( architecture )'    as FunctionCall with no-name
    //  - 'instance_id : ENTITY library.designUnit'                     as FieldReference
    //

    ViewReference *viewref_o = new ViewReference();
    setCodeInfo(viewref_o);

    Identifier *id       = dynamic_cast<Identifier *>(name);
    FunctionCall *fc_o   = dynamic_cast<FunctionCall *>(name);
    FieldReference *fr_o = dynamic_cast<FieldReference *>(name);
    setCodeInfo(fc_o);

    /*
     * identifier : designUnit
     */
    if (id != nullptr) {
        if (!entity && !component) {
            viewref_o->setDesignUnit(id->getName());
        }
        /*
         * identifier : ENTITY designUnit
         */
        else if (entity) {
            messageError("Unexpected entity flag (in instantiated unit)", name, nullptr);
        }
        /*
         * identifier : COMPONENT designUnit
         */
        else if (component) {
            messageError("Unexpected component flag (in instantiated unit)", name, nullptr);
        }
    }
    /*
     * identifier : ENTITY work.designUnit
     * identifier : ENTITY library.designUnit
     */
    else if (fr_o != nullptr) {
        Identifier *lib = dynamic_cast<Identifier *>(fr_o->getPrefix());
        messageAssert((lib != nullptr), "Unexpected FieldReference prefix", fr_o->getPrefix(), nullptr);

        if (lib->getName() == "work") {
            viewref_o->setName(_name_table->none()); // not available
            viewref_o->setDesignUnit(fr_o->getName());
        } else {
            yyerror("instantiated_unit: unsupported design-unit reference", fr_o);
        }
    }
    /*
     * identifier : ENTITY work.designUnit ( architecture )
     * identifier : ENTITY library.designUnit ( architecture )
     */
    else if (fc_o != nullptr) {
        fr_o                        = dynamic_cast<FieldReference *>(fc_o->getInstance());
        Identifier *architecture_id = dynamic_cast<Identifier *>(fc_o->parameterAssigns.back());

        if (fr_o != nullptr && architecture_id != nullptr) {
            Identifier *lib = dynamic_cast<Identifier *>(fr_o->getPrefix());
            messageAssert((lib != nullptr), "Unexpected FieldReference prefix", fr_o->getPrefix(), nullptr);

            if (lib->getName() == "work") {
                viewref_o->setName(architecture_id->getName());
                viewref_o->setDesignUnit(fr_o->getName());
            } else {
                yyerror("instantiated_unit: unsupported design-unit reference", fr_o);
            }
        } else {
            // Something like: work.du(arch).
            // Parsed as Identifier.fcall(Identifier)
            Identifier *lib = dynamic_cast<Identifier *>(fc_o->getInstance());
            messageAssert(fc_o->getInstance() == nullptr || lib != nullptr, "Unexpected instance", fc_o, _sem);
            Identifier *arch = nullptr;
            if (!fc_o->parameterAssigns.empty()) {
                messageAssert(fc_o->parameterAssigns.size() == 1, "Unexpected parameters number", fc_o, _sem);
                arch = dynamic_cast<Identifier *>(fc_o->parameterAssigns.front()->getValue());
                messageAssert(arch != nullptr, "Unexpected parameter value", fc_o, _sem);
            }

            viewref_o->setDesignUnit(fc_o->getName());
            if (lib != nullptr && lib->getName() != "work") {
                Library *l = new Library();
                setCodeInfo(fc_o);
                l->setName(lib->getName());
                viewref_o->setInstance(l);
            }

            if (arch != nullptr)
                viewref_o->setName(arch->getName());
        }
    } else {
        messageError("Unexpected object as instantiated unit", name, nullptr);
    }

    delete name;
    return viewref_o;
}

BList<Port> *VhdlParser::parse_InterfaceConstantDeclaration(
    BList<Identifier> *identifier_list,
    bool in_opt,
    subtype_indication_t *subtype_indication,
    Value *expression)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    BList<Port> *ret = new BList<Port>();
    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Port *port_o = new Port();
        setCodeInfo(port_o);

        port_o->setName((*i)->getName());
        port_o->setType(hif::copy(type_o));

        if (in_opt) {
            port_o->setDirection(dir_in);
        } else {
            port_o->setDirection(dir_none);
        }

        if (expression != nullptr) {
            port_o->setValue(hif::copy(expression));
        }

        ret->push_back(port_o);
    }

    delete identifier_list;
    delete type_o;
    delete subtype_indication;
    delete expression;

    return ret;
}

BList<Port> *VhdlParser::parse_InterfaceDeclaration(
    BList<Identifier> *identifier_list,
    PortDirection mode_opt,
    subtype_indication_t *subtype_indication)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    BList<Port> *port_list = new BList<Port>();

    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Port *p = new Port();
        setCodeInfo(p);
        p->setName((*i)->getName());
        p->setType(hif::copy(type_o));
        p->setDirection(mode_opt);

        port_list->push_back(p);
    }

    delete identifier_list;
    delete type_o;
    delete subtype_indication;

    return port_list;
}

BList<Port> *VhdlParser::parse_InterfaceSignalDeclaration(
    BList<Identifier> *identifier_list,
    PortDirection mode_opt,
    subtype_indication_t *subtype_indication,
    Value *expression)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    BList<Port> *ret = new BList<Port>();
    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Port *signal_o = new Port();
        setCodeInfo(signal_o);

        signal_o->setName((*i)->getName());
        signal_o->setType(hif::copy(type_o));
        signal_o->setDirection(mode_opt);

        if (expression != nullptr) {
            signal_o->setValue(hif::copy(expression));
        }

        ret->push_back(signal_o);
    }

    delete identifier_list;
    delete type_o;
    delete subtype_indication;
    delete expression;

    return ret;
}

BList<Port> *VhdlParser::parse_InterfaceVariableDeclaration(
    BList<Identifier> *identifier_list,
    PortDirection mode_opt,
    subtype_indication_t *subtype_indication,
    Value *expression)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    BList<Port> *ret = new BList<Port>();
    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Port *signal_o = new Port();
        setCodeInfo(signal_o);

        signal_o->setName((*i)->getName());
        signal_o->setType(hif::copy(type_o));
        signal_o->setDirection(mode_opt);

        if (expression != nullptr) {
            signal_o->setValue(hif::copy(expression));
        }

        ret->push_back(signal_o);
    }

    delete identifier_list;
    delete type_o;
    delete subtype_indication;
    delete expression;

    return ret;
}

Action *VhdlParser::parse_IterationScheme(Value *condition)
{
    While *wo = new While();
    setCodeInfo(wo);
    wo->setCondition(condition);
    return wo;
}

Action *VhdlParser::parse_IterationScheme(BList<Value> *parameter_specification)
{
    Range *forRange = static_cast<Range *>(hif::copy(parameter_specification->back()));
    setCodeInfo(forRange);
    std::string label = _name_table->getFreshName("forloop");
    BList<DataDeclaration> initD;
    BList<Action> initV;
    BList<Action> stepAc;
    BList<Action> forAct;

    Identifier *indName = static_cast<Identifier *>(hif::copy(parameter_specification->front()));
    setCodeInfo(indName);
    initD.push_back(_factory.variable(_factory.integer(), indName->getName(), nullptr));

    const bool isTyped = forRange->getRightBound() == nullptr;
    const bool isReverse =
        (dynamic_cast<FunctionCall *>(forRange->getLeftBound()) &&
         static_cast<FunctionCall *>(forRange->getLeftBound())->getName() == "reverse_range");

    Operator stepOp = (forRange->getDirection() == dir_upto && !isTyped) ? op_plus : op_minus;
    if (isReverse)
        stepOp = stepOp == op_plus ? op_minus : op_plus;

    stepAc.push_back(_factory.assignment(copy(indName), _factory.expression(indName, stepOp, _factory.intval(1))));

    For *for_ob = _factory.forLoop(label, initD, initV, copy(forRange), stepAc, forAct);
    setCodeInfo(for_ob);

    delete parameter_specification;
    return for_ob;

    //    For * fo = new For();
    //    $$ = fo;
    //    setCodeInfo( $$ );
    //    if( $2->size() == 2 )
    //    {
    //    yyerror("iteration_scheme: unsupported for statement form.");
    //    }
    //    Identifier * index = dynamic_cast<Identifier*>($2->front());
    //    Range * range = dynamic_cast<Range*>($2->back() );
    //    assert( index != nullptr );
    //    assert( range != nullptr );
    //    fo->SetIndex( static_cast<Identifier*>(copy( index )));
    //    fo->SetRange( static_cast<Range*>(copy( range )));
    //    delete $2;
}

BList<Library> *VhdlParser::parse_LibraryClause(BList<Identifier> *logical_name_list)
{
    delete logical_name_list;
    return nullptr;
}

Value *VhdlParser::parse_Literal_null()
{
    Cast *c = _factory.cast(_factory.pointer(nullptr), _factory.intval(0));

    setCodeInfo(c);
    _factory.codeInfo(c, c->getSourceFileName(), c->getSourceLineNumber());

    return c;
}

Action *VhdlParser::parse_LoopStatement(
    Identifier *identifier_colon_opt,
    Action *iteration_scheme_opt,
    BList<Action> *sequence_of_statements)
{
    Action *ret = nullptr;

    For *for_o     = dynamic_cast<For *>(iteration_scheme_opt);
    While *while_o = dynamic_cast<While *>(iteration_scheme_opt);

    if (for_o != nullptr) {
        if (identifier_colon_opt != nullptr) {
            for_o->setName(identifier_colon_opt->getName());
        }
        for_o->forActions.merge(*sequence_of_statements);
        ret = for_o;
    } else if (while_o != nullptr) {
        if (identifier_colon_opt != nullptr) {
            while_o->setName(identifier_colon_opt->getName());
        }
        while_o->actions.merge(*sequence_of_statements);
        ret = while_o;
    } else {
        while_o = new While();

        BoolValue *bool_o = new BoolValue(true);
        setCodeInfo(bool_o);
        while_o->setCondition(bool_o);

        if (identifier_colon_opt != nullptr) {
            while_o->setName(identifier_colon_opt->getName());
        }
        while_o->actions.merge(*sequence_of_statements);

        ret = while_o;
        setCodeInfo(ret);
    }

    if (iteration_scheme_opt != nullptr) {
        ret->setSourceFileName(iteration_scheme_opt->getSourceFileName());
        ret->setSourceLineNumber(iteration_scheme_opt->getSourceLineNumber());
        ret->setSourceColumnNumber(iteration_scheme_opt->getSourceColumnNumber());
    }

    delete identifier_colon_opt;
    delete sequence_of_statements;

    return ret;
}

Action *VhdlParser::parse_NextStatement(Identifier *identifier_opt, Value *condition)
{
    Continue *n = new Continue();
    setCodeInfo(n);

    if (identifier_opt != nullptr) {
        n->setName(identifier_opt->getName());
    }

    If *co = new If();
    setCodeInfo(co);
    IfAlt *cao = new IfAlt();
    setCodeInfo(cao);
    cao->setCondition(condition);
    cao->actions.push_back(n);
    co->alts.push_back(cao);

    delete identifier_opt;
    delete condition;
    return co;
}

Value *VhdlParser::parse_NumericLiteral(Value *num, Value *unit)
{
    messageDebugAssert(num != nullptr && unit != nullptr, "Unexpected case", nullptr, _sem);

    Identifier *u = dynamic_cast<Identifier *>(unit);
    messageAssert(u != nullptr, "Unexpected physical unit", unit, nullptr);

    RealValue *vReal = dynamic_cast<RealValue *>(num);
    IntValue *vInt   = dynamic_cast<IntValue *>(num);
    messageAssert(vReal != nullptr || vInt != nullptr, "Unexpected physical value", num, _sem);

    std::string str(u->getName());
    TimeValue::TimeUnit tu = TimeValue::time_fs;
    /*  time_fs,  // femtosecond
        time_ps,    // picosecond
        time_ns,    // nanosecond
        time_us,    // microsecond
        time_ms,    // millisecond
        time_sec,   // second
        time_min,   // minute
        time_hr     // hour
     */

    if (str == "fs")
        tu = TimeValue::time_fs;
    else if (str == "ps")
        tu = TimeValue::time_ps;
    else if (str == "ns")
        tu = TimeValue::time_ns;
    else if (str == "us")
        tu = TimeValue::time_us;
    else if (str == "ms")
        tu = TimeValue::time_ms;
    else if (str == "sec")
        tu = TimeValue::time_sec;
    else if (str == "min")
        tu = TimeValue::time_min;
    else if (str == "hr")
        tu = TimeValue::time_hr;
    else
        messageError("Not supported unit ", u, nullptr);

    TimeValue *tv = new TimeValue();
    tv->setValue((vReal != nullptr) ? vReal->getValue() : static_cast<double>(vInt->getValue()));
    tv->setUnit(tu);

    delete num;
    delete unit;
    return tv;
}

Action *VhdlParser::parse_NextStatement(Identifier *identifier_opt)
{
    Continue *n = new Continue();
    setCodeInfo(n);
    if (identifier_opt != nullptr) {
        n->setName(identifier_opt->getName());
    }

    delete identifier_opt;
    return n;
}

LibraryDef *VhdlParser::parse_PackageBody(Value *id, BList<Declaration> *package_body_declarative_part)
{
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    LibraryDef *librarydef_o = new LibraryDef();
    setCodeInfoFromCurrentBlock(librarydef_o);
    librarydef_o->setName(identifier->getName());
    librarydef_o->setLanguageID(hif::rtl);

    librarydef_o->declarations.merge(*package_body_declarative_part);

    delete identifier;
    delete package_body_declarative_part;
    return librarydef_o;
}

LibraryDef *VhdlParser::parse_PackageDeclaration(Value *id, BList<Declaration> *package_declarative_part)
{
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    LibraryDef *librarydef_o = new LibraryDef();
    setCodeInfoFromCurrentBlock(librarydef_o);
    librarydef_o->setName(identifier->getName());
    librarydef_o->setLanguageID(hif::rtl);

    if (package_declarative_part != nullptr) {
        for (BList<Declaration>::iterator i = package_declarative_part->begin(); i != package_declarative_part->end();
             ++i) {
            librarydef_o->declarations.push_back(hif::copy(*i));
        }
    }

    delete identifier;
    delete package_declarative_part;
    return librarydef_o;
}

ProcedureCall *VhdlParser::parse_ProcedureCall(Value *name)
{
    ProcedureCall *ret = new ProcedureCall();
    setCodeInfo(ret);
    if (dynamic_cast<Identifier *>(name) != nullptr) {
        Identifier *no = static_cast<Identifier *>(name);
        ret->setName(no->getName());
    } else if (dynamic_cast<FunctionCall *>(name) != nullptr) {
        FunctionCall *fo = static_cast<FunctionCall *>(name);
        ret->setName(fo->getName());
        ret->parameterAssigns.merge(fo->parameterAssigns);
    } else if (dynamic_cast<Member *>(name) != nullptr) {
        Member *member     = static_cast<Member *>(name);
        Identifier *pname  = dynamic_cast<Identifier *>(member->getPrefix());
        FieldReference *fr = dynamic_cast<FieldReference *>(member->getPrefix());

        messageAssert(pname != nullptr || fr != nullptr, "Unexpected prefix", member, _sem);

        if (pname != nullptr) {
            ret->setName(pname->getName());
        } else // fr != nullptr
        {
            ret->setName(fr->getName());
            Library *lib   = resolveLibraryType(fr->getPrefix());
            Instance *inst = nullptr;
            if (lib != nullptr) {
                inst = new Instance();
                setCodeInfo(inst);
                inst->setReferencedType(lib);
                inst->setName(inst->getReferencedType()->getName());
            }
            ret->setInstance(inst);
        }

        if (member->getIndex() != nullptr) {
            ParameterAssign *pao;
            pao = new ParameterAssign();
            pao->setValue(hif::copy(member->getIndex()));
            ret->parameterAssigns.push_back(pao);
        }
    } else {
        yyerror("procedure_call: unsupported procedure statement.", name);
    }

    delete name;
    return ret;
}

StateTable *VhdlParser::parse_ProcessStatement(
    Identifier *identifier_colon_opt,
    BList<Value> *sensitivity_list_paren_opt,
    BList<Declaration> *process_declarative_part,
    BList<Action> *process_statement_part,
    Identifier *identifier_opt)
{
    std::string name_o = _name_table->getFreshName("vhdl_process");

    StateTable *ret = new StateTable();
    setCodeInfoFromCurrentBlock(ret);
    ret->setName(name_o);

    State *state = new State();
    setCodeInfoFromCurrentBlock(state);
    state->setName(name_o);
    ret->states.push_back(state);

    if (identifier_colon_opt != nullptr) {
        ret->setName(identifier_colon_opt->getName());
        state->setName(identifier_colon_opt->getName());
    } else if (identifier_opt != nullptr) {
        ret->setName(identifier_opt->getName());
        state->setName(identifier_opt->getName());
    }

    delete identifier_colon_opt;
    delete identifier_opt;

    ret->sensitivity.merge(*sensitivity_list_paren_opt);
    delete sensitivity_list_paren_opt;

    ret->declarations.merge(*process_declarative_part);
    delete process_declarative_part;

    state->actions.merge(*process_statement_part);
    delete process_statement_part;
    return ret;
}

Range *VhdlParser::parse_Range(Value *simple_expression_left, RangeDirection direction, Value *simple_expression_right)
{
    Range *ret = new Range();
    setCodeInfo(ret);

    ret->setLeftBound(simple_expression_left);
    setCodeInfo(ret->getLeftBound());
    ret->setDirection(direction);
    ret->setRightBound(simple_expression_right);
    setCodeInfo(ret->getRightBound());

    return ret;
}

Range *VhdlParser::parse_Range(Value *attribute_name)
{
    FunctionCall *fco = dynamic_cast<FunctionCall *>(attribute_name);
    messageAssert((fco != nullptr), "Unsupported range specification (1).", attribute_name, nullptr);

    std::string no1 = "range";
    std::string no2 = "reverse_range";
    messageAssert(
        (fco->getName() == no1 || fco->getName() == no2), "Unsupported range specification (2).", attribute_name,
        nullptr);

    Range *range_o = new Range();
    setCodeInfo(range_o);
    range_o->setLeftBound(fco);
    return range_o;
}

FieldReference *VhdlParser::parse_SelectedName(Value *name, Value *suffix)
{
    Identifier *id = dynamic_cast<Identifier *>(suffix);
    if (id == nullptr) {
        yyerror("selected_name: suffix must be an identifier.");
    }

    FieldReference *fro = new FieldReference();
    setCodeInfo(fro);
    fro->setPrefix(name);
    fro->setName(id->getName());
    delete suffix;

    return fro;
}

Assign *VhdlParser::parse_SelectedSignalAssignment(Value *expression, Value *target, BList<WithAlt> *selected_waveforms)
{
    Assign *ao = new Assign();
    setCodeInfo(ao);

    With *wo = new With();
    setCodeInfo(wo);

    wo->setCondition(expression);
    for (BList<WithAlt>::iterator i = selected_waveforms->begin(); i != selected_waveforms->end(); ++i) {
        Identifier *hif_others = dynamic_cast<Identifier *>((*i)->conditions.front());
        setCodeInfo(hif_others);

        if (hif_others != nullptr && hif_others->getName() == "HIF_OTHERS") { // OTHERS of WITH .. SELECT
            wo->setDefault(hif::copy((*i)->getValue()));
        } else {
            wo->alts.push_back(hif::copy(*i));
        }
    }

    ao->setRightHandSide(wo);
    ao->setLeftHandSide(target);

    delete selected_waveforms;
    return ao;
}

WithAlt *VhdlParser::parse_SelectedWaveformsWhen(hif::BList<hif::Assign> *waveform, BList<Value> *choices)
{
    messageAssert(waveform != nullptr, "Expected waveform", nullptr, _sem);
    messageAssert(
        waveform->size() == 1 && waveform->front()->getDelay() == nullptr, "Expected waveform", nullptr, _sem);
    WithAlt *ret = new WithAlt();
    setCodeInfo(ret);

    ret->setValue(waveform->front()->setRightHandSide(nullptr));
    ret->conditions.merge(*choices);

    delete waveform;
    delete choices;
    return ret;
}

BList<Assign> *
VhdlParser::parse_SignalAssignmentStatement(Identifier *identifier_colon_opt, Value *target, BList<Assign> *waveform)
{
    messageAssert(waveform != nullptr, "Expected waveform", nullptr, _sem);

    for (BList<Assign>::iterator i = waveform->begin(); i != waveform->end(); ++i) {
        Assign *a = *i;
        messageDebugAssert(
            a->getLeftHandSide() == nullptr && a->getRightHandSide() != nullptr, "Unexpected waveform", a, _sem);

        setCodeInfoFromCurrentBlock(a);
        a->setLeftHandSide(hif::copy(target));
        setCodeInfo(a->getRightHandSide());
    }

    if (identifier_colon_opt != nullptr) {
        delete identifier_colon_opt;
    }

    delete target;
    return waveform;
}

BList<Declaration> *VhdlParser::parse_SignalDeclaration(
    BList<Identifier> *identifier_list,
    subtype_indication_t *subtype_indication,
    Value *expression)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    BList<Declaration> *ret = new BList<Declaration>();

    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Signal *s = new Signal();
        setCodeInfo(s);
        s->setName((*i)->getName());
        s->setType(hif::copy(type_o));
        s->setValue(hif::copy(expression));
        ret->push_back(s);
    }

    delete identifier_list;
    delete type_o;
    delete subtype_indication;
    delete expression;
    return ret;
}

BList<Declaration> *
VhdlParser::parse_SignalDeclaration(BList<Identifier> *identifier_list, subtype_indication_t *subtype_indication)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    BList<Declaration> *ret = new BList<Declaration>();

    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Signal *s = new Signal();
        setCodeInfo(s);
        s->setName((*i)->getName());
        s->setType(hif::copy(type_o));
        ret->push_back(s);
    }

    delete identifier_list;
    delete type_o;
    delete subtype_indication;
    return ret;
}

Value *VhdlParser::parse_StringLiteral(identifier_data_t stringLit)
{
    std::string name(stringLit.name);
    std::string::size_type i;
    while ((i = name.find('"')) != std::string::npos) {
        name.erase(i, 1);
    }

    bool is_bv = false;
    for (i = 0; i != name.size(); ++i) {
        switch (name[i]) {
        case '0':
        case '1':
        case 'u':
        case 'U':
        case 'x':
        case 'X':
        case 'z':
        case 'Z':
        case 'w':
        case 'W':
        case 'l':
        case 'L':
        case 'h':
        case 'H':
        case '-':
            is_bv = true;
            break;
        default:
            is_bv = false;
            break;
        }
        if (!is_bv)
            break;
    }

    if (is_bv) {
        BitvectorValue *bv = new BitvectorValue(name);
        setCodeInfo(bv);
        return bv;
    } else {
        StringValue *text_o = new StringValue(name.c_str());
        setCodeInfo(text_o);
        return text_o;
    }
}

Declaration *VhdlParser::parse_SubprogramBody(
    SubProgram *subprogram_specification,
    BList<Declaration> *subprogram_declarative_part,
    BList<Action> *subprogram_statement_part)
{
    StateTable *st = new StateTable();
    setCodeInfo(st);
    st->setName(subprogram_specification->getName());

    st->declarations.merge(*subprogram_declarative_part);

    State *state = new State();
    setCodeInfo(state);
    state->setName(subprogram_specification->getName());

    st->states.push_back(state);
    state->actions.merge(*subprogram_statement_part);

    delete subprogram_declarative_part;
    delete subprogram_statement_part;

    subprogram_specification->setStateTable(st);
    return subprogram_specification;
}

SubProgram *VhdlParser::parse_SubprogramSpecification(
    Identifier *designator,
    list<interface_declaration_t *> *formal_parameter_list_paren_opt)
{
    Procedure *po = new Procedure();
    setCodeInfoFromCurrentBlock(po);

    po->setName(designator->getName());

    if (formal_parameter_list_paren_opt != nullptr) {
        for (list<interface_declaration_t *>::iterator d = formal_parameter_list_paren_opt->begin();
             d != formal_parameter_list_paren_opt->end(); ++d) {
            interface_declaration_t *interface_item = *d;
            BList<Port> *port_list                  = nullptr;

            if (interface_item->port_list != nullptr) {
                port_list = interface_item->port_list;
            } else if (interface_item->constant_declaration != nullptr) {
                port_list = interface_item->constant_declaration;
            } else if (interface_item->signal_declaration != nullptr) {
                port_list = interface_item->signal_declaration;
            } else if (interface_item->variable_declaration != nullptr) {
                port_list = interface_item->variable_declaration;
            } else {
                messageDebugAssert(false, "Unexpected case", nullptr, _sem);
            }

            for (BList<Port>::iterator it = port_list->begin(); it != port_list->end(); ++it) {
                Port *port_o       = *it;
                Parameter *param_o = new Parameter();

                param_o->setSourceFileName(port_o->getSourceFileName());
                param_o->setSourceLineNumber(port_o->getSourceLineNumber());
                param_o->setSourceColumnNumber(port_o->getSourceColumnNumber());

                param_o->setName(port_o->getName());
                param_o->setDirection(port_o->getDirection());
                param_o->setType(hif::copy(port_o->getType()));
                param_o->setValue(hif::copy(port_o->getValue()));
                param_o->setSourceFileName(port_o->getSourceFileName());
                param_o->setSourceLineNumber(port_o->getSourceLineNumber());

                po->parameters.push_back(param_o);
            }

            delete port_list;
            delete *d;
        }

        delete formal_parameter_list_paren_opt;
    }

    delete designator;
    return po;
}

SubProgram *VhdlParser::parse_SubprogramSpecification(
    Identifier *designator,
    list<interface_declaration_t *> *formal_parameter_list_paren_opt,
    Value *name)
{
    Function *fo = new Function();
    setCodeInfo(fo);

    fo->setName(designator->getName());

    if (formal_parameter_list_paren_opt != nullptr) {
        for (list<interface_declaration_t *>::iterator d = formal_parameter_list_paren_opt->begin();
             d != formal_parameter_list_paren_opt->end(); ++d) {
            interface_declaration_t *interface_item = *d;
            BList<Port> *port_list                  = nullptr;

            if (interface_item->port_list != nullptr) {
                port_list = interface_item->port_list;
            } else if (interface_item->constant_declaration != nullptr) {
                port_list = interface_item->constant_declaration;
            } else if (interface_item->signal_declaration != nullptr) {
                port_list = interface_item->signal_declaration;
            } else if (interface_item->variable_declaration != nullptr) {
                port_list = interface_item->variable_declaration;
            } else {
                messageDebugAssert(false, "Unexpected case", nullptr, _sem);
            }

            for (BList<Port>::iterator i = port_list->begin(); i != port_list->end(); ++i) {
                Parameter *param_o = new Parameter();
                setCodeInfo(param_o);
                param_o->setName((*i)->getName());
                param_o->setDirection((*i)->getDirection());
                param_o->setType(hif::copy((*i)->getType()));
                param_o->setValue(hif::copy((*i)->getValue()));
                param_o->setSourceFileName((*i)->getSourceFileName());
                param_o->setSourceLineNumber((*i)->getSourceLineNumber());

                fo->parameters.push_back(param_o);
            }

            delete port_list;
            delete *d;
        }

        delete formal_parameter_list_paren_opt;
    }

    Identifier *no = dynamic_cast<Identifier *>(name);
    if (no == nullptr) {
        yyerror("subprogram_specification: unsupported type name.", name);
    }
    fo->setType(_resolveType(no->getName(), nullptr, nullptr));

    delete name;
    delete designator;
    return fo;
}

TypeDef *VhdlParser::parse_SubtypeDeclaration(Value *id, subtype_indication_t *subtype_indication)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    TypeDef *ret = new TypeDef();
    setCodeInfo(ret);

    ret->setOpaque(false);
    ret->setName(identifier->getName());
    ret->setType(type_o);

    delete identifier;
    delete subtype_indication;
    return ret;
}

subtype_indication_t *VhdlParser::parse_SubtypeIndication(Value *name, constraint_t *constraint_opt)
{
    subtype_indication_t *ret = new subtype_indication_t();

    Identifier *name_o    = dynamic_cast<Identifier *>(name);
    Cast *cast_o          = dynamic_cast<Cast *>(name);
    Slice *slice_o        = dynamic_cast<Slice *>(name);
    FunctionCall *fcall_o = dynamic_cast<FunctionCall *>(name);
    FieldReference *fr_o  = dynamic_cast<FieldReference *>(name);

    if (fcall_o != nullptr) {
        std::string no1 = "range";
        std::string no2 = "reverse_range";
        messageAssert(
            (fcall_o->getName() == no1 || fcall_o->getName() == no2), "Unsupported range specification.", fcall_o,
            nullptr);
        messageAssert((constraint_opt == nullptr), "Unexpected constraint opt", name, nullptr);

        Range *range_o = new Range();
        setCodeInfo(range_o);
        range_o->setLeftBound(fcall_o);
        ret->range = range_o;

        name = nullptr; // avoid delete
    } else if (cast_o != nullptr) {
        const bool hasOp   = (cast_o->getValue() != nullptr);
        const bool hasType = (cast_o->getType() != nullptr);
        messageAssert((hasOp || hasType), "Malformed cast", cast_o, nullptr);

        Range *range_o = nullptr;
        if (hasOp) {
            // FunctionCall is the only managed Cast Op.
            // A'RANGE      is the range  A'LEFT to A'RIGHT  or  A'LEFT downto A'RIGHT .
            // A'RANGE(N)   is the range of dimension N of A.
            //
            // BUT
            //
            // It can also be something as std_locic_vector(TYPE).

            Value *vv = cast_o->setValue(nullptr);

            if (dynamic_cast<FunctionCall *>(vv) != nullptr) {
                FunctionCall *fcall = static_cast<FunctionCall *>(vv);
                // sanity check
                messageAssert(
                    (objectMatchName(fcall, "range") || objectMatchName(fcall, "reverse_range")),
                    "Unexpteced attribute mapped as FunctionCall", vv, nullptr);
                range_o = new Range();
                setCodeInfo(range_o);
                range_o->setLeftBound(vv);
            } else if (dynamic_cast<Identifier *>(vv) != nullptr) {
                // should be a typeref
                Identifier *id = static_cast<Identifier *>(vv);
                range_o        = new Range();
                setCodeInfo(range_o);
                Type *tr = _resolveType(id->getName());
                setCodeInfo(tr);
                range_o->setType(tr);
                delete id;
            } else {
                messageError("Unexpected subtype ndication.", vv, nullptr);
            }
        }

        if (hasType && hasOp) {
            ret->type = cast_o->setType(nullptr);
            hif::typeSetSpan(ret->type, range_o, _sem);
        } else if (hasType) {
            ret->type = cast_o->setType(nullptr);
        } else // if (hasOp)
        {
            ret->range = range_o;
        }
    } else if (slice_o != nullptr) {
        BList<Value> *val_list = new BList<Value>();
        val_list->push_back(slice_o->getSpan());
        Identifier *id = dynamic_cast<Identifier *>(slice_o->getPrefix());
        ret->type      = _resolveType(id->getName(), val_list);
        setCodeInfo(ret->type);
    } else if (name_o != nullptr && constraint_opt != nullptr) {
        if (constraint_opt->index_constraint != nullptr) {
            BList<Value> *val_list = reinterpret_cast<BList<Value> *>(constraint_opt->index_constraint);

            ret->type = _resolveType(name_o->getName(), val_list);
            setCodeInfo(ret->type);

            // call to _resolveType generate a leak, probably $2 should be
            // copied inside the function and delete after.
            delete constraint_opt->index_constraint;
        } else if (constraint_opt->range_constraint != nullptr) {
            ret->type = _resolveType(name_o->getName(), nullptr, constraint_opt->range_constraint);
            setCodeInfo(ret->type);
            delete constraint_opt->range_constraint;
        } else {
            messageDebugAssert(false, "Unexpected case", nullptr, _sem);
        }
    } else if (name_o != nullptr) {
        ret->type = _resolveType(name_o->getName());
        setCodeInfo(ret->type);
    } else if (fr_o != nullptr) {
        ret->type = _resolveFieldReferenceType(fr_o);
    } else {
        messageError("Unexpected subtypeIndication", name, _sem);
    }

    delete constraint_opt;
    delete name;
    return ret;
}

BList<Library> *VhdlParser::parse_UseClause(hif::BList<hif::FieldReference> *selected_name_list)
{
    BList<Library> *libraryobj_l = new BList<Library>();

    for (BList<FieldReference>::iterator i = selected_name_list->begin(); i != selected_name_list->end(); ++i) {
        FieldReference *name_o = (*i);
        Library *library_o     = resolveLibraryType(name_o);
        if (library_o == nullptr)
            continue;
        setCodeInfo(library_o);
        _factory.codeInfo(library_o, library_o->getSourceFileName(), library_o->getSourceLineNumber());

        libraryobj_l->push_back(library_o);
    }

    delete selected_name_list;
    return libraryobj_l;
}

Assign *VhdlParser::parse_VariableAssignmentStatement(Value *target, Value *expression)
{
    Assign *assign_o = new Assign();
    setCodeInfo(assign_o);

    assign_o->setLeftHandSide(target);
    assign_o->setRightHandSide(expression);

    return assign_o;
}

BList<Declaration> *VhdlParser::parse_VariableDeclaration(
    BList<Identifier> *identifier_list,
    subtype_indication_t *subtype_indication,
    Value *expression)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    BList<Declaration> *ret = new BList<Declaration>();

    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Variable *v = new Variable();
        setCodeInfo(v);
        v->setName((*i)->getName());
        v->setType(hif::copy(type_o));
        v->setValue(hif::copy(expression));

        ret->push_back(v);
    }

    delete identifier_list;
    delete type_o;
    delete subtype_indication;
    delete expression;
    return ret;
}

BList<Declaration> *
VhdlParser::parse_VariableDeclaration(BList<Identifier> *identifier_list, subtype_indication_t *subtype_indication)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    BList<Declaration> *ret = new BList<Declaration>();

    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Variable *v = new Variable();
        setCodeInfo(v);
        v->setName((*i)->getName());
        v->setType(hif::copy(type_o));

        ret->push_back(v);
    }

    delete identifier_list;
    delete type_o;
    delete subtype_indication;
    return ret;
}

BList<ParameterAssign> *VhdlParser::parse_ActualParameterPart(BList<PortAssign> *association_list)
{
    BList<ParameterAssign> *ret = new BList<ParameterAssign>();

    for (BList<PortAssign>::iterator i = association_list->begin(); i != association_list->end(); ++i) {
        ParameterAssign *param = new ParameterAssign();
        setCodeInfo(param);
        param->setValue(hif::copy((*i)->getValue()));
        param->setName((*i)->getName());
        ret->push_back(param);
    }

    delete association_list;
    return ret;
}

Alias *VhdlParser::parse_AliasDeclaration(Identifier *designator, subtype_indication_t *subtype_indication, Value *name)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    Alias *ret = new Alias();
    setCodeInfo(ret);
    ret->setType(type_o);
    ret->setName(designator->getName());
    ret->setValue(name);

    delete designator;
    delete subtype_indication;
    return ret;
}

Value *VhdlParser::parse_Allocator(subtype_indication_t *subtype_indication)
{
    messageAssert(subtype_indication != nullptr, "Expected subtype_indication", nullptr, _sem);
    messageAssert(
        subtype_indication->value == nullptr && subtype_indication->range == nullptr &&
            subtype_indication->type != nullptr,
        "Unexpected subtype_indication", nullptr, _sem);

    FunctionCall *ret = _factory.functionCall(
        "new", nullptr, (_factory.templateTypeArgument("T", subtype_indication->type)),
        _factory.noParameterArguments());

    subtype_indication->type = nullptr;
    delete subtype_indication;
    return ret;
}

Value *VhdlParser::parse_Allocator(Cast *qualified_expression)
{
    messageAssert(qualified_expression != nullptr, "Expected qualified_expression", nullptr, _sem);

    FunctionCall *ret = _factory.functionCall(
        "new", nullptr, (_factory.templateTypeArgument("T", qualified_expression->setType(nullptr))),
        (_factory.parameterArgument("param1", qualified_expression->setValue(nullptr))));

    delete qualified_expression;
    return ret;
}

PortAssign *VhdlParser::parse_AssociationElement(Value *formal_part, Value *actual_part)
{
    PortAssign *ret = parse_AssociationElement(actual_part);
    setCodeInfo(ret);
    Identifier *name_o = dynamic_cast<Identifier *>(formal_part);
    Member *memb_o     = dynamic_cast<Member *>(formal_part);
    FunctionCall *fc_o = dynamic_cast<FunctionCall *>(formal_part);
    Slice *slice       = dynamic_cast<Slice *>(formal_part);
    if (name_o != nullptr) {
        ret->setName(name_o->getName());
    } else if (memb_o != nullptr) {
        name_o = dynamic_cast<Identifier *>(memb_o->getPrefix());
        if (name_o != nullptr)
            ret->setName(name_o->getName());
        else
            messageError("Unsupported member association_element.", formal_part, _sem);

        ret->setPartialBind(memb_o->setIndex(nullptr));
    } else if (fc_o != nullptr) {
        messageAssert(fc_o->parameterAssigns.size() == 1, "Unsupported function case", fc_o, _sem);

        // Should be a member.

        ret->setName(fc_o->getName());
        ret->setPartialBind(fc_o->parameterAssigns.front()->setValue(nullptr));
    } else if (slice != nullptr) {
        name_o = dynamic_cast<Identifier *>(slice->getPrefix());
        if (name_o != nullptr)
            ret->setName(name_o->getName());
        else
            messageError("Unsupported member association_element.", formal_part, _sem);

        ret->setPartialBind(slice->setSpan(nullptr));
    } else {
        messageError("Unsupported association_element.", formal_part, _sem);
    }

    delete formal_part;
    return ret;
}

PortAssign *VhdlParser::parse_AssociationElement(Value *actual_part)
{
    PortAssign *ret = new PortAssign();
    setCodeInfo(ret);

    // if actual_part is t_Open ("Open"), we set the value of the PortAssign to nullptr
    ret->setValue(actual_part);

    return ret;
}

SwitchAlt *VhdlParser::parse_CaseStatementAlternative(BList<Value> *choices, BList<Action> *statements)
{
    SwitchAlt *ret = new SwitchAlt();
    setCodeInfoFromCurrentBlock(ret);
    ret->conditions.merge(*choices);
    ret->actions.merge(*statements);

    delete choices;
    delete statements;
    return ret;
}

AggregateAlt *VhdlParser::parse_ElementAssociation(BList<Value> *choices, Value *expression)
{
    AggregateAlt *aggregateAlt_o = this->parse_ElementAssociation(expression);
    setCodeInfo(aggregateAlt_o);
    aggregateAlt_o->indices.merge(*choices);

    delete choices;
    return aggregateAlt_o;
}

AggregateAlt *VhdlParser::parse_ElementAssociation(Value *expression)
{
    AggregateAlt *aggregateAlt_o = new AggregateAlt();
    setCodeInfo(aggregateAlt_o);

    aggregateAlt_o->setValue(expression);

    return aggregateAlt_o;
}

BList<Field> *VhdlParser::parse_ElementDeclaration(BList<Identifier> *identifier_list, Type *element_subtype_definition)
{
    BList<Field> *ret = new BList<Field>();

    for (BList<Identifier>::iterator i = identifier_list->begin(); i != identifier_list->end(); ++i) {
        Field *f = new Field();
        setCodeInfo(f);
        f->setName((*i)->getName());
        f->setType(hif::copy(element_subtype_definition));
        ret->push_back(f);
    }

    delete identifier_list;
    delete element_subtype_definition;

    return ret;
}

hif::Type *VhdlParser::parse_ElementSubtypeDefinition(subtype_indication_t *subtype_indication)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;
    delete subtype_indication;
    return type_o;
}

DesignUnit *VhdlParser::parse_EntityDeclaration(
    Value *id,
    View *entity_header,
    std::list<entity_declarative_item_t *> *entity_declarative_part)
{
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    DesignUnit *du = new DesignUnit();
    du->setName(identifier->getName());
    setCodeInfoFromCurrentBlock(du);

    entity_header->setName(identifier->getName());
    entity_header->setLanguageID(hif::rtl);
    setCodeInfoFromCurrentBlock(entity_header);

    for (std::list<entity_declarative_item_t *>::iterator i = entity_declarative_part->begin();
         i != entity_declarative_part->end(); ++i) {
        if ((*i)->constant_declaration != nullptr) {
            entity_header->declarations.merge(*(*i)->constant_declaration);
            delete (*i)->constant_declaration;
        } else if ((*i)->variable_declaration != nullptr) {
            entity_header->declarations.merge(*(*i)->variable_declaration);
            delete (*i)->variable_declaration;
        } else if ((*i)->signal_declaration != nullptr) {
            entity_header->declarations.merge(*(*i)->signal_declaration);
            delete (*i)->signal_declaration;
        } else if ((*i)->alias_declaration != nullptr) {
            entity_header->declarations.push_back((*i)->alias_declaration);
        } else if ((*i)->subtype_declaration != nullptr) {
            entity_header->declarations.push_back((*i)->subtype_declaration);
        } else if ((*i)->type_declaration != nullptr) {
            entity_header->declarations.push_back((*i)->alias_declaration);
        } else if ((*i)->type_declaration != nullptr) {
            entity_header->declarations.push_back((*i)->type_declaration);
        } else if ((*i)->subprogram_declaration != nullptr) {
            yyerror(
                "entity_declaration: unsupported declaration in "
                "entity_declarative_part",
                (*i)->subprogram_declaration);
            //delete (*i)->signal_declaration;
        } else if ((*i)->subprogram_body != nullptr) {
            yyerror(
                "entity_declaration: unsupported declaration in "
                "entity_declarative_part",
                (*i)->subprogram_body);
            //delete (*i)->signal_declaration;
        } else if ((*i)->use_clause != nullptr) {
            entity_header->libraries.merge(*(*i)->use_clause);
            delete (*i)->use_clause;
        } else if ((*i)->isSkipped) {
            // ntd
        } else {
            messageDebugAssert(false, "Unexpected case", nullptr, _sem);
        }
    }

    du->views.push_back(entity_header);

    delete id;
    delete entity_declarative_part;
    return du;
}

FunctionCall *VhdlParser::parse_AttributeName(Value *prefix, Value *nn)
{
    Identifier *name = dynamic_cast<Identifier *>(nn);
    messageAssert(name != nullptr, "Unexpected attribute name", nn, nullptr);

    FunctionCall *fco = new FunctionCall();
    setCodeInfo(fco);

    fco->setName(stringToLower(name->getName()));
    fco->setInstance(prefix);

    delete nn;
    return fco;
}

FunctionCall *VhdlParser::parse_AttributeName(Value *prefix, Value *nn, Value *expression)
{
    Identifier *name = dynamic_cast<Identifier *>(nn);
    messageAssert(name != nullptr, "Unexpected attribute name", nn, nullptr);

    FunctionCall *ret = this->parse_AttributeName(prefix, name);
    name              = nullptr;
    nn                = nullptr;

    setCodeInfo(ret);
    assert(ret != nullptr);

    ParameterAssign *pao = new ParameterAssign();
    setCodeInfo(pao);
    pao->setName("attr_parameter");
    pao->setValue(expression);
    ret->parameterAssigns.push_back(pao);

    return ret;
}

Generate *VhdlParser::parse_GenerationScheme(For *for_scheme)
{
    ForGenerate *fgo = new ForGenerate();
    setCodeInfo(fgo);

    messageAssert(!for_scheme->initDeclarations.empty(), "Missini init declarations", for_scheme, _sem);
    fgo->initDeclarations.merge(for_scheme->initDeclarations);
    fgo->setCondition(hif::copy(for_scheme->getCondition()));

    delete (for_scheme);
    return fgo;
}

Generate *VhdlParser::parse_GenerationScheme(Value *condition)
{
    IfGenerate *ifg = new IfGenerate();
    setCodeInfoFromCurrentBlock(ifg);
    ifg->setCondition(condition);

    return ifg;
}

For *VhdlParser::parse_ParameterSpecification(Value *id, Range *discrete_range)
{
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    std::string label = _name_table->getFreshName("forloop");
    BList<DataDeclaration> initD;
    BList<Action> initV;
    BList<Action> stepAc;
    BList<Action> forAct;

    Variable *var = _factory.variable(_factory.integer(), identifier->getName(), nullptr);
    setCodeInfo(var);
    initD.push_back(var);

    const bool isTyped = discrete_range->getRightBound() == nullptr;
    const bool isReverse =
        (dynamic_cast<FunctionCall *>(discrete_range->getLeftBound()) &&
         static_cast<FunctionCall *>(discrete_range->getLeftBound())->getName() == "reverse_range");

    Operator stepOp = (discrete_range->getDirection() == dir_upto && !isTyped) ? op_plus : op_minus;
    if (isReverse)
        stepOp = stepOp == op_plus ? op_minus : op_plus;

    Expression *expr = _factory.expression(identifier, stepOp, _factory.intval(1));
    setCodeInfo(expr);
    Assign *assign_o = _factory.assignment(hif::copy(identifier), expr);
    setCodeInfo(assign_o);

    stepAc.push_back(assign_o);

    For *for_ob = _factory.forLoop(label, initD, initV, discrete_range, stepAc, forAct);
    setCodeInfo(for_ob);

    return for_ob;
}

BList<Port> *VhdlParser::parse_PortList(list<interface_declaration_t *> *interface_list)
{
    BList<Port> *ret = new BList<Port>();

    for (list<interface_declaration_t *>::iterator d = interface_list->begin(); d != interface_list->end(); ++d) {
        interface_declaration_t *interface_item = *d;

        if (interface_item->port_list != nullptr) {
            ret->merge(*interface_item->port_list);
            delete interface_item->port_list;
        } else if (interface_item->constant_declaration != nullptr) {
            ret->merge(*interface_item->constant_declaration);
            delete interface_item->constant_declaration;
        } else if (interface_item->signal_declaration != nullptr) {
            ret->merge(*interface_item->signal_declaration);
            delete interface_item->signal_declaration;
        } else if (interface_item->variable_declaration != nullptr) {
            ret->merge(*interface_item->variable_declaration);
            delete interface_item->variable_declaration;
        } else {
            messageDebugAssert(false, "Unexpected case", nullptr, _sem);
        }

        delete interface_item;
    }

    delete interface_list;
    return ret;
}

Value *VhdlParser::parse_Primary(Aggregate *aggregate)
{
    if (aggregate->alts.size() == 1) {
        //
        //  t_LeftParen expression t_RightParen
        //  ( Same syntax of aggregate )
        //  This is not an aggregate !
        //
        AggregateAlt *alt = aggregate->alts.front();
        setCodeInfo(alt);

        if (alt->indices.empty()) {
            Value *ret = hif::copy(alt->getValue());
            setCodeInfo(ret);
            delete aggregate;
            return ret;
        }
    }

    // otherwise, return the aggregate
    return aggregate;
}

BList<TPAssign> *VhdlParser::parse_GenericMapAspect(BList<PortAssign> *association_list)
{
    BList<TPAssign> *ret = new BList<TPAssign>();

    for (BList<PortAssign>::iterator i = association_list->begin(); i != association_list->end(); ++i) {
        ValueTPAssign *v = new ValueTPAssign();
        setCodeInfo(v);
        v->setName((*i)->getName());
        v->setValue(hif::copy((*i)->getValue()));
        ret->push_back(v);
    }

    delete association_list;
    return ret;
}

ViewReference *VhdlParser::parse_HdlModuleName(Value *entityIdentifier, Value *viewIdentifier)
{
    Identifier *entityName = dynamic_cast<Identifier *>(entityIdentifier);
    if (entityName == nullptr)
        messageError("Unexpected entity identifier", entityName, nullptr);

    ViewReference *ret = new ViewReference();
    ret->setDesignUnit(entityName->getName());

    if (viewIdentifier != nullptr) {
        Identifier *viewName = dynamic_cast<Identifier *>(viewIdentifier);
        if (viewName == nullptr)
            messageError("Unexpected view identifier", viewName, nullptr);

        ret->setName(viewName->getName());
    }

    setCodeInfo(ret);
    return ret;
}

hif::Type *VhdlParser::parse_HdlVariableType(subtype_indication_t *subtype_indication)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;
    delete subtype_indication;
    return type_o;
}

BList<Declaration> *VhdlParser::parse_GenericClause(list<interface_declaration_t *> *generic_list)
{
    BList<Declaration> *declobj_l = new BList<Declaration>();

    for (list<interface_declaration_t *>::iterator d = generic_list->begin(); d != generic_list->end(); ++d) {
        interface_declaration_t *interface_decl = *d;

        if (interface_decl->port_list != nullptr) {
            for (BList<Port>::iterator i = interface_decl->port_list->begin(); i != interface_decl->port_list->end();
                 ++i) {
                ValueTP *valuetp_o = new ValueTP();
                setCodeInfo(valuetp_o);

                valuetp_o->setSourceFileName((*i)->getSourceFileName());
                valuetp_o->setSourceLineNumber((*i)->getSourceLineNumber());

                valuetp_o->setName((*i)->getName());
                valuetp_o->setType(hif::copy((*i)->getType()));
                valuetp_o->setValue(hif::copy((*i)->getValue()));

                declobj_l->push_back(valuetp_o);
            }
            delete interface_decl->port_list;
        } else if (interface_decl->constant_declaration != nullptr) {
            // do nothing
            delete interface_decl->constant_declaration;
        } else if (interface_decl->signal_declaration != nullptr) {
            // do nothing
            delete interface_decl->signal_declaration;
        } else if (interface_decl->variable_declaration != nullptr) {
            // do nothing
            delete interface_decl->variable_declaration;
        } else {
            messageDebugAssert(false, "Unexpected case", nullptr, _sem);
        }

        delete interface_decl;
    }

    delete generic_list;

    return declobj_l;
}

component_configuration_t *VhdlParser::parse_ComponentConfiguration(
    component_specification_t *component_specification,
    binding_indication_t *binding_indication_opt,
    block_configuration_t *block_configuration_opt)
{
    component_configuration_t *ret = new component_configuration_t();

    if (block_configuration_opt != nullptr) {
        yyerror("component_configuration: t_FOR component_specification "
                "binding_indication_semicolon_opt block_configuration_opt "
                "t_END t_FOR t_Semicolon. 'block_configuration_opt' is not "
                "supported");
    }

    if (binding_indication_opt != nullptr) {
        ret->component_specification = component_specification;
        ret->binding_indication      = binding_indication_opt;
    } else {
        // ** Catched by the grammar **
        // Useful for future implementation
        yyerror("binding_indication_semicolon_opt: empty is not supported.");
    }

    return ret;
}

DesignUnit *
VhdlParser::parse_ComponentDeclaration(Value *id, BList<Declaration> *generic_clause_opt, BList<Port> *port_clause_opt)
{
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    DesignUnit *designUnit_o = new DesignUnit();
    setCodeInfoFromCurrentBlock(designUnit_o);
    View *view_o = new View();
    setCodeInfoFromCurrentBlock(view_o);
    view_o->setStandard(true);
    Entity *iface_o = new Entity();
    setCodeInfoFromCurrentBlock(iface_o);
    //Contents *contents_o = new Contents();
    //setBlockCodeInfo(contents_o);

    designUnit_o->setName(identifier->getName());

    if (generic_clause_opt != nullptr) {
        for (BList<Declaration>::iterator i = generic_clause_opt->begin(); i != generic_clause_opt->end(); ++i) {
            ValueTP *valuetp_o = dynamic_cast<ValueTP *>(*i);
            if (valuetp_o == nullptr)
                continue;

            view_o->templateParameters.push_back(hif::copy(valuetp_o));
        }
        delete generic_clause_opt;
    }

    if (port_clause_opt != nullptr) {
        iface_o->ports.merge(*port_clause_opt);
        delete port_clause_opt;
    }

    view_o->setEntity(iface_o);
    // Any name here is fine. It will overwritten by following fixes.
    view_o->setName(_name_table->registerName("behav"));
    //contents_o->setName(view_o->getName());
    //view_o->setContents( contents_o );
    //view_o->setStandard(false);
    view_o->setLanguageID(hif::rtl);

    designUnit_o->views.push_back(view_o);

    delete identifier;
    return designUnit_o;
}

hif::Instance *VhdlParser::parse_ComponentInstantiationStatement(
    hif::Value *id,
    hif::ViewReference *instantiated_unit,
    hif::BList<hif::TPAssign> *generic_map_aspect_opt,
    hif::BList<hif::PortAssign> *port_map_aspect_opt)
{
    Identifier *identifier = dynamic_cast<Identifier *>(id);
    messageAssert(identifier != nullptr, "Expected identifier", id, _sem);

    Instance *instance_o = new Instance();
    setCodeInfoFromCurrentBlock(instance_o);

    instance_o->setName(identifier->getName());
    instance_o->setReferencedType(instantiated_unit);

    if (generic_map_aspect_opt != nullptr) {
        ViewReference *viewref_o = dynamic_cast<ViewReference *>(instance_o->getReferencedType());
        setCodeInfoFromCurrentBlock(viewref_o);
        viewref_o->templateParameterAssigns.merge(*generic_map_aspect_opt);
        delete generic_map_aspect_opt;
    }

    if (port_map_aspect_opt != nullptr) {
        instance_o->portAssigns.merge(*port_map_aspect_opt);
        delete port_map_aspect_opt;
    }

    delete identifier;
    return instance_o;
}

Array *VhdlParser::parse_UnconstrainedArrayDefinition(
    BList<Range> *index_subtype_definition_list,
    subtype_indication_t *subtype_indication)
{
    messageAssert(
        subtype_indication->type != nullptr, "Unexpected subtype indication",
        subtype_indication->value != nullptr ? static_cast<Object *>(subtype_indication->value)
                                             : static_cast<Object *>(subtype_indication->range),
        nullptr);
    Type *type_o = subtype_indication->type;

    Array *array_o = new Array();
    setCodeInfo(array_o);
    Array *currentArray = array_o;
    for (BList<Range>::iterator i = index_subtype_definition_list->begin();
         i != index_subtype_definition_list->end();) {
        Range *r = *i;
        i        = i.remove();
        currentArray->setSpan(r);
        if (index_subtype_definition_list->empty()) {
            currentArray->setType(type_o);
        } else {
            Array *a = new Array();
            setCodeInfo(a);
            currentArray->setType(a);
            currentArray = a;
        }
    }

    delete subtype_indication;
    delete index_subtype_definition_list;
    return array_o;
}

Record *VhdlParser::parse_RecordTypeDefinition(BList<Field> *element_declaration_list)
{
    Record *ret = new Record();
    setCodeInfo(ret);
    ret->fields.merge(*element_declaration_list);

    delete element_declaration_list;
    return ret;
}

Return *VhdlParser::parse_ReturnStatement(Value *expression_opt)
{
    Return *ret = new Return();
    setCodeInfo(ret);
    ret->setValue(expression_opt);

    return ret;
}

Type *VhdlParser::parse_ScalarTypeDefinition(BList<EnumValue> *enumeration_type_definition)
{
    Enum *e = new Enum();
    setCodeInfo(e);
    e->values.merge(*enumeration_type_definition);

    delete enumeration_type_definition;
    return e;
}

Cast *VhdlParser::parse_QualifiedExpression(Value *name, Aggregate *aggregate)
{
    Identifier *no = dynamic_cast<Identifier *>(name);

    if (no == nullptr) {
        yyerror("qualified_expression: unsupported type name.", name);
    }

    Cast *ret = new Cast();
    setCodeInfo(ret);
    ret->setType(_resolveType(no->getName(), nullptr, nullptr));
    messageAssert(ret->getType() != nullptr, "Cannot resolve type", no, _sem);

    if (aggregate->alts.size() == 1) {
        //
        //  t_LeftParen expression t_RightParen
        //  ( Same syntax of aggregate )
        //  This is not an aggregate !
        //
        AggregateAlt *alt = aggregate->alts.front();
        if (alt->indices.empty()) {
            Value *val = hif::copy(alt->getValue());
            setCodeInfo(val);
            delete aggregate;
            ret->setValue(val);
        }
    } else {
        ret->setValue(aggregate);
    }

    delete name;
    return ret;
}

Wait *VhdlParser::parse_WaitStatement(
    BList<Value> *sensitivity_clause_opt,
    Value *condition_clause_opt,
    Value *timeout_clause_opt)
{
    Wait *ret = new Wait();
    setCodeInfo(ret);

    if (sensitivity_clause_opt != nullptr) {
        ret->sensitivity.merge(*sensitivity_clause_opt);
        delete sensitivity_clause_opt;
    }

    if (condition_clause_opt != nullptr) {
        ret->setCondition(condition_clause_opt);
    }

    ret->setTime(timeout_clause_opt);

    return ret;
}

Assign *VhdlParser::parse_WaveformElement(Value *expression, Value *afterExpression)
{
    // Return an assign without target that will be merged in parent rule.
    Assign *a = new Assign();
    setCodeInfo(a);
    a->setRightHandSide(expression);
    a->setDelay(afterExpression);

    return a;
}

Type *VhdlParser::resolveType(
    string type_ref,
    hif::BList<Value> *opt_arg,
    Range *ro,
    semantics::ILanguageSemantics *sem,
    bool mandatory)
{
    type_ref = stringToLower(type_ref.c_str());
    if (type_ref.compare("bit") == 0) {
        Bit *bit_o = new Bit();
        bit_o->setLogic(false);
        bit_o->setResolved(false);
        return bit_o;
    } else if (type_ref.compare("integer") == 0) {
        Int *io = new Int();
        io->setSigned(true);

        IntValue *lbound_o = nullptr;
        IntValue *rbound_o = nullptr;

        if (ro != nullptr) {
            io->setSpan(hif::copy(ro));
        } else if (ro == nullptr) {
            Range *nr = new Range(-2147483647LL, 2147483647LL);
            io->setSpan(nr);
        }

        if (ro == nullptr || (lbound_o && lbound_o->getValue() < 0) || (rbound_o && rbound_o->getValue() < 0)) {
            io->setSigned(true);
        }

        if (lbound_o != nullptr)
            delete lbound_o;
        if (rbound_o != nullptr)
            delete rbound_o;

        return io;
    } else if (type_ref.compare("natural") == 0) {
        Int *int_o = new Int();
        int_o->setSigned(false);

        if (opt_arg && !opt_arg->empty()) {
            for (BList<Value>::iterator i = opt_arg->begin(); i != opt_arg->end(); ++i) {
                if (dynamic_cast<Range *>(*i)) {
                    int_o->setSpan(hif::copy(static_cast<Range *>((*i))));
                }
            }
        } else if (ro) {
            // for backward compatibility
            int_o->setSpan(hif::copy(ro));
        } else if (ro == nullptr) {
            Range *nr = new Range(0, 2147483647LL);
            int_o->setSpan(nr);
        }

        return int_o;
    } else if (type_ref.compare("positive") == 0) {
        Int *int_o = new Int();
        int_o->setSigned(false);

        if (opt_arg && !opt_arg->empty()) {
            for (BList<Value>::iterator i = opt_arg->begin(); i != opt_arg->end(); ++i) {
                if (dynamic_cast<Range *>(*i)) {
                    int_o->setSpan(hif::copy(static_cast<Range *>(*i)));
                }
            }
        } else if (ro) {
            // for backward compatibility
            int_o->setSpan(hif::copy(ro));
        } else if (ro == nullptr) {
            Range *nr = new Range(1, 2147483647LL);
            int_o->setSpan(nr);
        }

        return int_o;
    } else if (type_ref.compare("boolean") == 0) {
        Bool *b = new Bool();
        return b;
    } else if (type_ref.compare("time") == 0) {
        Time *tm = new Time();
        return tm;
    } else if (type_ref.compare("bit_vector") == 0) {
        Bitvector *array_o = new Bitvector();
        array_o->setLogic(false);
        array_o->setResolved(false);

        if (opt_arg != nullptr) {
            if (!opt_arg->empty() && dynamic_cast<Range *>(opt_arg->front())) {
                array_o->setSpan(static_cast<Range *>(hif::copy(opt_arg->front())));
                opt_arg->begin().erase();
            } else {
                Range *range = new Range();
                if (opt_arg->empty()) {
                    // Range is typed.
                } else {
                    range->setDirection(dir_upto);
                    range->setLeftBound(new IntValue(1));
                    range->setRightBound(hif::copy(opt_arg->front()));
                    opt_arg->begin().erase();
                }
                array_o->setSpan(range);
            }

            if (opt_arg->empty()) {
                array_o->setLogic(false);
                array_o->setResolved(false);
            } else {
                //opt_arg->begin().erase();
                Array *arr = new Array();
                arr->setSpan(array_o->setSpan(nullptr));
                arr->setSigned(array_o->isSigned());
                arr->setType(resolveType(type_ref, opt_arg, ro, sem, mandatory));
                return arr;
            }
        } else {
            array_o->setSpan(new Range());
            // Range is typed.
        }
        return array_o;
    } else if (type_ref.compare("std_logic_vector") == 0) {
        Bitvector *array_o = new Bitvector();
        array_o->setLogic(true);
        array_o->setResolved(true);

        if (opt_arg) {
            if (!opt_arg->empty() && dynamic_cast<Range *>(opt_arg->front())) {
                array_o->setSpan(static_cast<Range *>(hif::copy(opt_arg->front())));
                opt_arg->begin().erase();
            } else {
                Range *range = new Range();
                if (opt_arg->empty()) {
                    // Range is typed.
                } else {
                    range->setDirection(dir_upto);
                    range->setLeftBound(new IntValue(1));
                    range->setRightBound(hif::copy(opt_arg->front()));
                    opt_arg->begin().erase();
                }
                array_o->setSpan(range);
            }

            if (opt_arg->empty()) {
                array_o->setLogic(true);
                array_o->setResolved(true);
            } else {
                //opt_arg->begin().erase();
                Array *arr = new Array();
                arr->setSpan(array_o->setSpan(nullptr));
                arr->setSigned(array_o->isSigned());
                arr->setType(resolveType(type_ref, opt_arg, ro, sem, mandatory));
                return arr;
            }
        } else {
            array_o->setSpan(new Range());
            // Range is typed.
        }
        return array_o;
    } else if (type_ref.compare("std_ulogic_vector") == 0) {
        Bitvector *array_o = new Bitvector();
        array_o->setLogic(true);
        array_o->setResolved(false);

        if (opt_arg) {
            if (!opt_arg->empty() && dynamic_cast<Range *>(opt_arg->front())) {
                array_o->setSpan(static_cast<Range *>(hif::copy(opt_arg->front())));
                opt_arg->begin().erase();
            } else {
                Range *range = new Range();
                if (opt_arg->empty()) {
                    // Range is typed.
                } else {
                    range->setDirection(dir_upto);
                    range->setLeftBound(new IntValue(1));
                    range->setRightBound(hif::copy(opt_arg->front()));
                    opt_arg->begin().erase();
                }
                array_o->setSpan(range);
            }

            if (opt_arg->empty()) {
                array_o->setLogic(true);
                array_o->setResolved(false);
            } else {
                //opt_arg->begin().erase();
                Array *arr = new Array();
                arr->setSpan(array_o->setSpan(nullptr));
                arr->setSigned(array_o->isSigned());
                arr->setType(resolveType(type_ref, opt_arg, ro, sem, mandatory));
                return arr;
            }
        } else {
            array_o->setSpan(new Range());
            // Range is typed.
        }
        return array_o;
    } else if (type_ref.compare("std_logic") == 0) {
        Bit *bit_o = new Bit();

        bit_o->setLogic(true);
        bit_o->setResolved(true);
        return bit_o;
    } else if (type_ref.compare("std_ulogic") == 0) {
        Bit *bit_o = new Bit();

        bit_o->setLogic(true);
        bit_o->setResolved(false);
        return bit_o;
    } else if (type_ref.compare("unsigned") == 0) {
        Unsigned *uo = new Unsigned();

        if (opt_arg && !opt_arg->empty()) {
            for (BList<Value>::iterator i = opt_arg->begin(); i != opt_arg->end(); ++i) {
                if (dynamic_cast<Range *>(*i)) {
                    messageDebugAssert(uo->getSpan() == nullptr, "Unexpected span set", uo, sem);
                    uo->setSpan(hif::copy(static_cast<Range *>(*i)));
                }
            }
        } else {
            Range *r = new Range();
            // Range is typed.
            uo->setSpan(r);
        }

        return uo;
    } else if (type_ref.compare("signed") == 0) {
        Signed *so = new Signed();

        if (opt_arg && !opt_arg->empty()) {
            for (BList<Value>::iterator i = opt_arg->begin(); i != opt_arg->end(); ++i) {
                if (dynamic_cast<Range *>(*i)) {
                    messageDebugAssert(so->getSpan() == nullptr, "Unexpected span set", so, sem);
                    so->setSpan(hif::copy(static_cast<Range *>(*i)));
                }
            }
        } else {
            Range *r = new Range();
            // Range is typed.
            so->setSpan(r);
        }

        return so;
    } else if (type_ref.compare("string") == 0) {
        String *s_o = new String();

        if (opt_arg && !opt_arg->empty()) {
            for (BList<Value>::iterator i = opt_arg->begin(); i != opt_arg->end(); ++i) {
                if (dynamic_cast<Range *>(*i)) {
                    messageDebugAssert(s_o->getSpanInformation() == nullptr, "Unexpected span set", s_o, sem);
                    s_o->setSpanInformation(hif::copy(static_cast<Range *>(*i)));
                }
            }
        } else {
            Range *r = new Range();
            r->setDirection(dir_upto); // this is the string default in vhdl
            r->setLeftBound(new IntValue(1));
            // Range is typed.
            s_o->setSpanInformation(r);
        }

        return s_o;
    } else if (type_ref.compare("x01") == 0) {
        Bit *bit_o = new Bit();

        bit_o->setLogic(true);
        bit_o->setResolved(true);
        return bit_o;
    } else if (type_ref.compare("character") == 0) {
        Char *c_o = new Char();
        return c_o;
    } else if (type_ref.compare("real") == 0) {
        Real *r_o = new Real();

        // TODO: manage options

        Range *r = new Range();
        r->setLeftBound(new IntValue(63));
        r->setDirection(dir_downto);
        r->setRightBound(new IntValue(0));
        r_o->setSpan(r);

        return r_o;
    } else if (mandatory) {
        TypeReference *typeref_o = new TypeReference();
        typeref_o->setName(type_ref.c_str());

        if (opt_arg) {
            for (BList<Value>::iterator i = opt_arg->begin(); i != opt_arg->end(); ++i) {
                Range *range_o = dynamic_cast<Range *>(hif::copy(*i));
                if (range_o != nullptr) {
                    typeref_o->ranges.push_back(range_o);
                }
            }
        }
        return typeref_o;
    }

    return nullptr;
}

Range *VhdlParser::parse_DiscreteRange(subtype_indication_t *subtype_indication)
{
    messageAssert(
        (subtype_indication->range != nullptr || subtype_indication->type != nullptr), "Unexpected subtype indication",
        subtype_indication->value, nullptr);

    Range *range_o = subtype_indication->range;
    if (range_o == nullptr) {
        if (dynamic_cast<TypeReference *>(subtype_indication->type) != nullptr) {
            // Create a typed range
            TypeReference *tr = static_cast<TypeReference *>(subtype_indication->type);
            range_o           = new Range();
            range_o->setType(hif::copy(tr));
        } else {
            range_o = hif::copy(hif::typeGetSpan(subtype_indication->type, _sem));
            messageAssert(range_o != nullptr, "Unable to calculate type span.", subtype_indication->type, _sem);
        }
        delete subtype_indication->type;
    }

    delete subtype_indication;
    return range_o;
}

Value *VhdlParser::parse_DiscreteRange(Value *expression)
{
    Value *ret = nullptr;

    FunctionCall *fco = dynamic_cast<FunctionCall *>(expression);

    std::string no1 = "range";
    std::string no2 = "reverse_range";

    if (fco != nullptr && (fco->getName() == no1 || fco->getName() == no2)) {
        Range *range_o = new Range();
        range_o->setLeftBound(fco);
        ret = range_o;
    } else {
        ret = expression;
    }

    setCodeInfo(ret);
    return ret;
}

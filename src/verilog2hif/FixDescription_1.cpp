/// @file FixDescription_1.cpp
/// @brief
/// Copyright (c) 2024-2025, Electronic Systems Design (ESD) Group,
/// Univeristy of Verona.
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#include <algorithm>

#include <hif/hif.hpp>

#include "verilog2hif/post_parsing_methods.hpp"
#include "verilog2hif/support.hpp"

// ///////////////////////////////////////////////////////////////////
// Fix Description 1 visitor
// ///////////////////////////////////////////////////////////////////

class FixDescription_1 : public hif::GuideVisitor
{
public:
    typedef std::set<hif::Declaration *> DeclarationSet;
    typedef std::set<hif::Object *> ObjectSet;
    typedef std::map<hif::Variable *, ObjectSet> GenVars;
    typedef std::list<hif::Variable *> GenVarsSeq;

    FixDescription_1(hif::semantics::ILanguageSemantics *sem);
    virtual ~FixDescription_1();
    int AfterVisit(hif::Object &o);

    int visitAlias(hif::Alias &o);
    int visitArray(hif::Array &o);
    int visitBitValue(hif::BitValue &o);
    int visitBitvectorValue(hif::BitvectorValue &o);
    int visitConst(hif::Const &o);
    int visitContents(hif::Contents &o);
    int visitForGenerate(hif::ForGenerate &o);
    int visitFunctionCall(hif::FunctionCall &o);
    int visitGlobalAction(hif::GlobalAction &o);
    int visitIdentifier(hif::Identifier &o);
    int visitIfGenerate(hif::IfGenerate &o);
    int visitInstance(hif::Instance &o);
    int visitLibraryDef(hif::LibraryDef &o);
    int visitPort(hif::Port &o);
    int visitProcedure(hif::Procedure &o);
    int visitProcedureCall(hif::ProcedureCall &o);
    int visitRange(hif::Range &o);
    int visitReal(hif::Real &o);
    int visitSignal(hif::Signal &o);
    int visitStateTable(hif::StateTable &o);
    int visitTypeReference(hif::TypeReference &o);
    int visitValueTP(hif::ValueTP &o);
    int visitVariable(hif::Variable &o);
    int visitView(hif::View &o);
    int visitViewReference(hif::ViewReference &o);
    int visitWhenAlt(hif::WhenAlt &o);
    int visitWhile(hif::While &o);

private:
    FixDescription_1(const FixDescription_1 &);
    FixDescription_1 operator=(const FixDescription_1 &);

    void _fixParameterNames(hif::Object *call, hif::BList<hif::ParameterAssign> &actuals);

    /// @brief Reference to AMS types are modeled as hif::TypeReference at parsing time.
    /// Here refining them as hif::ViewReference.
    bool _fixAMSDisciplines(hif::TypeReference *o);

    bool _isConstantExpr(hif::Value *v);

    void _fixAllSignalsSesitivity(hif::StateTable *o);
    void _fixProcessesWithWait(hif::StateTable *o);

    void _fixCollectedGenVarVariables();
    bool _fixGenVarVariable(hif::Variable *o);
    hif::Action *_getTopParent(hif::Action *a1, hif::Action *a2);

    void _fixMissingDeclarationType(hif::DataDeclaration *decl);
    void _fixMissingPortType(hif::Port *o, const hif::semantics::ReferencesSet &refs);
    void _fixMissingPortDir(hif::Port *o, const hif::semantics::ReferencesSet &refs);

    /// @brief Replace a system_task_enable with its correspondent in Verilog
    /// standard library (e.g., $display --> _system_display).
    template <typename T> void _fixSystemTaskCalls(T *call);
    bool _fixiteratedConcat(hif::FunctionCall *o, const bool aggressive);

    bool _fixLocalParam(hif::Identifier *o);
    bool _fixImplicitDeclaredNets(hif::Identifier *o);

    hif::semantics::ILanguageSemantics *_sem;
    hif::HifFactory _factory;
    DeclarationSet _declSet;
    hif::Trash _trash;
    hif::Trash _finalTrash;
    GenVars _genVars;
    GenVarsSeq _genVarsSeq;
    bool _insideStandard;
};

FixDescription_1::FixDescription_1(hif::semantics::ILanguageSemantics *sem)
    : _sem(sem)
    , _factory(sem)
    , _declSet()
    , _trash()
    , _finalTrash()
    , _genVars()
    , _genVarsSeq()
    , _insideStandard(false)
{
    hif::application_utils::initializeLogHeader("VERILOG2HIF", "FixDescription_1");
}

FixDescription_1::~FixDescription_1()
{
    _trash.clear();
    _finalTrash.clear();
    _fixCollectedGenVarVariables();
    hif::application_utils::restoreLogHeader();
}

int FixDescription_1::AfterVisit(hif::Object &o)
{
    _trash.clear(&o);
    return 0;
}

int FixDescription_1::visitAlias(hif::Alias &o)
{
    GuideVisitor::visitAlias(o);
    messageAssert(o.getType() == nullptr, "Unexpected alias.", &o, _sem);
    auto fc = dynamic_cast<hif::FunctionCall *>(o.getValue());
    messageAssert(fc != nullptr && fc->getName() == "vams_branch", "Unexpected alias.", &o, _sem);
    auto t = hif::semantics::getSemanticType(o.getValue(), _sem);
    messageAssert(t != nullptr, "Cannot type alias initial value.", &o, _sem);
    o.setType(hif::copy(t));

    return 0;
}

int FixDescription_1::visitArray(hif::Array &o)
{
    GuideVisitor::visitArray(o);
    auto subType = dynamic_cast<hif::Bit *>(hif::semantics::getBaseType(o.getType(), false, _sem));
    if (subType == nullptr)
        return 0;
    // Transforming arrays of bits to bitvectors
    auto bv = new hif::Bitvector();
    bv->setSpan(o.getSpan());
    bv->setSigned(o.isSigned());
    bv->setLogic(subType->isLogic());
    bv->setResolved(subType->isResolved());
    o.replace(bv);
    delete &o;
    return 0;
}

int FixDescription_1::visitBitValue(hif::BitValue &o)
{
    GuideVisitor::visitBitValue(o);
    if (o.getValue() == hif::bit_dontcare)
        o.setValue(hif::bit_z);
    return 0;
}

int FixDescription_1::visitBitvectorValue(hif::BitvectorValue &o)
{
    GuideVisitor::visitBitvectorValue(o);

    std::string s = o.getValue();
    for (std::string::size_type i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c != '-')
            continue;
        s[i] = 'Z';
    }

    o.setValue(s);
    return 0;
}

int FixDescription_1::visitIdentifier(hif::Identifier &o)
{
    GuideVisitor::visitIdentifier(o);

    // Verilog allows implicit declarations for nets.
    if (hif::semantics::getDeclaration(&o, _sem) != nullptr)
        return 0;

    if (_fixLocalParam(&o))
        return 0;
    if (_fixImplicitDeclaredNets(&o))
        return 0;

    return 0;
}

int FixDescription_1::visitIfGenerate(hif::IfGenerate &o)
{
    if (o.getGlobalAction())
        o.getGlobalAction()->acceptVisitor(*this);

    GuideVisitor::visitIfGenerate(o);

    return 0;
}

int FixDescription_1::visitConst(hif::Const &o)
{
    GuideVisitor::visitConst(o);
    if (o.getType() != nullptr)
        return 0;

    // This means initial value was not an actual constant (may be hif::Identifier, or
    // hif::Expression, etc. involving other Const)

    auto t = hif::semantics::getSemanticType(o.getValue(), _sem);
    o.setType(hif::copy(t));

    return 0;
}

int FixDescription_1::visitContents(hif::Contents &o)
{
    // fix AMS double declarations
    for (auto itr1 = o.declarations.begin(); itr1 != o.declarations.end(); ++itr1) {

        auto sig1 = dynamic_cast<hif::Signal *>(*itr1);

        if (sig1 == nullptr)
            continue;

        for (auto itr2 = ++itr1; itr2 != o.declarations.end();) {

            auto sig2 = dynamic_cast<hif::Signal *>(*itr2);

            if (sig2 == nullptr) {
                ++itr2;
                continue;
            }

            if (sig1->getName() != sig2->getName()) {
                ++itr2;
                continue;
            }

            auto bit1 = dynamic_cast<hif::Bit *>(sig1->getType());
            auto bit2 = dynamic_cast<hif::Bit *>(sig2->getType());

            auto tr1 = dynamic_cast<hif::TypeReference *>(sig1->getType());
            auto tr2 = dynamic_cast<hif::TypeReference *>(sig2->getType());

            const bool logic1 = tr1 != nullptr && tr1->getName() == "logic";
            const bool logic2 = tr2 != nullptr && tr2->getName() == "logic";

            const bool ground1 = tr1 != nullptr && tr1->getName() == "ground";
            const bool ground2 = tr2 != nullptr && tr2->getName() == "ground";

            if (ground1 || ground2) {
                // managed later
                ++itr2;
                continue;
            }

            const bool type1 = bit1 != nullptr || logic1;
            const bool type2 = bit2 != nullptr || logic2;
            messageAssert(type1 && type2, "Case not supported yet", sig2, _sem);

            if (bit1 != nullptr)
                delete sig1->setType(sig2->setType(nullptr));

            messageAssert(sig1->getValue() == nullptr || sig2->getValue() == nullptr, "Unexpected case", sig1, _sem);
            if (sig1->getValue() == nullptr)
                sig1->setValue(sig2->setValue(nullptr));
            itr2 = itr2.erase();
        }
    }

    if (o.getGlobalAction())
        o.getGlobalAction()->acceptVisitor(*this);

    GuideVisitor::visitContents(o);

    return 0;
}

int FixDescription_1::visitFunctionCall(hif::FunctionCall &o)
{
    GuideVisitor::visitFunctionCall(o);

    _fixSystemTaskCalls(&o);
    _fixParameterNames(&o, o.parameterAssigns);
    if (_fixiteratedConcat(&o, false))
        return 0;

    return 0;
}

int FixDescription_1::visitGlobalAction(hif::GlobalAction &o)
{
    GuideVisitor::visitGlobalAction(o);

    for (auto itr = o.actions.begin(); itr != o.actions.end();) {
        auto a = dynamic_cast<hif::Assign *>(*itr);
        messageAssert(a != nullptr, "Unexpected case", &o, _sem);

        if (!_isConstantExpr(a->getRightHandSide())) {
            ++itr;
            continue;
        }

        auto id = dynamic_cast<hif::Identifier *>(a->getLeftHandSide());
        if (id == nullptr) {
            ++itr;
            continue;
        }

        auto decl = hif::semantics::getDeclaration(id, _sem);
        messageAssert(decl != nullptr, "hif::Declaration not found", id, _sem);

        delete decl->setValue(a->setRightHandSide(nullptr));
        itr = itr.erase();
    }

    return 0;
}

int FixDescription_1::visitPort(hif::Port &o)
{
    auto v = hif::getNearestParent<hif::View>(&o);
    hif::semantics::ReferencesSet refs;
    hif::semantics::getReferences(&o, refs, _sem, v);

    _fixMissingPortType(&o, refs);
    _fixMissingPortDir(&o, refs);

    GuideVisitor::visitPort(o);

    if (!o.checkProperty(IS_VARIABLE_TYPE))
        return 0;

    o.removeProperty(IS_VARIABLE_TYPE);
    messageDebugAssert(o.getValue() == nullptr, "Unexpected port initial value", &o, _sem);
    delete o.setValue(_sem->getTypeDefaultValue(o.getType(), nullptr));

    return 0;
}

int FixDescription_1::visitProcedure(hif::Procedure &o)
{
    GuideVisitor::visitProcedure(o);

    // Fix non automatic task declaration
    if (!o.checkProperty(PROPERTY_TASK_NOT_AUTOMATIC))
        return 0;
    o.removeProperty(PROPERTY_TASK_NOT_AUTOMATIC);
    messageAssert(o.getStateTable() != nullptr && o.isInBList(), "Unexpected procedure", &o, _sem);

    // move declarations to parent module scope
    auto c = dynamic_cast<hif::Contents *>(o.getParent());
    messageAssert(c != nullptr, "Unexpected non-contents task parent", o.getParent(), _sem);
    messageAssert(
        o.getBList() == reinterpret_cast<hif::BList<hif::Object> *>(&(c->declarations)),
        "Unexpected position of procedure w.r.t. parent contents", &o, _sem);
    auto taskPos = c->declarations.getPosition(&o);

    for (auto itr = o.getStateTable()->declarations.rbegin(); itr != o.getStateTable()->declarations.rend();
         itr      = o.getStateTable()->declarations.rbegin()) {
        auto *d = *itr;
        hif::manipulation::moveDeclaration(d, c, &o, _sem, "", taskPos);
    }
    return 0;
}

int FixDescription_1::visitProcedureCall(hif::ProcedureCall &o)
{
    GuideVisitor::visitProcedureCall(o);

    _fixSystemTaskCalls(&o);
    _fixParameterNames(&o, o.parameterAssigns);

    return 0;
}

int FixDescription_1::visitRange(hif::Range &o)
{
    GuideVisitor::visitRange(o);

    if (_insideStandard)
        return 0;

    hif::semantics::updateDeclarations(&o, _sem);
    hif::Expression *e = _factory.expression(
        _factory.expression(hif::copy(o.getLeftBound()), hif::op_minus, hif::copy(o.getRightBound())), hif::op_le,
        _factory.intval(0));

    hif::manipulation::SimplifyOptions sopt;
    sopt.simplify_constants           = true;
    sopt.simplify_parameters          = true;
    sopt.simplify_template_parameters = true;
    sopt.simplify_statements          = true;
    hif::Value *simplified            = hif::manipulation::simplify(e, _sem, sopt);
    hif::BitValue *bv                 = dynamic_cast<hif::BitValue *>(simplified);
    if (bv != nullptr && (bv->getValue() == hif::bit_one || bv->getValue() == hif::bit_zero)) {
        o.setDirection(bv->getValue() == hif::bit_one ? hif::dir_upto : hif::dir_downto);
        delete simplified;
        return 0;
    }

    if (bv == nullptr) {
        delete e;
        e = nullptr;
    }

    // cannot check statically.. use heuristics
    hif::ConstValue *cvLeft  = dynamic_cast<hif::ConstValue *>(o.getLeftBound());
    hif::ConstValue *cvRight = dynamic_cast<hif::ConstValue *>(o.getRightBound());

    if (cvLeft != nullptr && cvRight == nullptr) {
        o.setDirection(hif::dir_upto);
    } else if (cvLeft == nullptr && cvRight != nullptr) {
        o.setDirection(hif::dir_downto);
    } else if (cvLeft != nullptr && cvRight != nullptr) {
        messageError("Should be resolved by simplify", &o, _sem);
    } else {
        o.setDirection(hif::dir_downto);
    }

    messageWarning(
        "Unable to set range direction, assuming " + hif::rangeDirectionToString(o.getDirection()), &o, _sem);

    return 0;
}

int FixDescription_1::visitReal(hif::Real &o)
{
    GuideVisitor::visitReal(o);
    if (o.getSpan() == nullptr)
        o.setSpan(new hif::Range(63, 0));
    return 0;
}

int FixDescription_1::visitSignal(hif::Signal &o)
{
    GuideVisitor::visitSignal(o);

    if (o.checkProperty(IS_VARIABLE_TYPE)) {
        if (o.getValue() == nullptr)
            o.setValue(_sem->getTypeDefaultValue(o.getType(), nullptr));
        o.removeProperty(IS_VARIABLE_TYPE);
        return 0;
    }

    // Managing ams ground case:
    if (dynamic_cast<hif::TypeReference *>(o.getType()) != nullptr &&
        static_cast<hif::TypeReference *>(o.getType())->getName() == "ground") {
        hif::TypeReference *tr = static_cast<hif::TypeReference *>(o.getType());
        // Already fixed?
        if (!tr->templateParameterAssigns.empty())
            return 0;
        hif::BList<hif::Declaration> *decls = &o.getBList()->toOtherBList<hif::Declaration>();
        hif::Signal *sig                    = nullptr;
        for (hif::BList<hif::Declaration>::iterator i = decls->begin(); i != decls->end(); ++i) {
            hif::Declaration *d = *i;
            if (d == &o || d->getName() != o.getName())
                continue;
            sig = dynamic_cast<hif::Signal *>(d);
            messageAssert(sig != nullptr, "Unexpected case (1)", d, _sem);
            break;
        }
        messageAssert(sig != nullptr, "Unexpected case (2)", &o, _sem);
        hif::TypeTPAssign *tp =
            static_cast<hif::TypeTPAssign *>(_factory.templateTypeArgument("T", sig->setType(nullptr)).getObject());
        tr->templateParameterAssigns.push_back(tp);
        sig->setType(tr);
        _trash.insert(&o);
        return 0;
    }

    if (o.getValue() == nullptr)
        return 0;
    if (_isConstantExpr(o.getValue()))
        return 0;

    // wires declared with initial values are implicit concurrent processes.
    hif::Contents *c = hif::getNearestParent<hif::Contents>(&o);
    messageAssert(c != nullptr, "hif::Contents not found", &o, _sem);

    if (c->getGlobalAction() == nullptr)
        c->setGlobalAction(new hif::GlobalAction());

    hif::Assign *a = new hif::Assign();
    a->setLeftHandSide(new hif::Identifier(o.getName()));
    a->setRightHandSide(o.setValue(nullptr));
    //a->addProperty(NONBLOCKING_ASSIGNMENT);
    messageDebugAssert(o.getSourceLineNumber() != 0, "unknown line", &o, _sem);
    _factory.codeInfo(a, o.getSourceFileName(), o.getSourceLineNumber());

    c->getGlobalAction()->actions.push_back(a);

    return 0;
}

int FixDescription_1::visitForGenerate(hif::ForGenerate &o)
{
    if (o.getGlobalAction())
        o.getGlobalAction()->acceptVisitor(*this);

    GuideVisitor::visitForGenerate(o);

    return 0;
}

int FixDescription_1::visitInstance(hif::Instance &o)
{
    hif::Entity *entity_o = nullptr;

    if (o.portAssigns.size() != 0 && o.portAssigns.front()->getName() == hif::NameTable::getInstance()->none()) {
        entity_o = hif::semantics::getDeclaration(&o, _sem);

        messageAssert(entity_o != nullptr, "hif::Declaration of instance not found", &o, nullptr);
        messageAssert(
            o.portAssigns.size() == entity_o->ports.size(), "Mismatch between portAssigns and entity port size", &o,
            _sem);

        hif::BList<hif::PortAssign>::iterator itOnPortAssign = o.portAssigns.begin();

        for (hif::BList<hif::Port>::iterator it = entity_o->ports.begin(); it != entity_o->ports.end(); ++it) {
            hif::Port *port_o             = *it;
            hif::PortAssign *portAssign_o = *itOnPortAssign;
            portAssign_o->setName(port_o->getName());
            ++itOnPortAssign;
        }
    }

    hif::ViewReference *viewref_o = dynamic_cast<hif::ViewReference *>(o.getReferencedType());

    if (viewref_o != nullptr && viewref_o->templateParameterAssigns.size() != 0 &&
        viewref_o->templateParameterAssigns.front()->getName() == hif::NameTable::getInstance()->none()) {
        if (entity_o == nullptr) {
            entity_o = hif::semantics::getDeclaration(&o, _sem);
        }

        messageAssert(entity_o != nullptr, "hif::Declaration of instance not found", &o, nullptr);

        hif::View *view_o = static_cast<hif::View *>(entity_o->getParent());

        messageAssert(
            view_o->templateParameters.size() >= viewref_o->templateParameterAssigns.size(),
            "Actual template parameters are more than formal ones", nullptr, nullptr);

        hif::manipulation::sortParameters(
            viewref_o->templateParameterAssigns, view_o->templateParameters, true,
            hif::manipulation::SortMissingKind::NOTHING, _sem);
    }

    GuideVisitor::visitInstance(o);

    return 0;
}

int FixDescription_1::visitLibraryDef(hif::LibraryDef &o)
{
    const bool restore = _insideStandard;
    _insideStandard    = o.isStandard();
    GuideVisitor::visitLibraryDef(o);
    _insideStandard = restore;

    return 0;
}

int FixDescription_1::visitTypeReference(hif::TypeReference &o)
{
    GuideVisitor::visitTypeReference(o);

    if (_fixAMSDisciplines(&o))
        return 0;

    return 0;
}

int FixDescription_1::visitValueTP(hif::ValueTP &o)
{
    hif::HifTypedQuery<hif::FunctionCall> q;
    std::list<hif::FunctionCall *> list;
    hif::search(list, &o, q);
    if (!list.empty()) {
        for (std::list<hif::FunctionCall *>::iterator i = list.begin(); i != list.end(); ++i) {
            hif::FunctionCall *fc = *i;
            messageAssert(
                fc->getName() == "iterated_concat", "ConstExprs with function calls are not supported yet.", &o, _sem);

            raiseUniqueWarning("Found at least one replication (iterated concat)"
                               " inside a parameter. Replacing it with its result.");

            _fixiteratedConcat(fc, true);
        }
    }

    _fixMissingDeclarationType(&o);

    GuideVisitor::visitValueTP(o);
    return 0;
}

int FixDescription_1::visitVariable(hif::Variable &o)
{
    GuideVisitor::visitVariable(o);
    if (_fixGenVarVariable(&o))
        return 0;
    return 0;
}

int FixDescription_1::visitView(hif::View &o)
{
    if (_declSet.find(&o) != _declSet.end())
        return 0;
    _declSet.insert(&o);

    const bool restore = _insideStandard;
    _insideStandard    = o.isStandard();
    GuideVisitor::visitView(o);
    _insideStandard = restore;

    return 0;
}

int FixDescription_1::visitViewReference(hif::ViewReference &o)
{
    hif::ViewReference::DeclarationType *v = hif::semantics::getDeclaration(&o, _sem);
    messageAssert(v != nullptr, "hif::Declaration not found", &o, _sem);
    v->acceptVisitor(*this);

    GuideVisitor::visitViewReference(o);

    return 0;
}

int FixDescription_1::visitWhenAlt(hif::WhenAlt &o)
{
    GuideVisitor::visitWhenAlt(o);

    auto t  = hif::semantics::getSemanticType(o.getCondition(), _sem);
    auto bt = hif::semantics::getBaseType(t, false, _sem);
    messageAssert(bt != nullptr, "Cannot type whenalt condition", o.getCondition(), _sem);

    if (dynamic_cast<hif::Bitvector *>(bt) != nullptr) {
        hif::Expression *reduce = new hif::Expression(hif::op_orrd, o.setCondition(nullptr));
        o.setCondition(reduce);
    } else if (dynamic_cast<hif::Real *>(bt) != nullptr) {
        auto w = static_cast<hif::When *>(o.getParent());
        w->setLogicTernary(false);
    }

    return 0;
}

int FixDescription_1::visitWhile(hif::While &o)
{
    GuideVisitor::visitWhile(o);

    // Check it is a do-while(0), i.e. a block
    if (!o.isDoWhile())
        return 0;
    hif::BitValue *bvCond = dynamic_cast<hif::BitValue *>(o.getCondition());
    if (bvCond == nullptr || bvCond->getValue() != hif::bit_zero)
        return 0;
    // No label --> useless while
    // No break on label --> useless while
    bool isReferenced = false;
    if (o.getName() != hif::NameTable::getInstance()->none()) {
        hif::HifTypedQuery<hif::Break> q;
        std::list<hif::Break *> list;
        hif::search(list, &o, q);
        for (std::list<hif::Break *>::iterator i = list.begin(); i != list.end(); ++i) {
            hif::Break *b = (*i);
            if (b->getName() != o.getName())
                continue;
            isReferenced = true;
            break;
        }
    }

    // In this case, the label is already set as statetable name.
    const bool isTopLevel = (dynamic_cast<hif::State *>(o.getParent()) != nullptr) &&
                            static_cast<hif::State *>(o.getParent())->actions.size() == 1UL;

    const bool isUseless = o.getName() == hif::NameTable::getInstance()->none() || !isReferenced || isTopLevel;

    if (!isUseless)
        return 0;

    messageAssert(o.isInBList(), "Unsupported position.", &o, _sem);

    hif::BList<hif::Action>::iterator it(&o);
    it.insert_before(o.actions); // eventual internal whiles are already visited by GuideVisitor.
    _trash.insert(&o);

    return 0;
}

int FixDescription_1::visitStateTable(hif::StateTable &o)
{
    GuideVisitor::visitStateTable(o);

    _fixAllSignalsSesitivity(&o);
    _fixProcessesWithWait(&o);

    return 0;
}

void FixDescription_1::_fixParameterNames(hif::Object *call, hif::BList<hif::ParameterAssign> &actuals)
{
    hif::SubProgram *decl = dynamic_cast<hif::SubProgram *>(hif::semantics::getDeclaration(call, _sem));
    messageAssert(decl != nullptr, "Unable to get call declaration.", call, _sem);

    std::list<std::string> formalNames;
    for (hif::BList<hif::Parameter>::iterator it(decl->parameters.begin()); it != decl->parameters.end(); ++it) {
        formalNames.push_back((*it)->getName());
    }

    messageAssert(formalNames.size() >= actuals.size(), "Unexpected numbed of parameters.", call, _sem);

    std::list<std::string>::iterator portIt = formalNames.begin();
    for (hif::BList<hif::ParameterAssign>::iterator it(actuals.begin()); it != actuals.end(); ++it, ++portIt) {
        std::string sName = *portIt;
        (*it)->setName(hif::NameTable::getInstance()->registerName(sName));
    }
}

template <typename T> void FixDescription_1::_fixSystemTaskCalls(T *call)
{
    std::string callName(call->getName());
    if (callName[0] != '$')
        return;

    // fix name
    std::replace(callName.begin(), callName.end(), '$', '_');
    callName = "_system" + callName;
    call->setName(callName);

    // removing unsupported calls
    if (callName == "_system_monitor" || callName == "_system_monitorb" || callName == "_system_monitoro" ||
        callName == "_system_monitorh" || callName == "_system_fmonitor" || callName == "_system_fmonitorb" ||
        callName == "_system_fmonitoro" || callName == "_system_fmonitorh" || callName == "_system_monitoron" ||
        callName == "_system_monitoroff") {
        messageWarning("Monitor-related system task are not supported, skipped", call, _sem);
        _trash.insert(call);
    }
}

bool FixDescription_1::_fixAMSDisciplines(hif::TypeReference *tr)
{
    messageAssert(hif::isInTree(tr), "Object not in tree", tr, _sem);

    hif::TypeReference::DeclarationType *trDec = hif::semantics::getDeclaration(tr, _sem);
    if (trDec != nullptr)
        return false;

    // AMS disciplines are modeled as hif::ViewReference to avoid ambiguous matches
    // on method signatures, etc.
    // If an unknown port type is found, parser sets a generic hif::TypeReference and
    // this is fine for all other cases. For AMS, we are going to refine type as hif::ViewReference here.

    std::string trName = tr->getName();
    std::string vrName = "ams_discipline";

    // Logic discipline is mapped as TypeDef since it has descrete domain.
    if (trName == "logic") {
        tr->setName("ams_logic");
        return true;
    }

    hif::ViewReference *vr = new hif::ViewReference();
    vr->setDesignUnit(trName);
    vr->setName(vrName);

    tr->replace(vr);
    hif::ViewReference::DeclarationType *vrDec = hif::semantics::getDeclaration(vr, _sem);
    if (vrDec == nullptr) {
        vr->replace(tr); // restore
        return false;
    }

    delete tr;
    return true;
}

bool FixDescription_1::_isConstantExpr(hif::Value *v)
{
    if (v == nullptr)
        return true;

    std::list<hif::Object *> list;
    hif::semantics::collectSymbols(list, v, _sem);
    for (std::list<hif::Object *>::iterator i = list.begin(); i != list.end(); ++i) {
        if (dynamic_cast<hif::Instance *>(*i) != nullptr) {
            // Skipping library instances.
            auto ii = static_cast<hif::Instance *>(*i);
            if (dynamic_cast<hif::Library *>(ii->getReferencedType()) != nullptr)
                continue;
        }

        hif::Declaration *decl = hif::semantics::getDeclaration(*i, _sem);
        messageAssert(decl != nullptr, "hif::Declaration not found", *i, _sem);
        if (dynamic_cast<hif::Signal *>(decl) != nullptr || dynamic_cast<hif::Port *>(decl) != nullptr ||
            dynamic_cast<hif::Variable *>(decl) != nullptr) {
            return false;
        }
    }

    return true;
}

void FixDescription_1::_fixAllSignalsSesitivity(hif::StateTable *o)
{
    if (!o->checkProperty("ALL_SIGNALS"))
        return;
    o->removeProperty("ALL_SIGNALS");

    typedef std::list<hif::Object *> List;
    List list;
    hif::semantics::collectSymbols(list, o, _sem);
    for (List::iterator i = list.begin(); i != list.end(); ++i) {
        hif::Value *v = dynamic_cast<hif::Value *>(*i);
        if (v == nullptr)
            continue;

        if (hif::manipulation::isInLeftHandSide(v))
            continue;

        if (dynamic_cast<hif::Instance *>(v) != nullptr &&
            dynamic_cast<hif::Library *>(static_cast<hif::Instance *>(v)->getReferencedType()) != nullptr) {
            continue;
        }

        auto decl = hif::semantics::getDeclaration(v, _sem);
        messageAssert(decl != nullptr, "Unable to get declaration.", v, _sem);
        auto ddecl = dynamic_cast<hif::DataDeclaration *>(decl);
        if (ddecl == nullptr)
            continue;
        if (dynamic_cast<hif::Signal *>(ddecl) == nullptr && dynamic_cast<hif::Port *>(ddecl) == nullptr)
            continue;
        hif::ViewReference *vr = dynamic_cast<hif::ViewReference *>(ddecl->getType());
        if (vr != nullptr) {
            hif::DeclarationIsPartOfStandardOptions opts;
            opts.reset();
            opts.allowVerilogAMS = true;
            opts.sem             = _sem;
            hif::View *view      = hif::semantics::getDeclaration(vr, _sem);
            if (hif::declarationIsPartOfStandard(view, opts))
                continue;
        }

        hif::Value *val = hif::copy(v);
        hif::manipulation::AddUniqueObjectOptions addOpt;
        addOpt.deleteIfNotAdded = true;
        hif::manipulation::addUniqueObject(val, o->sensitivity, addOpt);
    }
}

void FixDescription_1::_fixProcessesWithWait(hif::StateTable *o)
{
    // NOTE: adding of Wait is not necessary for subprograms.
    auto sub = dynamic_cast<hif::SubProgram *>(o->getParent());
    if (sub != nullptr)
        return;
    if (o->getFlavour() == hif::pf_initial || o->getFlavour() == hif::pf_analog)
        return;

    hif::HifTypedQuery<hif::Wait> q;
    std::list<hif::Wait *> list;
    hif::search(list, o, q);

    if (list.empty())
        return;
    auto w = new hif::Wait();
    o->states.front()->actions.push_back(w);
}

void FixDescription_1::_fixCollectedGenVarVariables()
{
    for (GenVarsSeq::reverse_iterator i = _genVarsSeq.rbegin(); i != _genVarsSeq.rend(); ++i) {
        hif::Variable *v = *i;
        v->replace(nullptr);
        v->removeProperty(PROPERTY_GENVAR);

        for (GenVars::mapped_type::iterator j = _genVars[v].begin(); j != _genVars[v].end(); ++j) {
            hif::Object *ss   = *j;
            hif::Variable *vc = hif::copy(v);

            if (dynamic_cast<hif::For *>(ss) != nullptr) {
                auto ff = dynamic_cast<hif::For *>(ss);
                ff->initDeclarations.push_front(vc);
                // Searching matching init action and moving it as initial value of var.
                for (hif::BList<hif::Action>::iterator k = ff->initValues.begin(); k != ff->initValues.end(); ++k) {
                    hif::Assign *ass = dynamic_cast<hif::Assign *>(*k);
                    if (ass == nullptr)
                        continue;
                    hif::Identifier *id = dynamic_cast<hif::Identifier *>(ass->getLeftHandSide());
                    if (id == nullptr || id->getName() != vc->getName())
                        continue;
                    vc->setValue(ass->setRightHandSide(nullptr));
                    ass->replace(nullptr);
                    delete ass;
                    break;
                }
            } else if (dynamic_cast<hif::Scope *>(ss)) {
                hif::BList<hif::Declaration> *declList = hif::objectGetDeclarationList(ss);
                declList->push_front(vc);
            }

            // resetting potential wrong declarations
            hif::semantics::resetDeclarations(ss);
        }

        delete v;
    }
}

bool FixDescription_1::_fixGenVarVariable(hif::Variable *o)
{
    if (!o->checkProperty(PROPERTY_GENVAR))
        return false;
    if (_genVars.find(o) != _genVars.end())
        return true;

    auto bc = hif::getNearestParent<hif::BaseContents>(o);
    messageAssert(bc != nullptr, "Base contents not found.", o, _sem);
    typedef hif::semantics::ReferencesSet Refs;
    Refs refs;
    hif::semantics::getReferences(o, refs, _sem, bc);

    // If the GenVar is not used, it's not a problem.
    if (refs.empty())
        return false;
    //messageAssert(!refs.empty(), "Unexpected missing refs", o, _sem);

    typedef std::list<hif::Action *> Scopes;
    typedef std::set<hif::ForGenerate *> ForScopes;
    Scopes scopes;
    ForScopes forScopes;

    for (Refs::iterator i = refs.begin(); i != refs.end(); ++i) {
        auto r   = *i;
        auto ff  = hif::getNearestParent<hif::For>(r);
        auto ii  = hif::getNearestParent<hif::If>(r);
        auto ffg = hif::getNearestParent<hif::ForGenerate>(r);
        auto iig = hif::getNearestParent<hif::IfGenerate>(r);
        messageAssert(ff != nullptr || ii != nullptr || ffg != nullptr || iig != nullptr, "unexpected parent", r, _sem);
        if (ff != nullptr)
            scopes.push_back(ff);
        if (ii != nullptr)
            scopes.push_back(ii);
        if (ffg != nullptr) {
            if (!hif::manipulation::isInLeftHandSide(r))
                continue;
            if (!hif::isSubNode(r, ffg->initValues))
                continue;
            forScopes.insert(ffg);
        }
        // iig != nullptr => ntd
    }

    hif::CopyOptions copt;
    copt.copyProperties = false;
    for (ForScopes::iterator i = forScopes.begin(); i != forScopes.end(); ++i) {
        auto fg = *i;
        for (hif::BList<hif::Action>::iterator j = fg->initValues.begin(); j != fg->initValues.end(); ++j) {
            hif::Assign *ass = dynamic_cast<hif::Assign *>(*j);
            if (ass == nullptr)
                continue;
            hif::Identifier *id = dynamic_cast<hif::Identifier *>(ass->getLeftHandSide());
            if (id == nullptr)
                continue;
            if (id->getName() != o->getName())
                continue;
            hif::Variable *var = hif::copy(o, copt);
            _finalTrash.insert(o);
            fg->initDeclarations.push_back(var);
            delete var->setValue(ass->setRightHandSide(nullptr));
            ass->replace(nullptr);
            delete ass;
            break;
        }
    }

    if (scopes.empty() || !forScopes.empty())
        return true;

    // calculating tops
    for (Scopes::iterator i = scopes.begin(); i != scopes.end(); ++i) {
        Scopes::iterator j = i;
        ++j;
        for (; j != scopes.end();) {
            hif::Action *a = _getTopParent(*i, *j);
            if (a == nullptr) {
                ++j;
                continue;
            }
            if (a == *i) {
                j = scopes.erase(j);
                continue;
            }

            i = scopes.erase(i);
            if (i != scopes.begin())
                --i;
            break;
        }
    }

    messageAssert(!scopes.empty(), "Unexpected empty list", nullptr, _sem);

    // fixing all tops
    _genVarsSeq.push_back(o);
    for (Scopes::iterator i = scopes.begin(); i != scopes.end(); ++i) {
        auto a  = *i;
        auto ff = dynamic_cast<hif::For *>(a);
        auto ii = dynamic_cast<hif::If *>(a);

        if (ii != nullptr) {
            // Move to neareset if scope:
            auto s  = hif::getNearestScope(ii, true, false, false);
            auto ps = hif::getNearestParent<hif::Scope>(o);
            if (s == ps)
                continue;
            _genVars[o].insert(s);
            continue;
        }

        // Move to for scope:
        auto pf = hif::getNearestParent<hif::For>(o);
        if (ff == pf)
            continue;
        _genVars[o].insert(ff);
    }

    return true;
}

hif::Action *FixDescription_1::_getTopParent(hif::Action *a1, hif::Action *a2)
{
    assert(a1 != nullptr);
    assert(a2 != nullptr);
    if (a1 == a2)
        return a1;
    if (hif::isSubNode(a1, a2))
        return a2;
    if (hif::isSubNode(a2, a1))
        return a1;
    return nullptr;
}

void FixDescription_1::_fixMissingDeclarationType(hif::DataDeclaration *decl)
{
    const bool isSigned = dynamic_cast<hif::BoolValue *>(decl->getProperty("signed")) != nullptr &&
                          static_cast<hif::BoolValue *>(decl->getProperty("signed"))->getValue();
    decl->removeProperty("signed");

    if (decl->getType() != nullptr)
        return;

    //messageAssert(decl->checkProperty("signed"), "Signed property not found", decl, _sem);
    messageAssert(decl->getValue() != nullptr, "Missing initial value", decl, _sem);

    auto type_o = hif::semantics::getSemanticType(decl->getValue(), _sem);
    messageAssert(type_o != nullptr, "Type not found of the initial value", decl, _sem);

    auto t = hif::copy(type_o);
    if (isSigned)
        hif::typeSetSigned(t, true, _sem);
    decl->setType(t);
}

void FixDescription_1::_fixMissingPortType(hif::Port *o, const hif::semantics::ReferencesSet &refs)
{
    if (o->getType() != nullptr)
        return;

    // AMS allows this shit!
    messageAssert(!refs.empty(), "Refs to untyped port not found.", o, _sem);
    // It must be connected with submodules.
    hif::PortAssign *pa = nullptr;
    for (hif::semantics::ReferencesSet::iterator i = refs.begin(); i != refs.end(); ++i) {
        pa = dynamic_cast<hif::PortAssign *>((*refs.begin())->getParent());
        if (pa != nullptr)
            break;
    }

    messageAssert(pa != nullptr, "Unexpected parent different from port assign.", o, _sem);

    hif::Port *p = nullptr;
    pa->getParent()->getParent()->acceptVisitor(*this);
    p = hif::semantics::getDeclaration(pa, _sem);
    messageAssert(p != nullptr, "Cannot get declaration of port assign.", pa, _sem);
    p->acceptVisitor(*this);
    auto t = hif::semantics::getSemanticType(pa, _sem);
    messageAssert(t != nullptr, "Cannot type port assign.", pa, _sem);
    o->setType(hif::copy(t));
}

void FixDescription_1::_fixMissingPortDir(hif::Port *o, const hif::semantics::ReferencesSet &refs)
{
    if (o->getDirection() != hif::dir_none)
        return;

    // AMS allows this shit!
    hif::PortAssign *pa = nullptr;
    for (hif::semantics::ReferencesSet::iterator i = refs.begin(); i != refs.end(); ++i) {
        pa = dynamic_cast<hif::PortAssign *>((*refs.begin())->getParent());
        if (pa != nullptr)
            break;
    }

    hif::Port *p = nullptr;
    if (pa != nullptr) {
        pa->getParent()->getParent()->acceptVisitor(*this);
        p = hif::semantics::getDeclaration(pa, _sem);
        messageAssert(p != nullptr, "Cannot get declaration of port assign.", pa, _sem);
        p->acceptVisitor(*this);
        o->setDirection(p->getDirection());
    } else {
        // Unkwnow.. forcing dir_inout to this case!
        o->setDirection(hif::dir_inout);
    }
}

bool FixDescription_1::_fixiteratedConcat(hif::FunctionCall *o, const bool aggressive)
{
    if (o->getName() != "iterated_concat")
        return false;
    hif::manipulation::SimplifyOptions opt;
    if (aggressive) {
        opt.simplify_template_parameters = true;
        opt.simplify_constants           = true;
    }

    hif::manipulation::simplify(o, _sem, opt);

    hif::ConstValue *cv1 = dynamic_cast<hif::ConstValue *>(
        static_cast<hif::ValueTPAssign *>(o->templateParameterAssigns.front())->getValue());
    messageAssert(!aggressive || cv1 != nullptr, "Unexpected non-const value first parameter assign", o, _sem);
    hif::ConstValue *cv2 = dynamic_cast<hif::ConstValue *>(o->parameterAssigns.front()->getValue());
    messageAssert(!aggressive || cv2 != nullptr, "Unexpected non-const value second parameter assign", o, _sem);

    if (cv1 == nullptr || cv2 == nullptr)
        return false;

    auto intType = new hif::Int();
    intType->setSpan(new hif::Range(63, 0));
    intType->setSigned(true);
    auto ivo = dynamic_cast<hif::IntValue *>(hif::manipulation::transformConstant(cv1, intType, _sem));
    messageAssert(ivo != nullptr && ivo->getValue() > 0, "Cannot transform const value to int value", cv1, _sem);

    auto bit = dynamic_cast<hif::BitValue *>(cv2);
    auto bv  = dynamic_cast<hif::BitvectorValue *>(cv2);
    messageAssert(bit != nullptr || bv != nullptr, "Unexpected second parameter type", cv2, _sem);

    std::string val;
    if (bit != nullptr)
        val = bit->toString();
    else
        val = bv->getValue();
    std::string res;
    for (long long int ii = 0; ii < ivo->getValue(); ++ii) {
        res += val;
    }

    auto bvRes = new hif::BitvectorValue(res);
    bvRes->setType(_sem->getTypeForConstant(bvRes));
    o->replace(bvRes);
    delete o;

    return true;
}

bool FixDescription_1::_fixLocalParam(hif::Identifier *o)
{
    // hif::Declaration could be a local param, i.e. a constant inside the
    // view constents, but the current expression occurring as
    // initial value of a param (i.e. a template of view).
    auto view = hif::getNearestParent<hif::View>(o);
    auto vtp  = hif::getNearestParent<hif::ValueTP>(o);
    if (vtp == nullptr || view == nullptr)
        return false;
    hif::semantics::DeclarationOptions dopt;
    dopt.location = view->getContents();
    auto d        = hif::semantics::getDeclaration(o, _sem, dopt);
    auto c        = dynamic_cast<hif::Const *>(d);
    if (c == nullptr)
        return false;
    // OK: move to template:
    auto v = new hif::ValueTP();
    v->setName(c->getName());
    v->setValue(c->setValue(nullptr));
    v->setType(c->setType(nullptr));

    // Updating references
    hif::semantics::ReferencesSet refs;
    hif::semantics::getReferences(c, refs, _sem, view);
    for (hif::semantics::ReferencesSet::iterator i = refs.begin(); i != refs.end(); ++i) {
        hif::semantics::setDeclaration(*i, v);
    }

    c->replace(nullptr);
    delete c;
    hif::BList<hif::Declaration>::iterator it(vtp);
    it.insert_before(v);
    v->acceptVisitor(*this);

    return true;
}

bool FixDescription_1::_fixImplicitDeclaredNets(hif::Identifier *o)
{
    auto t = hif::semantics::getOtherOperandType(o, _sem);
    messageAssert(t != nullptr, "Other operand type not found.", o, _sem);
    auto bc = hif::getNearestParent<hif::BaseContents>(o);
    messageAssert(bc != nullptr, "Suitable scope not found.", o, _sem);
    auto s = new hif::Signal();
    s->setName(o->getName());
    s->setType(hif::copy(t));
    bc->declarations.push_front(s); // front to avoid order declaration problems

    return true;
}

void performStep1Refinements(hif::System *o, hif::semantics::ILanguageSemantics *sem)
{
    FixDescription_1 v(sem);
    o->acceptVisitor(v);
    hif::semantics::resetDeclarations(o);
}
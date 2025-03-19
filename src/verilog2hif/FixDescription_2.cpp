/// @file FixDescription_2.cpp
/// @brief
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#include <cstdlib>
#include <iostream>

#include <hif/hif.hpp>

#include "verilog2hif/post_parsing_methods.hpp"
#include "verilog2hif/support.hpp"

using std::string;
using namespace hif;

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-member-function"
#elif defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

typedef hif::semantics::ReferencesMap RefMap;

bool _fixFunctionReturnVariable(Object *o, std::string function_name)
{
    Identifier *no = dynamic_cast<Identifier *>(o);
    if (no != nullptr) {
        if (function_name.compare(no->getName()) == 0) {
            function_name.append("_return");
            no->setName(function_name);
        }
    } else {
        Variable *vo = dynamic_cast<Variable *>(o);
        if (vo != nullptr) {
            if (function_name.compare(vo->getName()) == 0) {
                function_name.append("_return");
                vo->setName(function_name);
            }
        }
    }
    return true;
}

void _fixRange(Range *r, RangeDirection d, hif::semantics::ILanguageSemantics *sem)
{
    hif::manipulation::simplify(r->getLeftBound(), sem);
    hif::manipulation::simplify(r->getRightBound(), sem);

    if (dynamic_cast<IntValue *>(r->getLeftBound()) != nullptr &&
        dynamic_cast<IntValue *>(r->getRightBound()) != nullptr) {
        long long int lbound, rbound;

        lbound = (static_cast<IntValue *>(r->getLeftBound()))->getValue();
        rbound = (static_cast<IntValue *>(r->getRightBound()))->getValue();

        if (lbound > rbound)
            r->setDirection(dir_downto);
        else if (lbound < rbound)
            r->setDirection(dir_upto);
        else
            r->setDirection(d);
    } else {
        r->setDirection(d);
    }
}

int _getSignalEdge(StateTable *process, const std::string &signalName)
{
    BList<Value>::iterator it;
    for (it = process->sensitivityPos.begin(); it != process->sensitivityPos.end(); ++it) {
        Identifier *id = dynamic_cast<Identifier *>(*it);
        messageAssert(id != nullptr, "Expected identifier", *it, nullptr);
        if (id->getName() == signalName)
            return 1;
    }
    for (it = process->sensitivityNeg.begin(); it != process->sensitivityNeg.end(); ++it) {
        Identifier *id = dynamic_cast<Identifier *>(*it);
        messageAssert(id != nullptr, "Expected identifier", *it, nullptr);
        if (id->getName() == signalName)
            return -1;
    }
    return 0;
}

void _createClockedCondition(BList<Action> &actions, const std::string &clockName, const int clock_edge)
{
    ParameterAssign *pao = new ParameterAssign();
    pao->setName("clock");
    pao->setValue(new Identifier(clockName));

    std::string call = (clock_edge == 1) ? "__hif_rising_edge" : "__hif_falling_edge";

    FunctionCall *fco = new FunctionCall();
    fco->parameterAssigns.push_back(pao);
    fco->setName(call);

    IfAlt *ncao = new IfAlt();
    ncao->setCondition(fco);
    ncao->actions.merge(actions);

    If *nco = new If();
    nco->alts.push_back(ncao);

    actions.push_back(nco);
}

// Assuming two signals, clock and reset, detect which one is the clock
// basing on the edges of signals.
std::string _detectClockSignal(StateTable *process)
{
    messageAssert(process != nullptr, "Unexpected nullptr process", process, nullptr);
    size_t listsSize = process->sensitivity.size() + process->sensitivityPos.size() + process->sensitivityNeg.size();

    Value *sig = nullptr;

    // If only one element is defined, it must be the clock.
    if (listsSize == 1) {
        if (!process->sensitivity.empty())
            sig = process->sensitivity.front();
        else if (!process->sensitivityPos.empty())
            sig = process->sensitivityPos.front();
        else if (!process->sensitivityNeg.empty())
            sig = process->sensitivityNeg.front();
    }
    // With two or more elements in the sensitivity lists, the only chance
    // is that only one of them is edge-defined.
    else if (listsSize >= 2) {
        // More than one element is edge-defined, fail.
        if (listsSize - process->sensitivity.size() > 1)
            return nullptr;

        // All elements are edge-defined, fail.
        if (process->sensitivity.empty())
            return nullptr;

        // The only edge-defined one must be the clock.
        if (!process->sensitivityPos.empty())
            sig = process->sensitivityPos.front();
        else if (!process->sensitivityNeg.empty())
            sig = process->sensitivityNeg.front();
    }

    Identifier *sigId = dynamic_cast<Identifier *>(sig);
    if (sigId == nullptr)
        return nullptr;
    return sigId->getName();
}

typedef std::list<StateTable *> InitialProcesses;

bool collectObjectMethod(Object *o, const HifQueryBase *)
{
    if (dynamic_cast<Wait *>(o) != nullptr) {
        return true;
    } else if (dynamic_cast<Assign *>(o) != nullptr) {
        Assign *ass = static_cast<Assign *>(o);
        return (ass->getDelay() != nullptr);
    } else if (dynamic_cast<Expression *>(o) != nullptr) {
        hif::semantics::ILanguageSemantics *sem = hif::semantics::VerilogSemantics::getInstance();
        Expression *e                           = static_cast<Expression *>(o);
        if (e->getOperator() != op_deref)
            return false;

        Type *exprType = hif::semantics::getBaseType(hif::semantics::getSemanticType(e, sem), false, sem);
        messageAssert(exprType != nullptr, "Cannot type expression", e, sem);

        Event *event = dynamic_cast<Event *>(exprType);
        if (event == nullptr)
            return false;

        return true;
    }

    return false;
}

/// @brief Fixes the generated description, before calling the standard visitor..
///
/// 1- Adds the types for Verilog param.
/// 2- Transform sensitivity list to a VHDL-like one.
/// 3- Reorders declarations, by putting const decls before other decls.
/// 4- Fix iterated concatenations parameters
/// 5- Manage re-declaration of variables as signals
/// -- if variables are assigned only with non-blocking assignments, used in
///     process sensitivity lists, used for module binding, used inside RH value
///     of global actions
/// -- if variables are assigned only with blocking assignments, and none of other
///     conditions is verified, they remains variables
/// -- for any other case, a signal associated to the variable is introduced, and
///     both assignment are performed
///
class FixDescription_2 : public hif::GuideVisitor
{
public:
    FixDescription_2(RefMap &refMap, hif::semantics::ILanguageSemantics *sem);
    virtual ~FixDescription_2();

    int visitDesignUnit(hif::DesignUnit &o);
    int visitValueTP(hif::ValueTP &o);
    /// @brief Performs the following fixes:
    /// - separates Const from other declarations. On these ones calls a visitor
    /// 	to fix the kind of assignment
    int visitContents(hif::Contents &o);
    int visitExpression(Expression &o);
    int visitInt(hif::Int &o);
    int visitSlice(hif::Slice &o);
    int visitFunction(hif::Function &o);

    // constant
    virtual int visitBitValue(hif::BitValue &o);
    virtual int visitBitvectorValue(hif::BitvectorValue &o);
    virtual int visitBoolValue(hif::BoolValue &o);
    virtual int visitCharValue(hif::CharValue &o);
    virtual int visitIntValue(hif::IntValue &o);
    virtual int visitRealValue(hif::RealValue &o);
    virtual int visitStringValue(hif::StringValue &o);

    /// @name fix timescale and time related symbols
    /// @{

    virtual int visitAssign(Assign &o);
    virtual int visitConst(Const &o);
    virtual int visitFunctionCall(FunctionCall &o);
    virtual int visitSignal(Signal &o);
    virtual int visitSystem(System &o);
    virtual int visitVariable(Variable &o);
    virtual int visitView(View &o);
    virtual int visitWait(Wait &o);

    /// @}

    virtual int visitParameterAssign(ParameterAssign &o);
    virtual int visitPortAssign(PortAssign &o);

private:
    void _fixConstValue(hif::ConstValue &o);

    /// @name StateTable-related fixes
    //@{

    /// @brief Takes the mixed sensitivity list created by the parser and moves
    /// each element in the proper sensitivity list, distinguishing between
    /// positive sensitive, negative sensitive and both sensitive.
    void _manageSensitivityLists(hif::StateTable *process);

    /// @brief Depending on the edge of clock signal, creates the correspondent
    /// if statement exploiting function event.
    ///
    /// Accepting only two kind of processes:
    /// - with clock (and eventually reset) into sensitivity
    /// - w/o clock in the sensitivity
    void _manageClockCondition(hif::StateTable *process);

    //@}

    void _collectInitialProcesses(Contents *c, InitialProcesses &initialProcesses);
    void _splitInitialProcesses(Contents *c, InitialProcesses &initialProcesses);
    void _mergeInitialProcesses(InitialProcesses &initialProcesses);

    void _manageWaitActions(Wait *o);
    void _checkViewTimeScale();
    void _scaleTimeValue(Value *v);

    bool _checkWrongStatement(Object *root);

    FixDescription_2(const FixDescription_2 &);
    FixDescription_2 operator=(const FixDescription_2 &);

    RefMap &_refMap;
    hif::semantics::ILanguageSemantics *_sem;

    hif::HifFactory _factory;

    View *_currentView;
    System *_currentSystem;

    bool _addedDefaultTimeScale;
};

FixDescription_2::FixDescription_2(RefMap &refMap, hif::semantics::ILanguageSemantics *sem)
    : _refMap(refMap)
    , _sem(sem)
    , _factory(sem)
    , _currentView(nullptr)
    , _currentSystem(nullptr)
    , _addedDefaultTimeScale(false)
{
    hif::application_utils::initializeLogHeader("VERILOG2HIF", "FixDescription_2");
}

FixDescription_2::~FixDescription_2() { hif::application_utils::restoreLogHeader(); }

int FixDescription_2::visitDesignUnit(DesignUnit &o)
{
    messageAssert(o.views.size() == 1, "Wrong design unit with more than one view", &o, _sem);

    Contents *c = o.views.front()->getContents();
    if (c == nullptr) {
        GuideVisitor::visitDesignUnit(o);
        return 0;
    }

    InitialProcesses initialProcesses;

    // collect module initial processes
    _collectInitialProcesses(c, initialProcesses);

    // First refine: split initial processes with wrong objects
    _splitInitialProcesses(c, initialProcesses);

    // Second refine: merge all initial processes
    _mergeInitialProcesses(initialProcesses);

    GuideVisitor::visitDesignUnit(o);

    return 0;
}

int FixDescription_2::visitFunction(Function &o)
{
    GuideVisitor::visitFunction(o);

    std::string funName = o.getName();

    // append the suffix "_return" to all Identifier, in the Function body,
    // refering to the variable used for return value
    // (this variable has the same name of the function)
    hif::apply::visit(&o, _fixFunctionReturnVariable, funName);

    return 0;
}

int FixDescription_2::visitValueTP(ValueTP &o)
{
    GuideVisitor::visitValueTP(o);

    if (o.getType() != nullptr)
        return 0;
    messageAssert(o.getValue() != nullptr, "Expected initial value", &o, _sem);
    Type *to = hif::semantics::getSemanticType(o.getValue(), _sem);
    if (to == nullptr) {
        messageWarning("Type not found for ValueTP", &o, _sem);
        messageDebugAssert(o.getType() != nullptr, "Unexpected case", &o, _sem);
    }
    o.setType(hif::copy(to));
    return 0;
}

int FixDescription_2::visitContents(Contents &o)
{
    // Managing decls:
    BList<Declaration> consts;
    BList<Declaration> others;
    while (!o.declarations.empty()) {
        Declaration *decl = o.declarations.front();
        o.declarations.remove(decl);

        if (dynamic_cast<Const *>(decl) != nullptr) {
            consts.push_back(decl);
        } else {
            others.push_back(decl);
        }
    }
    o.declarations.merge(consts);
    o.declarations.merge(others);

    // NOTE: pre-visit instead of post-visit because the visit of the fcall
    // needs a fixed edge sensitivity
    GuideVisitor::visitContents(o);

    return 0;
}

int FixDescription_2::visitExpression(Expression &o)
{
    GuideVisitor::visitExpression(o);

    if (hif::operatorIsShift(o.getOperator())) {
        // Fix shift expressions: second operand must be considered unsigned
        Type *t = hif::semantics::getSemanticType(o.getValue2(), _sem);
        messageAssert(t != nullptr, "Cannot type operand 2", &o, _sem);
        if (hif::typeIsSigned(t, _sem)) {
            Type *ct = hif::copy(t);
            hif::typeSetSigned(ct, false, _sem);
            Cast *c = new Cast();
            c->setType(ct);
            c->setValue(o.setValue2(nullptr));
            o.setValue2(c);
        }
    } else if (hif::operatorIsLogical(o.getOperator())) {
        // In case of not on bit-types, it must be translated as bitwise not.
        // Ref design: or1200_top
        Type *type1 = hif::semantics::getSemanticType(o.getValue1(), _sem);
        messageAssert(type1 != nullptr, "Cannot type description", o.getValue1(), _sem);
        Type *type2 = nullptr;
        if (o.getValue2() != nullptr)
            type2 = hif::semantics::getSemanticType(o.getValue2(), _sem);
        messageAssert(o.getValue2() == nullptr || type2 != nullptr, "Cannot type description", o.getValue2(), _sem);

        Bit *bb1        = dynamic_cast<Bit *>(type1);
        Bitvector *bbv1 = dynamic_cast<Bitvector *>(type1);
        Bit *bb2        = dynamic_cast<Bit *>(type2);
        Bitvector *bbv2 = dynamic_cast<Bitvector *>(type2);
        if (bb1 == nullptr && bbv1 == nullptr)
            return 0;
        if (o.getValue2() != nullptr && bb2 == nullptr && bbv2 == nullptr)
            return 0;

        const hif::Operator newOp = hif::operatorGetLogicBitwiseConversion(o.getOperator());
        o.setOperator(newOp);
        if (bbv1 != nullptr) {
            Expression *newExpr = _factory.expression(op_orrd, o.setValue1(nullptr));
            o.setValue1(newExpr);
        }

        if (bbv2 != nullptr) {
            Expression *newExpr = _factory.expression(op_orrd, o.setValue2(nullptr));
            o.setValue2(newExpr);
        }
    }
    return 0;
}

int FixDescription_2::visitInt(Int &o)
{
    GuideVisitor::visitInt(o);

    Int *i      = &o;
    Range *span = i->getSpan();
    if (span == nullptr) {
        span = new Range(31, 0);
        i->setSpan(span);
    }

    return 0;
}

int FixDescription_2::visitSlice(Slice &o)
{
    GuideVisitor::visitSlice(o);

    Range *r = o.getSpan();
    Value *v = o.getPrefix();

    Type *t = hif::semantics::getSemanticType(v, _sem);

    Range *vr = hif::typeGetSpan(t, _sem);

    _fixRange(r, vr->getDirection(), _sem);

    return 0;
}

int FixDescription_2::visitBitValue(BitValue &o)
{
    GuideVisitor::visitBitValue(o);
    _fixConstValue(o);
    return 0;
}

int FixDescription_2::visitBitvectorValue(BitvectorValue &o)
{
    GuideVisitor::visitBitvectorValue(o);
    _fixConstValue(o);
    return 0;
}

int FixDescription_2::visitBoolValue(BoolValue &o)
{
    GuideVisitor::visitBoolValue(o);
    _fixConstValue(o);
    return 0;
}

int FixDescription_2::visitCharValue(CharValue &o)
{
    GuideVisitor::visitCharValue(o);
    _fixConstValue(o);
    return 0;
}

int FixDescription_2::visitIntValue(IntValue &o)
{
    GuideVisitor::visitIntValue(o);
    _fixConstValue(o);
    return 0;
}

int FixDescription_2::visitRealValue(RealValue &o)
{
    GuideVisitor::visitRealValue(o);
    _fixConstValue(o);
    return 0;
}

int FixDescription_2::visitStringValue(StringValue &o)
{
    GuideVisitor::visitStringValue(o);
    _fixConstValue(o);
    return 0;
}

int FixDescription_2::visitAssign(Assign &o)
{
    GuideVisitor::visitAssign(o);
    if (o.getDelay() == nullptr)
        return 0;
    _checkViewTimeScale();
    _scaleTimeValue(o.getDelay());
    return 0;
}

int FixDescription_2::visitConst(Const &o)
{
    GuideVisitor::visitConst(o);
    if (dynamic_cast<Time *>(o.getType()) == nullptr)
        return 0;
    _checkViewTimeScale();
    _scaleTimeValue(o.getValue());
    return 0;
}

int FixDescription_2::visitFunctionCall(FunctionCall &o)
{
    GuideVisitor::visitFunctionCall(o);

    if (o.getName() != "_system_time" && o.getName() != "_system_stime" && o.getName() != "_system_realtime")
        return 0;

    Time *t = dynamic_cast<Time *>(hif::semantics::getOtherOperandType(&o, _sem));
    if (t == nullptr)
        return 0;

    _checkViewTimeScale();
    _scaleTimeValue(&o);

    return 0;
}

int FixDescription_2::visitSignal(Signal &o)
{
    GuideVisitor::visitSignal(o);
    if (dynamic_cast<Time *>(o.getType()) == nullptr)
        return 0;
    _checkViewTimeScale();
    if (o.getValue() == nullptr)
        o.setValue(_factory.realval(1.0));
    _scaleTimeValue(o.getValue());
    return 0;
}

int FixDescription_2::visitSystem(System &o)
{
    _currentSystem = &o;
    GuideVisitor::visitSystem(o);
    return 0;
}

int FixDescription_2::visitVariable(Variable &o)
{
    GuideVisitor::visitVariable(o);

    if (dynamic_cast<Time *>(o.getType()) == nullptr)
        return 0;
    _checkViewTimeScale();
    if (o.getValue() == nullptr)
        o.setValue(_factory.realval(1.0));
    _scaleTimeValue(o.getValue());
    return 0;
}

int FixDescription_2::visitView(View &o)
{
    View *restore = _currentView;
    _currentView  = &o;
    GuideVisitor::visitView(o);
    _currentView = restore;

    return 0;
}

int FixDescription_2::visitWait(Wait &o)
{
    GuideVisitor::visitWait(o);
    _manageWaitActions(&o);

    if (o.getTime() == nullptr)
        return 0;
    _checkViewTimeScale();
    _scaleTimeValue(o.getTime());

    return 0;
}

int FixDescription_2::visitParameterAssign(ParameterAssign &o)
{
    GuideVisitor::visitParameterAssign(o);

    Type *valT = hif::semantics::getSemanticType(o.getValue(), _sem);
    messageAssert(valT != nullptr, "Cannot type parameter assign value", o.getValue(), _sem);

    String *valString = dynamic_cast<String *>(valT);
    if (valString == nullptr)
        return 0;

    Type *passT = hif::semantics::getSemanticType(&o, _sem);
    messageAssert(passT != nullptr, "Cannot type parameter assign", &o, _sem);

    Bitvector *passBv = dynamic_cast<Bitvector *>(passT);
    if (passBv == nullptr)
        return 0;

    // to fix
    Parameter *p = hif::semantics::getDeclaration(&o, _sem);
    messageAssert(p != nullptr, "Declaration not found", &o, _sem);

    p->setType(_factory.string());
    for (auto & itr : _refMap[p]) {
        hif::semantics::resetTypes(itr, false);
    }

    return 0;
}

int FixDescription_2::visitPortAssign(PortAssign &o)
{
    GuideVisitor::visitPortAssign(o);

    Port *p = hif::semantics::getDeclaration(&o, _sem);
    messageAssert(p != nullptr, "Declaration not found", &o, _sem);
    if (p->getDirection() == dir_in)
        return 0;

    Type *paType = hif::semantics::getSemanticType(&o, _sem);
    messageAssert(paType != nullptr, "Cannot type portAssign", &o, _sem);
    if (o.getValue() == nullptr)
        return 0;
    Type *valueType = hif::semantics::getSemanticType(o.getValue(), _sem);
    messageAssert(valueType != nullptr, "Cannot type portAssign value", o.getValue(), _sem);

    Value *paSize    = hif::semantics::typeGetSpanSize(paType, _sem);
    Value *valueSize = hif::semantics::typeGetSpanSize(valueType, _sem);

    hif::manipulation::SimplifyOptions sopt;
    sopt.simplify_constants           = true;
    sopt.simplify_template_parameters = true;
    sopt.simplify_defines             = true;
    Value *paSizeSimpl                = hif::manipulation::simplify(paSize, _sem, sopt);
    Value *valueSizeSimpl             = hif::manipulation::simplify(valueSize, _sem, sopt);

    if (hif::equals(paSizeSimpl, valueSizeSimpl)) {
        delete paSizeSimpl;
        delete valueSizeSimpl;
        return 0;
    }

    Int tmp;
    tmp.setSpan(new Range(63, 0));
    tmp.setSigned(true);
    tmp.setConstexpr(false);

    Value *transPaSize = hif::manipulation::transformValue(paSizeSimpl, &tmp, _sem);
    delete paSizeSimpl;

    Value *transValueSize = hif::manipulation::transformValue(valueSizeSimpl, &tmp, _sem);
    delete valueSizeSimpl;

    IntValue *paIvSize    = dynamic_cast<IntValue *>(transPaSize);
    IntValue *valueIvSize = dynamic_cast<IntValue *>(transValueSize);

    if (paIvSize == nullptr || valueIvSize == nullptr) {
        delete transPaSize;
        delete transValueSize;
        return 0;
    }

    const long long int paIntSize    = paIvSize->getValue();
    const long long int valueIntSize = valueIvSize->getValue();
    delete transPaSize;
    delete transValueSize;

    if (valueIntSize <= paIntSize) {
        // truncated.. no problem
        return 0;
    }

    // create a support signal and related continuos assign.

    Signal *sig = new Signal();
    sig->setName(NameTable::getInstance()->getFreshName(p->getName(), "_partial_sig"));
    sig->setType(hif::copy(paType));
    Port *instPort = hif::manipulation::instantiate(&o, _sem);
    if (instPort != nullptr && instPort->getValue() != nullptr) {
        sig->setValue(hif::copy(instPort->getValue()));
    } else {
        sig->setValue(_sem->getTypeDefaultValue(sig->getType(), p));
    }

    BaseContents *bc = hif::getNearestParent<BaseContents>(&o);
    messageAssert(bc != nullptr, "Cannot find parent contents", &o, _sem);
    bc->declarations.push_back(sig);

    // Adding process to update output port:
    Value *v    = _factory.slice(o.setValue(nullptr), hif::copy(hif::typeGetSpan(sig->getType(), _sem)));
    Assign *ass = _factory.assignment(v, new Identifier(sig->getName()));
    o.setValue(new Identifier(sig->getName()));

    if (bc->getGlobalAction() == nullptr)
        bc->setGlobalAction(new GlobalAction());
    bc->getGlobalAction()->actions.push_back(ass);

    return 0;
}

void FixDescription_2::_fixConstValue(ConstValue &o)
{
    if (o.getType() != nullptr)
        return;
    Range *r       = dynamic_cast<Range *>(o.getParent());
    Aggregate *agg = dynamic_cast<Aggregate *>(o.getParent());
    if (r != nullptr || agg != nullptr)
        return;
    o.setType(_sem->getTypeForConstant(&o));
}

void FixDescription_2::_manageClockCondition(StateTable *process)
{
    messageAssert(process != nullptr, "Unexpected nullptr process", process, nullptr);
    // Assuming only one state.
    messageAssert(process->states.size() == 1, "Unexpected states size", process, nullptr);

    // Assuming to have a filled sensitivity list (at least one).
    if (process->sensitivity.empty() && process->sensitivityPos.empty() && process->sensitivityNeg.empty())
        return;

    // If no clock has been passed, the process cannot be considered synch.
    size_t listsSize = process->sensitivity.size() + process->sensitivityPos.size() + process->sensitivityNeg.size();

    // Detect clock.
    std::string clockName = _detectClockSignal(process);

    // Unable to detect the clock.
    if (clockName.empty())
        return;

    int clockEdge = _getSignalEdge(process, clockName);

    // Insert the "if..else if (rising_edge)".
    if (listsSize == 2) {
        // Asynch reset: thus the body of the process should already have
        // "if ( reset == 1 )". Thus, just insert a clock condition:
        if (clockEdge == 0)
            return;

        State *state = process->states.front();
        Action *ao   = state->actions.front();
        If *co       = dynamic_cast<If *>(ao);
        messageAssert(co != nullptr, "Unexpected non-IF action", ao, _sem);
        for (BList<IfAlt>::iterator i = co->alts.begin(); i != co->alts.end(); ++i) {
            // Skipping the first condition: should be the reset.
            if (i == co->alts.begin())
                continue;
            _createClockedCondition((*i)->actions, clockName, clockEdge);
        }

        _createClockedCondition(co->defaults, clockName, clockEdge);

        return;
    }

    messageDebugAssert(listsSize == 1, "Unexpecte case", nullptr, _sem);
    // Means: synchronous reset!
    // Reset means nothing, since should be already managed inside process actions.
    if (clockEdge == 0)
        return;
    _createClockedCondition(process->states.front()->actions, clockName, clockEdge);
}

void FixDescription_2::_collectInitialProcesses(Contents *c, InitialProcesses &initialProcesses)
{
    for (BList<StateTable>::iterator i = c->stateTables.begin(); i != c->stateTables.end(); ++i) {
        StateTable *st = *i;
        if (st->getFlavour() != pf_initial)
            continue;

        initialProcesses.push_back(st);
    }
}

void FixDescription_2::_splitInitialProcesses(Contents *c, InitialProcesses &initialProcesses)
{
    for (InitialProcesses::iterator i = initialProcesses.begin(); i != initialProcesses.end(); ++i) {
        StateTable *st = *i;
        messageAssert(st->states.size() == 1, "Unexpected number of states", st, _sem);

        // Move actions after first wrong statement in a new non-initial process
        BList<Action> afterWrongStatementActions;
        bool found = false;
        for (BList<Action>::iterator j = st->states.front()->actions.begin(); j != st->states.front()->actions.end();) {
            Action *act = *j;
            if (!found) {
                // not already found.. search wrong statements in current action.
                if (!_checkWrongStatement(act)) {
                    // not found wrong statement continue with next!
                    ++j;
                    continue;
                }
            }

            // Found invalid statement. Move current and following action to new list.
            found = true;
            j     = j.remove();
            afterWrongStatementActions.push_back(act);
        }

        if (!found)
            continue;
        messageAssert(!afterWrongStatementActions.empty(), "Wrong list size!", nullptr, _sem);

        // If found a wrong statement, create a new non-initial state table:
        // - moving in it all actions after first wrong statement
        // - setting dont-initialize flag to false
        StateTable *splitted = _factory.stateTable(
            NameTable::getInstance()->getFreshName(st->getName()), _factory.noDeclarations(), _factory.noActions(),
            false, pf_hdl);

        splitted->states.front()->actions.merge(afterWrongStatementActions);
        splitted->states.front()->actions.push_back(new Wait());

        c->stateTables.push_back(splitted);
    }
}

void FixDescription_2::_mergeInitialProcesses(InitialProcesses &initialProcesses)
{
    if (initialProcesses.empty()) {
        // almost one initial process: nothing to do.
        return;
    }

    // Merging all actions of all initial processes into the first
    // initial process state table.
    StateTable *initialSt = initialProcesses.front();
    for (InitialProcesses::iterator i = initialProcesses.begin(); i != initialProcesses.end(); ++i) {
        if (i == initialProcesses.begin())
            continue;

        StateTable *st = *i;
        initialSt->states.front()->actions.merge(st->states.front()->actions);
        st->replace(nullptr);
        delete st;
    }
}

void FixDescription_2::_manageWaitActions(Wait *o)
{
    ProcessFlavour flavour;
    const auto found = hif::objectGetProcessFlavour(o, flavour);
    const bool isRtl = !found || flavour != pf_analog;
    if (!isRtl)
        return;
    messageAssert(o->isInBList(), "Unexpected object location in tree", o, _sem);
    BList<Action>::iterator it(o);
    it.insert_after(o->actions);
}

void FixDescription_2::_checkViewTimeScale()
{
    if (_currentView == nullptr)
        return;
    if (!_currentView->declarations.empty()) {
        messageAssert(_currentView->declarations.size() == 2, "Unexpected view declarations", _currentView, _sem);
        return;
    }

    if (_addedDefaultTimeScale)
        return;

    _currentSystem->declarations.push_back(
        _factory.constant(_factory.time(), "hif_verilog_timescale_unit", _factory.timeval(1.0, TimeValue::time_ns)));

    _currentSystem->declarations.push_back(_factory.constant(
        _factory.time(), "hif_verilog_timescale_precision", _factory.timeval(10.0, TimeValue::time_ps)));

    _addedDefaultTimeScale = true;
}

void FixDescription_2::_scaleTimeValue(Value *v)
{
    // If view == nullptr --> constants into system
    if (_currentView == nullptr)
        return;
    DataDeclaration *pdd = dynamic_cast<DataDeclaration *>(v->getParent());
    if (pdd != nullptr) {
        // absolute times
        if (pdd->getName() == "hif_verilog_timescale_unit")
            return;
        if (pdd->getName() == "hif_verilog_timescale_precision")
            return;
    }

    // skip time values
    Type *t = hif::semantics::getSemanticType(v, _sem);
    messageAssert(t != nullptr, "Cannot type value", v, _sem);

    if (dynamic_cast<Time *>(t) != nullptr) {
        // already absolute
        return;
    }

    Expression *e = new Expression();
    e->setOperator(op_mult);
    e->setValue2(_factory.identifier("hif_verilog_timescale_unit"));
    v->replace(e);
    e->setValue1(v);
}

bool FixDescription_2::_checkWrongStatement(Object *root)
{
    hif::HifTypedQuery<Wait> q1;
    q1.collectObjectMethod          = &collectObjectMethod;
    q1.sem                          = _sem;
    q1.checkInsideCallsDeclarations = true;
    q1.onlyFirstMatch               = true;

    hif::HifTypedQuery<Assign> q2;
    q1.setNextQueryType(&q2);

    hif::HifTypedQuery<Expression> q3;
    q2.setNextQueryType(&q3);

    std::list<Object *> result;
    hif::search(result, root, q1);

    return !result.empty();
}

void performStep2Refinements(hif::System *o, hif::semantics::ILanguageSemantics *sem)
{
    RefMap refMap;
    hif::semantics::GetReferencesOptions opt;
    hif::semantics::getAllReferences(refMap, sem, o, opt);
    hif::semantics::typeTree(o, sem);

    FixDescription_2 v(refMap, sem);
    o->acceptVisitor(v);
}

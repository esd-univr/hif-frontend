/// @file FixDescription_3.cpp
/// @brief
/// Copyright (c) 2024-2025, Electronic Systems Design (ESD) Group,
/// Univeristy of Verona.
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#include <cstdlib>
#include <iostream>

#include <hif/hif.hpp>

#include "verilog2hif/post_parsing_methods.hpp"
#include "verilog2hif/support.hpp"

#ifndef NDEBUG
//#define DBG_PRINT_FIX3_STEP_FILES
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-member-function"
#elif defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

using namespace hif;

// ///////////////////////////////////////////////////////////////////
// Support
// ///////////////////////////////////////////////////////////////////
typedef hif::manipulation::ViewSet Views;
typedef std::set<ViewReference *> ViewRefs;
typedef std::set<DataDeclaration *> DataDeclarations;

BaseContents *_getBaseContents(Declaration *context)
{
    Port *p = dynamic_cast<Port *>(context);
    if (p != nullptr) {
        View *v = hif::getNearestParent<View>(p);
        messageAssert(v != nullptr, "Cannot find parent view", p, nullptr);
        messageAssert(v->getContents() != nullptr, "Contents not found.", p, nullptr);
        return v->getContents();
    }

    BaseContents *bc = getNearestParent<BaseContents>(context);
    messageAssert(bc != nullptr, "Contents not found.", p, nullptr);
    return bc;
}

bool _skipStandardFunctionCall(Object *o, const hif::HifQueryBase *q)
{
    FunctionCall *fc = dynamic_cast<FunctionCall *>(o);
    if (fc == nullptr)
        return false;

    Function *foo = hif::semantics::getDeclaration(fc, q->sem);
    if (foo == nullptr)
        return true;

    return !hif::declarationIsPartOfStandard(foo);
}

// ///////////////////////////////////////////////////////////////////
// prerefine fixes
// ///////////////////////////////////////////////////////////////////

typedef hif::semantics::ReferencesMap RefMap;
typedef hif::semantics::ReferencesSet RefSet;

void _fixUnreferenced(RefMap &refMap, hif::semantics::ILanguageSemantics * /*sem*/)
{
    RefSet trash;
    for (RefMap::iterator i = refMap.begin(); i != refMap.end(); ++i) {
        Signal *sig = dynamic_cast<Signal *>(i->first);

        if (sig == nullptr || !i->second.empty())
            continue;

        // sig w/o refs is refined to var
        Variable *var = new Variable();
        var->setName(sig->getName());
        var->setType(sig->setType(nullptr));
        var->setValue(sig->setValue(nullptr));

        sig->replace(var);
        trash.insert(sig);
        refMap[var];
    }

    // clean signals moved to variables.
    for (RefSet::iterator i = trash.begin(); i != trash.end(); ++i) {
        refMap.erase(static_cast<Declaration *>(*i));
        delete *i;
    }
}

bool _checkSubNodeOfTop(Port *p, Views &topViews)
{
    for (Views::iterator i = topViews.begin(); i != topViews.end(); ++i) {
        View *top = *i;
        if (hif::isSubNode(p, top))
            return true;
    }
    return false;
}

void _fixOutputPorts(Views &topViews, RefMap &refMap, hif::semantics::ILanguageSemantics *sem)
{
    hif::HifFactory factory(sem);

    // Output ports assigned by continuous assignments are replaced with explicit
    // processes.
    hif::application_utils::WarningList fixed;
    for (RefMap::iterator i = refMap.begin(); i != refMap.end(); ++i) {
        Port *p = dynamic_cast<Port *>(i->first);
        if (p == nullptr)
            continue;
        if (p->getDirection() != dir_out && p->getDirection() != dir_inout)
            continue;
        const bool inTopView = _checkSubNodeOfTop(p, topViews);
        RefSet &portRefs     = i->second;

        RefSet continuousTargets;
        RefSet readRefs;
        RefSet nonblockingSet;
        RefSet portassSet;
        for (RefSet::iterator j = portRefs.begin(); j != portRefs.end(); ++j) {
            Object *symb        = *j;
            PortAssign *portAss = dynamic_cast<PortAssign *>(symb);
            if (portAss != nullptr) {
                portassSet.insert(symb);
                continue;
            }

            const bool isRead = !hif::manipulation::isInLeftHandSide(symb);
            if (isRead) {
                readRefs.insert(symb);
                continue;
            }

            Assign *ass = hif::getNearestParent<Assign>(symb);
            messageAssert(ass != nullptr, "Cannot find parent assign", symb, sem);

            GlobalAction *gact          = hif::getNearestParent<GlobalAction>(symb);
            const bool isBlockingAssign = gact != nullptr || !ass->checkProperty(NONBLOCKING_ASSIGNMENT);

            if (isBlockingAssign)
                continuousTargets.insert(symb);
            else
                nonblockingSet.insert(symb);
        }

        // Assigned only by non-blocking.
        // Possible reads to the output port will see a delta-cycle delay,
        // as in Verilog.
        if (continuousTargets.empty())
            continue;

        // Ref designs:
        // - openCores/or1200_immu (submodule of or1200_toop)
        // - openCores/i2cSlaveTop
        if (readRefs.empty()) {
            for (RefSet::iterator j = continuousTargets.begin(); j != continuousTargets.end(); ++j) {
                Object *symb = *j;
                Assign *ass  = hif::getNearestParent<Assign>(symb);
                if (!inTopView)
                    fixed.push_back(ass);
                ass->addProperty(NONBLOCKING_ASSIGNMENT);
                GlobalAction *gact = hif::getNearestParent<GlobalAction>(symb);
                if (gact == nullptr)
                    continue;
                std::set<StateTable *> list;
                hif::manipulation::transformGlobalActions(ass, list, sem);
                // Update references
                for (std::set<StateTable *>::iterator k = list.begin(); k != list.end(); ++k) {
                    StateTable *st = *k;
                    // Ref design: openCores/log2
                    //st->setDontInitialize(true);
                    hif::semantics::getAllReferences(refMap, sem, st);
                }
            }
        } else {
            // There are some reads, and also some blocking/continuous assignments.
            // Replacing all refs with a new signal, and adding a new process to
            // update the output port.
            // Ref design: openCores/or1200_top
            Signal *sig = new Signal();
            sig->setName(NameTable::getInstance()->getFreshName(p->getName(), "_out_sig"));
            sig->setType(hif::copy(p->getType()));
            sig->setValue(hif::copy(p->getValue()));
            hif::manipulation::addDeclarationInContext(sig, p);

            RefSet &sigRefs = refMap[sig];
            for (RefSet::iterator j = continuousTargets.begin(); j != continuousTargets.end(); ++j) {
                Object *symb = *j;
                hif::objectSetName(symb, sig->getName());
                hif::semantics::setDeclaration(symb, sig);
                sigRefs.insert(symb);
            }
            for (RefSet::iterator j = readRefs.begin(); j != readRefs.end(); ++j) {
                Object *symb = *j;
                hif::objectSetName(symb, sig->getName());
                hif::semantics::setDeclaration(symb, sig);
                sigRefs.insert(symb);
            }
            for (RefSet::iterator j = nonblockingSet.begin(); j != nonblockingSet.end(); ++j) {
                Object *symb = *j;
                hif::objectSetName(symb, sig->getName());
                hif::semantics::setDeclaration(symb, sig);
                sigRefs.insert(symb);
            }

            // Updating ref map
            portRefs.clear();
            portRefs.insert(portassSet.begin(), portassSet.end());

            // Adding process to update output port:
            Assign *ass = factory.assignment(new Identifier(p->getName()), new Identifier(sig->getName()));
            ass->addProperty(NONBLOCKING_ASSIGNMENT);
            StateTable *st = factory.stateTable(
                (p->getName() + std::string("_update_process")), factory.noDeclarations(), ass,
                false // Ref design: openCores/i2cSlaveTop
            );
            st->sensitivity.push_back(new Identifier(sig->getName()));
            hif::manipulation::addDeclarationInContext(st, p);
            hif::semantics::getAllReferences(refMap, sem, st);
        }
    }

    messageWarningList(
        true,
        "Found at least one output port assigned by continuous"
        " assignment. It will be replaced with a process."
        " This could lead to a non-equivalent output design description.",
        fixed);
}

void _performChecks(RefMap &refMap, hif::semantics::ILanguageSemantics *sem)
{
    hif::application_utils::WarningSet fixedParams;
    hif::application_utils::WarningSet fixedVars;

    for (RefMap::iterator i = refMap.begin(); i != refMap.end(); ++i) {
        if (dynamic_cast<Parameter *>(i->first) != nullptr) {
            // Parameters cannot be signal (are always temporaries)
            // So they must be assign as blocking.
            Parameter *p   = static_cast<Parameter *>(i->first);
            RefSet &refSet = i->second;
            for (RefSet::iterator j = refSet.begin(); j != refSet.end(); ++j) {
                if (!hif::manipulation::isInLeftHandSide(*j))
                    continue;

                Assign *ass = hif::getNearestParent<Assign>(*j);
                messageAssert(ass != nullptr, "Cannot find assign", *j, sem);

                if (ass->checkProperty(NONBLOCKING_ASSIGNMENT)) {
                    ass->removeProperty(NONBLOCKING_ASSIGNMENT);
                    fixedParams.insert(p);
                }
            }
        } else if (dynamic_cast<Variable *>(i->first) != nullptr) {
            // variables of automatic task or function cannot be signals.
            // So they must be assign as blocking.
            Variable *var  = static_cast<Variable *>(i->first);
            RefSet &refSet = i->second;
            for (RefSet::iterator j = refSet.begin(); j != refSet.end(); ++j) {
                if (!hif::manipulation::isInLeftHandSide(*j))
                    continue;

                Assign *ass = hif::getNearestParent<Assign>(*j);
                messageAssert(ass != nullptr, "Cannot find assign", *j, sem);

                if (ass->checkProperty(NONBLOCKING_ASSIGNMENT)) {
                    ass->removeProperty(NONBLOCKING_ASSIGNMENT);
                    fixedVars.insert(var);
                }
            }
        }
    }

    messageWarningList(
        true,
        "Found at least one task or function parameter assigned through a"
        " non-blocking assignment. The assignment will be translated as "
        "blocking."
        " This could lead to a non-equivalent output design description.",
        fixedParams);

    messageWarningList(
        true,
        "Found at least one task or function internal variable assigned "
        "through a"
        " non-blocking assignment. The assignment will be translated as "
        "blocking."
        " This could lead to a non-equivalent output design description.",
        fixedVars);
}

void _prerefineFixes(Views &topViews, RefMap &refMap, hif::semantics::ILanguageSemantics *sem)
{
    // Unreferenced signal can be safetely translated as variables.
    _fixUnreferenced(refMap, sem);

    // Output ports assigned by continuous assignments are replaced with explicit
    // processes. If found, a warnings is raised.
    _fixOutputPorts(topViews, refMap, sem);

    // Checks:
    // - calls parameters. They cannot be signal (are always temporaries).
    //   So they must be assign as blocking.
    // - variables of automatic task or function. They cannot be signals.
    //   So they must be assign as blocking.
    _performChecks(refMap, sem);
}

// /////////////////////////////////////////////////////////////////////////////
// _splitLogicConesLoops
// /////////////////////////////////////////////////////////////////////////////

bool _assignQuery(Object *o, const HifQueryBase * /*query*/)
{
    // collecting continuous assigns on members and slices
    Assign *ass = dynamic_cast<Assign *>(o);
    if (ass == nullptr)
        return false;
    GlobalAction *ga = dynamic_cast<GlobalAction *>(ass->getParent());
    if (ga == nullptr)
        return false;
    Member *member = dynamic_cast<Member *>(ass->getLeftHandSide());
    Slice *slice   = dynamic_cast<Slice *>(ass->getLeftHandSide());
    return slice != nullptr || member != nullptr;
}

Value *_refMatchesToken(Object *ref, Value *token)
{
    Object *last = ref;
    while (last != nullptr) {
        Member *member = dynamic_cast<Member *>(last->getParent());
        Slice *slice   = dynamic_cast<Slice *>(last->getParent());
        if (member == nullptr && slice == nullptr)
            break;
        last = last->getParent();
    }

    if (hif::equals(last, token))
        return static_cast<Value *>(last);
    return nullptr;
}

void _splitLogicConesLoops(System *system, RefMap &refMap, hif::semantics::ILanguageSemantics *sem)
{
    // Manipulation example:
    // a[0] = expr1
    // a[1] = expr2
    // -->
    // a_0 = expr1
    // a_1 = expr2
    // a[0] = a_0
    // a[1] = a_1
    // This should ensure to split possible loops.
    typedef hif::HifTypedQuery<Assign> Query;
    typedef Query::Results QueryResults;
    typedef std::set<Value *> ValueSet;
    typedef std::map<DataDeclaration *, ValueSet> DeclarationTokens;
    typedef std::map<Value *, std::string> TokenNames;

    // Get candidate assigns.
    Query query;
    query.collectObjectMethod = &_assignQuery;
    query.skipStandardScopes  = true;
    Query::Results assigns;
    hif::search(assigns, system, query);
    // Collect assign targets
    DataDeclarations declarations;
    DeclarationTokens declarationTokens;
    TokenNames tokenNames;
    for (QueryResults::iterator i = assigns.begin(); i != assigns.end(); ++i) {
        Assign *ass          = *i;
        Value *tgtValue      = ass->getLeftHandSide();
        Value *tgt           = hif::getTerminalPrefix(tgtValue);
        Declaration *tgtDecl = hif::semantics::getDeclaration(tgt, sem);
        messageAssert(tgtDecl != nullptr, "Declaration not found", tgt, sem);
        DataDeclaration *tgtDataDecl = dynamic_cast<DataDeclaration *>(tgtDecl);
        messageAssert(tgtDataDecl != nullptr, "Expected DataDeclaration", tgtDecl, sem);
        declarations.insert(tgtDataDecl);
        ValueSet &valueSet = declarationTokens[tgtDataDecl];
        std::stringstream ss;
        ss << tgtDataDecl->getName() << "_partial" << valueSet.size();
        std::string name = hif::NameTable::getInstance()->getFreshName(ss.str());
        valueSet.insert(tgtValue);
        tokenNames[tgtValue] = name;
    }

    // 1- Replacing all prefixed refs with fresh signals.
    // 2- Generating assigns from tokens to original declaration.
    // 3- Generating new signal declarations
    hif::Trash trash;
    for (DataDeclarations::iterator i = declarations.begin(); i != declarations.end(); ++i) {
        DataDeclaration *decl = *i;
        ValueSet &values      = declarationTokens[decl];

        // @TODO remove refs from refMap and add tokendecl refs to refMap
        for (ValueSet::const_iterator j = values.begin(); j != values.end(); ++j) {
            Value *token          = *j;
            RefSet &refSet        = refMap[decl];
            std::string tokenName = tokenNames[token];

            // 1
            Signal *sig = new Signal();
            hif::manipulation::addDeclarationInContext(sig, decl, false);
            sig->setName(tokenName);
            Type *tokenType = hif::semantics::getSemanticType(token, sem);
            messageAssert(tokenType != nullptr, "Cannot type token", token, sem);
            sig->setType(hif::copy(tokenType));
            if (decl->getValue() == nullptr) {
                sig->setValue(sem->getTypeDefaultValue(tokenType, decl));
            } else {
                Value *init = hif::copy(token);
                sig->setValue(init); // before getTerminalPrefix!
                Value *prefix = hif::getTerminalPrefix(init);
                prefix->replace(hif::copy(decl->getValue()));
                delete prefix;
            }

            // 2
            Assign *ass = new Assign();
            ass->setLeftHandSide(hif::copy(token));
            ass->setRightHandSide(new Identifier(tokenName));
            BList<Object>::iterator jt(token->getParent());
            jt.insert_after(ass);

            // 3
            for (RefSet::const_iterator k = refSet.begin(); k != refSet.end(); ++k) {
                Object *ref   = *k;
                Value *topRef = _refMatchesToken(ref, token);
                if (topRef == nullptr)
                    continue;
                trash.insert(topRef);
                Identifier *id = new Identifier(tokenName);
                topRef->replace(id);
            }
        }
    }

    // Final cleanup.
    trash.clear();
}

// ///////////////////////////////////////////////////////////////////
// Info struct
// ///////////////////////////////////////////////////////////////////

struct InfoStruct {
    InfoStruct();
    ~InfoStruct();
    InfoStruct(const InfoStruct &other);
    InfoStruct &operator=(const InfoStruct &other);

    // Ref is port in instance binding
    RefSet portUsing;
    // Ref is in process sensitivity
    RefSet sensitivityUsing;
    // Ref is used as rhs of port binding
    RefSet bindUsing;
    // Ref is used in wait
    RefSet waitUsing;
    // Ref is rhs of continuous
    RefSet rhsContinuousUsing;
    // Ref is lhs of continuous
    RefSet lhsContinuousUsing;
    // Ref is lhs of non-blocking
    RefSet lhsNonBlockingUsing;
    // Ref is lhs of blocking
    RefSet lhsBlockingUsing;
    // Ref is read (in condition or RHS of procedural assigns or param of method call)
    RefSet readUsing;

    bool wasLhsContinuous;

    bool isOnlyInContinuousAssignments() const;
};

InfoStruct::InfoStruct()
    : portUsing()
    , sensitivityUsing()
    , bindUsing()
    , waitUsing()
    , rhsContinuousUsing()
    , lhsContinuousUsing()
    , lhsNonBlockingUsing()
    , lhsBlockingUsing()
    , readUsing()
    , wasLhsContinuous(false)
{
    // ntd
}

InfoStruct::~InfoStruct()
{
    // ntd
}

InfoStruct::InfoStruct(const InfoStruct &other)
    : portUsing(other.portUsing)
    , sensitivityUsing(other.sensitivityUsing)
    , bindUsing(other.bindUsing)
    , waitUsing(other.waitUsing)
    , rhsContinuousUsing(other.rhsContinuousUsing)
    , lhsContinuousUsing(other.lhsContinuousUsing)
    , lhsNonBlockingUsing(other.lhsNonBlockingUsing)
    , lhsBlockingUsing(other.lhsBlockingUsing)
    , readUsing(other.readUsing)
    , wasLhsContinuous(other.wasLhsContinuous)
{
    // ntd
}

InfoStruct &InfoStruct::operator=(const InfoStruct &other)
{
    if (this == &other)
        return *this;

    portUsing           = other.portUsing;
    sensitivityUsing    = other.sensitivityUsing;
    bindUsing           = other.bindUsing;
    waitUsing           = other.waitUsing;
    rhsContinuousUsing  = other.rhsContinuousUsing;
    lhsContinuousUsing  = other.lhsContinuousUsing;
    lhsNonBlockingUsing = other.lhsNonBlockingUsing;
    lhsBlockingUsing    = other.lhsBlockingUsing;
    readUsing           = other.readUsing;
    wasLhsContinuous    = other.wasLhsContinuous;

    return *this;
}

typedef std::map<DataDeclaration *, InfoStruct> InfoMap;

bool InfoStruct::isOnlyInContinuousAssignments() const
{
    if (!sensitivityUsing.empty())
        return false;
    if (!bindUsing.empty())
        return false;
    if (!waitUsing.empty())
        return false;
    if (!lhsNonBlockingUsing.empty())
        return false;
    if (!lhsBlockingUsing.empty())
        return false;
    if (!readUsing.empty())
        return false;

    return true;
}

void _calculateRequiredDeclType(
    bool &isConnectionSignal,
    bool &isSignal,
    bool &isVariable,
    const InfoStruct &infos,
    DataDeclaration *decl)
{
    //        const bool isOutputPort = dynamic_cast<Port*>(decl) != nullptr
    //                && static_cast<Port*>(decl)->getDirection() == dir_out;

    isConnectionSignal = dynamic_cast<Port *>(decl) != nullptr || !infos.portUsing.empty() || !infos.bindUsing.empty();

    isSignal = isConnectionSignal || !infos.sensitivityUsing.empty() || !infos.waitUsing.empty() ||
               !infos.lhsNonBlockingUsing.empty();

    isVariable = !infos.lhsContinuousUsing.empty() || !infos.lhsBlockingUsing.empty() || infos.wasLhsContinuous;
}

void _fillInfoMap(RefMap &refMap, InfoMap &infoMap, const bool removeProperty)
{
    for (RefMap::iterator i = refMap.begin(); i != refMap.end(); ++i) {
        DataDeclaration *decl = dynamic_cast<DataDeclaration *>(i->first);

        Signal *sig = dynamic_cast<Signal *>(decl);
        Port *port  = dynamic_cast<Port *>(decl);
        if (sig == nullptr && port == nullptr)
            continue;

        // For each interesting symbol check the context and push it into the
        // related list.
        for (RefSet::iterator j = i->second.begin(); j != i->second.end(); ++j) {
            Object *symb = *j;

            Assign *ass = hif::getNearestParent<Assign>(symb);
            Wait *wait  = hif::getNearestParent<Wait>(symb);
            ObjectSensitivityOptions opts;
            opts.checkAll = true;
            if (dynamic_cast<PortAssign *>(symb) != nullptr) {
                infoMap[decl].portUsing.insert(symb);
            } else if (hif::objectIsInSensitivityList(symb)) {
                infoMap[decl].sensitivityUsing.insert(symb);
            } else if (hif::getNearestParent<PortAssign>(symb) != nullptr) {
                infoMap[decl].bindUsing.insert(symb);
            } else if (
                wait != nullptr &&
                (hif::objectIsInSensitivityList(symb, opts) || hif::isSubNode(symb, wait->getCondition()))) {
                infoMap[decl].waitUsing.insert(symb);
            } else if (ass != nullptr) {
                const bool isTarget    = hif::manipulation::isInLeftHandSide(symb);
                const bool isInGlobact = dynamic_cast<GlobalAction *>(ass->getParent()) != nullptr;
                const bool hasBlocking = !ass->checkProperty(NONBLOCKING_ASSIGNMENT);
                if (!isTarget && isInGlobact) {
                    infoMap[decl].rhsContinuousUsing.insert(symb);
                } else if (isTarget && isInGlobact) {
                    infoMap[decl].wasLhsContinuous = true;
                    infoMap[decl].lhsContinuousUsing.insert(symb);
                } else if (isTarget && !isInGlobact && !hasBlocking) {
                    if (removeProperty)
                        ass->removeProperty(NONBLOCKING_ASSIGNMENT);
                    infoMap[decl].lhsNonBlockingUsing.insert(symb);
                } else if (isTarget && !isInGlobact && hasBlocking) {
                    infoMap[decl].lhsBlockingUsing.insert(symb);
                } else {
                    // rhs of assign
                    infoMap[decl].readUsing.insert(symb);
                }
            } else {
                // pcall, condition of if, etc.
                infoMap[decl].readUsing.insert(symb);
            }
        }
    }
}

// ///////////////////////////////////////////////////////////////////
// _fixLogicCones()
// ///////////////////////////////////////////////////////////////////
typedef hif::analysis::Types<DataDeclaration, DataDeclaration>::Graph LogicGraph;
typedef hif::analysis::Types<DataDeclaration, DataDeclaration>::Set LogicSet;
typedef hif::analysis::Types<DataDeclaration, DataDeclaration>::List LogicList;
typedef hif::semantics::SymbolList SymbList;
typedef std::map<DataDeclaration *, Procedure *> ConesMap;
typedef hif::analysis::Types<DataDeclaration, DataDeclaration>::Map SensitivityMap;

void _generateLogicConesGraph(InfoMap &infoMap, LogicGraph &logicGraph, hif::semantics::ILanguageSemantics *sem)
{
    // For all interesting decls (i.e. ports and signals)
    for (InfoMap::iterator i = infoMap.begin(); i != infoMap.end(); ++i) {
        DataDeclaration *decl       = i->first;
        InfoStruct &info            = infoMap[decl];
        bool hasOnlyConstantDrivers = true;

        // The graph is related to target of continuous assigns,
        // between the target and its source symbols
        for (RefSet::iterator j = info.lhsContinuousUsing.begin(); j != info.lhsContinuousUsing.end(); ++j) {
            Object *symb = *j;
            Assign *ass  = hif::getNearestParent<Assign>(symb);
            messageAssert(ass != nullptr, "Unexpected scope", symb, sem);

            SymbList symbolList;
            hif::semantics::collectSymbols(symbolList, ass, sem);
            for (SymbList::iterator k = symbolList.begin(); k != symbolList.end(); ++k) {
                Object *innerSymb   = *k;
                Identifier *innerId = dynamic_cast<Identifier *>(innerSymb);
                if (innerId == nullptr) {
                    hasOnlyConstantDrivers = false;
                    continue;
                }

                DataDeclaration *innerDecl = hif::semantics::getDeclaration(innerId, sem);
                messageAssert(innerDecl != nullptr, "Declaration not found", innerId, sem);

                Signal *sig = dynamic_cast<Signal *>(innerDecl);
                Port *port  = dynamic_cast<Port *>(innerDecl);
                if (sig == nullptr && port == nullptr) {
                    hasOnlyConstantDrivers = false;
                    continue;
                }

                // Skipping refs to signals which are only read and never written
                // They will be moved to vars
                if (sig != nullptr) {
                    InfoStruct &infos       = infoMap[sig];
                    /*
                    if (infos.lhsBlockingUsing.empty()
                        && infos.lhsContinuousUsing.empty()
                        && infos.lhsNonBlockingUsing.empty()
                        && infos.bindUsing.empty()
                        && infos.portUsing.empty()
                        )
                        */
                    bool isConnectionSignal = false;
                    bool isSignal           = false;
                    bool isVariable         = false;
                    _calculateRequiredDeclType(isConnectionSignal, isSignal, isVariable, infos, sig);
                    if (!isSignal && !isVariable)
                        continue;
                }

                // Skipping self references: e.g. assign a = b + c (skip a)
                if (innerDecl == decl)
                    continue;

                hasOnlyConstantDrivers = false;

                // Fill the map (see typedefs in analysis.hh)
                logicGraph.first[decl].insert(innerDecl);
                logicGraph.first[innerDecl];
                logicGraph.second[innerDecl].insert(decl);
                logicGraph.second[decl];
            }
        }

        // This case:
        // assign a = 5;
        // assign b = a;
        // The signal b is only written by costant a.
        // The only problem is the initial value of b:
        // we must initialize it safely. The simplest chance is to leave the
        // assign to b, as a process which will be executed only one time, during
        // processes initialization. This supports also cases where b is written
        // partially.
        //
        // Ref design:
        // - opencCores/or1200_top
        // - custom/test_assigns
        if (hasOnlyConstantDrivers) {
            info.wasLhsContinuous = false;
            info.lhsContinuousUsing.clear();
        }
    }
}

void _generateConeFunctions(
    RefMap &refMap,
    InfoMap &infoMap,
    hif::semantics::ILanguageSemantics *sem,
    LogicList &sortedGraph,
    LogicGraph &logicGraph,
    ConesMap &conesMap)
{
    hif::HifFactory f(sem);

    // Used to ensure same order of cones declarations.
    typedef std::map<std::string, Procedure *> Cones;
    Cones cones;

    for (LogicList::iterator i = sortedGraph.begin(); i != sortedGraph.end(); ++i) {
        DataDeclaration *decl = *i;

        // Skipping all shit
        Signal *sig = dynamic_cast<Signal *>(decl);
        Port *port  = dynamic_cast<Port *>(decl);
        if (sig == nullptr && port == nullptr)
            continue;

        InfoStruct &infos     = infoMap[decl];
        const bool onlyInCont = (infos.isOnlyInContinuousAssignments());
        if (onlyInCont)
            continue;
        // Means: signal read by continuous, but written only by processes or modules.
        // No need of cone.
        if (infos.lhsContinuousUsing.empty())
            continue;

        // decl is used by processes (or in other places),
        // and therefore it must have its cone procedure.
        std::string pName = hif::NameTable::getInstance()->getFreshName((std::string("hif_cone_") + decl->getName()));
        Procedure *cone   = static_cast<Procedure *>(f.subprogram(nullptr, pName, f.noTemplates(), f.noParameters()));

        decl->getBList()->push_back(cone);
        cones[cone->getName()] = cone;
        //hif::manipulation::addDeclarationInContext(cone, decl, false); // wrong, globact was after all decls

        StateTable *st = f.stateTable("hif_cone", f.noDeclarations(), f.noActions());
        cone->setStateTable(st);
        State *s = st->states.front();

        // Inserting the contiunuous assigns,
        // since they will be then removed from the tree.
        for (RefSet::iterator j = infos.lhsContinuousUsing.begin(); j != infos.lhsContinuousUsing.end(); ++j) {
            Assign *ass = hif::getNearestParent<Assign>(*j);
            messageAssert(ass != nullptr, "Assign not found", *j, sem);

            s->actions.push_front(hif::copy(ass));
        }

        conesMap[decl] = cone;

        // Managing case of decl using both as target of continuous and
        // target of assing (blocking or not blocking) inside any process.
        if (!infos.lhsNonBlockingUsing.empty() || !infos.lhsBlockingUsing.empty()) {
            // adding a decl_old
            std::string dOldName = hif::NameTable::getInstance()->getFreshName((std::string("old_") + decl->getName()));
            Variable *declOld    = new Variable();
            declOld->setType(hif::copy(decl->getType()));
            declOld->setValue(hif::copy(decl->getValue()));
            declOld->setName(dOldName);
            hif::manipulation::addDeclarationInContext(declOld, decl, false);

            // adding decl_tmp local to the procedure
            Variable *declTmp = new Variable();
            declTmp->setType(hif::copy(decl->getType()));
            declTmp->setValue(hif::copy(decl->getValue()));
            std::string dTmpName = hif::NameTable::getInstance()->getFreshName((std::string("tmp_") + decl->getName()));
            declTmp->setName(dTmpName);
            st->declarations.push_front(declTmp);

            // Changing function body as following:
            // Original: a = b + c
            // New:
            // tmp_a = b + c;
            // if (old_a != tmp_a)
            // {
            //     old_a = tmp_a;
            //     a = tmp_a;
            // }
            Identifier *tmpId  = new Identifier(declTmp->getName());
            Identifier *oldId  = new Identifier(declOld->getName());
            Identifier *declId = new Identifier(decl->getName());

            for (BList<Assign>::iterator j = s->actions.toOtherBList<Assign>().begin();
                 j != s->actions.toOtherBList<Assign>().end(); ++j) {
                Assign *ass             = *j;
                std::list<Value *> tgts = hif::manipulation::collectLeftHandSideSymbols(ass);
                messageAssert(tgts.size() == 1, "Unexpected targets", ass, sem);
                Value *v = tgts.front();
                v->replace(hif::copy(tmpId));
                delete v;
            }

            f.ifStmt(
                f.noActions(), (f.ifAlt(
                                   f.expression(hif::copy(oldId), op_case_neq, hif::copy(tmpId)),
                                   (f.assignAction(oldId, hif::copy(tmpId)), f.assignAction(declId, tmpId)))));
        }

        // Update of refs and infos will be performed later.
    }

    // Fixing cone declarations order
    for (Cones::iterator i = cones.begin(); i != cones.end(); ++i) {
        Procedure *cone     = i->second;
        BList<Object> *list = cone->getBList();
        cone->replace(nullptr);
        list->push_back(cone);
    }

    for (ConesMap::iterator i = conesMap.begin(); i != conesMap.end(); ++i) {
        DataDeclaration *decl = i->first;
        Procedure *myProc     = i->second;
        State *myState        = myProc->getStateTable()->states.front();

        for (LogicList::iterator j = sortedGraph.begin(); j != sortedGraph.end(); ++j) {
            DataDeclaration *parent = *j;
            if (logicGraph.first[decl].find(parent) == logicGraph.first[decl].end())
                continue;

            if (conesMap.find(parent) != conesMap.end()) {
                // PCall creation moved later.
                /*
                Procedure * parentProc = conesMap[parent];

                // created procedure for parent: just call it!
                ProcedureCall * pcall = f.procedureCall(
                            parentProc->getName(),
                            nullptr,
                            f.noTemplateArguments(),
                            f.noParameterArguments()
                            );

                myState->actions.push_front(pcall);
                */
            } else {
                // push_front() of copy of all continuous assigns
                InfoStruct &parentInfos = infoMap[parent];
                for (RefSet::iterator k = parentInfos.lhsContinuousUsing.begin();
                     k != parentInfos.lhsContinuousUsing.end(); ++k) {
                    Assign *ass = hif::getNearestParent<Assign>(*k);
                    messageAssert(ass != nullptr, "Assign not found", *k, sem);

                    myState->actions.push_front(hif::copy(ass));

                    // Update of refs and infos will be performed later.
                }

                // since it has been merged, update the logic graph
                // by pushing my parent-parents (i.e. grandparents)
                // as my parents! LOL!
                logicGraph.first[decl].insert(logicGraph.first[parent].begin(), logicGraph.first[parent].end());
                for (LogicSet::iterator k = logicGraph.first[parent].begin(); k != logicGraph.first[parent].end();
                     ++k) {
                    logicGraph.second[*k].insert(decl);
                }
            }
        }
    }

    // Updating refs and infos
    for (ConesMap::iterator i = conesMap.begin(); i != conesMap.end(); ++i) {
        Procedure *p = i->second;

        RefMap procRefs;
        hif::semantics::getAllReferences(procRefs, sem, p);

        // updating infos
        _fillInfoMap(procRefs, infoMap, true);

        // updating refs
        for (RefMap::iterator j = procRefs.begin(); j != procRefs.end(); ++j) {
            Declaration *decl = j->first;
            RefSet &refSet    = j->second;

            refMap[decl].insert(refSet.begin(), refSet.end());
        }
    }
}

typedef std::map<Object *, std::set<Procedure *>> CallsMap;

Procedure *_getParentCone(Object *symb, ConesMap &conesMap)
{
    Procedure *parentProc = hif::getNearestParent<Procedure>(symb);
    if (parentProc == nullptr)
        return nullptr;

    for (ConesMap::iterator i = conesMap.begin(); i != conesMap.end(); ++i) {
        if (i->second == parentProc)
            return parentProc;
    }

    return nullptr;
}

void _addConesPCalls(InfoMap &infoMap, hif::semantics::ILanguageSemantics *sem, ConesMap &conesMap, CallsMap &callsMap)
{
    hif::HifFactory f(sem);

    // adding cone calls where related symbol is read or written.
    for (ConesMap::iterator i = conesMap.begin(); i != conesMap.end(); ++i) {
        DataDeclaration *decl = i->first;
        Procedure *p          = i->second;

        InfoStruct &infos = infoMap[decl];

        RefSet tmpSet;
        tmpSet.insert(infos.readUsing.begin(), infos.readUsing.end());
        tmpSet.insert(infos.lhsBlockingUsing.begin(), infos.lhsBlockingUsing.end());
        tmpSet.insert(infos.lhsNonBlockingUsing.begin(), infos.lhsNonBlockingUsing.end());

        for (RefSet::iterator j = tmpSet.begin(); j != tmpSet.end(); ++j) {
            Object *symb      = *j;
            Action *parentAct = hif::getNearestParent<Action>(symb);
            messageAssert(parentAct != nullptr, "cannot find parent action", symb, sem);

            // Skip symbols inside the cones itself
            if (hif::isSubNode(symb, p))
                continue;

            // Avoid multiple calls inside same cone
            Procedure *parentCone = _getParentCone(symb, conesMap);
            if (parentCone != nullptr) {
                if (callsMap[parentCone].find(p) != callsMap[parentCone].end())
                    continue;

                callsMap[parentCone].insert(p);
            }

            // Avoid case: a = a + 1: add only one cone call.
            if (callsMap[parentAct].find(p) != callsMap[parentAct].end())
                continue;
            callsMap[parentAct].insert(p);

            ProcedureCall *pcall =
                f.procedureCall(p->getName(), nullptr, f.noTemplateArguments(), f.noParameterArguments());

            hif::semantics::setDeclaration(pcall, p);
            if (parentCone == nullptr) {
                BList<Action>::iterator it(parentAct);
                it.insert_before(pcall);
            } else {
                // Ensuring that the pcall happens always before all the cone var refs.
                hif::semantics::GetReferencesOptions opt;
                opt.onlyFirst = true;
                hif::semantics::ReferencesSet res;
                hif::semantics::getReferences(decl, res, sem, parentCone, opt);
                messageAssert(res.size() == 1U, "Expected just one ref", decl, sem);

                Object *firstRef  = *res.begin();
                Action *firstPact = hif::getNearestParent<Action>(firstRef);
                messageAssert(
                    firstPact != nullptr && firstPact->isInBList(), "cannot find parent action", firstRef, sem);
                BList<Action>::iterator it(firstPact);
                it.insert_before(pcall);
            }
        }
    }
}

void _calculateParentNodes(DataDeclaration *decl, SensitivityMap &parentMap, SensitivityMap &sensMap)
{
    for (LogicSet::iterator i = parentMap[decl].begin(); i != parentMap[decl].end(); ++i) {
        DataDeclaration *node = *i;
        if (sensMap[node].empty()) {
            _calculateParentNodes(node, parentMap, sensMap);
        }

        if (sensMap[node].empty()) {
            // input port
            sensMap[decl].insert(node);
            continue;
        }

        // adding decl parent-parents as current decl parents.
        sensMap[decl].insert(sensMap[node].begin(), sensMap[node].end());
    }
}

void _fixSensitivities(
    RefMap &refMap,
    InfoMap &infoMap,
    hif::semantics::ILanguageSemantics *sem,
    LogicGraph &logicGraph,
    ConesMap &conesMap,
    SensitivityMap &sensMap)
{
    for (ConesMap::iterator i = conesMap.begin(); i != conesMap.end(); ++i) {
        DataDeclaration *decl = i->first;

        // For all continuous,collects top-level parents in cones.
        // This can be useful for future fixes.
        _calculateParentNodes(decl, logicGraph.first, sensMap);

        // Actual fix.
        // Fix only when target of continuous assignment is used in sensitivity of
        // processes or wait.
        InfoStruct &infos = infoMap[decl];
        if (infos.sensitivityUsing.empty() && infos.waitUsing.empty())
            continue;

        // For each using adding top level parents in sensitivities and remove it
        // from the list.
        for (RefSet::iterator j = infos.sensitivityUsing.begin(); j != infos.sensitivityUsing.end();) {
            Object *ref            = *j;
            BList<Value> *sensList = hif::objectGetSensitivityList(ref);
            messageAssert(sensList != nullptr, "Cannot find parent sensitivity", ref, sem);
            sensList->removeSubTree(static_cast<Value *>(ref));
            infos.sensitivityUsing.erase(j++);
            refMap[decl].erase(ref);
            delete ref;
            for (LogicSet::iterator k = sensMap[decl].begin(); k != sensMap[decl].end(); ++k) {
                DataDeclaration *parentNodeDecl = *k;

                Signal *sig = dynamic_cast<Signal *>(parentNodeDecl);
                Port *port  = dynamic_cast<Port *>(parentNodeDecl);
                if (sig == nullptr && port == nullptr)
                    continue;

                Identifier *sensEntry = new Identifier(parentNodeDecl->getName());
                hif::manipulation::AddUniqueObjectOptions addOpt;
                addOpt.equalsOptions.checkOnlyNames = true;
                addOpt.deleteIfNotAdded             = true;
                const bool inserted                 = hif::manipulation::addUniqueObject(sensEntry, *sensList, addOpt);
                if (inserted) {
                    infoMap[parentNodeDecl].sensitivityUsing.insert(sensEntry);
                    refMap[parentNodeDecl].insert(sensEntry);
                }
            }
        }

        for (RefSet::iterator j = infos.waitUsing.begin(); j != infos.waitUsing.end();) {
            Object *ref = *j;
            ObjectSensitivityOptions opts;
            opts.checkAll          = true;
            BList<Value> *sensList = hif::objectGetSensitivityList(ref, opts);
            if (sensList == nullptr)
                continue; // i.e. in wait condition
            sensList->removeSubTree(static_cast<Value *>(ref));
            infos.waitUsing.erase(j++);
            refMap[decl].erase(ref);
            delete ref;
            for (LogicSet::iterator k = sensMap[decl].begin(); k != sensMap[decl].end(); ++k) {
                DataDeclaration *parentNodeDecl = *k;

                Signal *sig = dynamic_cast<Signal *>(parentNodeDecl);
                Port *port  = dynamic_cast<Port *>(parentNodeDecl);
                if (sig == nullptr && port == nullptr)
                    continue;

                Identifier *sensEntry = new Identifier(parentNodeDecl->getName());
                hif::manipulation::AddUniqueObjectOptions addOpt;
                addOpt.equalsOptions.checkOnlyNames = true;
                addOpt.deleteIfNotAdded             = true;
                const bool inserted                 = hif::manipulation::addUniqueObject(sensEntry, *sensList, addOpt);
                if (inserted) {
                    infoMap[parentNodeDecl].waitUsing.insert(sensEntry);
                    refMap[parentNodeDecl].insert(sensEntry);
                }
            }
        }
    }
}

void _clearContinuousAssigns(RefMap &refMap, InfoMap &infoMap, hif::semantics::ILanguageSemantics *sem)
{
    for (InfoMap::iterator i = infoMap.begin(); i != infoMap.end(); ++i) {
        //DataDeclaration * decl = i->first;
        InfoStruct &infos = i->second;

        for (RefSet::iterator j = infos.lhsContinuousUsing.begin(); j != infos.lhsContinuousUsing.end(); ++j) {
            Object *ref = *j;
            Assign *ass = hif::getNearestParent<Assign>(ref);
            messageAssert(ass != nullptr, "Cannot find parent assign", ref, sem);

            SymbList symbolList;
            hif::semantics::collectSymbols(symbolList, ass, sem);
            for (SymbList::iterator k = symbolList.begin(); k != symbolList.end(); ++k) {
                Object *innerSymb   = *k;
                Identifier *innerId = dynamic_cast<Identifier *>(innerSymb);
                if (innerId == nullptr)
                    continue;

                DataDeclaration *innerDecl = hif::semantics::getDeclaration(innerId, sem);
                messageAssert(innerDecl != nullptr, "Declaration not found", innerId, sem);

                refMap[innerDecl].erase(innerSymb);

                Signal *sig = dynamic_cast<Signal *>(innerDecl);
                Port *port  = dynamic_cast<Port *>(innerDecl);
                if (sig == nullptr && port == nullptr)
                    continue;

                infoMap[innerDecl].readUsing.erase(innerSymb);
                infoMap[innerDecl].rhsContinuousUsing.erase(innerSymb);
            }

            ass->replace(nullptr);
            delete ass;
        }

        infos.lhsContinuousUsing.clear();
    }
}

void _fixLogicCones(
    RefMap &refMap,
    InfoMap &infoMap,
    ConesMap &conesMap,
    hif::semantics::ILanguageSemantics *sem,
    CallsMap &callsMap,
    SensitivityMap &sensMap)
{
    // Generate logic cones graph. It is related to target of continuous assigns,
    // between the target and its source symbols
    LogicGraph logicGraph;
    _generateLogicConesGraph(infoMap, logicGraph, sem);

    // Sort the graphs w.r.t. symbols declarations.
    LogicList sortedGraph;
    hif::analysis::sortGraph<DataDeclaration, DataDeclaration>(logicGraph, sortedGraph, true);

    // create all necessary hif_cone procedures starting from sorted graph result.
    _generateConeFunctions(refMap, infoMap, sem, sortedGraph, logicGraph, conesMap);

#ifdef DBG_PRINT_FIX3_STEP_FILES
    System *s = hif::getNearestParent<System>(refMap.begin()->first);
    hif::writeFile("FIX3_3_1_after_generate_cone_functions", s, true);
    hif::writeFile("FIX3_3_1_after_generate_cone_functions", s, false);
#endif

    // add all necessary calls to hif_cone procedures.
    _addConesPCalls(infoMap, sem, conesMap, callsMap);

#ifdef DBG_PRINT_FIX3_STEP_FILES
    hif::writeFile("FIX3_3_2_after_add_cones_PCalls", s, true);
    hif::writeFile("FIX3_3_2_after_add_cones_PCalls", s, false);
#endif

    // fix the sensitivity list replacing symbols assigned by continuous assigns
    // with top level parents in sensitivities.
    _fixSensitivities(refMap, infoMap, sem, logicGraph, conesMap, sensMap);

#ifdef DBG_PRINT_FIX3_STEP_FILES
    hif::writeFile("FIX3_3_3_after_fix_sensitivities", s, true);
    hif::writeFile("FIX3_3_3_after_fix_sensitivities", s, false);
#endif

    // removes all continuous assigns.
    _clearContinuousAssigns(refMap, infoMap, sem);
}

// ///////////////////////////////////////////////////////////////////
// Refine to variables
// ///////////////////////////////////////////////////////////////////
void _refineToVariables(
    RefMap &refMap,
    InfoMap &infoMap,
    ConesMap &conesMap,
    hif::semantics::ILanguageSemantics *sem,
    CallsMap &callsMap,
    SensitivityMap &sensMap)
{
    hif::HifFactory f(sem);
    hif::application_utils::WarningList bindWarnings;
    hif::application_utils::WarningList delayWarnings;

    for (InfoMap::iterator i = infoMap.begin(); i != infoMap.end(); ++i) {
        DataDeclaration *decl = i->first;
        InfoStruct &infos     = i->second;

        bool isConnectionSignal = false;
        bool isSignal           = false;
        bool isVariable         = false;
        _calculateRequiredDeclType(isConnectionSignal, isSignal, isVariable, infos, decl);

        if ((isSignal && !isVariable)) {
            // nothing to do since is already a signal.
            continue;
        } else if ((isVariable && !isSignal) || (!isVariable && !isSignal)) {
            // Refine to variable
            Variable *var = new Variable();
            var->setName(decl->getName());
            var->setType(decl->setType(nullptr));
            var->setValue(decl->setValue(nullptr));

            decl->replace(var);
            delete decl;

            for (RefSet::iterator j = refMap[decl].begin(); j != refMap[decl].end(); ++j) {
                Object *reference = *j;
                hif::semantics::setDeclaration(reference, var);
            }
        } else // (isVariable && isSignal) || isOutputPort
        {
            if (isConnectionSignal && !infos.bindUsing.empty()) {
                bindWarnings.push_back(decl);
            }

            // mixed case
            std::string varName = NameTable::getInstance()->getFreshName(decl->getName(), "_sig_var");

            // creating support var
            Variable *var = new Variable();
            var->setName(varName);
            var->setType(hif::copy(decl->getType()));
            var->setValue(hif::copy(decl->getValue()));

            // adding after signal
            hif::manipulation::addDeclarationInContext(var, decl, false);

            messageAssert(infos.lhsContinuousUsing.empty(), "Unexpected lhs of continuous", decl, sem);

            for (RefSet::iterator j = infos.lhsBlockingUsing.begin(); j != infos.lhsBlockingUsing.end(); ++j) {
                // 1- sig = expr  -->
                // var[5] = expr;
                // sig[5] <= var[5]; // only if is not inside cone
                // or in case of after:
                // var[5] = expr;
                // sig[5] <= expr; + warning if expr contains fcalls.

                Object *ref = *j;
                Assign *ass = hif::getNearestParent<Assign>(ref);
                messageAssert(ass != nullptr, "Parent assign not found", ref, sem);

                Assign *sigAss      = hif::copy(ass);
                const bool hasDelay = (ass->getDelay() != nullptr);

                hif::objectSetName(ref, varName);
                hif::semantics::setDeclaration(ref, var);
                if (!hasDelay) {
                    delete sigAss->setRightHandSide(hif::copy(ass->getLeftHandSide()));
                } else {
                    hif::HifTypedQuery<FunctionCall> q;
                    q.collectObjectMethod = &_skipStandardFunctionCall;
                    std::list<Object *> list;
                    hif::search(list, ass->getRightHandSide(), q);

                    if (!list.empty()) {
                        delayWarnings.push_back(sigAss);
                    }
                }

                // We ceannot just skip cone related to current decl!
                // ref design: verilog/yogitech/m6502
                Procedure *parentCone = _getParentCone(ref, conesMap);
                if (parentCone != nullptr) {
                    // Inside cone: skip!
                    delete sigAss;
                    continue;
                }
                BList<Assign>::iterator jt(ass);
                jt.insert_after(sigAss);
            }

            RefSet readUsing;
            readUsing.insert(infos.readUsing.begin(), infos.readUsing.end());
            readUsing.insert(infos.rhsContinuousUsing.begin(), infos.rhsContinuousUsing.end());

            for (RefSet::iterator j = readUsing.begin(); j != readUsing.end(); ++j) {
                // Replace sig with var

                Object *ref = *j;
                hif::objectSetName(ref, varName);
                hif::semantics::setDeclaration(ref, var);
            }

            // Adding writing of var with sig as source in case of non-blocking
            if (!infos.lhsNonBlockingUsing.empty()) {
                BaseContents *bc = _getBaseContents(decl);

                // check if decl cones method is already creaded, otherwise create it
                if (conesMap.find(decl) == conesMap.end()) {
                    std::string pName =
                        hif::NameTable::getInstance()->getFreshName((std::string("hif_cone_") + decl->getName()));
                    Procedure *cone =
                        static_cast<Procedure *>(f.subprogram(nullptr, pName, f.noTemplates(), f.noParameters()));
                    bc->declarations.push_back(cone);
                    StateTable *st = f.stateTable("hif_cone", f.noDeclarations(), f.noActions());
                    cone->setStateTable(st);
                    conesMap[decl] = cone;

                    // use temporary map to add missings cones calls.
                    ConesMap tmp;
                    tmp[decl] = cone;
                    _addConesPCalls(infoMap, sem, tmp, callsMap);
                }

                // added if inside cone function to assure synchronization between
                // signal and variable.

                // adding a decl_old
                std::string dOldName =
                    hif::NameTable::getInstance()->getFreshName((std::string("old_") + decl->getName()));
                Variable *declOld = new Variable();
                declOld->setType(hif::copy(decl->getType()));
                declOld->setValue(hif::copy(decl->getValue()));
                declOld->setName(dOldName);
                hif::manipulation::addDeclarationInContext(declOld, decl, false);

                State *coneState = conesMap[decl]->getStateTable()->states.front();
                coneState->actions.push_front(f.ifStmt(
                    f.noActions(),
                    f.ifAlt(
                        f.expression(new Identifier(decl->getName()), op_case_neq, new Identifier(dOldName)),
                        (f.assignAction(new Identifier(dOldName), new Identifier(decl->getName())),
                         f.assignAction(new Identifier(varName), new Identifier(dOldName))))));
            }

            // Adding process to synchronize sig and var.
            if (!sensMap[decl].empty()) {
                std::string name = NameTable::getInstance()->getFreshName(
                    (std::string(var->getName()) + "_" + decl->getName() + "_sync_process"));
                StateTable *process = f.stateTable(
                    name, f.noDeclarations(),
                    f.assignAction(new Identifier(decl->getName()), new Identifier(var->getName())),
                    false //true ? no ref design found at the moment
                );

                if (conesMap.find(decl) != conesMap.end()) {
                    process->states.front()->actions.push_front(f.procedureCallAction(
                        conesMap[decl]->getName(), nullptr, f.noTemplateArguments(), f.noParameterArguments()));
                }

                for (SensitivityMap::mapped_type::iterator k = sensMap[decl].begin(); k != sensMap[decl].end(); ++k) {
                    process->sensitivity.push_back(f.identifier((*k)->getName()));
                }
                BaseContents *bc = _getBaseContents(decl);
                bc->stateTables.push_back(process);
            }
        }
    }

    // raise warnings
    messageWarningList(
        true,
        "Found at least one declaration appearing in port "
        "binding and written through blocking or continuous "
        "assignment. This could lead to a non-equivalent output "
        "design description.",
        bindWarnings);

    messageWarningList(
        true,
        "Found a continuous assignment featuring a RHS "
        "containing a function call. If such function call has "
        "side effects, the generated output design description "
        "will not be equivalent.",
        delayWarnings);
}

// /////////////////////////////////////////////////////////////////////////////
// Partial flattening
// /////////////////////////////////////////////////////////////////////////////

void _collectOnOutputPorts(RefMap &refMap, hif::semantics::ILanguageSemantics * /*sem*/, Views &views)
{
    // Output ports assigned by continuous assignments are replaced with explicit
    // processes.
    for (RefMap::iterator i = refMap.begin(); i != refMap.end(); ++i) {
        Port *p = dynamic_cast<Port *>(i->first);
        if (p == nullptr)
            continue;
        if (p->getDirection() != dir_out && p->getDirection() != dir_inout)
            continue;

        for (RefSet::iterator j = i->second.begin(); j != i->second.end(); ++j) {
            Object *symb        = *j;
            PortAssign *portAss = dynamic_cast<PortAssign *>(symb);
            if (portAss != nullptr)
                continue;
            if (!hif::manipulation::isInLeftHandSide(symb))
                continue;
            GlobalAction *gact = hif::getNearestParent<GlobalAction>(symb);
            if (gact == nullptr)
                continue;

            View *v = hif::getNearestParent<View>(symb);
            if (v == nullptr)
                continue;
            views.insert(v);
            break;
        }
    }
}

void _fillViewSets(
    DataDeclaration *decl,
    InfoStruct &infos,
    Views &views,
    ViewRefs &viewRefs,
    hif::semantics::ILanguageSemantics *sem)
{
    if (dynamic_cast<Port *>(decl) != nullptr) {
        View *v = hif::getNearestParent<View>(decl);
        if (v != nullptr)
            views.insert(v);
    }

    if (!infos.portUsing.empty()) {
        PortAssign *portAss = dynamic_cast<PortAssign *>(*infos.portUsing.begin());
        messageAssert(portAss != nullptr, "Unexpected object", *infos.portUsing.begin(), sem);
        PortAssign::DeclarationType *port = hif::semantics::getDeclaration(portAss, sem);
        messageAssert(port != nullptr, "Declaration not found", portAss, sem);
        View *v = hif::getNearestParent<View>(decl);
        if (v != nullptr)
            views.insert(v);
    }

    for (RefSet::iterator i = infos.bindUsing.begin(); i != infos.bindUsing.end(); ++i) {
        Instance *inst = hif::getNearestParent<Instance>(*i);
        if (inst == nullptr)
            continue;
        ViewReference *vr = dynamic_cast<ViewReference *>(inst->getReferencedType());
        if (vr == nullptr)
            continue;
        viewRefs.insert(vr);
    }
}

void _collectOnAssigns(InfoMap &infoMap, hif::semantics::ILanguageSemantics *sem, Views &views, ViewRefs &viewRefs)
{
    for (InfoMap::iterator i = infoMap.begin(); i != infoMap.end(); ++i) {
        DataDeclaration *decl = i->first;
        InfoStruct &infos     = i->second;

        bool isConnectionSignal = false;
        bool isSignal           = false;
        bool isVariable         = false;
        _calculateRequiredDeclType(isConnectionSignal, isSignal, isVariable, infos, decl);

        if ((isSignal && !isVariable)) {
            // nothing to do since is already a signal.
            continue;
        } else if ((isVariable && !isSignal) || (!isVariable && !isSignal)) {
            // ntd
        } else // (isVariable && isSignal) || isOutputPort
        {
            if (isConnectionSignal)
                _fillViewSets(decl, infos, views, viewRefs, sem);
        }
    }
}

void _performPartialFlattening(
    System *system,
    Views &views,
    ViewRefs &viewRefs,
    hif::semantics::ILanguageSemantics *sem)
{
    if (views.empty() && viewRefs.empty())
        return;

    hif::manipulation::FlattenDesignOptions fopt;
    fopt.verbose = true;

    for (Views::iterator i = views.begin(); i != views.end(); ++i) {
        View *view = *i;
        fopt.rootDUs.insert(hif::manipulation::buildHierarchicalSymbol(view, sem));
    }

    for (ViewRefs::iterator i = viewRefs.begin(); i != viewRefs.end(); ++i) {
        ViewReference *viewRef = *i;
        fopt.rootInstances.insert(hif::manipulation::buildHierarchicalSymbol(viewRef, sem));
    }

    hif::manipulation::flattenDesign(system, sem, fopt);
}

bool _checkTopViews(Views &views, Views &topViews)
{
    for (Views::iterator i = views.begin(); i != views.end(); ++i) {
        View *v = *i;
        if (topViews.find(v) != topViews.end())
            continue;
        return false;
    }

    return true;
}

void _performOriginalDesignChecks(RefMap &refMap, hif::semantics::ILanguageSemantics * /*sem*/)
{
    // Check non-determinism basic FSM-like case.
    // E.g.:
    // p1: @(...) sig = expr;
    // p2: @(...) out = sig;
    // This is NON-DETERMINISTIC when sig is not in p2 sensitivity.

    typedef std::set<Declaration *> Declarations;
    Declarations collectedDecls;
    typedef std::set<StateTable *> Processes;
    typedef std::map<Declaration *, Processes> ProcessesMap;
    ProcessesMap processesMap;

    // Collecting candiadate declarations
    for (RefMap::iterator i = refMap.begin(); i != refMap.end(); ++i) {
        Declaration *decl = i->first;
        RefSet &refSet    = i->second;
        Signal *sigDecl   = dynamic_cast<Signal *>(decl);
        Port *portDecl    = dynamic_cast<Port *>(decl);
        if ((sigDecl == nullptr && portDecl == nullptr) ||
            (portDecl != nullptr && portDecl->getDirection() == hif::dir_in))
            continue;

        for (RefSet::iterator j = refSet.begin(); j != refSet.end(); ++j) {
            Object *symbol = *j;
            // Must be in process body
            State *state   = getNearestParent<State>(symbol);
            if (state == nullptr)
                continue;
            StateTable *process = dynamic_cast<StateTable *>(state->getParent());
            SubProgram *sub     = dynamic_cast<SubProgram *>(process->getParent());
            if (sub != nullptr)
                continue; // Too hard to check!
            // Skip possible initial() processes:
            if (process->getFlavour() == hif::pf_initial || process->getFlavour() == hif::pf_analog)
                continue;
            // Must be written as blocking
            const bool isTarget = hif::manipulation::isInLeftHandSide(symbol);
            if (!isTarget)
                continue;
            Assign *ass = hif::getNearestParent<Assign>(symbol);
            if (ass->checkProperty(NONBLOCKING_ASSIGNMENT))
                continue;
            // Collecting
            collectedDecls.insert(decl);
            processesMap[decl].insert(process);
        }
    }

    // Checking collected candiadate declarations
    hif::application_utils::WarningSet warnings;
    for (Declarations::iterator i = collectedDecls.begin(); i != collectedDecls.end(); ++i) {
        Declaration *decl = *i;
        RefSet &refSet    = refMap[decl];
        for (RefSet::iterator j = refSet.begin(); j != refSet.end(); ++j) {
            Object *symbol = *j;
            // Must be in process body
            State *state   = getNearestParent<State>(symbol);
            if (state == nullptr)
                continue;
            StateTable *process = dynamic_cast<StateTable *>(state->getParent());
            SubProgram *sub     = dynamic_cast<SubProgram *>(process->getParent());
            if (sub != nullptr)
                continue; // Too hard to check!
            // Skip possible initial() processes:
            if (process->getFlavour() == hif::pf_initial || process->getFlavour() == hif::pf_analog)
                continue;
            // Skip processes that both write and read: this should become
            // a local variable!
            if (processesMap[decl].find(process) != processesMap[decl].end())
                continue;
            // Must be read
            const bool isTarget = hif::manipulation::isInLeftHandSide(symbol);
            if (isTarget)
                continue;
            // Check sensitivities!
            typedef hif::HifTypedQuery<Identifier> Query;
            Query query;
            Query::Results results;
            query.onlyFirstMatch = true;
            query.name           = decl->getName();
            hif::search(results, process->sensitivity, query);
            hif::search(results, process->sensitivityPos, query);
            hif::search(results, process->sensitivityNeg, query);
            if (!results.empty())
                continue;
            warnings.insert(decl);
            break;
        }
    }

    // Rising warnings.
    messageWarningList(
        true,
        "Found at least one signal or port written by a blocking assignment"
        " and read by a process which has not such a signal or port into its "
        "sensitivity. This could lead to non-equivalent translation.",
        warnings);
}

void _partialFlattening(
    System *system,
    Views &topViews,
    RefMap &refMap,
    hif::semantics::ILanguageSemantics *sem,
    const bool preserveStructure)
{
    // Flattening performed only when preserveStructure is false.
    if (preserveStructure)
        return;

    // Unreferenced signal can be safely translated as variables.
    _fixUnreferenced(refMap, sem);

    Views views;
    ViewRefs viewRefs;
    // Output ports assigned by continuous assignments are collected to be flattened.
    _collectOnOutputPorts(refMap, sem, views);

    // FIXME: after partially flattening the input description once, the resulting
    //        description may contain new troublesome assignments (i.e., raise new
    //        warnings). As such, partial flattening and its preliminary analysis
    //        may be required to be executed more than once. At the moment,
    //        partial flattening and its preliminary analysis are repeated
    //        in a loop. In the future, the preliminary analysis phase should be
    //        extended so that it will be necessary to carry out partial flattening
    //        only once.

    hif::semantics::GetReferencesOptions opt;
    opt.includeUnreferenced = true;

    for (;;) {
        // Filling info map.
        InfoMap infoMap;
        _fillInfoMap(refMap, infoMap, false);

        // Collecting output ports targets of blocking/continuous assignments
        _collectOnAssigns(infoMap, sem, views, viewRefs);

        // Flattening design
        const bool isOnlyTop       = _checkTopViews(views, topViews);
        const bool needsFlattening = ((!views.empty() && !isOnlyTop) || !viewRefs.empty());
        if (!needsFlattening)
            break;
        _performPartialFlattening(system, views, viewRefs, sem);

        // Resetting infos
        refMap.clear();
        hif::semantics::resetDeclarations(system);
        hif::semantics::resetTypes(system);
        hif::semantics::typeTree(system, sem);
        hif::semantics::getAllReferences(refMap, sem, system, opt);
        views.clear();
        viewRefs.clear();
    }
}

void performStep3Refinements(hif::System *o, hif::semantics::ILanguageSemantics *sem, const bool preserveStructure)
{
    // ///////////////////////////////////////////////////////////////////
    // Ensuring no concats as targets.
    // ///////////////////////////////////////////////////////////////////
    hif::manipulation::SplitAssignTargetOptions splitOpts;
    splitOpts.splitConcats     = true;
    splitOpts.createSignals    = true;
    splitOpts.splitPortassigns = true;
    hif::manipulation::splitAssignTargets(o, sem, splitOpts);

#ifdef DBG_PRINT_FIX3_STEP_FILES
    hif::writeFile("FIX3_0_after_split_concat_target", o, true);
    hif::writeFile("FIX3_0_after_split_concat_target", o, false);
#endif

    // ///////////////////////////////////////////////////////////////////
    // Updating all declarations and types
    // //////////////////////////////////////////////////////////////////
    RefMap refMap;
    hif::semantics::GetReferencesOptions opt;
    opt.includeUnreferenced = true;
    hif::semantics::getAllReferences(refMap, sem, o, opt);
    hif::semantics::typeTree(o, sem);

    // ///////////////////////////////////////////////////////////////////
    // Prerefine checks
    // ///////////////////////////////////////////////////////////////////
    _performOriginalDesignChecks(refMap, sem);

    // ///////////////////////////////////////////////////////////////////
    // Heuristic to avoid loops in logic cones.
    // ///////////////////////////////////////////////////////////////////
    _splitLogicConesLoops(o, refMap, sem);
    refMap.clear();
    hif::semantics::getAllReferences(refMap, sem, o, opt);

#ifdef DBG_PRINT_FIX3_STEP_FILES
    hif::writeFile("FIX3_1_after_split_logic_cones", o, true);
    hif::writeFile("FIX3_1_after_split_logic_cones", o, false);
#endif

    // ///////////////////////////////////////////////////////////////////
    // Find top level
    // ///////////////////////////////////////////////////////////////////
    hif::manipulation::FindTopOptions topt;
    topt.verbose         = true;
    topt.checkAtLeastOne = true;
    Views topViews       = hif::manipulation::findTopLevelModules(o, sem, topt);

    // ///////////////////////////////////////////////////////////////////
    // Partial flattening
    // ///////////////////////////////////////////////////////////////////
    _partialFlattening(o, topViews, refMap, sem, preserveStructure);

#ifdef DBG_PRINT_FIX3_STEP_FILES
    hif::writeFile("FIX3_2_after_partial_flattening", o, true);
    hif::writeFile("FIX3_2_after_partial_flattening", o, false);
#endif

    // ///////////////////////////////////////////////////////////////////
    // Pre refinements
    // ///////////////////////////////////////////////////////////////////
    _prerefineFixes(topViews, refMap, sem);

#ifdef DBG_PRINT_FIX3_STEP_FILES
    hif::writeFile("FIX3_3_after_prerefine_fixes", o, true);
    hif::writeFile("FIX3_3_after_prerefine_fixes", o, false);
#endif

    // ///////////////////////////////////////////////////////////////////
    // Filling info map
    // ///////////////////////////////////////////////////////////////////
    InfoMap infoMap;
    _fillInfoMap(refMap, infoMap, true);

    // ///////////////////////////////////////////////////////////////////
    // Fix logic cones
    // ///////////////////////////////////////////////////////////////////
    ConesMap conesMap;
    CallsMap callsMap;
    SensitivityMap sensMap;
    _fixLogicCones(refMap, infoMap, conesMap, sem, callsMap, sensMap);

#ifdef DBG_PRINT_FIX3_STEP_FILES
    hif::writeFile("FIX3_4_after_fix_logic_cones", o, true);
    hif::writeFile("FIX3_4_after_fix_logic_cones", o, false);
#endif

    // ///////////////////////////////////////////////////////////////////
    // Refine in variables
    // ///////////////////////////////////////////////////////////////////
    _refineToVariables(refMap, infoMap, conesMap, sem, callsMap, sensMap);
}

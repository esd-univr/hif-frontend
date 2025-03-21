/// @file PostParsingVisitor_step1.cpp
/// @brief
/// Copyright (c) 2024-2025, Electronic Systems Design (ESD) Group,
/// Univeristy of Verona.
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "vhdl2hif/vhdl_post_parsing_methods.hpp"
#include "vhdl2hif/vhdl_support.hpp"

using namespace hif;

//#define DEBUG_MSG 1

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-member-function"
#elif defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

namespace
{ // anon namespace

struct RangeInfo {
    RangeInfo();
    RangeInfo(ValueTPAssign *l, ValueTPAssign *r);
    ~RangeInfo();
    RangeInfo(const RangeInfo &other);
    RangeInfo &operator=(RangeInfo other);
    bool operator<(const RangeInfo &other) const;
    void swap(RangeInfo &other);

    ValueTPAssign *left;
    ValueTPAssign *right;
};

RangeInfo::RangeInfo()
    : left(nullptr)
    , right(nullptr)
{
    // ntd
}

RangeInfo::RangeInfo(ValueTPAssign *l, ValueTPAssign *r)
    : left(l)
    , right(r)
{
    // ntd
}

RangeInfo::~RangeInfo()
{
    // ntd
}

RangeInfo::RangeInfo(const RangeInfo &other)
    : left(other.left)
    , right(other.right)
{
    // ntd
}

RangeInfo &RangeInfo::operator=(RangeInfo other)
{
    swap(other);
    return *this;
}

bool RangeInfo::operator<(const RangeInfo &other) const
{
    if (left < other.left)
        return true;
    if (left > other.left)
        return false;
    return right < other.right;
}

void RangeInfo::swap(RangeInfo &other)
{
    std::swap(left, other.left);
    std::swap(right, other.right);
}

typedef std::set<RangeInfo> RangeInfoSet;
typedef std::set<TypeReference *> TypeReferenceSet;

struct RangeRefs {
    RangeRefs();
    ~RangeRefs();
    RangeRefs(const RangeRefs &other);
    RangeRefs &operator=(RangeRefs other);
    void swap(RangeRefs &other);

    RangeInfoSet uptos;
    RangeInfoSet downtos;
    TypeDef *decl;
    TypeReferenceSet uptoTyperefs;
    TypeReferenceSet downtoTyperefs;
};

RangeRefs::RangeRefs()
    : uptos()
    , downtos()
    , decl(nullptr)
    , uptoTyperefs()
    , downtoTyperefs()
{
    // ntd
}

RangeRefs::~RangeRefs()
{
    // ntd
}

RangeRefs::RangeRefs(const RangeRefs &other)
    : uptos(other.uptos)
    , downtos(other.downtos)
    , decl(other.decl)
    , uptoTyperefs(other.uptoTyperefs)
    , downtoTyperefs(other.downtoTyperefs)
{
    // ntd
}

RangeRefs &RangeRefs::operator=(RangeRefs other)
{
    swap(other);
    return *this;
}

void RangeRefs::swap(RangeRefs &other)
{
    std::swap(uptos, other.uptos);
    std::swap(downtos, other.downtos);
    std::swap(decl, other.decl);
    std::swap(uptoTyperefs, other.uptoTyperefs);
    std::swap(downtoTyperefs, other.downtoTyperefs);
}

typedef std::map<Range *, RangeRefs> RangeMap;

/// @brief Gets the declaration for a method call.
/// Calling here the getDeclaration() is unsafe, and could fail, since
/// Aggregates (whose type is Array) can be the value of Record parameters.
/// the fix of changing Aggregates to RecordValues is done in step2,
/// therefore here we have to workaround.
template <typename T>
typename T::DeclarationType *_getMethodDeclaration(T *call, hif::semantics::ILanguageSemantics *sem)
{
    std::list<typename T::DeclarationType *> candidates;
    hif::semantics::GetCandidatesOptions opt;
    opt.atLeastOne      = true;
    opt.looseTypeChecks = true;
    hif::semantics::getCandidates(candidates, call, sem, opt);
    if (candidates.empty())
        return nullptr;
    messageAssert(candidates.size() == 1, "Cannot resolve candidates for method call.", call, sem);
    typename T::DeclarationType *function_o = candidates.front();
    hif::semantics::setDeclaration(call, function_o);
    return function_o;
}

void make_template_bounds(ValueTP *&tp_l, ValueTP *&tp_r, Range *o)
{
    std::string left  = NameTable::getInstance()->getFreshName("lbound");
    std::string right = NameTable::getInstance()->getFreshName("rbound");

    Int *lint_o = new Int();
    lint_o->setSpan(new Range(31, 0));
    lint_o->setConstexpr(true);
    lint_o->setSigned(true);

    tp_l = new ValueTP();
    tp_l->setName(left);
    tp_l->setType(lint_o);

    tp_r = new ValueTP();
    tp_r->setName(right);
    tp_r->setType(hif::copy(lint_o));

    Identifier *lid = new Identifier(left);
    Identifier *rid = new Identifier(right);

    o->setLeftBound(lid);
    o->setRightBound(rid);
    o->setDirection(dir_downto);
}

/// @brief Given a logical operator returns its bwise form.
Operator _getBwise(Operator currOp)
{
    if (currOp == op_xor)
        return op_bxor;
    if (currOp == op_and)
        return op_band;
    if (currOp == op_or)
        return op_bor;
    if (currOp == op_not)
        return op_bnot;
    return currOp;
}

bool _replaceLogicalOperators(Expression &o, hif::semantics::ILanguageSemantics *sem)
{
    // if is logical: replace with bitwise if operands are not bool, bit.
    if (!hif::operatorIsLogical(o.getOperator()))
        return false;

    messageAssert(o.getValue1() != nullptr, "Expected expression without op1", &o, sem);
    Type *op1Type = hif::semantics::getSemanticType(o.getValue1(), sem);
    Bit *bit1     = dynamic_cast<Bit *>(op1Type);
    Bool *bool1   = dynamic_cast<Bool *>(op1Type);

    if (bool1 != nullptr)
        return false;
    if (bit1 != nullptr && !bit1->isLogic() && o.getOperator() != op_not) {
        if (o.getValue2() == nullptr)
            return false;
        Type *op2Type = hif::semantics::getSemanticType(o.getValue2(), sem);
        Bit *bit2     = dynamic_cast<Bit *>(op2Type);
        Bool *bool2   = dynamic_cast<Bool *>(op2Type);
        if (bool2 != nullptr)
            return false;
        if (bit2 != nullptr && !bit2->isLogic() && o.getOperator() != op_xor)
            return false;
    }

    // If at least an operand is not ( bit(not logic) or boolean )
    // set operation as bitwise.
#ifdef DEBUG_MSG
    messageDebug("Operator changed from logical to bwise for expression.", &o, sem);
#endif
    o.setOperator(_getBwise(o.getOperator()));

    return true;
}

bool _replaceRelationalOperators(Expression &o, hif::semantics::ILanguageSemantics *sem)
{
    // if is logical: replace with bitwise if operands are not bool, bit.
    if (!hif::operatorIsRelational(o.getOperator()))
        return false;
    if (o.getOperator() != op_eq && o.getOperator() != op_neq)
        return false;

    hif::semantics::getSemanticType(&o, sem);

    Type *op1Type = hif::semantics::getSemanticType(o.getValue1(), sem);
    Type *op2Type = hif::semantics::getSemanticType(o.getValue2(), sem);
    Type *base1   = hif::semantics::getBaseType(op1Type, false, sem);
    Type *base2   = hif::semantics::getBaseType(op2Type, false, sem);

    Bit *bit1      = dynamic_cast<Bit *>(base1);
    Bit *bit2      = dynamic_cast<Bit *>(base2);
    Bitvector *bv1 = dynamic_cast<Bitvector *>(base1);
    Bitvector *bv2 = dynamic_cast<Bitvector *>(base2);
    Signed *s1     = dynamic_cast<Signed *>(base1);
    Signed *s2     = dynamic_cast<Signed *>(base2);
    Unsigned *u1   = dynamic_cast<Unsigned *>(base1);
    Unsigned *u2   = dynamic_cast<Unsigned *>(base2);

    if (u1 != nullptr || u2 != nullptr || s1 != nullptr || s2 != nullptr) {
        // If numeric_std: then replace with case_eq/case_neq
        // If arith/signed/unsigned: keep eq/neq
        Scope *s          = hif::getNearestScope(&o, false, true, false);
        BList<Library> *l = hif::objectGetLibraryList(s);
        const bool isInLogicLib =
            (l->findByName("ieee_std_logic_arith") != nullptr || l->findByName("ieee_std_logic_signed") != nullptr ||
             l->findByName("ieee_std_logic_unsigned") != nullptr);
        if (isInLogicLib)
            return false;
    } else if (bv1 == nullptr && bv2 == nullptr) {
        if (bit1 == nullptr || bit2 == nullptr)
            return false;
        if (!bit1->isLogic() && !bit2->isLogic())
            return false;
    } else {
        if (bv1 == nullptr || bv2 == nullptr)
            return false;
    }

    if (o.getOperator() == op_eq)
        o.setOperator(op_case_eq);
    else
        o.setOperator(op_case_neq);

    return true;
}

class PostParsingVisitor_step1 : public hif::GuideVisitor
{
public:
    PostParsingVisitor_step1(hif::semantics::ILanguageSemantics *sem);
    virtual ~PostParsingVisitor_step1();

    int visitAggregate(hif::Aggregate &o);
    int visitArray(hif::Array &o);
    int visitBitvector(hif::Bitvector &o);
    int visitEnumValue(hif::EnumValue &o);
    int visitExpression(hif::Expression &o);
    int visitFieldReference(hif::FieldReference &o);
    int visitFunctionCall(hif::FunctionCall &o);
    int visitFunction(hif::Function &o);
    int visitIdentifier(hif::Identifier &o);
    int visitLibraryDef(hif::LibraryDef &o);
    int visitPointer(hif::Pointer &o);
    int visitProcedure(hif::Procedure &o);
    int visitProcedureCall(hif::ProcedureCall &o);
    int visitRange(hif::Range &o);
    int visitView(hif::View &o);
    int visitViewReference(hif::ViewReference &o);
    int visitSystem(hif::System &o);
    int visitTypeReference(hif::TypeReference &o);

    // constant
    virtual int visitBitValue(hif::BitValue &o);
    virtual int visitBitvectorValue(hif::BitvectorValue &o);
    virtual int visitBoolValue(hif::BoolValue &o);
    virtual int visitCharValue(hif::CharValue &o);
    virtual int visitIntValue(hif::IntValue &o);
    virtual int visitRealValue(hif::RealValue &o);
    virtual int visitStringValue(hif::StringValue &o);

    // data declarations
    virtual int visitAlias(Alias &o);
    virtual int visitConst(Const &o);
    //virtual int visitEnumValue(EnumValue &o);
    virtual int visitField(Field &o);
    virtual int visitParameter(Parameter &o);
    virtual int visitPort(Port &o);
    virtual int visitSignal(Signal &o);
    virtual int visitVariable(Variable &o);
    virtual int visitValueTP(ValueTP &o);

    virtual int visitAssign(Assign &o);
    virtual int visitPortAssign(PortAssign &o);
    virtual int visitParameterAssign(ParameterAssign &o);
    //virtual int visitFunctionCall(FunctionCall &o);

    bool fixCollectRanges();

    bool secondVisit;

private:
    PostParsingVisitor_step1(const PostParsingVisitor_step1 &o);
    PostParsingVisitor_step1 &operator=(const PostParsingVisitor_step1 &o);

    void _fixConstValue(hif::ConstValue &o);
    void _fixTemplateTypereferences(hif::TypeReference *tr, hif::TypeDef *decl);
    bool _setSignOfArray(hif::Bitvector &o, hif::BList<hif::Library> &libraries);

    Value *_refineAggregate2Record(Aggregate &o);

    bool _isTypedRange(Range *r);
    void _fixRangeTypedrange(Range &o);

    // Fix attribute RANGE/REVERSE_RANGE
    bool _isSingleBoundRange(Range &o);
    Range *_fixSingleBoundRange(Range &o);

    /// @name View-related fixes
    /// @{
    void _fixUselessLibraryInclusions(View *o);
    void _fixBlockStatements(View *o);
    /// @}

    Value *_fixFunctionCall(FunctionCall *o);
    Value *_fixFunctionCall2TypeRef(FunctionCall *o, TypeReference *typeref, TypeReference::DeclarationType *declTr);
    Value *_fixFunctionCall2MemberOrSlice(FunctionCall *o, Value *currentMember, Declaration *memDecl);

    hif::semantics::ILanguageSemantics *_sem;
    hif::HifFactory _factory;

    std::set<hif::SubProgram *> _currentSubs;

    bool _haveMeetAggregate;
    bool _haveToFixAggregate;
    bool _addArith;

    RangeMap _rangeMap;

    hif::application_utils::WarningSet _unconstrainedGenerics;
};

PostParsingVisitor_step1::PostParsingVisitor_step1(hif::semantics::ILanguageSemantics *sem)
    : secondVisit(false)
    , _sem(sem)
    , _factory(_sem)
    , _currentSubs()
    , _haveMeetAggregate(false)
    , _haveToFixAggregate(false)
    , _addArith(false)
    , _rangeMap()
    , _unconstrainedGenerics()
{
    hif::application_utils::initializeLogHeader("VHDL2HIF", "PostParsingVisitor_step1");
}

PostParsingVisitor_step1::~PostParsingVisitor_step1()
{
    hif::application_utils::restoreLogHeader();

    messageWarningList(
        true,
        "Unconstrained ranges in generics are not supported. "
        "Approximating with the range of initial value or 64 bits.",
        _unconstrainedGenerics);
}

int PostParsingVisitor_step1::visitAggregate(hif::Aggregate &o)
{
    // First run to perform other fixes different from _refineAggregate2Record
    // Second run (top-down) to perform _refineAggregate2Record

    //    if (!secondVisit && getNearestParent<ParameterAssign>(&o) != nullptr)
    //        return GuideVisitor::visitAggregate(o);
    //    else if (!secondVisit)
    //        GuideVisitor::visitAggregate(o);
    if (!secondVisit)
        return GuideVisitor::visitAggregate(o);

    // 1st run
    if (!_haveToFixAggregate) {
        const bool restore = _haveMeetAggregate;
        _haveMeetAggregate = true;
        GuideVisitor::visitAggregate(o);
        _haveMeetAggregate = restore;
    }

    // 2nd run
    if (!_haveMeetAggregate) // is top aggregate
    {
        _haveToFixAggregate = true;
    }
    if (_haveToFixAggregate) {
        Value *v = _refineAggregate2Record(o);
        if (v != nullptr) {
            if (v != &o)
                v->acceptVisitor(*this); // fix the new Record
            else
                GuideVisitor::visitAggregate(o); // fix only Aggregate children
        }
    }
    if (!_haveMeetAggregate) // is top aggregate
    {
        _haveToFixAggregate = false;
    }

    return 0;
}

int PostParsingVisitor_step1::visitArray(hif::Array &o)
{
    GuideVisitor::visitArray(o);

    // All arrays of type bit are mapped to vectors.
    if (dynamic_cast<Bit *>(o.getType()) != nullptr) {
        Bit *b        = static_cast<Bit *>(o.getType());
        Bitvector *bv = new Bitvector();
        bv->setSpan(o.setSpan(nullptr));
        bv->setSigned(o.isSigned());
        bv->setConstexpr(typeIsConstexpr(&o, _sem));
        bv->setResolved(b->isResolved());
        bv->setLogic(b->isLogic());
        o.replace(bv);
        delete &o;
        bv->acceptVisitor(*this);
    }

    return 0;
}

int PostParsingVisitor_step1::visitBitvector(hif::Bitvector &o)
{
    GuideVisitor::visitBitvector(o);
    // Set sign of logic vector depending on which header is included.

    Contents *contP = hif::getNearestParent<Contents>(&o);
    View *viewP     = hif::getNearestParent<View>(&o);
    LibraryDef *ldP = hif::getNearestParent<LibraryDef>(&o);
    System *sysP    = hif::getNearestParent<System>(&o);
    bool isFixed    = false;
    if (contP != nullptr && !isFixed) {
        isFixed = _setSignOfArray(o, contP->libraries);
    }
    if (viewP != nullptr && !isFixed) {
        isFixed = _setSignOfArray(o, viewP->libraries);
    }
    if (ldP != nullptr && !isFixed) {
        isFixed = _setSignOfArray(o, ldP->libraries);
    }
    if (sysP != nullptr && !isFixed) {
        isFixed = _setSignOfArray(o, sysP->libraries);
    }

    if (isFixed) {
        Type *t = nullptr;
        if (o.isSigned()) {
            Signed *s = new Signed();
            s->setSpan(o.setSpan(nullptr));
            s->setConstexpr(o.isConstexpr());
            s->setSourceFileName(o.getSourceFileName());
            s->setSourceLineNumber(o.getSourceLineNumber());
            t = s;
        } else {
            Unsigned *s = new Unsigned();
            s->setSpan(o.setSpan(nullptr));
            s->setConstexpr(o.isConstexpr());
            s->setSourceFileName(o.getSourceFileName());
            s->setSourceLineNumber(o.getSourceLineNumber());
            t = s;
        }

        o.replace(t);
        delete &o;
    }

    return 0;
}

int PostParsingVisitor_step1::visitSystem(System &o)
{
    // Notes: during compiling VHDL (vcom) just looks for components of packages.
    // Compiled entities are associated with libraries and not packages.
    // During linking (vsim) required entities are matched just checking libraries.

    std::list<DesignUnit *> dusList;
    hif::HifTypedQuery<DesignUnit> q;
    hif::search(dusList, &o, q);

    typedef std::set<DesignUnit *> DesignUnits;
    DesignUnits dus;
    dus.insert(dusList.begin(), dusList.end());

    // In some design parser cannot establish if a design unit is
    // declared inside a design or not. This is the case when the component
    // is declared inside a library but the entity is implemented inside
    // an external file (that has not got library specification).
    // In this case parser generate two Design Unit:
    // DU_1: Empty inside the library definition.
    // DU_2: Full as child of system object.
    // Following code fix this bad parsing by replacing view of DU_1 with
    // view of DU_2 and erasing the DU_2.
    for (BList<DesignUnit>::iterator i = o.designUnits.begin(); i != o.designUnits.end();) {
        DesignUnit *currentDu = (*i);
        View *view            = currentDu->views.front();
        ViewReference *vr     = new ViewReference();
        vr->setDesignUnit(currentDu->getName());
        Variable *fake = new Variable();
        fake->setType(vr);
        const bool isComponent = view->getContents() == nullptr;
        if (isComponent) {
            Contents *contents_o = new Contents();
            contents_o->setName(view->getName());
            view->setContents(contents_o);
            view->setStandard(true);
        }
        view->getContents()->declarations.push_back(fake);

        // For get check this case we use getDeclaration adding a fake var with
        // a custom viewref type added to DU_2. If declaration is not found
        // or is the same view, this is not our case. Otherwise that means
        // that get declaration has found DU_1 inside libraries of DU_2->view.
        View *libView = hif::semantics::getDeclaration(vr, _sem);
        if (libView == view) {
            // Try to search into inclusions of System
            hif::semantics::DeclarationOptions dopt;
            dopt.location = &o;
            libView       = hif::semantics::getDeclaration(vr, _sem, dopt);
        }

        BList<Declaration>::iterator it(fake);
        it.erase();
        if (isComponent) {
            delete view->setContents(nullptr);
        }

        if (libView == view || libView == nullptr) {
            // Could be a view declared in a package not used by its implementation.
            libView = nullptr;
            for (DesignUnits::iterator j = dus.begin(); j != dus.end(); ++j) {
                DesignUnit *du = *j;
                if (du == *i)
                    continue;
                if (du->getName() != currentDu->getName())
                    continue;

                messageAssert(
                    libView == nullptr,
                    std::string("Found more than one entities with same name,"
                                " this is not supported yet. Entity name is: ") +
                        du->getName(),
                    nullptr, nullptr);
                libView = du->views.front();
            }
        }

        if (libView == view || libView == nullptr) {
            ++i;
            continue;
        }

        // moving the current du inside the library definition.
        libView->replace(hif::copy(view));
        delete libView;
        DesignUnits::iterator dusEntry = dus.find(currentDu);
        if (dusEntry != dus.end())
            dus.erase(dusEntry);
        i = i.erase();
    }

    GuideVisitor::visitSystem(o);

    if (_addArith) {
        hif::manipulation::AddUniqueObjectOptions addOpt;
        addOpt.equalsOptions.checkOnlyNames = true;
        hif::manipulation::addUniqueObject(_sem->getStandardLibrary("ieee_std_logic_arith"), o.libraryDefs, addOpt);
    }

    return 0;
}

int PostParsingVisitor_step1::visitEnumValue(hif::EnumValue &o)
{
    GuideVisitor::visitEnumValue(o);

    if (o.getType() != nullptr)
        return 0;

    Enum *e = dynamic_cast<Enum *>(o.getParent());
    if (e == nullptr) {
        messageError("Wrong enum value parent (1).", &o, _sem);
    }

    TypeDef *td = dynamic_cast<TypeDef *>(e->getParent());
    if (td == nullptr || !td->isOpaque()) {
        messageError("Wrong enum value parent (2).", &o, _sem);
    }

    TypeReference *tr = new TypeReference();
    tr->setName(td->getName());
    o.setType(tr);

    const bool restore = secondVisit;
    GuideVisitor::visitEnumValue(o);
    secondVisit = restore;

    return 0;
}

int PostParsingVisitor_step1::visitExpression(Expression &o)
{
    GuideVisitor::visitExpression(o);

    // fix the operators:
    if (_replaceLogicalOperators(o, _sem))
        return 0;
    if (_replaceRelationalOperators(o, _sem))
        return 0;

    return 0;
}

int PostParsingVisitor_step1::visitFieldReference(FieldReference &o)
{
    GuideVisitor::visitFieldReference(o);

    Identifier *prefix = dynamic_cast<Identifier *>(o.getPrefix());

    if (prefix == nullptr)
        return 0;

    std::string prefixString = prefix->getName();
    if (prefixString != "ieee")
        return 0;

    prefixString += std::string("_") + o.getName();

    Instance *i = _factory.libraryInstance(prefixString.c_str(), false, true);
    o.replace(i);
    delete &o;

    i->acceptVisitor(*this);

    return 0;
}

int PostParsingVisitor_step1::visitLibraryDef(LibraryDef &o)
{
    // Removing references of current library def into sub tree because they
    // are unuseful and can create a loop.
    hif::semantics::ReferencesSet list;
    hif::semantics::getReferences(&o, list, _sem, &o);

    for (hif::semantics::ReferencesSet::iterator i = list.begin(); i != list.end(); ++i) {
        Object *obj = *i;
        if (!obj->isInBList())
            continue;
        if (dynamic_cast<Library *>(obj) == nullptr)
            continue;

        Library *lib = static_cast<Library *>(obj);
        BList<Library>::iterator it(lib);
        it.erase();
    }

    GuideVisitor::visitLibraryDef(o);
    return 0;
}

int PostParsingVisitor_step1::visitFunctionCall(FunctionCall &o)
{
    GuideVisitor::visitFunctionCall(o);
    Value *ret         = _fixFunctionCall(&o);
    const bool restore = secondVisit;
    secondVisit        = true;
    if (&o == ret) {
        GuideVisitor::visitFunctionCall(o);
    } else {
        ret->acceptVisitor(*this);
    }
    secondVisit = restore;

    return 0;
}

int PostParsingVisitor_step1::visitFunction(Function &o)
{
    // Break recursive calls
    if (_currentSubs.find(&o) != _currentSubs.end())
        return 0;
    _currentSubs.insert(&o);

    // Reimplementing visiting order to fix ranges with dir typedrange:
    // parameter and the body must be visited before return value.

    visitList(o.templateParameters);
    visitList(o.parameters);
    if (o.getStateTable())
        o.getStateTable()->acceptVisitor(*this);
    if (o.getType())
        o.getType()->acceptVisitor(*this);

    _currentSubs.erase(&o);
    return 0;
}

int PostParsingVisitor_step1::visitIdentifier(Identifier &o)
{
    GuideVisitor::visitIdentifier(o);

    // Special characters management:
    if (o.getName() == std::string("nul")) {
        Identifier::DeclarationType *decl = hif::semantics::getDeclaration(&o, _sem);
        if (decl != nullptr)
            return 0;
        CharValue *c = _factory.charval('\0');
        o.replace(c);
        delete &o;
    } else if (o.getName() == std::string("lf")) {
        Identifier::DeclarationType *decl = hif::semantics::getDeclaration(&o, _sem);
        if (decl != nullptr)
            return 0;
        CharValue *c = _factory.charval('\n');
        o.replace(c);
        delete &o;
    }

    return 0;
}

int PostParsingVisitor_step1::visitPointer(Pointer &o)
{
    GuideVisitor::visitPointer(o);

    if (o.getType() != nullptr)
        return 0;

    Cast *c = dynamic_cast<Cast *>(o.getParent());
    messageAssert(c != nullptr, "Unexpected pointer parent", &o, _sem);

    Type *t = getOtherOperandType(c, _sem);
    messageAssert(t != nullptr, "Other type not found", &o, _sem);

    o.setType(hif::copy(t));

    return 0;
}

int PostParsingVisitor_step1::visitProcedure(Procedure &o)
{
    // Break recursive calls
    if (_currentSubs.find(&o) != _currentSubs.end())
        return 0;
    _currentSubs.insert(&o);

    GuideVisitor::visitProcedure(o);

    _currentSubs.erase(&o);

    return 0;
}

int PostParsingVisitor_step1::visitProcedureCall(hif::ProcedureCall &o)
{
    GuideVisitor::visitProcedureCall(o);

    ProcedureCall::DeclarationType *sub_o = _getMethodDeclaration(&o, _sem);
    if (sub_o == nullptr) {
        messageError("Not found declaration of procedure ", &o, _sem);
    }

    hif::manipulation::sortParameters(
        o.parameterAssigns, sub_o->parameters, true, hif::manipulation::SortMissingKind::NOTHING, _sem);

    return 0;
}

int PostParsingVisitor_step1::visitRange(Range &o)
{
    GuideVisitor::visitRange(o);

    if (_isTypedRange(&o)) {
        _fixRangeTypedrange(o);
    } else if (_isSingleBoundRange(o)) {
        _fixSingleBoundRange(o);
    }

    return 0;
}

int PostParsingVisitor_step1::visitTypeReference(hif::TypeReference &o)
{
    GuideVisitor::visitTypeReference(o);

    TypeDef *td = dynamic_cast<TypeDef *>(hif::semantics::getDeclaration(&o, _sem));
    if (td == nullptr)
        return 0;

    _fixTemplateTypereferences(&o, td);

    return 0;
}

int PostParsingVisitor_step1::visitView(View &o)
{
    _fixUselessLibraryInclusions(&o);
    _fixBlockStatements(&o);

    GuideVisitor::visitView(o);

    return 0;
}

int PostParsingVisitor_step1::visitViewReference(ViewReference &o)
{
    // Set the correct View name in all ViewReference:
    // In VHDL you can not specify the architecture name when there is only
    // one architecture. This fix that case setting the name of viewref that
    // has not got view name set but only unit name.
    //
    GuideVisitor::visitViewReference(o);
    std::string s = o.getName();
    if (s != NameTable::getInstance()->none()) {
        // if the name of the viewref is already configured, then do nothing ...
        return 0;
    }

    View *v = hif::semantics::getDeclaration(&o, _sem);
    messageAssert(v != nullptr, "Cannot find declaration of viewref", &o, _sem);
    o.setName(v->getName());

    return 0;
}

int PostParsingVisitor_step1::visitBitValue(BitValue &o)
{
    GuideVisitor::visitBitValue(o);
    _fixConstValue(o);
    return 0;
}

int PostParsingVisitor_step1::visitBitvectorValue(BitvectorValue &o)
{
    GuideVisitor::visitBitvectorValue(o);
    _fixConstValue(o);
    return 0;
}

int PostParsingVisitor_step1::visitBoolValue(BoolValue &o)
{
    GuideVisitor::visitBoolValue(o);
    _fixConstValue(o);
    return 0;
}

int PostParsingVisitor_step1::visitCharValue(CharValue &o)
{
    GuideVisitor::visitCharValue(o);
    _fixConstValue(o);
    return 0;
}

int PostParsingVisitor_step1::visitIntValue(IntValue &o)
{
    GuideVisitor::visitIntValue(o);
    _fixConstValue(o);
    return 0;
}

int PostParsingVisitor_step1::visitRealValue(RealValue &o)
{
    GuideVisitor::visitRealValue(o);
    _fixConstValue(o);
    return 0;
}

int PostParsingVisitor_step1::visitStringValue(StringValue &o)
{
    GuideVisitor::visitStringValue(o);
    _fixConstValue(o);
    return 0;
}

int PostParsingVisitor_step1::visitAlias(Alias &o)
{
    const bool restore = secondVisit;
    secondVisit        = true;
    GuideVisitor::visitAlias(o);
    secondVisit = restore;
    return 0;
}

int PostParsingVisitor_step1::visitConst(Const &o)
{
    GuideVisitor::visitConst(o);
    const bool restore = secondVisit;
    secondVisit        = true;
    GuideVisitor::visitConst(o);
    secondVisit = restore;
    return 0;
}

int PostParsingVisitor_step1::visitField(Field &o)
{
    GuideVisitor::visitField(o);
    const bool restore = secondVisit;
    secondVisit        = true;
    GuideVisitor::visitField(o);
    secondVisit = restore;
    return 0;
}

int PostParsingVisitor_step1::visitParameter(Parameter &o)
{
    GuideVisitor::visitParameter(o);
    const bool restore = secondVisit;
    secondVisit        = true;
    GuideVisitor::visitParameter(o);
    secondVisit = restore;
    return 0;
}

int PostParsingVisitor_step1::visitPort(Port &o)
{
    GuideVisitor::visitPort(o);
    const bool restore = secondVisit;
    secondVisit        = true;
    GuideVisitor::visitPort(o);
    secondVisit = restore;
    return 0;
}

int PostParsingVisitor_step1::visitSignal(Signal &o)
{
    GuideVisitor::visitSignal(o);
    const bool restore = secondVisit;
    secondVisit        = true;
    GuideVisitor::visitSignal(o);
    secondVisit = restore;
    return 0;
}

int PostParsingVisitor_step1::visitVariable(Variable &o)
{
    GuideVisitor::visitVariable(o);
    const bool restore = secondVisit;
    secondVisit        = true;
    GuideVisitor::visitVariable(o);
    secondVisit = restore;
    return 0;
}

int PostParsingVisitor_step1::visitValueTP(ValueTP &o)
{
    GuideVisitor::visitValueTP(o);
    const bool restore = secondVisit;
    secondVisit        = true;
    GuideVisitor::visitValueTP(o);
    secondVisit = restore;
    return 0;
}

int PostParsingVisitor_step1::visitAssign(Assign &o)
{
    GuideVisitor::visitAssign(o);
    const bool restore = secondVisit;
    secondVisit        = true;
    GuideVisitor::visitAssign(o);
    secondVisit = restore;
    return 0;
}

int PostParsingVisitor_step1::visitPortAssign(PortAssign &o)
{
    GuideVisitor::visitPortAssign(o);
    const bool restore = secondVisit;
    secondVisit        = true;
    GuideVisitor::visitPortAssign(o);
    secondVisit = restore;
    return 0;
}

int PostParsingVisitor_step1::visitParameterAssign(ParameterAssign &o)
{
    GuideVisitor::visitParameterAssign(o);
    return 0;
}

bool PostParsingVisitor_step1::fixCollectRanges()
{
    typedef std::set<Range *> RangeSet;
    typedef std::map<TypeReference *, RangeSet> TrMap;
    typedef std::map<RangeSet, TypeReferenceSet> PartitionMap;
    TrMap trMap;

    bool ret = false;
    for (RangeMap::iterator i = _rangeMap.begin(); i != _rangeMap.end(); ++i) {
        Range *range                   = (*i).first;
        RangeRefs &refs                = (*i).second;
        //TypeDef * decl = refs.decl;
        TypeReferenceSet &uptoTyperefs = refs.uptoTyperefs;
        //TypeReferenceSet & downtoTyperefs = refs.downtoTyperefs;
        RangeInfoSet &uptos            = refs.uptos;
        RangeInfoSet &downtos          = refs.downtos;

        if (uptos.empty()) {
            // nothing to do since typedef is already downto
            continue;
        } else if (downtos.empty()) {
            // all upto: reverse range direction!
            range->setDirection(dir_upto);
            ret = true;
            continue;
        } else {
            // mixed case
            ret = true;
            // collecting for further fixes
            for (TypeReferenceSet::iterator j = uptoTyperefs.begin(); j != uptoTyperefs.end(); ++j) {
                TypeReference *tr = *j;
                trMap[tr].insert(range);
            }
        }
    }

    // Partitioning conlicting typerefs
    PartitionMap partitionMap;
    for (TrMap::iterator i = trMap.begin(); i != trMap.end(); ++i) {
        TypeReference *tr  = (*i).first;
        RangeSet &rangeSet = (*i).second;
        partitionMap[rangeSet].insert(tr);
    }

    // For all partitions, copy declaration and revert its ranges.
    for (PartitionMap::iterator i = partitionMap.begin(); i != partitionMap.end(); ++i) {
        const RangeSet &rangeSet   = (*i).first;
        TypeReferenceSet &typerefs = (*i).second;
        assert(!rangeSet.empty());
        TypeDef *originalDecl = _rangeMap[*(rangeSet.begin())].decl;

        // Trick: to revert ranges, revert on original, copy, and then revert back original.
        for (RangeSet::const_iterator j = rangeSet.begin(); j != rangeSet.end(); ++j) {
            Range *range = *j;
            range->setDirection(dir_upto);
        }
        TypeDef *newTypedef = hif::copy(originalDecl);
        for (RangeSet::const_iterator j = rangeSet.begin(); j != rangeSet.end(); ++j) {
            Range *range = *j;
            range->setDirection(dir_downto);
        }
        newTypedef->setOpaque(false); // to assure type checking compatibility
        BList<Declaration>::iterator pos(originalDecl);
        pos.insert_after(newTypedef);

        std::string newName = NameTable::getInstance()->getFreshName(originalDecl->getName(), "_upto");
        newTypedef->setName(newName);

        for (TypeReferenceSet::iterator j = typerefs.begin(); j != typerefs.end(); ++j) {
            TypeReference *tr = *j;
            tr->setName(newName);
        }
    }

    return ret;
}

void PostParsingVisitor_step1::_fixConstValue(ConstValue &o)
{
    // Avoid double visit, just for optimization.
    const bool hasType = (o.getType() != nullptr);
    hif::manipulation::assureSyntacticType(&o, _sem);
    if (o.getType() == nullptr || hasType)
        return;
    o.getType()->acceptVisitor(*this);
}

void PostParsingVisitor_step1::_fixTemplateTypereferences(hif::TypeReference *tr, hif::TypeDef *decl)
{
    // First of all, assure that declaration is already fixed.
    if (dynamic_cast<Enum *>(decl->getType()) == nullptr) {
        decl->acceptVisitor(*this);
    }

    // If all template parameters are assigned, there is no fix to be done.
    // This is just an optimization
    if (decl->templateParameters.size() == tr->templateParameterAssigns.size()) {
        tr->ranges.clear();
        return;
    }

    // In this case, it means that there is an implicit template parameter.
    // Try to add it to the nearest scope.
    // Unfortunately there is no common parent for template holders,
    // thus we have to perform many casts...
    TypeDef *parentTd      = hif::getNearestParent<TypeDef>(tr);
    DataDeclaration *ddecl = hif::getNearestParent<DataDeclaration>(tr);
    SubProgram *sub        = hif::getNearestParent<SubProgram>(tr);
    View *view             = hif::getNearestParent<View>(tr);

    // Since Parameter is child class of data declaration, we have to skip it to
    // get the subprogram template list
    if (dynamic_cast<Parameter *>(ddecl) != nullptr) {
        ddecl = nullptr;
        messageDebugAssert(sub != nullptr, "Unexpected param scope", tr, nullptr);
    }

    hif::BList<hif::Declaration> *templates = nullptr;

    if (parentTd != nullptr) {
        templates = &parentTd->templateParameters;
    } else if (ddecl != nullptr) {
        templates                  = nullptr;
        // Ref design: vhdl/ips/mephisto_core
        const bool isImplicitRange = tr->ranges.size() * 2 < decl->templateParameters.size();
        if (ddecl->getType() == tr && ddecl->getValue() != nullptr && isImplicitRange) {
            ddecl->getValue()->acceptVisitor(*this);
            Range *r       = nullptr;
            // Ref design: vhdl/openCores/minimips
            //             vhdl/ips/mephisto_core
            Aggregate *agg = dynamic_cast<Aggregate *>(ddecl->getValue());
            if (agg != nullptr) {
                r               = new Range(0, agg->alts.size() - 1);
                Range *declSpan = (hif::typeGetSpan(decl->getType(), _sem));
                if (declSpan != nullptr && declSpan->getDirection() == dir_downto) {
                    r->swapBounds();
                }
            } else {
                Type *ot = hif::semantics::getSemanticType(ddecl->getValue(), _sem);
                r        = hif::copy(hif::typeGetSpan(ot, _sem));
            }

            messageAssert(r != nullptr, "Cannot type initial value", ddecl, _sem);
            tr->ranges.push_back(r);
        }
    } else if (sub != nullptr) {
        templates = &sub->templateParameters;
    } else if (view != nullptr) {
        templates = &view->templateParameters;
    } else {
        // a global or non-scoped typeref is used, which has not all
        // template parameters assigned...error?
        messageDebug("Decl:", decl, _sem);
        messageDebug("Parent:", tr->getParent(), _sem);
        messageError("Not found scope with template parameters of a typeref", tr, _sem);
    }

    const BList<Range>::size_t dim = 2 * tr->ranges.size();

    // Sanity check: if no new implicit templates can be added,
    // then the number of specified ranges must match the number of
    // typeref-required template assigns.
    messageAssert(
        (templates != nullptr || dim >= decl->templateParameters.size()), "Unsupported template typeref found.", tr,
        _sem);

    for (BList<Declaration>::iterator i = decl->templateParameters.begin() + dim; i != decl->templateParameters.end();
         ++i) {
        messageAssert(templates != nullptr, "Unexpected case", nullptr, nullptr);

        bool found = false;
        for (BList<TPAssign>::iterator j = tr->templateParameterAssigns.begin();
             j != tr->templateParameterAssigns.end(); ++j) {
            if ((*i)->getName() == (*j)->getName()) {
                found = true;
                break;
            }
        }

        if (found)
            continue;

        ValueTP *originalTp = dynamic_cast<ValueTP *>(*i);
        if (originalTp == nullptr) {
            // error?
            messageWarning("Unexpected template parameter kind for a typeref:", tr, _sem);
            messageError("Corresponding Typedef:", decl, _sem);
        }

        // creating and adding the new tp assign
        Identifier *newTp = new Identifier();
        newTp->setName(hif::NameTable::getInstance()->getFreshName(originalTp->getName()));

        ValueTPAssign *vtpa = new ValueTPAssign();
        vtpa->setName(originalTp->getName());
        vtpa->setValue(newTp);
        tr->templateParameterAssigns.push_back(vtpa);

        ValueTP *vtp = new ValueTP();
        vtp->setName(newTp->getName());
        vtp->setType(hif::copy(originalTp->getType()));
        templates->push_back(vtp);
    }

    BList<Range>::iterator i       = tr->ranges.begin();
    BList<Declaration>::iterator j = decl->templateParameters.begin();
    for (; i != tr->ranges.end() && j != decl->templateParameters.end(); ++i) {
        // For each iteration on range, we must consider a couple of bounds.

        std::string lName = (*j)->getName();
        ++j;
        std::string rName = (*j)->getName();
        ++j;

        const bool invertedRange = (*i)->getDirection() == dir_upto;

        ValueTPAssign *tpa_l = new ValueTPAssign();
        tpa_l->setName(lName);
        tpa_l->setValue(hif::copy((*i)->getLeftBound()));

        ValueTPAssign *tpa_r = new ValueTPAssign();
        tpa_r->setName(rName);
        tpa_r->setValue(hif::copy((*i)->getRightBound()));

        tr->templateParameterAssigns.push_back(tpa_l);
        tr->templateParameterAssigns.push_back(tpa_r);

        // fill range map infos
        hif::HifUntypedQuery q;
        q.name = lName;
        hif::HifUntypedQuery::Results list;
        hif::search(list, decl->getType(), q);
        messageAssert(list.size() == 1, "Unexpected number of references", decl->getType(), _sem);
        Identifier *id = dynamic_cast<Identifier *>(list.front());
        messageAssert(id != nullptr, "Unexpected case", list.front(), _sem);
        Range *pRange = dynamic_cast<Range *>(id->getParent());
        messageAssert(pRange != nullptr, "Unexpected parent", id->getParent(), _sem);
        _rangeMap[pRange].decl = decl;
        if (invertedRange) {
            _rangeMap[pRange].uptoTyperefs.insert(tr);
            _rangeMap[pRange].uptos.insert(RangeInfo(tpa_l, tpa_r));
        } else {
            _rangeMap[pRange].downtoTyperefs.insert(tr);
            _rangeMap[pRange].downtos.insert(RangeInfo(tpa_l, tpa_r));
        }
    }

    tr->ranges.clear();
}

bool PostParsingVisitor_step1::_setSignOfArray(Bitvector &o, BList<Library> &libraries)
{
    for (BList<Library>::iterator it = libraries.begin(); it != libraries.end(); ++it) {
        if (!(*it)->isSystem())
            continue;
        if ((*it)->getName() == "ieee_std_logic_signed") {
            Library arith;
            arith.setName("ieee_std_logic_arith");
            hif::manipulation::AddUniqueObjectOptions addOpt;
            addOpt.equalsOptions.checkOnlyNames = true;
            addOpt.copyIfUnique                 = true;
            hif::manipulation::addUniqueObject(&arith, libraries, addOpt);
            _addArith = true;

            o.setSigned(true);
            return true;
        } else if ((*it)->getName() == "ieee_std_logic_unsigned") {
            Library arith;
            arith.setName("ieee_std_logic_arith");
            hif::manipulation::AddUniqueObjectOptions addOpt;
            addOpt.equalsOptions.checkOnlyNames = true;
            addOpt.copyIfUnique                 = true;
            hif::manipulation::addUniqueObject(&arith, libraries, addOpt);
            _addArith = true;

            o.setSigned(false);
            return true;
        }
    }
    return false;
}

Value *PostParsingVisitor_step1::_refineAggregate2Record(Aggregate &o)
{
    Type *t = hif::semantics::getOtherOperandType(&o, _sem);
    if (t == nullptr) {
        // first iteration
        return nullptr;
    }

    t = hif::semantics::getBaseType(t, false, _sem);
    if (dynamic_cast<Record *>(t) != nullptr) {
        Record *rec = static_cast<Record *>(t);
        if (rec->fields.size() != o.alts.size() || o.getOthers() != nullptr) {
            messageError("Unable to convert aggregate to proper record type (1).", &o, _sem);
        }

        RecordValue *recVal      = new RecordValue();
        BList<Field>::iterator j = rec->fields.begin();
        for (BList<AggregateAlt>::iterator i = o.alts.begin(); i != o.alts.end(); ++i, ++j) {
            if ((*i)->indices.size() != 1) {
                messageError("Unable to convert aggregate to proper record type (2).", &o, _sem);
            }
            RecordValueAlt *rva = new RecordValueAlt();
            messageAssert(
                !(*j)->getName().empty() && (*j)->getName() != NameTable::getInstance()->none(),
                "Unexpected name not set", (*j), _sem);
            rva->setName((*j)->getName());
            rva->setValue(hif::copy((*i)->getValue()));
            recVal->alts.push_back(rva);
        }

        o.replace(recVal);
        delete &o;
        return recVal;
    }

    return &o;
}

bool PostParsingVisitor_step1::_isTypedRange(Range *r)
{
    messageAssert(r != nullptr, "Unexpected NULl range", nullptr, _sem);
    const bool isString    = dynamic_cast<String *>(r->getParent()) != nullptr;
    const bool leftIsNull  = r->getLeftBound() == nullptr;
    const bool rightIsNull = r->getRightBound() == nullptr;

    return (leftIsNull && rightIsNull) || (isString && (leftIsNull || rightIsNull));
}

void PostParsingVisitor_step1::_fixRangeTypedrange(Range &o)
{
    if (o.getType() != nullptr) {
        TypeReference *tref = dynamic_cast<TypeReference *>(o.getType());
        if (tref != nullptr) {
            Enum *e = dynamic_cast<Enum *>(hif::semantics::getDeclaration(tref, _sem));
            if (e != nullptr) {
                // WTF?
                unsigned int size = e->values.size();
                Range *r          = new Range(size, 0);
                o.replace(r);
                delete &o;
                return;
            }

            TypeDef *td = dynamic_cast<TypeDef *>(hif::manipulation::instantiate(tref, _sem));
            messageAssert(td != nullptr, "Unsupported subtype indication for range (1).", &o, _sem);
            // At the moment, only int is supported
            Int *ii = dynamic_cast<Int *>(td->getType());
            messageAssert(ii != nullptr, "Unsupported subtype indication for range.", &o, _sem);
            o.replace(hif::copy(td->getRange()));
            delete &o;
            return;
        }

        if (dynamic_cast<Int *>(o.getType()) != nullptr) {
            TypeDef *td = hif::getNearestParent<TypeDef>(&o);
            Int *ii     = static_cast<Int *>(o.getType());

            if (td == nullptr) {
                messageError("Unsupported subtype indication for range (2).", &o, _sem);
            }

            ValueTP *tp_l = nullptr;
            ValueTP *tp_r = nullptr;
            make_template_bounds(tp_l, tp_r, ii->getSpan());

            o.setLeftBound(new Identifier(tp_l->getName()));
            o.setRightBound(new Identifier(tp_r->getName()));
            o.setDirection(dir_downto);

            td->templateParameters.push_back(tp_l);
            td->templateParameters.push_back(tp_r);
            delete o.setType(nullptr);
        } else {
            messageError("Unsupported subtype indication for range (3).", &o, _sem);
        }

        return;
    }

    String *stringParent = dynamic_cast<String *>(o.getParent());
    const bool isString  = (stringParent != nullptr);

    // Range with unsetted bounds
    TypeDef *tdo       = hif::getNearestParent<TypeDef>(&o);
    Function *fo       = hif::getNearestParent<Function>(&o);
    Procedure *po      = hif::getNearestParent<Procedure>(&o);
    Parameter *paramo  = hif::getNearestParent<Parameter>(&o);
    Variable *var      = hif::getNearestParent<Variable>(&o);
    Const *constant    = hif::getNearestParent<Const>(&o);
    ValueTP *vtpo      = hif::getNearestParent<ValueTP>(&o);
    TypeTPAssign *ttpa = hif::getNearestParent<TypeTPAssign>(&o);
    Cast *co           = dynamic_cast<Cast *>(o.getParent() != nullptr ? o.getParent()->getParent() : nullptr);

    // add two parameters to the searched types, one for each bound
    if (co != nullptr) {
        Value *v = co->getValue();

        //hif::semantics::resetTypes( v );
        //hif::manipulation::flushInstanceCache();
        Type *t = hif::semantics::getSemanticType(v, _sem);
        messageAssert(t != nullptr, "Cannot type value", v, _sem);
        Range *r = hif::typeGetSpan(t, _sem, true);
        messageDebugAssert(r != nullptr, "Span not found", t, _sem);
        messageDebugAssert(
            r->getLeftBound() != nullptr || r->getRightBound() != nullptr, "Unexpected range without any bound", r,
            _sem);
        o.replace(hif::copy(r));
        delete &o;
    } else if (tdo != nullptr) {
        messageDebugAssert(o.getType() == nullptr, "Unexpected type set", tdo, _sem);

        ValueTP *tp_l = nullptr;
        ValueTP *tp_r = nullptr;
        make_template_bounds(tp_l, tp_r, &o);
        tdo->templateParameters.push_back(tp_l);
        tdo->templateParameters.push_back(tp_r);
    } else if (paramo != nullptr) {
        if (!isString) {
            ValueTP *tp_l = nullptr;
            ValueTP *tp_r = nullptr;
            // this case should be of a lv or bv --> range always positive
            make_template_bounds(tp_l, tp_r, &o);

            SubProgram *parent = (po != nullptr ? static_cast<SubProgram *>(po) : static_cast<SubProgram *>(fo));

            parent->templateParameters.push_back(tp_l);
            parent->templateParameters.push_back(tp_r);
        }
    } else if (vtpo != nullptr) {
        Range *ro = nullptr;

        if (vtpo->getValue() != nullptr) {
            Type *type = hif::semantics::getSemanticType(vtpo->getValue(), _sem);
            messageAssert(type != nullptr, "Cannot type init val", vtpo, _sem);
            if (isString) {
                String *typeString = dynamic_cast<String *>(type);
                messageAssert(typeString != nullptr, "Unexpected case", type, _sem);
                ro = hif::copy(typeString->getSpanInformation());
                hif::manipulation::simplify(ro, _sem);
                o.replace(ro);
                delete &o;
            } else {
                ro = hif::copy(hif::typeGetSpan(type, _sem));
                messageAssert(ro != nullptr, "Cannot find range", type, _sem);
                o.replace(ro);
                delete &o;
            }
        } else if (!isString) {
            _unconstrainedGenerics.insert(vtpo);
            ro = new Range(63, 0);
            o.replace(ro);
            delete &o;
        }
    } else if (fo != nullptr) {
        if (isString) {
            // Function returning strings may have different return span.
            // Therefore remove typed range.
            // See vhdl/gaisler/can_oc/stdlib.v function tost(v:std_logic_vector)
            o.setDirection(dir_downto);
            delete o.setRightBound(new IntValue(1));
            delete o.setLeftBound(nullptr);
            return;
        }

        hif::HifTypedQuery<Return> q;
        std::list<Return *> result;
        hif::search(result, fo, q);

        for (std::list<Return *>::iterator i = result.begin(); i != result.end();) {
            if (hif::getNearestParent<Function>(*i) != fo) {
                i = result.erase(i);
            } else {
                break;
            }
        }

        if (result.empty()) {
            messageError("Not found any return in function.", &o, _sem);
        }

        Type *type = hif::semantics::getSemanticType(result.front()->getValue(), _sem);
        messageAssert(type != nullptr, "Cannot type value", result.front(), _sem);

        Range *ro = hif::copy(hif::typeGetSpan(type, _sem));
        messageAssert(ro != nullptr, "Span not found", type, _sem);

        o.replace(ro);
        delete &o;

        // Simplify constants inside local scopes referred by return type (global scope).
        // See vhdl/gaisler/can_oc/stdlib.v function tost(v:std_logic_vector)
        // skipping isString check
        hif::manipulation::SimplifyOptions sopt;
        sopt.simplify_constants = true;
        sopt.context            = fo;
        hif::manipulation::simplify(ro, _sem, sopt);
    } else if (ttpa != nullptr) {
        // support only copy constructor at the moment
        FunctionCall *fcall = dynamic_cast<FunctionCall *>(ttpa->getParent());
        messageAssert(fcall != nullptr, "Unexpected typed range location", ttpa->getParent(), _sem);
        messageAssert(
            fcall->getName() == "new" && fcall->templateParameterAssigns.size() == 1 &&
                fcall->parameterAssigns.size() == 1,
            "Found typed range inside unsupported function call."
            " At the moment is supported only copy constructors.",
            fcall, _sem);
        Value *v = fcall->parameterAssigns.front()->getValue();
        messageAssert(v != nullptr, "Expected value", fcall->parameterAssigns.front(), _sem);

        Type *type = hif::semantics::getSemanticType(v, _sem);
        messageAssert(type != nullptr, "Cannot type value", v, _sem);

        if (isString) {
            String *typeString = dynamic_cast<String *>(type);
            messageAssert(typeString != nullptr, "Unexpected case", type, _sem);
            Range *ro = hif::copy(typeString->getSpanInformation());
            o.replace(ro);
            delete &o;
            hif::manipulation::simplify(ro, _sem);
        } else {
            Range *ro = hif::copy(hif::typeGetSpan(type, _sem));
            messageAssert(ro != nullptr, "Span not found", type, _sem);
            o.replace(ro);
            delete &o;
            hif::manipulation::simplify(ro, _sem);
        }
    } else if (po != nullptr) {
        messageError("Typed range in procedure objects are not supported yet.", po, _sem);
    } else if (var != nullptr) {
        messageAssert(var->getValue() != nullptr, "Unexpected variable without initial value", var, _sem);

        Type *initType = nullptr;
        if (hif::isSubNode(&o, var->getType())) {
            var->getValue()->acceptVisitor(*this);
            initType = hif::semantics::getSemanticType(var->getValue(), _sem);
        } else // from initial value
        {
            var->getType()->acceptVisitor(*this);
            initType = var->getType();
        }

        messageAssert(initType != nullptr, "Cannot type variable initial value", var, _sem);
        if (isString) {
            String *typeString = dynamic_cast<String *>(initType);
            messageAssert(typeString != nullptr, "Unexpected case", initType, _sem);
            Range *ro = hif::copy(typeString->getSpanInformation());
            o.replace(ro);
            delete &o;
            hif::manipulation::simplify(ro, _sem);
        } else {
            Range *ro = hif::copy(hif::typeGetSpan(initType, _sem));
            messageAssert(ro != nullptr, "Span not found", initType, _sem);
            o.replace(ro);
            delete &o;
            hif::manipulation::simplify(ro, _sem);
        }
    } else if (constant != nullptr) {
        messageAssert(constant->getValue() != nullptr, "Unexpected constant without initial value", constant, _sem);

        Type *initType = nullptr;
        if (hif::isSubNode(&o, constant->getType())) {
            constant->getValue()->acceptVisitor(*this);
            initType = hif::semantics::getSemanticType(constant->getValue(), _sem);
        } else // from initial value
        {
            constant->getType()->acceptVisitor(*this);
            initType = constant->getType();
        }

        messageAssert(initType != nullptr, "Cannot type constant initial value", constant, _sem);
        if (isString) {
            String *typeString = dynamic_cast<String *>(initType);
            messageAssert(typeString != nullptr, "Unexpected case", initType, _sem);
            Range *ro = hif::copy(typeString->getSpanInformation());
            o.replace(ro);
            delete &o;
            hif::manipulation::simplify(ro, _sem);
        } else {
            Range *ro = hif::copy(hif::typeGetSpan(initType, _sem));
            messageAssert(ro != nullptr, "Span not found", initType, _sem);
            o.replace(ro);
            delete &o;
            hif::manipulation::simplify(ro, _sem);
        }
    } else if (!isString) {
        messageError("Unexpected object with typed range.", o.getParent()->getParent()->getParent()->getParent(), _sem);
    }
}

bool PostParsingVisitor_step1::_isSingleBoundRange(Range &o)
{
    if (o.getLeftBound() == nullptr)
        return false;
    FunctionCall *lb = dynamic_cast<FunctionCall *>(o.getLeftBound());
    if (lb == nullptr)
        return false;
    if (!objectMatchName(lb, "range") && !objectMatchName(lb, "reverse_range"))
        return false;

    FunctionCall::DeclarationType *decl = hif::semantics::getDeclaration(lb, _sem);
    messageAssert((decl != nullptr), "Cannot find declaration of function call", lb, _sem);

    // avoiding unknown implementations...
    LibraryDef *std = dynamic_cast<LibraryDef *>(decl->getParent());
    if (std == nullptr)
        return false;
    const bool isStd = (std->getName() == "standard");
    if (!isStd)
        return false;

    messageAssert(
        ((lb->parameterAssigns.size() == 0 || lb->parameterAssigns.size() == 1) && o.getRightBound() == nullptr),
        "Bad parsing of VHDL attribute range / reverse_range", &o, _sem);

    return true;
}

Range *PostParsingVisitor_step1::_fixSingleBoundRange(Range &o)
{
    FunctionCall *lb = dynamic_cast<FunctionCall *>(o.getLeftBound());
    Value *p1        = lb->getInstance();
    Value *p2        = nullptr;
    if (lb->parameterAssigns.size() == 1) {
        p2 = lb->parameterAssigns.front()->getValue();
    }

    Type *a = hif::semantics::getSemanticType(p1, _sem);
    messageAssert((a != nullptr), "Missing type", p1, _sem);

    long long int n = 1; // Default.
    if (p2 != nullptr) {
        hif::manipulation::simplify(p2, _sem);
        ConstValue *nC = dynamic_cast<ConstValue *>(p2);
        messageAssert(
            nC != nullptr,
            "Unsupported VHDL attribute RANGE/REVERSE_RANGE with "
            "unpredictable parameter N",
            lb, _sem);

        Int *ivt     = _factory.integer();
        IntValue *iv = dynamic_cast<IntValue *>(hif::manipulation::transformConstant(nC, ivt, _sem));
        delete ivt;
        messageAssert(
            iv != nullptr,
            "Unsupported VHDL attribute RANGE/REVERSE_RANGE with "
            "unpredictable parameter N",
            lb, _sem);

        n = iv->getValue();
        delete iv;
    }

    // A'RANGE[(N)] / A'REVERSE_RANGE[(N)]

    Type *innerT = hif::typeGetNestedType(a, _sem, n - 1);
    Range *ret   = hif::copy(hif::typeGetSpan(innerT, _sem));

    const bool doRevert = (objectMatchName(lb, "reverse_range"));
    if (doRevert) {
        Value *tmp = ret->getLeftBound();
        ret->setLeftBound(ret->getRightBound());
        ret->setRightBound(tmp);
        ret->setDirection(ret->getDirection() == dir_upto ? dir_downto : dir_upto);
    }

    o.replace(ret);
    delete &o;
    return ret;
}

void PostParsingVisitor_step1::_fixUselessLibraryInclusions(View *o)
{
    // Scan the list of libraries to look for useless library inclusion
    // that refer to design units in the system
    System *sys = hif::getNearestParent<System>(o);
    bool erased;
    for (BList<Library>::iterator i = o->libraries.begin(); i != o->libraries.end();) {
        erased       = false;
        Library *lib = *i;
        // Check whether there is a design unit having the same name
        // of the current library
        for (BList<DesignUnit>::iterator j = sys->designUnits.begin(); j != sys->designUnits.end(); ++j) {
            if (lib->getName() == (*j)->getName()) {
                i      = i.erase();
                erased = true;
                break;
            }
        }
        if (!erased)
            ++i;
    }
}

void PostParsingVisitor_step1::_fixBlockStatements(View *o)
{
    if (o->getContents() == nullptr)
        return;

    for (BList<Declaration>::iterator it(o->getContents()->declarations.begin());
         it != o->getContents()->declarations.end();) {
        View *v = dynamic_cast<View *>(*it);
        if (v == nullptr || !v->checkProperty("block_statement")) {
            ++it;
            continue;
        }

        // Restricting VHDL block-statement case
        const bool emptyTP    = v->templateParameters.empty();
        const bool emptyPorts = v->getEntity()->ports.empty();
        messageAssert(emptyTP && emptyPorts, "Unsupported", v, _sem);

        // Recursion must be performed here, before GuideVisitor of View
        _fixBlockStatements(v);

        hif::manipulation::moveToScope(v, o, _sem, "");
        it = it.erase();
    }
}

Value *PostParsingVisitor_step1::_fixFunctionCall(FunctionCall *o)
{
    if (o->checkProperty(RECOGNIZED_FCALL_PROPERTY)) {
        // It is certainly a function call.
        o->removeProperty(RECOGNIZED_FCALL_PROPERTY);
        //return 0; // TODO ENABLE
    }

    // fixing instance
    if (o->getInstance() != nullptr) {
        hif::semantics::updateDeclarations(o->getInstance(), _sem);
    }

    Value *originalInst = hif::copy(o->getInstance());
    if (o->getInstance() != nullptr) {
        Library *lib = resolveLibraryType(o->getInstance(), true);
        if (lib != nullptr) {
            Instance *inst = new Instance();
            inst->setName(lib->getName());
            inst->setReferencedType(lib);
            delete o->setInstance(inst);
        }
    }

    Value *ret = nullptr;

    // //////////////////////////////////////////////////////////
    // it's a type ref?
    TypeReference *typeref = new TypeReference();
    typeref->setName(o->getName());
    Instance *inst = dynamic_cast<Instance *>(o->getInstance());
    if (inst != nullptr) {
        typeref->setInstance(hif::copy(inst->getReferencedType()));
    }

    hif::semantics::DeclarationOptions dopt;
    dopt.location                          = o;
    dopt.looseTypeChecks                   = true;
    TypeReference::DeclarationType *declTr = hif::semantics::getDeclaration(typeref, _sem, dopt);

    if (declTr != nullptr) {
        ret = _fixFunctionCall2TypeRef(o, typeref, declTr);
        delete originalInst;
        return ret;
    }

    // it was not a typeref: restoring old object.
    delete typeref;

    // //////////////////////////////////////////////////////////
    // it's a member?
    Value *currentMember = nullptr;
    if (originalInst != nullptr) {
        FieldReference *fr = new FieldReference();
        fr->setName(o->getName());
        fr->setPrefix(originalInst);
        currentMember = fr;
    } else {
        Identifier *idf = new Identifier();
        idf->setName(o->getName());
        currentMember = idf;
        delete originalInst;
    }

    Declaration *memDecl = hif::semantics::getDeclaration(currentMember, _sem, dopt);
    if (memDecl != nullptr && dynamic_cast<SubProgram *>(memDecl) == nullptr &&
        dynamic_cast<EnumValue *>(memDecl) == nullptr) // ref design: can_oc
    {
        ret = _fixFunctionCall2MemberOrSlice(o, currentMember, memDecl);
        return ret;
    }

    // it was not a member: restoring old object.
    delete currentMember;

    // //////////////////////////////////////////////////////////
    // it's a real function call: checking declaration!

    // Fixing candidates declarations, since can contain bad stuff as typed ranges.
    std::list<FunctionCall::DeclarationType *> candidates;
    hif::semantics::getCandidates(candidates, o, _sem);
    for (std::list<FunctionCall::DeclarationType *>::iterator i = candidates.begin(); i != candidates.end(); ++i) {
        (*i)->acceptVisitor(*this);
    }

    // Now ensuring correct declaration.
    Function *decl = _getMethodDeclaration<FunctionCall>(o, _sem);
    messageAssert(decl != nullptr, "Cannot resolve function call", o, _sem);
    messageAssert(
        decl->parameters.size() >= o->parameterAssigns.size(), "Unexpected formal parameters number less than actuals",
        decl, _sem);

    hif::manipulation::sortParameters(
        o->parameterAssigns, decl->parameters, true, hif::manipulation::SortMissingKind::NOTHING, _sem);
    ret = o;
    return ret;
}

Value *PostParsingVisitor_step1::_fixFunctionCall2TypeRef(
    FunctionCall *o,
    TypeReference *typeref,
    TypeReference::DeclarationType *declTr)
{
    Value *ret = nullptr;

    // ok, it was a typeref.
    // Now checks if was a typedef o a typeTP.
    if (dynamic_cast<TypeTP *>(declTr) != nullptr) {
        // nothing to do?
        messageError("Unhandeled case", declTr, _sem);
    }

    messageAssert(dynamic_cast<TypeDef *>(declTr) != nullptr, "Unexpected declaration", declTr, _sem);

    TypeDef *td        = static_cast<TypeDef *>(declTr);
    Value *first       = o->parameterAssigns.front()->getValue();
    Identifier *valRef = dynamic_cast<Identifier *>(first);
    Slice *slice       = dynamic_cast<Slice *>(first);
    if (valRef != nullptr) {
        DataDeclaration *valDecl = hif::semantics::getDeclaration(valRef, _sem);

        Range *range_o = hif::typeGetSpan(valDecl->getType(), _sem);
        typeref->ranges.push_back(hif::copy(range_o));

        Value *second = o->parameterAssigns.back()->getValue();
        Cast *co      = new Cast();
        co->setValue(hif::copy(second));
        co->setType(typeref);
        typeref->setSourceFileName(o->getSourceFileName());
        typeref->setSourceLineNumber(o->getSourceLineNumber());
        co->setSourceFileName(o->getSourceFileName());
        co->setSourceLineNumber(o->getSourceLineNumber());
        o->replace(co);
        _factory.codeInfo(co, o->getCodeInfo());
        ret = co;
    } else if (slice != nullptr) {
        Range *range_o = slice->getSpan();
        typeref->ranges.push_back(hif::copy(range_o));

        Value *second = o->parameterAssigns.back()->getValue();
        Cast *co      = new Cast();
        co->setValue(hif::copy(second));
        co->setType(typeref);
        typeref->setSourceFileName(o->getSourceFileName());
        typeref->setSourceLineNumber(o->getSourceLineNumber());
        co->setSourceFileName(o->getSourceFileName());
        co->setSourceLineNumber(o->getSourceLineNumber());
        o->replace(co);
        _factory.codeInfo(co, o->getCodeInfo());
        ret = co;
    } else {
        messageAssert(o->parameterAssigns.size() == 1u, "Unexpected size inside typeref", o, _sem);

        Type *t = hif::semantics::getSemanticType(first, _sem);
        if (t == nullptr) {
            messageError("Unable to calculate type of typeref value", first, _sem);
        }

        Range *range_o = hif::typeGetSpan(t, _sem);
        typeref->ranges.push_back(hif::copy(range_o));

        Value *second = o->parameterAssigns.back()->getValue();
        Cast *co      = new Cast();
        co->setValue(hif::copy(second));
        co->setType(typeref);
        typeref->setSourceFileName(o->getSourceFileName());
        typeref->setSourceLineNumber(o->getSourceLineNumber());
        co->setSourceFileName(o->getSourceFileName());
        co->setSourceLineNumber(o->getSourceLineNumber());
        o->replace(co);
        _factory.codeInfo(co, o->getCodeInfo());
        ret = co;
    }

    _fixTemplateTypereferences(typeref, td);
    delete o;

    return ret;
}

Value *PostParsingVisitor_step1::_fixFunctionCall2MemberOrSlice(
    FunctionCall *o,
    Value *currentMember,
    Declaration * /*memDecl*/)
{
    for (BList<ParameterAssign>::iterator i = o->parameterAssigns.begin(); i != o->parameterAssigns.end(); ++i) {
        ParameterAssign *pa = *i;
        Value *val          = pa->setValue(nullptr);
        if (dynamic_cast<Range *>(val) != nullptr) {
            Range *r = static_cast<Range *>(val);
            Slice *s = new Slice();
            s->setSpan(r);
            s->setPrefix(currentMember);
            currentMember = s;
            continue;
        } else if (dynamic_cast<Identifier *>(val) != nullptr) {
            Identifier *id = static_cast<Identifier *>(val);
            // can be an identifier referred to a typedef
            TypeReference tr;
            tr.setName(id->getName());

            hif::semantics::DeclarationOptions dopt;
            dopt.location   = o;
            TypeDef *trDecl = dynamic_cast<TypeDef *>(hif::semantics::getDeclaration(&tr, _sem, dopt));

            if (trDecl != nullptr) {
                // It is a slice
                TypeDef *inst = dynamic_cast<TypeDef *>(hif::manipulation::instantiate(&tr, _sem));
                messageAssert(inst != nullptr, "Instantiate failed.", &tr, _sem);
                Range *r = nullptr;
                if (dynamic_cast<Int *>(inst->getType()) != nullptr) {
                    r = hif::copy(inst->getRange());
                } else {
                    r = hif::copy(hif::typeGetSpan(inst->getType(), _sem));
                }
                Slice *s = new Slice();
                s->setSpan(r);
                s->setPrefix(currentMember);
                currentMember = s;
                continue;
            }
        }

        Member *m = new Member();
        m->setIndex(val);
        m->setPrefix(currentMember);
        currentMember = m;
    }

    o->replace(currentMember);
    _factory.codeInfo(currentMember, o->getCodeInfo());
    delete o;

    return currentMember;
}

} // namespace

void performStep1Refinements(hif::System *o, hif::semantics::ILanguageSemantics *sem)
{
    hif::application_utils::initializeLogHeader("VHDL2HIF", "performStep1Refinements");

    PostParsingVisitor_step1 v(sem);
    o->acceptVisitor(v);

    v.fixCollectRanges();

    hif::application_utils::restoreLogHeader();
}

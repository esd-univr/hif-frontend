/// @file mark_ams_language.cpp
/// @brief
/// @copyright (c) 2024 Electronic Systems Design (ESD) Lab @ UniVR
/// This file is distributed under the BSD 2-Clause License.
/// See LICENSE.md for details.

#include "verilog2hif/mark_ams_language.hpp"

class AmsMarker : public hif::DeclarationVisitor
{
public:
    AmsMarker(const hif::DeclarationVisitorOptions &opts);
    virtual ~AmsMarker();

    virtual int visitFunctionCall(hif::FunctionCall &o);
    virtual int visitIdentifier(hif::Identifier &o);
    virtual int visitProcedureCall(hif::ProcedureCall &o);
    virtual int visitLibraryDef(hif::LibraryDef &o);
    virtual int visitStateTable(hif::StateTable &o);
    virtual int visitSystem(hif::System &o);
    virtual int visitTypeReference(hif::TypeReference &o);
    virtual int visitView(hif::View &o);
    virtual int visitViewReference(hif::ViewReference &o);

private:
    template <typename T> void _checkAmsSymbol(T *symbol);
    void _markScope(hif::Object *o);

    bool _isAms;

    AmsMarker(AmsMarker &);
    AmsMarker &operator=(const AmsMarker &);
};

AmsMarker::AmsMarker(const hif::DeclarationVisitorOptions &opts)
    : hif::DeclarationVisitor(opts)
    , _isAms(false)
{
    // ntd
}

AmsMarker::~AmsMarker()
{
    // ntd
}

int AmsMarker::visitFunctionCall(hif::FunctionCall &o)
{
    DeclarationVisitor::visitFunctionCall(o);
    _checkAmsSymbol(&o);
    return 0;
}

int AmsMarker::visitIdentifier(hif::Identifier &o)
{
    DeclarationVisitor::visitIdentifier(o);
    _checkAmsSymbol(&o);
    return 0;
}

int AmsMarker::visitProcedureCall(hif::ProcedureCall &o)
{
    DeclarationVisitor::visitProcedureCall(o);
    _checkAmsSymbol(&o);
    return 0;
}

int AmsMarker::visitLibraryDef(hif::LibraryDef &o)
{
    if (o.isStandard())
        return 0;
    return DeclarationVisitor::visitLibraryDef(o);
}

int AmsMarker::visitStateTable(hif::StateTable &o)
{
    DeclarationVisitor::visitStateTable(o);
    if (o.getFlavour() != hif::pf_analog)
        return 0;
    _markScope(&o);
    return 0;
}

int AmsMarker::visitSystem(hif::System &o)
{
    DeclarationVisitor::visitSystem(o);
    if (_isAms)
        o.setLanguageID(hif::ams);
    return 0;
}

int AmsMarker::visitTypeReference(hif::TypeReference &o)
{
    DeclarationVisitor::visitTypeReference(o);
    _checkAmsSymbol(&o);
    return 0;
}

int AmsMarker::visitView(hif::View &o)
{
    if (o.isStandard())
        return 0;
    return DeclarationVisitor::visitView(o);
}

int AmsMarker::visitViewReference(hif::ViewReference &o)
{
    DeclarationVisitor::visitViewReference(o);
    _checkAmsSymbol(&o);
    return 0;
}

template <typename T> void AmsMarker::_checkAmsSymbol(T *symbol)
{
    typename T::DeclarationType *decl = hif::semantics::getDeclaration(symbol, _opt.sem);
    messageAssert(decl != nullptr, "Declaration not found", symbol, _opt.sem);
    if (hif::objectGetLanguage(decl) != hif::ams)
        return;
    _markScope(symbol);
}

void AmsMarker::_markScope(hif::Object *o)
{
    hif::View *v = hif::getNearestParent<hif::View>(o);
    if (v != nullptr)
        v->setLanguageID(hif::ams);
    hif::LibraryDef *ld = hif::getNearestParent<hif::LibraryDef>(o);
    if (ld != nullptr)
        ld->setLanguageID(hif::ams);
    //    hif::System * sys = hif::getNearestParent<hif::System>(o);
    //    if (sys != nullptr) sys->setLanguageID(hif::ams);

    _isAms = true;
}

void markAmsLanguage(hif::Object *o, hif::semantics::ILanguageSemantics *sem)
{
    hif::DeclarationVisitorOptions opt;
    opt.sem                             = sem;
    opt.visitDeclarationsOnce           = true;
    opt.visitSymbolsOnce                = true;
    opt.visitReferencesAfterDeclaration = false;
    AmsMarker vis(opt);
    o->acceptVisitor(vis);
}

// slang-ast-type.cpp
#include "slang-ast-builder.h"
#include <assert.h>
#include <typeinfo>

#include "slang-syntax.h"

#include "slang-ast-generated-macro.h"

namespace Slang {

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Type !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

Type* Type::createCanonicalType()
{
    SLANG_AST_NODE_VIRTUAL_CALL(Type, createCanonicalType, ())
}

bool Type::equals(Type* type)
{
    return getCanonicalType()->equalsImpl(type->getCanonicalType());
}

bool Type::equalsImpl(Type* type)
{
    SLANG_AST_NODE_VIRTUAL_CALL(Type, equalsImpl, (type))
}

bool Type::_equalsImplOverride(Type* type)
{
    SLANG_UNUSED(type)
    SLANG_UNEXPECTED("Type::_equalsImplOverride not overridden");
    //return false;
}

Type* Type::_createCanonicalTypeOverride()
{
    SLANG_UNEXPECTED("Type::_createCanonicalTypeOverride not overridden");
    //return Type*();
}

bool Type::_equalsValOverride(Val* val)
{
    if (auto type = dynamicCast<Type>(val))
        return const_cast<Type*>(this)->equals(type);
    return false;
}

Val* Type::_substituteImplOverride(ASTBuilder* astBuilder, SubstitutionSet subst, int* ioDiff)
{
    int diff = 0;
    auto canSubst = getCanonicalType()->substituteImpl(astBuilder, subst, &diff);

    // If nothing changed, then don't drop any sugar that is applied
    if (!diff)
        return this;

    // If the canonical type changed, then we return a canonical type,
    // rather than try to re-construct any amount of sugar
    (*ioDiff)++;
    return canSubst;
}

Type* Type::getCanonicalType()
{
    Type* et = const_cast<Type*>(this);
    if (!et->canonicalType)
    {
        // TODO(tfoley): worry about thread safety here?
        auto canType = et->createCanonicalType();
        et->canonicalType = canType;

        SLANG_ASSERT(et->canonicalType);
    }
    return et->canonicalType;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! OverloadGroupType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String OverloadGroupType::_toStringOverride()
{
    return "overload group";
}

bool OverloadGroupType::_equalsImplOverride(Type * /*type*/)
{
    return false;
}

Type* OverloadGroupType::_createCanonicalTypeOverride()
{
    return this;
}

HashCode OverloadGroupType::_getHashCodeOverride()
{
    return (HashCode)(size_t(this));
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! InitializerListType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String InitializerListType::_toStringOverride()
{
    return "initializer list";
}

bool InitializerListType::_equalsImplOverride(Type * /*type*/)
{
    return false;
}

Type* InitializerListType::_createCanonicalTypeOverride()
{
    return this;
}

HashCode InitializerListType::_getHashCodeOverride()
{
    return (HashCode)(size_t(this));
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ErrorType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String ErrorType::_toStringOverride()
{
    return "error";
}

bool ErrorType::_equalsImplOverride(Type* type)
{
    if (auto errorType = as<ErrorType>(type))
        return true;
    return false;
}

Type* ErrorType::_createCanonicalTypeOverride()
{
    return this;
}

Val* ErrorType::_substituteImplOverride(ASTBuilder* /* astBuilder */, SubstitutionSet /*subst*/, int* /*ioDiff*/)
{
    return this;
}

HashCode ErrorType::_getHashCodeOverride()
{
    return HashCode(size_t(this));
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! DeclRefType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String DeclRefType::_toStringOverride()
{
    return declRef.toString();
}

HashCode DeclRefType::_getHashCodeOverride()
{
    return (declRef.getHashCode() * 16777619) ^ (HashCode)(typeid(this).hash_code());
}

bool DeclRefType::_equalsImplOverride(Type * type)
{
    if (auto declRefType = as<DeclRefType>(type))
    {
        return declRef.equals(declRefType->declRef);
    }
    return false;
}

Type* DeclRefType::_createCanonicalTypeOverride()
{
    // A declaration reference is already canonical
    return this;
}

Val* DeclRefType::_substituteImplOverride(ASTBuilder* astBuilder, SubstitutionSet subst, int* ioDiff)
{
    if (!subst) return this;

    // the case we especially care about is when this type references a declaration
    // of a generic parameter, since that is what we might be substituting...
    if (auto genericTypeParamDecl = as<GenericTypeParamDecl>(declRef.getDecl()))
    {
        // search for a substitution that might apply to us
        for (auto s = subst.substitutions; s; s = s->outer)
        {
            auto genericSubst = as<GenericSubstitution>(s);
            if (!genericSubst)
                continue;

            // the generic decl associated with the substitution list must be
            // the generic decl that declared this parameter
            auto genericDecl = genericSubst->genericDecl;
            if (genericDecl != genericTypeParamDecl->parentDecl)
                continue;

            int index = 0;
            for (auto m : genericDecl->members)
            {
                if (m == genericTypeParamDecl)
                {
                    // We've found it, so return the corresponding specialization argument
                    (*ioDiff)++;
                    return genericSubst->args[index];
                }
                else if (auto typeParam = as<GenericTypeParamDecl>(m))
                {
                    index++;
                }
                else if (auto valParam = as<GenericValueParamDecl>(m))
                {
                    index++;
                }
                else
                {
                }
            }
        }
    }
    else if (auto globalGenParam = as<GlobalGenericParamDecl>(declRef.getDecl()))
    {
        // search for a substitution that might apply to us
        for (auto s = subst.substitutions; s; s = s->outer)
        {
            auto genericSubst = as<GlobalGenericParamSubstitution>(s);
            if (!genericSubst)
                continue;

            if (genericSubst->paramDecl == globalGenParam)
            {
                (*ioDiff)++;
                return genericSubst->actualType;
            }
        }
    }
    int diff = 0;
    DeclRef<Decl> substDeclRef = declRef.substituteImpl(astBuilder, subst, &diff);

    if (!diff)
        return this;

    // Make sure to record the difference!
    *ioDiff += diff;

    // If this type is a reference to an associated type declaration,
    // and the substitutions provide a "this type" substitution for
    // the outer interface, then try to replace the type with the
    // actual value of the associated type for the given implementation.
    //
    if (auto substAssocTypeDecl = as<AssocTypeDecl>(substDeclRef.decl))
    {
        for (auto s = substDeclRef.substitutions.substitutions; s; s = s->outer)
        {
            auto thisSubst = as<ThisTypeSubstitution>(s);
            if (!thisSubst)
                continue;

            if (auto interfaceDecl = as<InterfaceDecl>(substAssocTypeDecl->parentDecl))
            {
                if (thisSubst->interfaceDecl == interfaceDecl)
                {
                    // We need to look up the declaration that satisfies
                    // the requirement named by the associated type.
                    Decl* requirementKey = substAssocTypeDecl;
                    RequirementWitness requirementWitness = tryLookUpRequirementWitness(astBuilder, thisSubst->witness, requirementKey);
                    switch (requirementWitness.getFlavor())
                    {
                        default:
                            // No usable value was found, so there is nothing we can do.
                            break;

                        case RequirementWitness::Flavor::val:
                        {
                            auto satisfyingVal = requirementWitness.getVal();
                            return satisfyingVal;
                        }
                        break;
                    }
                }
            }
        }
    }

    // Re-construct the type in case we are using a specialized sub-class
    return DeclRefType::create(astBuilder, substDeclRef);
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ArithmeticExpressionType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!


BasicExpressionType* ArithmeticExpressionType::getScalarType()
{
    SLANG_AST_NODE_VIRTUAL_CALL(ArithmeticExpressionType, getScalarType, ())
}

BasicExpressionType* ArithmeticExpressionType::_getScalarTypeOverride()
{
    SLANG_UNEXPECTED("ArithmeticExpressionType::_getScalarTypeOverride not overridden");
    //return nullptr;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! BasicExpressionType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

bool BasicExpressionType::_equalsImplOverride(Type * type)
{
    auto basicType = as<BasicExpressionType>(type);
    return basicType && basicType->baseType == this->baseType;
}

Type* BasicExpressionType::_createCanonicalTypeOverride()
{
    // A basic type is already canonical, in our setup
    return this;
}

BasicExpressionType* BasicExpressionType::_getScalarTypeOverride()
{
    return this;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! VectorExpressionType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String VectorExpressionType::_toStringOverride()
{
    StringBuilder sb;
    sb << "vector<" << elementType->toString() << "," << elementCount->toString() << ">";
    return sb.ProduceString();
}

BasicExpressionType* VectorExpressionType::_getScalarTypeOverride()
{
    return as<BasicExpressionType>(elementType);
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! MatrixExpressionType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String MatrixExpressionType::_toStringOverride()
{
    StringBuilder sb;
    sb << "matrix<" << getElementType()->toString() << "," << getRowCount()->toString() << "," << getColumnCount()->toString() << ">";
    return sb.ProduceString();
}

BasicExpressionType* MatrixExpressionType::_getScalarTypeOverride()
{
    return as<BasicExpressionType>(getElementType());
}

Type* MatrixExpressionType::getElementType()
{
    return as<Type>(findInnerMostGenericSubstitution(declRef.substitutions)->args[0]);
}

IntVal* MatrixExpressionType::getRowCount()
{
    return as<IntVal>(findInnerMostGenericSubstitution(declRef.substitutions)->args[1]);
}

IntVal* MatrixExpressionType::getColumnCount()
{
    return as<IntVal>(findInnerMostGenericSubstitution(declRef.substitutions)->args[2]);
}

Type* MatrixExpressionType::getRowType()
{
    if (!rowType)
    {
        rowType = m_astBuilder->getVectorType(getElementType(), getColumnCount());
    }
    return rowType;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ArrayExpressionType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

bool ArrayExpressionType::_equalsImplOverride(Type* type)
{
    auto arrType = as<ArrayExpressionType>(type);
    if (!arrType)
        return false;
    return (areValsEqual(arrayLength, arrType->arrayLength) && baseType->equals(arrType->baseType));
}

Val* ArrayExpressionType::_substituteImplOverride(ASTBuilder* astBuilder, SubstitutionSet subst, int* ioDiff)
{
    int diff = 0;
    auto elementType = as<Type>(baseType->substituteImpl(astBuilder, subst, &diff));
    auto arrlen = as<IntVal>(arrayLength->substituteImpl(astBuilder, subst, &diff));
    SLANG_ASSERT(arrlen);
    if (diff)
    {
        *ioDiff = 1;
        auto rsType = getArrayType(
            astBuilder,
            elementType,
            arrlen);
        return rsType;
    }
    return this;
}

Type* ArrayExpressionType::_createCanonicalTypeOverride()
{
    auto canonicalElementType = baseType->getCanonicalType();
    auto canonicalArrayType = getASTBuilder()->getArrayType(
        canonicalElementType,
        arrayLength);
    return canonicalArrayType;
}

HashCode ArrayExpressionType::_getHashCodeOverride()
{
    if (arrayLength)
        return (baseType->getHashCode() * 16777619) ^ arrayLength->getHashCode();
    else
        return baseType->getHashCode();
}

Slang::String ArrayExpressionType::_toStringOverride()
{
    if (arrayLength)
        return baseType->toString() + "[" + arrayLength->toString() + "]";
    else
        return baseType->toString() + "[]";
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! TypeType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String TypeType::_toStringOverride()
{
    StringBuilder sb;
    sb << "typeof(" << type->toString() << ")";
    return sb.ProduceString();
}

bool TypeType::_equalsImplOverride(Type * t)
{
    if (auto typeType = as<TypeType>(t))
    {
        return t->equals(typeType->type);
    }
    return false;
}

Type* TypeType::_createCanonicalTypeOverride()
{
    return getASTBuilder()->getTypeType(type->getCanonicalType());
}

HashCode TypeType::_getHashCodeOverride()
{
    SLANG_UNEXPECTED("TypeType::_getHashCodeOverride should be unreachable");
    //return HashCode(0);
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! GenericDeclRefType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String GenericDeclRefType::_toStringOverride()
{
    // TODO: what is appropriate here?
    return "<DeclRef<GenericDecl>>";
}

bool GenericDeclRefType::_equalsImplOverride(Type * type)
{
    if (auto genericDeclRefType = as<GenericDeclRefType>(type))
    {
        return declRef.equals(genericDeclRefType->declRef);
    }
    return false;
}

HashCode GenericDeclRefType::_getHashCodeOverride()
{
    return declRef.getHashCode();
}

Type* GenericDeclRefType::_createCanonicalTypeOverride()
{
    return this;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! NamespaceType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String NamespaceType::_toStringOverride()
{
    String result;
    result.append("namespace ");
    result.append(declRef.toString());
    return result;
}

bool NamespaceType::_equalsImplOverride(Type * type)
{
    if (auto namespaceType = as<NamespaceType>(type))
    {
        return declRef.equals(namespaceType->declRef);
    }
    return false;
}

HashCode NamespaceType::_getHashCodeOverride()
{
    return declRef.getHashCode();
}

Type* NamespaceType::_createCanonicalTypeOverride()
{
    return this;
}


// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! PtrTypeBase !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

Type* PtrTypeBase::getValueType()
{
    return as<Type>(findInnerMostGenericSubstitution(declRef.substitutions)->args[0]);
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! NamedExpressionType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String NamedExpressionType::_toStringOverride()
{
    return getText(declRef.getName());
}

bool NamedExpressionType::_equalsImplOverride(Type * /*type*/)
{
    SLANG_UNEXPECTED("NamedExpressionType::_equalsImplOverride should be unreachable");
    //return false;
}

Type* NamedExpressionType::_createCanonicalTypeOverride()
{
    if (!innerType)
        innerType = getType(m_astBuilder, declRef);
    return innerType->getCanonicalType();
}

HashCode NamedExpressionType::_getHashCodeOverride()
{
    // Type equality is based on comparing canonical types,
    // so the hash code for a type needs to come from the
    // canonical version of the type. This really means
    // that `Type::getHashCode()` should dispatch out to
    // something like `Type::getHashCodeImpl()` on the
    // canonical version of a type, but it is less invasive
    // for now (and hopefully equivalent) to just have any
    // named types automaticlaly route hash-code requests
    // to their canonical type.
    return getCanonicalType()->getHashCode();
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! FuncType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String FuncType::_toStringOverride()
{
    StringBuilder sb;
    sb << "(";
    UInt paramCount = getParamCount();
    for (UInt pp = 0; pp < paramCount; ++pp)
    {
        if (pp != 0) sb << ", ";
        sb << getParamType(pp)->toString();
    }
    sb << ") -> ";
    sb << getResultType()->toString();
    return sb.ProduceString();
}

bool FuncType::_equalsImplOverride(Type * type)
{
    if (auto funcType = as<FuncType>(type))
    {
        auto paramCount = getParamCount();
        auto otherParamCount = funcType->getParamCount();
        if (paramCount != otherParamCount)
            return false;

        for (UInt pp = 0; pp < paramCount; ++pp)
        {
            auto paramType = getParamType(pp);
            auto otherParamType = funcType->getParamType(pp);
            if (!paramType->equals(otherParamType))
                return false;
        }

        if (!resultType->equals(funcType->resultType))
            return false;

        // TODO: if we ever introduce other kinds
        // of qualification on function types, we'd
        // want to consider it here.
        return true;
    }
    return false;
}

Val* FuncType::_substituteImplOverride(ASTBuilder* astBuilder, SubstitutionSet subst, int* ioDiff)
{
    int diff = 0;

    // result type
    Type* substResultType = as<Type>(resultType->substituteImpl(astBuilder, subst, &diff));

    // parameter types
    List<Type*> substParamTypes;
    for (auto pp : paramTypes)
    {
        substParamTypes.add(as<Type>(pp->substituteImpl(astBuilder, subst, &diff)));
    }

    // early exit for no change...
    if (!diff)
        return this;

    (*ioDiff)++;
    FuncType* substType = astBuilder->create<FuncType>();
    substType->resultType = substResultType;
    substType->paramTypes = substParamTypes;
    return substType;
}

Type* FuncType::_createCanonicalTypeOverride()
{
    // result type
    Type* canResultType = resultType->getCanonicalType();

    // parameter types
    List<Type*> canParamTypes;
    for (auto pp : paramTypes)
    {
        canParamTypes.add(pp->getCanonicalType());
    }

    FuncType* canType = getASTBuilder()->create<FuncType>();
    canType->resultType = canResultType;
    canType->paramTypes = canParamTypes;

    return canType;
}

HashCode FuncType::_getHashCodeOverride()
{
    HashCode hashCode = getResultType()->getHashCode();
    UInt paramCount = getParamCount();
    hashCode = combineHash(hashCode, Slang::getHashCode(paramCount));
    for (UInt pp = 0; pp < paramCount; ++pp)
    {
        hashCode = combineHash(
            hashCode,
            getParamType(pp)->getHashCode());
    }
    return hashCode;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ExtractExistentialType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String ExtractExistentialType::_toStringOverride()
{
    String result;
    result.append(declRef.toString());
    result.append(".This");
    return result;
}

bool ExtractExistentialType::_equalsImplOverride(Type* type)
{
    if (auto extractExistential = as<ExtractExistentialType>(type))
    {
        return declRef.equals(extractExistential->declRef);
    }
    return false;
}

HashCode ExtractExistentialType::_getHashCodeOverride()
{
    return declRef.getHashCode();
}

Type* ExtractExistentialType::_createCanonicalTypeOverride()
{
    return this;
}

Val* ExtractExistentialType::_substituteImplOverride(ASTBuilder* astBuilder, SubstitutionSet subst, int* ioDiff)
{
    int diff = 0;
    auto substDeclRef = declRef.substituteImpl(astBuilder, subst, &diff);
    if (!diff)
        return this;

    (*ioDiff)++;

    ExtractExistentialType* substValue = astBuilder->create<ExtractExistentialType>();
    substValue->declRef = declRef;
    return substValue;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! TaggedUnionType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String TaggedUnionType::_toStringOverride()
{
    String result;
    result.append("__TaggedUnion(");
    bool first = true;
    for (auto caseType : caseTypes)
    {
        if (!first) result.append(", ");
        first = false;

        result.append(caseType->toString());
    }
    result.append(")");
    return result;
}

bool TaggedUnionType::_equalsImplOverride(Type* type)
{
    auto taggedUnion = as<TaggedUnionType>(type);
    if (!taggedUnion)
        return false;

    auto caseCount = caseTypes.getCount();
    if (caseCount != taggedUnion->caseTypes.getCount())
        return false;

    for (Index ii = 0; ii < caseCount; ++ii)
    {
        if (!caseTypes[ii]->equals(taggedUnion->caseTypes[ii]))
            return false;
    }
    return true;
}

HashCode TaggedUnionType::_getHashCodeOverride()
{
    HashCode hashCode = 0;
    for (auto caseType : caseTypes)
    {
        hashCode = combineHash(hashCode, caseType->getHashCode());
    }
    return hashCode;
}

Type* TaggedUnionType::_createCanonicalTypeOverride()
{
    TaggedUnionType* canType = m_astBuilder->create<TaggedUnionType>();

    for (auto caseType : caseTypes)
    {
        auto canCaseType = caseType->getCanonicalType();
        canType->caseTypes.add(canCaseType);
    }

    return canType;
}

Val* TaggedUnionType::_substituteImplOverride(ASTBuilder* astBuilder, SubstitutionSet subst, int* ioDiff)
{
    int diff = 0;

    List<Type*> substCaseTypes;
    for (auto caseType : caseTypes)
    {
        substCaseTypes.add(as<Type>(caseType->substituteImpl(astBuilder, subst, &diff)));
    }
    if (!diff)
        return this;

    (*ioDiff)++;

    TaggedUnionType* substType = astBuilder->create<TaggedUnionType>();
    substType->caseTypes.swapWith(substCaseTypes);
    return substType;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ExistentialSpecializedType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String ExistentialSpecializedType::_toStringOverride()
{
    String result;
    result.append("__ExistentialSpecializedType(");
    result.append(baseType->toString());
    for (auto arg : args)
    {
        result.append(", ");
        result.append(arg.val->toString());
    }
    result.append(")");
    return result;
}

bool ExistentialSpecializedType::_equalsImplOverride(Type * type)
{
    auto other = as<ExistentialSpecializedType>(type);
    if (!other)
        return false;

    if (!baseType->equals(other->baseType))
        return false;

    auto argCount = args.getCount();
    if (argCount != other->args.getCount())
        return false;

    for (Index ii = 0; ii < argCount; ++ii)
    {
        auto arg = args[ii];
        auto otherArg = other->args[ii];

        if (!arg.val->equalsVal(otherArg.val))
            return false;

        if (!areValsEqual(arg.witness, otherArg.witness))
            return false;
    }
    return true;
}

HashCode ExistentialSpecializedType::_getHashCodeOverride()
{
    Hasher hasher;
    hasher.hashObject(baseType);
    for (auto arg : args)
    {
        hasher.hashObject(arg.val);
        if (auto witness = arg.witness)
            hasher.hashObject(witness);
    }
    return hasher.getResult();
}

static Val* _getCanonicalValue(Val* val)
{
    if (!val)
        return nullptr;
    if (auto type = as<Type>(val))
    {
        return type->getCanonicalType();
    }
    // TODO: We may eventually need/want some sort of canonicalization
    // for non-type values, but for now there is nothing to do.
    return val;
}

Type* ExistentialSpecializedType::_createCanonicalTypeOverride()
{
    ExistentialSpecializedType* canType = m_astBuilder->create<ExistentialSpecializedType>();

    canType->baseType = baseType->getCanonicalType();
    for (auto arg : args)
    {
        ExpandedSpecializationArg canArg;
        canArg.val = _getCanonicalValue(arg.val);
        canArg.witness = _getCanonicalValue(arg.witness);
        canType->args.add(canArg);
    }
    return canType;
}

static Val* _substituteImpl(ASTBuilder* astBuilder, Val* val, SubstitutionSet subst, int* ioDiff)
{
    if (!val) return nullptr;
    return val->substituteImpl(astBuilder, subst, ioDiff);
}

Val* ExistentialSpecializedType::_substituteImplOverride(ASTBuilder* astBuilder, SubstitutionSet subst, int* ioDiff)
{
    int diff = 0;

    auto substBaseType = as<Type>(baseType->substituteImpl(astBuilder, subst, &diff));

    ExpandedSpecializationArgs substArgs;
    for (auto arg : args)
    {
        ExpandedSpecializationArg substArg;
        substArg.val = _substituteImpl(astBuilder, arg.val, subst, &diff);
        substArg.witness = _substituteImpl(astBuilder, arg.witness, subst, &diff);
        substArgs.add(substArg);
    }

    if (!diff)
        return this;

    (*ioDiff)++;

    ExistentialSpecializedType* substType = astBuilder->create<ExistentialSpecializedType>();
    substType->baseType = substBaseType;
    substType->args = substArgs;
    return substType;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ThisType !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

String ThisType::_toStringOverride()
{
    String result;
    result.append(interfaceDeclRef.toString());
    result.append(".This");
    return result;
}

bool ThisType::_equalsImplOverride(Type * type)
{
    auto other = as<ThisType>(type);
    if (!other)
        return false;

    if (!interfaceDeclRef.equals(other->interfaceDeclRef))
        return false;

    return true;
}

HashCode ThisType::_getHashCodeOverride()
{
    return combineHash(
        HashCode(typeid(*this).hash_code()),
        interfaceDeclRef.getHashCode());
}

Type* ThisType::_createCanonicalTypeOverride()
{
    ThisType* canType = m_astBuilder->create<ThisType>();

    // TODO: need to canonicalize the decl-ref
    canType->interfaceDeclRef = interfaceDeclRef;
    return canType;
}

Val* ThisType::_substituteImplOverride(ASTBuilder* astBuilder, SubstitutionSet subst, int* ioDiff)
{
    int diff = 0;

    auto substInterfaceDeclRef = interfaceDeclRef.substituteImpl(astBuilder, subst, &diff);

    auto thisTypeSubst = findThisTypeSubstitution(subst.substitutions, substInterfaceDeclRef.getDecl());
    if (thisTypeSubst)
    {
        return thisTypeSubst->witness->sub;
    }

    if (!diff)
        return this;

    (*ioDiff)++;

    ThisType* substType = m_astBuilder->create<ThisType>();
    substType->interfaceDeclRef = substInterfaceDeclRef;
    return substType;
}


} // namespace Slang

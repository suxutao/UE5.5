// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMEmergentType.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/VVMAtomics.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMShape.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VEmergentType);

template <typename TVisitor>
void VEmergentType::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Shape, TEXT("Shape"));
	Visitor.Visit(Type, TEXT("Type"));
	Visitor.Visit(MeltTransition, TEXT("MeltTransition"));
}

VEmergentType& VEmergentType::GetOrCreateMeltTransitionSlow(FAllocationContext Context)
{
	if (Shape->NumIndexedFields == Shape->GetNumFields())
	{
		// We're good to go. No need to make anything.
		MeltTransition.Set(Context, this);
		return *this;
	}

	VShape& NewShape = Shape->CopyToMeltedShape(Context);
	V_DIE_UNLESS(NewShape.NumIndexedFields == NewShape.GetNumFields());

	VEmergentType* Transition = VEmergentType::New(Context, &NewShape, Type.Get(), CppClassInfo);
	Transition->MeltTransition.Set(Context, Transition); // The MeltTransition of a MeltTransition is itself.

	// The object should be done constructing before we expose it to the concurrent GC.
	StoreStoreFence();
	MeltTransition.Set(Context, Transition);
	return *Transition;
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)

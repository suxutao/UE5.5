// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaskProfile/MaskProfileTypeHandler.h"
#include "MaskProfile/HierarchyTableTypeMask.h"
#include "MaskProfile/MaskProfileProxyColumn.h"

FInstancedStruct UHierarchyTableTypeHandler_Mask::GetDefaultEntry() const
{
	FInstancedStruct NewEntry;
	NewEntry.InitializeAs(FHierarchyTableType_Mask::StaticStruct());
	FHierarchyTableType_Mask& NewEntryRef = NewEntry.GetMutable<FHierarchyTableType_Mask>();
	NewEntryRef.Value = 1.0f;

	return NewEntry;
}

TArray<UScriptStruct*> UHierarchyTableTypeHandler_Mask::GetColumns() const
{
	return { FHierarchyTableMaskColumn_Value::StaticStruct() };
}
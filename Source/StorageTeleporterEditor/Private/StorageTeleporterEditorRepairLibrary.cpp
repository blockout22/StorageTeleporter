#include "StorageTeleporterEditorRepairLibrary.h"

#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

bool UStorageTeleporterEditorRepairLibrary::SetBlueprintVariableReplicated(
	UBlueprint* Blueprint,
	FName VariableName,
	FString& OutError)
{
	OutError.Reset();

	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null.");
		return false;
	}

	uint64* PropertyFlags = FBlueprintEditorUtils::GetBlueprintVariablePropertyFlags(Blueprint, VariableName);
	if (!PropertyFlags)
	{
		OutError = FString::Printf(TEXT("Variable '%s' was not found in '%s'."), *VariableName.ToString(), *Blueprint->GetPathName());
		return false;
	}

	*PropertyFlags |= CPF_Net;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	if (Blueprint->Status == BS_Error)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' failed to compile."), *Blueprint->GetPathName());
		return false;
	}

	return true;
}

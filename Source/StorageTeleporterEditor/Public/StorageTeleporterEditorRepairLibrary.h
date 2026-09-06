#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StorageTeleporterEditorRepairLibrary.generated.h"

class UBlueprint;

UCLASS()
class STORAGETELEPORTEREDITOR_API UStorageTeleporterEditorRepairLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Storage Teleporter|Editor Repair")
	static bool SetBlueprintVariableReplicated(UBlueprint* Blueprint, FName VariableName, FString& OutError);
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

class FStorageTeleporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	bool RepairRegistries(float DeltaTime);
	void RepairWorld(class UWorld* World);

	FTSTicker::FDelegateHandle RegistryRepairTickerHandle;
};

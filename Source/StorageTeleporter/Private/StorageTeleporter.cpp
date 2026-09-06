#include "StorageTeleporter.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FGRemoteCallObject.h"
#include "Buildables/FGBuildableFactory.h"
#include "FGFactoryConnectionComponent.h"
#include "FGInventoryComponent.h"
#include "GameFramework/Actor.h"
#include "Registry/RemoteCallObjectRegistry.h"
#include "Subsystem/ModSubsystem.h"
#include "Subsystem/SubsystemActorManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogStorageTeleporterRepair, Log, All);

namespace
{
	constexpr float RegistryRepairIntervalSeconds = 2.0f;

	bool IsClassNamed(const UObject* Object, const TCHAR* ClassName)
	{
		return IsValid(Object) && Object->GetClass()->GetName().Equals(ClassName);
	}

	UObject* ReadTeleporterInfo(AActor* Teleporter)
	{
		if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Teleporter->GetClass(), TEXT("TeleporterInfo")))
		{
			if (UObject* Value = Property->GetObjectPropertyValue_InContainer(Teleporter))
			{
				return Value;
			}
		}

		for (TFieldIterator<FObjectPropertyBase> It(Teleporter->GetClass()); It; ++It)
		{
			FObjectPropertyBase* Property = *It;
			if (Property->GetName().Equals(TEXT("TeleporterInfo"), ESearchCase::IgnoreCase) ||
				(Property->PropertyClass && Property->PropertyClass->GetName().Equals(TEXT("TeleporterInfo_C"))))
			{
				if (UObject* Value = Property->GetObjectPropertyValue_InContainer(Teleporter))
				{
					return Value;
				}
			}
		}
		return nullptr;
	}

	UObject* ReadObjectProperty(UObject* Owner, const TCHAR* PropertyName)
	{
		if (!Owner)
		{
			return nullptr;
		}
		if (FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), PropertyName))
		{
			return Property->GetObjectPropertyValue_InContainer(Owner);
		}
		return nullptr;
	}

	UObject* CreateTeleporterInfo(UObject* Subsystem, AActor* Teleporter)
	{
		UClass* InfoClass = LoadClass<UObject>(nullptr, TEXT("/StorageTeleporter/TeleporterInfo.TeleporterInfo_C"));
		if (!InfoClass || !Subsystem || !Teleporter)
		{
			return nullptr;
		}

		UObject* Info = NewObject<UObject>(Subsystem, InfoClass);
		UFunction* SetInfo = Info ? Info->FindFunction(TEXT("SetInfo")) : nullptr;
		if (!SetInfo)
		{
			return nullptr;
		}

		FStructOnScope Parameters(SetInfo);
		uint8* ParameterMemory = Parameters.GetStructMemory();
		for (TFieldIterator<FProperty> It(SetInfo); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
			{
				continue;
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == TBaseStructure<FVector>::Get())
				{
					*StructProperty->ContainerPtrToValuePtr<FVector>(ParameterMemory) = Teleporter->GetActorLocation();
				}
			}
			else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				UObject* Value = nullptr;
				if (Property->GetName().Contains(TEXT("Teleporter"), ESearchCase::IgnoreCase))
				{
					Value = Teleporter;
				}
				else if (Property->GetName().Contains(TEXT("Input"), ESearchCase::IgnoreCase))
				{
					Value = ReadObjectProperty(Teleporter, TEXT("InputConnection"));
				}
				else if (Property->GetName().Contains(TEXT("Output"), ESearchCase::IgnoreCase))
				{
					Value = ReadObjectProperty(Teleporter, TEXT("OutputConnection"));
				}
				ObjectProperty->SetObjectPropertyValue_InContainer(ParameterMemory, Value);
			}
		}

		Info->ProcessEvent(SetInfo, ParameterMemory);
		if (FObjectPropertyBase* InfoProperty = FindFProperty<FObjectPropertyBase>(Teleporter->GetClass(), TEXT("TeleporterInfo")))
		{
			InfoProperty->SetObjectPropertyValue_InContainer(Teleporter, Info);
		}
		return Info;
	}

	bool ReadBoolProperty(UObject* Owner, const TCHAR* PropertyName, bool DefaultValue = false)
	{
		if (Owner)
		{
			if (FBoolProperty* Property = FindFProperty<FBoolProperty>(Owner->GetClass(), PropertyName))
			{
				return Property->GetPropertyValue_InContainer(Owner);
			}
		}
		return DefaultValue;
	}

	bool RegistryContains(FMapProperty* MapProperty, UObject* Subsystem, const FString& StorageId, UObject* TeleporterInfo)
	{
		if (!MapProperty || !Subsystem || !TeleporterInfo)
		{
			return false;
		}

		FStrProperty* KeyProperty = CastField<FStrProperty>(MapProperty->KeyProp);
		FStructProperty* ValueProperty = CastField<FStructProperty>(MapProperty->ValueProp);
		if (!KeyProperty || !ValueProperty)
		{
			return false;
		}

		FArrayProperty* InfoArrayProperty = nullptr;
		for (TFieldIterator<FArrayProperty> It(ValueProperty->Struct); It; ++It)
		{
			if (CastField<FObjectPropertyBase>((*It)->Inner))
			{
				InfoArrayProperty = *It;
				break;
			}
		}
		if (!InfoArrayProperty)
		{
			return false;
		}

		FScriptMapHelper MapHelper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(Subsystem));
		for (int32 Index = 0; Index < MapHelper.GetMaxIndex(); ++Index)
		{
			if (!MapHelper.IsValidIndex(Index) || KeyProperty->GetPropertyValue(MapHelper.GetKeyPtr(Index)) != StorageId)
			{
				continue;
			}

			void* ArrayAddress = InfoArrayProperty->ContainerPtrToValuePtr<void>(MapHelper.GetValuePtr(Index));
			FScriptArrayHelper ArrayHelper(InfoArrayProperty, ArrayAddress);
			FObjectPropertyBase* InnerProperty = CastFieldChecked<FObjectPropertyBase>(InfoArrayProperty->Inner);
			for (int32 ArrayIndex = 0; ArrayIndex < ArrayHelper.Num(); ++ArrayIndex)
			{
				if (InnerProperty->GetObjectPropertyValue(ArrayHelper.GetRawPtr(ArrayIndex)) == TeleporterInfo)
				{
					return true;
				}
			}
			return false;
		}
		return false;
	}

	bool InvokeAddTeleporterId(UObject* Subsystem, UObject* TeleporterInfo, const FString& StorageId)
	{
		UFunction* Function = Subsystem ? Subsystem->FindFunction(TEXT("AddTeleporterID")) : nullptr;
		if (!Function || !TeleporterInfo)
		{
			return false;
		}

		FStructOnScope Parameters(Function);
		uint8* ParameterMemory = Parameters.GetStructMemory();
		bool bSetInfo = false;
		bool bSetId = false;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
			{
				continue;
			}
			if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				ObjectProperty->SetObjectPropertyValue_InContainer(ParameterMemory, TeleporterInfo);
				bSetInfo = true;
			}
			else if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
			{
				StringProperty->SetPropertyValue_InContainer(ParameterMemory, StorageId);
				bSetId = true;
			}
		}
		if (!bSetInfo || !bSetId)
		{
			return false;
		}

		Subsystem->ProcessEvent(Function, ParameterMemory);
		return true;
	}
}

void FStorageTeleporterModule::StartupModule()
{
	UE_LOG(LogStorageTeleporterRepair, Warning, TEXT("Storage Teleporter registry repair module started."));
	RegistryRepairTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FStorageTeleporterModule::RepairRegistries),
		RegistryRepairIntervalSeconds);
}

void FStorageTeleporterModule::ShutdownModule()
{
	if (RegistryRepairTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RegistryRepairTickerHandle);
		RegistryRepairTickerHandle.Reset();
	}
}

bool FStorageTeleporterModule::RepairRegistries(float DeltaTime)
{
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE))
			{
				RepairWorld(World);
			}
		}
	}
	return true;
}

void FStorageTeleporterModule::RepairWorld(UWorld* World)
{
	static TSet<TWeakObjectPtr<UGameInstance>> RegisteredRcoGameInstances;
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		if (!RegisteredRcoGameInstances.Contains(GameInstance))
		{
			UClass* RemoteCallObjectClass = LoadClass<UFGRemoteCallObject>(
				nullptr, TEXT("/StorageTeleporter/RCO_StorageTeleporter.RCO_StorageTeleporter_C"));
			if (RemoteCallObjectClass)
			{
				if (URemoteCallObjectRegistry* Registry = GameInstance->GetSubsystem<URemoteCallObjectRegistry>())
				{
					Registry->RegisterRemoteCallObject(RemoteCallObjectClass);
					RegisteredRcoGameInstances.Add(GameInstance);
					UE_LOG(LogStorageTeleporterRepair, Warning,
						TEXT("Registered RCO_StorageTeleporter with SML for game instance %s."),
						*GameInstance->GetName());
				}
			}
		}
	}

	UClass* DesiredSubsystemClass = LoadClass<AModSubsystem>(
		nullptr, TEXT("/StorageTeleporter/STSubsystem.STSubsystem_C"));
	USubsystemActorManager* SubsystemManager = World->GetSubsystem<USubsystemActorManager>();
	static TSet<TWeakObjectPtr<UWorld>> RegistrationAttemptedWorlds;
	if (DesiredSubsystemClass && SubsystemManager && !RegistrationAttemptedWorlds.Contains(World))
	{
		RegistrationAttemptedWorlds.Add(World);
		AModSubsystem* RegisteredSubsystem = SubsystemManager->K2_GetSubsystemActor(DesiredSubsystemClass);
		if (!RegisteredSubsystem)
		{
			SubsystemManager->RegisterSubsystemActor(DesiredSubsystemClass);
			RegisteredSubsystem = SubsystemManager->K2_GetSubsystemActor(DesiredSubsystemClass);
			UE_LOG(LogStorageTeleporterRepair, Warning,
				TEXT("Registered STSubsystem with SML in %s; immediately available=%s."),
				*World->GetName(), RegisteredSubsystem ? TEXT("yes") : TEXT("no"));
		}
	}

	AActor* Subsystem = nullptr;
	AActor* PrimaryHub = nullptr;
	AActor* FallbackHub = nullptr;
	TArray<AActor*> Teleporters;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if ((DesiredSubsystemClass && Actor->IsA(DesiredSubsystemClass)) ||
			FindFProperty<FMapProperty>(Actor->GetClass(), TEXT("StorageDataMap")))
		{
			Subsystem = Actor;
		}
		else if (IsClassNamed(Actor, TEXT("Build_STHub_C")) ||
			Actor->GetClass()->GetPathName().Contains(TEXT("/StorageTeleporter/Buildables/Hub/")))
		{
			FallbackHub = FallbackHub ? FallbackHub : Actor;
			if (ReadBoolProperty(Actor, TEXT("isMainHub"), true))
			{
				PrimaryHub = Actor;
			}
		}
		else if (IsClassNamed(Actor, TEXT("ItemTeleporter_Build_C")) ||
			(Actor->GetClass()->GetPathName().Contains(TEXT("/StorageTeleporter/")) &&
			 FindFProperty<FStrProperty>(Actor->GetClass(), TEXT("StorageID"))))
		{
			Teleporters.Add(Actor);
		}
	}
	if (!Subsystem || Teleporters.IsEmpty())
	{
		return;
	}

	AActor* Hub = PrimaryHub ? PrimaryHub : FallbackHub;
	if (FObjectPropertyBase* HubProperty = FindFProperty<FObjectPropertyBase>(Subsystem->GetClass(), TEXT("HUB")))
	{
		UObject* CurrentHub = HubProperty->GetObjectPropertyValue_InContainer(Subsystem);
		if (Hub && CurrentHub != Hub)
		{
			HubProperty->SetObjectPropertyValue_InContainer(Subsystem, Hub);
			UE_LOG(LogStorageTeleporterRepair, Warning,
				TEXT("Restored STSubsystem HUB reference to '%s' in %s."),
				*Hub->GetName(), *World->GetName());
		}
	}

	static TSet<TWeakObjectPtr<UWorld>> LoggedWorlds;
	if (!LoggedWorlds.Contains(World))
	{
		LoggedWorlds.Add(World);
		const AFGBuildableFactory* HubFactory = Cast<AFGBuildableFactory>(Hub);
		UE_LOG(LogStorageTeleporterRepair, Warning,
			TEXT("Registry scan found subsystem '%s', hub '%s' (powered=%s), and %d item teleporter(s) in %s (net mode %d)."),
			*Subsystem->GetName(), Hub ? *Hub->GetName() : TEXT("none"),
			HubFactory && HubFactory->HasPower() ? TEXT("yes") : TEXT("no"), Teleporters.Num(),
			*World->GetName(), static_cast<int32>(World->GetNetMode()));

		for (AActor* Teleporter : Teleporters)
		{
			const UFGFactoryConnectionComponent* InputConnection =
				Cast<UFGFactoryConnectionComponent>(ReadObjectProperty(Teleporter, TEXT("In")));
			const UFGFactoryConnectionComponent* OutputConnection =
				Cast<UFGFactoryConnectionComponent>(ReadObjectProperty(Teleporter, TEXT("Out")));
			const UFGInventoryComponent* InputInventory =
				Cast<UFGInventoryComponent>(ReadObjectProperty(Teleporter, TEXT("InputInventory")));
			const UFGInventoryComponent* OutputInventory =
				Cast<UFGInventoryComponent>(ReadObjectProperty(Teleporter, TEXT("OutputInventory")));
			UE_LOG(LogStorageTeleporterRepair, Log,
				TEXT("Transport state '%s': enabled=%s, input-connected=%s, output-connected=%s, input=%d/%d, output=%d/%d."),
				*Teleporter->GetName(), ReadBoolProperty(Teleporter, TEXT("isSwitchedOn")) ? TEXT("yes") : TEXT("no"),
				InputConnection && InputConnection->IsConnected() ? TEXT("yes") : TEXT("no"),
				OutputConnection && OutputConnection->IsConnected() ? TEXT("yes") : TEXT("no"),
				InputInventory ? InputInventory->GetNumItems(nullptr) : -1,
				InputInventory ? InputInventory->GetSizeLinear() : -1,
				OutputInventory ? OutputInventory->GetNumItems(nullptr) : -1,
				OutputInventory ? OutputInventory->GetSizeLinear() : -1);
		}
	}

	FMapProperty* MapProperty = FindFProperty<FMapProperty>(Subsystem->GetClass(), TEXT("StorageDataMap"));
	for (AActor* Teleporter : Teleporters)
	{
		FStrProperty* IdProperty = FindFProperty<FStrProperty>(Teleporter->GetClass(), TEXT("StorageID"));
		UObject* TeleporterInfo = ReadTeleporterInfo(Teleporter);
		if (!TeleporterInfo)
		{
			TeleporterInfo = CreateTeleporterInfo(Subsystem, Teleporter);
		}
		if (!IdProperty || !TeleporterInfo)
		{
			UE_LOG(LogStorageTeleporterRepair, Warning,
				TEXT("Could not repair '%s': StorageID property=%s, TeleporterInfo=%s."),
				*Teleporter->GetName(), IdProperty ? TEXT("yes") : TEXT("no"), TeleporterInfo ? TEXT("yes") : TEXT("no"));
			continue;
		}

		const FString StorageId = IdProperty->GetPropertyValue_InContainer(Teleporter);
		if (!RegistryContains(MapProperty, Subsystem, StorageId, TeleporterInfo) &&
			InvokeAddTeleporterId(Subsystem, TeleporterInfo, StorageId))
		{
			UE_LOG(LogStorageTeleporterRepair, Log,
				TEXT("Restored teleporter '%s' to connection '%s' in %s."),
				*Teleporter->GetName(), *StorageId, *World->GetName());
		}
	}
}

IMPLEMENT_MODULE(FStorageTeleporterModule, StorageTeleporter)

using UnrealBuildTool;

public class StorageTeleporterEditor : ModuleRules
{
	public StorageTeleporterEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"BlueprintGraph",
			"Kismet",
			"UnrealEd"
		});
	}
}

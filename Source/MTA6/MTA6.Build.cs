using UnrealBuildTool;

public class MTA6 : ModuleRules
{
	public MTA6(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"ChaosVehicles", "Json", "JsonUtilities", "UMG", "Slate", "SlateCore"
		});
	}
}

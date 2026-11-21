using UnrealBuildTool;

public class MTA6 : ModuleRules
{
	public MTA6(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Viele Dateien haben gleichnamige Hilfsfunktionen in eigenen Namespaces; ohne Unity-Build
		// bleiben diese Dateien voneinander getrennt (sonst entstehen mehrdeutige Aufrufe).
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"ChaosVehicles", "Json", "JsonUtilities", "UMG", "Slate", "SlateCore"
		});
	}
}

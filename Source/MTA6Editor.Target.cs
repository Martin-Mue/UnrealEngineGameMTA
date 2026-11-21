using UnrealBuildTool;

public class MTA6EditorTarget : TargetRules
{
	public MTA6EditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("MTA6");
	}
}

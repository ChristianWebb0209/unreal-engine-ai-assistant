using UnrealBuildTool;

public class UnrealAiEditor : ModuleRules
{
	public UnrealAiEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Projects",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"EditorStyle",
				"EditorWidgets",
				"DeveloperSettings",
				"WorkspaceMenuStructure",
				"LevelEditor",
				"ApplicationCore",
				"Settings",
				"Json",
				"HTTP",
				"AssetRegistry",
				"ContentBrowser",
				"ContentBrowserData",
				"AssetTools",
				"KismetCompiler",
				"BlueprintGraph",
				"StructUtils",
				"RenderCore",
			});
	}
}

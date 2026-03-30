using UnrealBuildTool;

public class UnrealAiEditor : ModuleRules
{
	public UnrealAiEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// SDockingTabStack (OpenTab with insert index) — not exposed under Slate/Public.
		PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Source", "Runtime", "Slate", "Private"));

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
				"EditorFramework",
				"AppFramework",
				"DesktopPlatform",
				"LevelEditor",
				"ApplicationCore",
				"Settings",
				"Json",
				"HTTP",
				"AssetRegistry",
				"SourceCodeAccess",
				"ContentBrowser",
				"ContentBrowserData",
				"AssetTools",
				"KismetCompiler",
				"BlueprintGraph",
				"StructUtils",
				"RenderCore",
				"LevelSequence",
				"MovieScene",
				"ImageWrapper",
				"SQLiteCore",
				"Kismet",
				"KismetWidgets",
				"GraphEditor"
			});
	}
}

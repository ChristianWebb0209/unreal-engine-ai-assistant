#pragma once

#include "CoreMinimal.h"

/**
 * Restrict project-relative file paths to safe roots (Source, Config, Content, Plugins, docs/.github/scripts/tools/build
 * trees, root *.uproject, root-level README/LICENSE-style files). Prevents escaping the project directory or touching
 * Engine/Saved/Intermediate/Binaries.
 */
bool UnrealAiIsAllowedProjectRelativePath(const FString& RelativePath, FString& OutError);

/** Absolute path under FPaths::ProjectDir() after allowlist check. */
bool UnrealAiResolveProjectFilePath(const FString& RelativePath, FString& OutAbsolute, FString& OutError);

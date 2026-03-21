#pragma once

#include "CoreMinimal.h"

/**
 * Restrict project-relative file paths to safe roots (Source, Config, each plugin's Source tree, Content, .uproject).
 * Prevents escaping the project directory or touching Engine/Saved/Intermediate.
 */
bool UnrealAiIsAllowedProjectRelativePath(const FString& RelativePath, FString& OutError);

/** Absolute path under FPaths::ProjectDir() after allowlist check. */
bool UnrealAiResolveProjectFilePath(const FString& RelativePath, FString& OutAbsolute, FString& OutError);

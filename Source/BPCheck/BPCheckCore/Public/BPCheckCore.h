// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintCompilerExtension.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBPCheckCore, Log, All);

class FBPCheckCoreModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

class BPCHECKCORE_API FAnalyzer
{
public:
	static void Analyze(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data);
	static void Analyze(UBlueprint* Blueprint);
};
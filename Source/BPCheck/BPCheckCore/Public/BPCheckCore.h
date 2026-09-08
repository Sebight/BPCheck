// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BlueprintCompilerExtension.h"
#include "BPCheckCore.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBPCheckCore, Log, All);

class FBPCheckCoreModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};


USTRUCT()
struct FAnalyzerResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString BlueprintName;

	UPROPERTY()
	FString Message;

	UPROPERTY()
	bool Success = false;
};

USTRUCT()
struct FAnalyzerReport
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAnalyzerResult> Results;
};

class BPCHECKCORE_API FAnalyzer
{
public:
	static FAnalyzerResult Analyze(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data);
	static FAnalyzerResult Analyze(UBlueprint* Blueprint);
};
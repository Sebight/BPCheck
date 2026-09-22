// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Suggestions.h"
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
struct FSuggestionContext
{
	GENERATED_BODY()

	ESuggestion Suggestion;
	FGuid NodeGuid;
};

USTRUCT()
struct FAnalyzerResult
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSuggestionContext> Suggestions;

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

enum class ENodeFlag
{
	Dead = 0,
	InTick = 1,
	InALoop = 2
};

class FAnalysisContext
{
public:
	void AddFlags(FGuid NodeGuid, TSet<ENodeFlag> Flags);
	bool HasFlag(FGuid NodeGuid, ENodeFlag Flag);

private:
	TMap<FGuid, TSet<ENodeFlag>> NodeData;
};

class BPCHECKCORE_API FAnalysisRule
{
public:
	FAnalysisRule() = default;
	virtual ~FAnalysisRule() = default;
	virtual void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions) = 0;
};

class BPCHECKCORE_API FPrintInHotPathRule : public FAnalysisRule
{
public:
	void Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions) override;
};

class BPCHECKCORE_API FAnalyzer
{
public:
	FAnalyzer();
	FAnalyzer(const FAnalyzer&) = delete;
	FAnalyzer& operator=(const FAnalyzer&) = delete;
	FAnalyzer(FAnalyzer&&) = default;
	FAnalyzer& operator=(FAnalyzer&&) = default;

	FAnalyzerResult Analyze(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data);
	FAnalyzerResult Analyze(const UBlueprint* Blueprint);

private:
	// I think these could be static'd and moved into a namespace
	UEdGraphNode* FindTick(const UBlueprint* Blueprint);
	TArray<UEdGraphNode*> GetNextExecNode(const UEdGraphNode* CurrentNode);
	static bool IsDead(const UEdGraphNode* Node);

	FAnalysisContext BuildContext(const UBlueprint* Blueprint);

private:
	TArray<TUniquePtr<FAnalysisRule>> Rules;
};
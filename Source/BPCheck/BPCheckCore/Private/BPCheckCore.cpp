// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPCheckCore.h"

#define LOCTEXT_NAMESPACE "FBPCheckModule"

DEFINE_LOG_CATEGORY(LogBPCheckCore);

void FBPCheckCoreModule::StartupModule()
{
	UE_LOG(LogBPCheckCore, Log, TEXT("BPCheck-Core started."));
}

void FBPCheckCoreModule::ShutdownModule()
{

}

FAnalyzerResult FAnalyzer::Analyze(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	return Analyze(CompilationContext.Blueprint);
}

FAnalyzerResult FAnalyzer::Analyze(const UBlueprint* Blueprint)
{
	UE_LOG(LogBPCheckCore, Log, TEXT("Analyze %s"), *Blueprint->GetName());

	UEdGraphNode* TickNode = FindTick(Blueprint);

	if (!TickNode) return Fail(Blueprint, "Tick not found.");

	UEdGraphNode* Current = TickNode;
	while (Current)
	{
		UE_LOG(LogBPCheckCore, Log, TEXT("Walked: %s"), *Current->GetName());

		Current = GetNextExecNode(Current);
	}

	FAnalyzerResult Result;
	Result.BlueprintName = Blueprint->GetName();
	Result.Success = true;
	Result.Message = "no suggestions";

	return Result;
}

UEdGraphNode* FAnalyzer::FindTick(const UBlueprint* Blueprint)
{
	for (auto& Graph : Blueprint->UbergraphPages)
	{
		for (auto& Node : Graph->Nodes)
		{
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				if (EventNode->GetFunctionName() == "ReceiveTick") return Node;
			}
		}
	}
	return nullptr;
}

UEdGraphNode* FAnalyzer::GetNextExecNode(const UEdGraphNode* CurrentNode)
{
	// TODO(Seb): This is a temporary (wasteful) warning
	int8 NumberOfOutputPins = 0;
	for (auto Pin : CurrentNode->Pins)
	{
		if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			NumberOfOutputPins++;
		}
	}

	if (NumberOfOutputPins > 1)
	{
		UE_LOG(LogBPCheckCore, Warning, TEXT("Nodes with multiple output pins are not supported."));
	}

	// WARNING: This is for now only the first output pin
	for (auto Pin : CurrentNode->Pins)
	{
		if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			auto LinkedToCount = Pin->LinkedTo.Num();

			if (LinkedToCount == 0) continue;

			return Pin->LinkedTo[0]->GetOwningNode();
		}
	}

	return nullptr;
}

FAnalyzerResult FAnalyzer::Fail(const UBlueprint* Blueprint, const FString& Reason)
{
	FAnalyzerResult Result;
	Result.BlueprintName = Blueprint->GetName();
	Result.Success = false;
	Result.Message = Reason.IsEmpty() ? "unknown reason" : Reason;

	return Result;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPCheckCoreModule, BPCheckCore)
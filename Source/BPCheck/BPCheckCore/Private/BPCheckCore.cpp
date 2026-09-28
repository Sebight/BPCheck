// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPCheckCore.h"

#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MacroInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "FBPCheckModule"

DEFINE_LOG_CATEGORY(LogBPCheckCore);

void FBPCheckCoreModule::StartupModule()
{
	UE_LOG(LogBPCheckCore, Log, TEXT("BPCheck-Core started."));
}

void FBPCheckCoreModule::ShutdownModule()
{

}

void FAnalysisContext::AddFlags(FGuid NodeGuid, TSet<ENodeFlag> Flags)
{
	if (!NodeData.Contains(NodeGuid))
	{
		NodeData.Add(NodeGuid, Flags);
	}
	else
	{
		NodeData.Find(NodeGuid)->Append(Flags);
	}
}

bool FAnalysisContext::HasFlag(FGuid NodeGuid, ENodeFlag Flag) const
{
	if (!NodeData.Contains(NodeGuid)) return false;
	return NodeData.Find(NodeGuid)->Contains(Flag);
}

void FAnalysisContext::MarkNodeAlive(FGuid NodeGuid)
{
	AliveNodes.Add(NodeGuid);
}

bool FAnalysisContext::IsDead(FGuid NodeGuid) const
{
	return !AliveNodes.Contains(NodeGuid);
}

void FExpensiveHotNodeRule::Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	static UFunction* PrintStringFn = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString"));
	static UFunction* GetAllActorsOfClassFn = UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("GetAllActorsOfClass"));
	static UFunction* GetActorsOfClassFn = UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("GetActorsOfClass"));
	static UFunction* GetAllActorsWithTagFn = UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("GetAllActorsOfClassWithTag"));
	static UFunction* GetAllActorsWithInterfaceFn = UGameplayStatics::StaticClass()->FindFunctionByName(TEXT("GetAllActorsWithInterface"));

	if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
	{
		UFunction* TargetFn = Call->GetTargetFunction();
		if (TargetFn == PrintStringFn)
		{
			if (Context.HasFlag(Node->NodeGuid, ENodeFlag::InTick) || Context.HasFlag(Node->NodeGuid, ENodeFlag::InALoop))
			{
				Suggestions.Push(
					{
						ESuggestion::PrintInHotPath,
						Call->NodeGuid
					}
				);
			}
		}

		if (TargetFn == GetAllActorsOfClassFn || TargetFn == GetActorsOfClassFn || TargetFn == GetAllActorsWithTagFn || TargetFn == GetAllActorsWithInterfaceFn)
		{
			if (Context.HasFlag(Node->NodeGuid, ENodeFlag::InTick) || Context.HasFlag(Node->NodeGuid, ENodeFlag::InALoop))
			{
				Suggestions.Push(
					{
						ESuggestion::HotGetActorsOfClass,
						Call->NodeGuid
					}
				);
			}
		}
	}
}

void FCastRule::Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	if (const UK2Node_DynamicCast* DynamicCast = Cast<UK2Node_DynamicCast>(Node))
	{
		UClass* TargetClass = DynamicCast->TargetType;
		if (TargetClass && Cast<UBlueprintGeneratedClass>(TargetClass))
		{
			Suggestions.Push(
				{
					ESuggestion::HardCastToBPClass,
					DynamicCast->NodeGuid
				}
			);
		}

		if (!DynamicCast->IsNodePure())
		{
			UEdGraphPin* FailPin = DynamicCast->GetInvalidCastPin();
			if (FailPin && FailPin->LinkedTo.Num() == 0)
			{
				Suggestions.Push(
					{
						ESuggestion::CastWithoutFailHandler,
						DynamicCast->NodeGuid
					}
				);
			}
		}
	}
}

void FDeadCodeRule::Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	if (FAnalyzer::IsNodePure(Node)) return;

	if (Context.IsDead(Node->NodeGuid))
	{
		bool AnyConnections = false;
		bool AnyInputs = false;

		for (auto Pin : Node->Pins)
		{
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				auto LinkedToCount = Pin->LinkedTo.Num();

				if (LinkedToCount > 0)
				{
					if (Pin->Direction == EGPD_Input)
					{
						AnyInputs = true;
					}

					AnyConnections = true;
					break;
				}
			}
		}

		if (AnyConnections)
		{
			// This makes the chain warning only for the first disconnected
			if (!AnyInputs)
			{
				Suggestions.Push(
					{
						ESuggestion::Unreachable,
						Node->NodeGuid
					}
				);
			}
		}
		else
		{
			Suggestions.Push(
				{
					ESuggestion::Disconnected,
					Node->NodeGuid
				}
			);
		}

	}
}

FAnalyzer::FAnalyzer()
{
	Rules.Push(MakeUnique<FExpensiveHotNodeRule>());
	Rules.Push(MakeUnique<FCastRule>());
	Rules.Push(MakeUnique<FDeadCodeRule>());
}

FAnalyzerResult FAnalyzer::Analyze(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	return Analyze(CompilationContext.Blueprint);
}

FAnalyzerResult FAnalyzer::Analyze(const UBlueprint* Blueprint)
{
	UE_LOG(LogBPCheckCore, Log, TEXT("Analyze %s"), *Blueprint->GetName());

	const FAnalysisContext Context = BuildContext(Blueprint);

	TArray<FSuggestionContext> Suggestions;
	for (auto& Page : Blueprint->UbergraphPages)
	{
		for (auto Node : Page->Nodes)
		{
			// TODO(Seb): This blocks the Dead node rules, maybe adding an exception there would make sense
			// or manually checking for deadness in all other rules?
			// if (!Context.IsPure(Node->NodeGuid) && Context.IsDead(Node->NodeGuid)) continue;

			for (const auto& Rule : Rules)
			{
				Rule->Check(Node, Suggestions, Context);
			}
		}
	}

	FAnalyzerResult Result;
	Result.Success = true;
	Result.Suggestions = MoveTemp(Suggestions);

	return Result;
}

TSet<UEdGraphNode*> FAnalyzer::CollectEntryPoints(const UEdGraph* Graph) const
{
	TSet<UEdGraphNode*> OutNodes;

	for (auto Node : Graph->Nodes)
	{
		if (!Node) continue;

		if (Node->IsA<UK2Node_Event>())
		{
			OutNodes.Add(Node);
		}

		if (Node->IsA<UK2Node_FunctionEntry>())
		{
			OutNodes.Add(Node);
		}
	}

	return OutNodes;
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

TArray<FNextExecNode> FAnalyzer::GetNextExecNode(const UEdGraphNode* CurrentNode)
{
	TArray<FNextExecNode> Out;

	uint8 PinIdx = 0;
	for (auto Pin : CurrentNode->Pins)
	{
		if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			auto LinkedToCount = Pin->LinkedTo.Num();

			if (LinkedToCount == 0)
			{
				PinIdx++;
				continue;
			}

			FNextExecNode NextNode;
			NextNode.PinIndex = PinIdx++;
			// Note(Seb): I think assuming 0 here is okay, since the output execution pin can only go to one?
			NextNode.Node = Pin->LinkedTo[0]->GetOwningNode();
			Out.Push(NextNode);
		}

	}

	return Out;
}

bool FAnalyzer::IsNodePure(const UEdGraphNode* Node)
{
	if (const UK2Node* K2Node = Cast<UK2Node>(Node))
	{
		return K2Node->IsNodePure();
	}

	return false;
}

FAnalysisContext FAnalyzer::BuildContext(const UBlueprint* Blueprint)
{
	TSet<UEdGraphNode*> EntryPoints;

	for (auto Graph : Blueprint->UbergraphPages)
	{
		EntryPoints.Append(CollectEntryPoints(Graph));
	}

	for (auto Graph : Blueprint->FunctionGraphs)
	{
		EntryPoints.Append(CollectEntryPoints(Graph));
	}

	FAnalysisContext Context;

	TFunction<void(const UEdGraphNode*, TSet<ENodeFlag>, FAnalysisContext&)> WalkNode;
	WalkNode = [&WalkNode, this](const UEdGraphNode* Node, TSet<ENodeFlag> Inherit, FAnalysisContext& Context)
		{
			Context.MarkNodeAlive(Node->NodeGuid);

			TMap<uint8, TSet<ENodeFlag>> PinModifiers;

			if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				if (EventNode->GetFunctionName() == "ReceiveTick")
				{
					Inherit.Add(ENodeFlag::InTick);
				}
			}

			if (const UK2Node_MacroInstance* MacroInstance = Cast<UK2Node_MacroInstance>(Node))
			{
				FString Name = MacroInstance->GetMacroGraph()->GetName();
				if (Name.Equals("ForLoop") || Name.Equals("ForEachLoop") || Name.Equals("WhileLoop"))
				{
					// Pin 0 is the Loop Body, which means that we want to flag all of the nodes under there with InLoop.
					// From what I've tried, the pin order seems to be consistent, I hope it doesn't hurt me later.
					PinModifiers.Add(0, { ENodeFlag::InALoop });
				}
			}

			Context.AddFlags(Node->NodeGuid, Inherit);

			auto NextNodes = GetNextExecNode(Node);
			for (auto& NextNode : NextNodes)
			{
				TSet<ENodeFlag> Flags = Inherit;
				if (PinModifiers.Contains(NextNode.PinIndex))
				{
					Flags.Append(*PinModifiers.Find(NextNode.PinIndex));
				}

				WalkNode(NextNode.Node, Flags, Context);
			}
		};

	for (auto* Entry : EntryPoints)
	{
		WalkNode(Entry, {}, Context);
	}

	return Context;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPCheckCoreModule, BPCheckCore)
// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPCheckCore.h"

#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "FBPCheckModule"

DEFINE_LOG_CATEGORY(LogBPCheckCore);

void FBPCheckCoreModule::StartupModule()
{
	UE_LOG(LogBPCheckCore, Log, TEXT("BPCheck-Core started."));
}

void FBPCheckCoreModule::ShutdownModule()
{

}

bool UBPCheckSettings::IsSuppressed(const UBlueprint* Blueprint, TOptional<FGuid> NodeGuid, ESuggestion Suggestion) const
{
	return Suppressions.Contains(FSuppression{ FSoftObjectPath(Blueprint), NodeGuid, Suggestion });
}

void UBPCheckSettings::PruneDeadNodes(UBlueprint* Blueprint, const TArray<FGuid>& AliveNodes)
{
	for (auto It = Suppressions.CreateIterator(); It; ++It)
	{
		const FSuggestionInfo& Info = GetSuggestionInfo(It->Suggestion);
		if (Info.Scope != ESuggestionScope::Node) continue;

		if (It->Blueprint == FSoftObjectPath(Blueprint))
		{
			if (It->TargetGuid.IsSet() && !AliveNodes.Contains(It->TargetGuid))
			{
				It.RemoveCurrent();
			}
		}
	}
}

void UBPCheckSettings::Suppress(const UBlueprint* Blueprint, TOptional<FGuid> NodeGuid, ESuggestion Suggestion)
{
	Suppressions.Add({ FSoftObjectPath(Blueprint), NodeGuid, Suggestion });
	TryUpdateDefaultConfigFile();
	OnSettingsChanged.Broadcast();
}

void UBPCheckSettings::Unsuppress(const FSuppression& Suppression)
{
	Suppressions.Remove(Suppression);
	TryUpdateDefaultConfigFile();
	OnSettingsChanged.Broadcast();
}

void UBPCheckSettings::DisableSuggestion(ESuggestion Suggestion)
{
	SuggestionEnabled.Add(Suggestion, false);
	TryUpdateDefaultConfigFile();
	OnSettingsChanged.Broadcast();
}

void UBPCheckSettings::ClearSuppressions()
{
	Suppressions.Empty();
	TryUpdateDefaultConfigFile();
	OnSettingsChanged.Broadcast();
}

void UBPCheckSettings::ClearAll()
{
	Suppressions.Empty();
	for (TPair<ESuggestion, bool>& P : SuggestionEnabled)
	{
		P.Value = true;
	}
	TryUpdateDefaultConfigFile();
	OnSettingsChanged.Broadcast();
}

bool UBPCheckSettings::IsSuggestionEnabled(ESuggestion Suggestion) const
{
	const bool* Found = SuggestionEnabled.Find(Suggestion);
	return Found ? *Found : true;
}

void UBPCheckSettings::SetSuggestionEnabled(ESuggestion S, bool bEnabled)
{
	SuggestionEnabled.Add(S, bEnabled);
	TryUpdateDefaultConfigFile();
	OnSettingsChanged.Broadcast();
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

bool FAnalysisContext::Covers(FGuid NodeGuid, const TSet<ENodeFlag>& Flags) const
{
	const TSet<ENodeFlag>* Existing = NodeData.Find(NodeGuid);
	return Existing && Flags.Difference(*Existing).IsEmpty();
}

void FAnalysisContext::MarkNodeAlive(FGuid NodeGuid)
{
	AliveNodes.Add(NodeGuid);
}

bool FAnalysisContext::IsDead(FGuid NodeGuid) const
{
	return !AliveNodes.Contains(NodeGuid);
}

FFunctionSummary FAnalysisContext::EffectsOf(UFunction* Fn) const
{
	return EffectResolver->EffectsOf(Fn);
}

void FExpensiveHotNodeRule::Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
	{
		bool InHotPath = Context.HasFlag(Node->NodeGuid, ENodeFlag::InTick) || Context.HasFlag(Node->NodeGuid, ENodeFlag::InALoop);

		UFunction* TargetFn = Call->GetTargetFunction();

		FFunctionSummary Summary = Context.EffectsOf(TargetFn);

		if (InHotPath && Summary.Effects.Contains(EFunctionEffect::DebugSideEffect))
		{
			FSuggestionContext Ctx = FSuggestionContext(ESuggestion::PrintInHotPath, Call->NodeGuid);
			Ctx.StackTrace = Summary.StackTrace.FindRef(EFunctionEffect::DebugSideEffect);
			Suggestions.Push(Ctx);
		}

		if (InHotPath && Summary.Effects.Contains(EFunctionEffect::ExpensiveActorQuery))
		{
			FSuggestionContext Ctx = FSuggestionContext(ESuggestion::HotGetActorsOfClass, Call->NodeGuid);
			Ctx.StackTrace = Summary.StackTrace.FindRef(EFunctionEffect::ExpensiveActorQuery);
			Suggestions.Push(Ctx);
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
			Suggestions.Push(FSuggestionContext(ESuggestion::HardCastToBPClass, DynamicCast->NodeGuid));
		}

		if (!DynamicCast->IsNodePure())
		{
			UEdGraphPin* FailPin = DynamicCast->GetInvalidCastPin();
			if (FailPin && FailPin->LinkedTo.Num() == 0)
			{
				Suggestions.Push(FSuggestionContext(ESuggestion::CastWithoutFailHandler, DynamicCast->NodeGuid));
			}
		}

		if (Context.HasFlag(DynamicCast->NodeGuid, ENodeFlag::InALoop))
		{
			Suggestions.Push(FSuggestionContext(ESuggestion::CastInLoop, DynamicCast->NodeGuid));
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
				Suggestions.Push(FSuggestionContext(ESuggestion::Unreachable, Node->NodeGuid));
			}
		}
		else
		{
			Suggestions.Push(FSuggestionContext(ESuggestion::Disconnected, Node->NodeGuid));
		}

	}
}

void FLatentNodeInLoopRule::Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
	{
		if (!Context.HasFlag(Node->NodeGuid, ENodeFlag::InALoop)) return;

		UFunction* TargetFn = Call->GetTargetFunction();

		FFunctionSummary Summary = Context.EffectsOf(TargetFn);

		if (Summary.Effects.Contains(EFunctionEffect::Latent))
		{
			FSuggestionContext Ctx = FSuggestionContext(ESuggestion::LatentNodeInLoop, Call->NodeGuid);
			Ctx.StackTrace = Summary.StackTrace.FindRef(EFunctionEffect::Latent);

			Suggestions.Push(Ctx);
		}
	}
}

void FGraphNodeCountRule::Check(const UEdGraph* Graph, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	FString GraphName = Graph->GetName();
	int32 GraphSize = Graph->Nodes.Num();

	if (GraphSize > Context.Settings->MaxGraphNodes)
	{
		Suggestions.Push(FSuggestionContext(ESuggestion::GraphSize, MakeWeakObjectPtr<const UEdGraph>(Graph)));
	}
}

void FFunctionArgumentRule::Check(const UEdGraph* Graph, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	const UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	const bool bIsFunctionGraph = BP && BP->FunctionGraphs.Contains(Graph);

	if (!bIsFunctionGraph) return;

	auto FindFunc = [](const UBlueprint* BP, const UEdGraph* Graph) -> UFunction*
		{
			if (BP->GeneratedClass)
			{
				if (UFunction* Fn = BP->GeneratedClass->FindFunctionByName(Graph->GetFName()))
				{
					return Fn;
				}
			}

			return nullptr;
		};

	UFunction* Func = FindFunc(BP, Graph);

	int32 Count = 0;
	for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		Count++;
	}

	if (Count > Context.Settings->MaxFunctionArguments)
	{
		Suggestions.Push(FSuggestionContext(ESuggestion::FunctionArgsCount, Graph));
	}
}

void FEmptyEventHandlerRule::Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
	{
		bool ConnectedToAnything = false;
		for (auto* Pin : Event->Pins)
		{
			if (Pin->Direction == EGPD_Output)
			{
				if (Pin->HasAnyConnections())
				{
					ConnectedToAnything = true;
					break;
				}
			}
		}

		if (Event->GetFunctionName() == "ReceiveTick" && !ConnectedToAnything)
		{
			Suggestions.Push(FSuggestionContext(ESuggestion::UnusedTick, Event->NodeGuid));
		}
		else if (!ConnectedToAnything)
		{
			Suggestions.Push(FSuggestionContext(ESuggestion::EmptyEventHandler, Event->NodeGuid));
		}
	}
}

void FLoopNestingRule::Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	bool bIsLoop = false;
	if (const UK2Node_MacroInstance* MacroInstance = Cast<UK2Node_MacroInstance>(Node))
	{
		FString Name = MacroInstance->GetMacroGraph()->GetName();
		if (Name.Equals("ForLoop") || Name.Equals("ForEachLoop") || Name.Equals("WhileLoop"))
		{
			bIsLoop = true;
		}
	}

	if (!bIsLoop) return;

	if (Context.HasFlag(Node->NodeGuid, ENodeFlag::InALoop))
	{
		Suggestions.Push(FSuggestionContext(ESuggestion::NestedLoops, Node->NodeGuid));
	}
}

void FRedundantBranchRule::Check(const UEdGraphNode* Node, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	if (const UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Node))
	{
		TArray<FNextExecNode> Outputs = FAnalyzer::GetNextExecNode(Node);

		FNextExecNode* True = Outputs.FindByPredicate([&](const FNextExecNode& Item)
			{
				return Item.PinIndex == 0;
			});

		FNextExecNode* False = Outputs.FindByPredicate([&](const FNextExecNode& Item)
			{
				return Item.PinIndex == 1;
			});

		if (True && False && (True->Node == False->Node) || !Branch->GetConditionPin()->HasAnyConnections())
		{
			Suggestions.Push(FSuggestionContext(ESuggestion::RedundantBranch, Node->NodeGuid));
		}
	}
}

void FUnusedVariableRule::Check(const UBlueprint* Blueprint, TArray<FSuggestionContext>& Suggestions, const FAnalysisContext& Context)
{
	TArray<FString> UnusedVars;
	for (const auto& Var : Blueprint->NewVariables)
	{
		if (!FBlueprintEditorUtils::IsVariableUsed(Blueprint, Var.VarName))
		{
			UnusedVars.Push(Var.VarName.ToString());
		}
	}

	for (const auto& Var : UnusedVars)
	{
		FSuggestionContext Ctx = FSuggestionContext(ESuggestion::UnusedVariable, ESuggestionScope::Blueprint);
		Ctx.Subject = Var;

		Suggestions.Push(Ctx);
	}
}

FFunctionSummary FEffectResolver::EffectsOf(UFunction* Fn)
{
	BuildKnownEffects();

	if (!Fn) return {};
	if (KnownEffects.Contains(Fn))
	{
		return *KnownEffects.Find(Fn);
	}

	if (EffectsCache.Contains(Fn))
	{
		return *EffectsCache.Find(Fn);
	}

	if (!Fn->IsInBlueprint())
	{
		EffectsCache.Add(Fn, {});
		return {};
	}

	return ComputeBPFunctionEffects(Fn);
}

void FEffectResolver::ClearCache()
{
	EffectsCache.Empty();
}

void FEffectResolver::BuildKnownEffects()
{
	if (bKnownEffectsBuilt) return;
	bKnownEffectsBuilt = true;

	auto AddEffect = [&](UClass* Class, const TCHAR* Name, EFunctionEffect Effect)
		{
			if (UFunction* Func = Class->FindFunctionByName(Name))
			{
				FFunctionSummary& Summary = KnownEffects.FindOrAdd(Func);
				Summary.Effects.Add(Effect);
				Summary.StackTrace.Add(Effect, { Func->GetName() });
			}
			else
			{
				UE_LOG(LogBPCheckCore, Warning, TEXT("KnownEffects: Function %s could not be resolved."), Name);
			}
		};

	UClass* Sys = UKismetSystemLibrary::StaticClass();
	UClass* Gameplay = UGameplayStatics::StaticClass();

	AddEffect(Sys, TEXT("Delay"), EFunctionEffect::Latent);
	AddEffect(Sys, TEXT("RetriggerableDelay"), EFunctionEffect::Latent);
	AddEffect(Gameplay, TEXT("GetAllActorsOfClass"), EFunctionEffect::ExpensiveActorQuery);
	AddEffect(Gameplay, TEXT("GetActorsOfClass"), EFunctionEffect::ExpensiveActorQuery);
	AddEffect(Gameplay, TEXT("GetAllActorsOfClassWithTag"), EFunctionEffect::ExpensiveActorQuery);
	AddEffect(Gameplay, TEXT("GetAllActorsWithInterface"), EFunctionEffect::ExpensiveActorQuery);
	AddEffect(Sys, TEXT("PrintString"), EFunctionEffect::DebugSideEffect);
}

FFunctionSummary FEffectResolver::ComputeBPFunctionEffects(UFunction* Fn)
{
	// This avoids re-entry from the recursive call
	EffectsCache.Add(Fn, {});

	UEdGraph* Graph = FindFunctionGraph(Fn);

	if (!Graph) return {};

	UEdGraphNode* Entry = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->IsA<UK2Node_FunctionEntry>())
		{
			Entry = Node;
			break;
		}
	}

	if (!Entry) return {};

	FFunctionSummary Acc;
	TSet<UEdGraphNode*> Visited;

	TFunction<void(UEdGraphNode*)> Walk;
	Walk = [&](UEdGraphNode* Node)
		{
			if (!Node || Visited.Contains(Node)) return;
			Visited.Add(Node);

			if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				if (UFunction* Target = Call->GetTargetFunction())
				{
					const FFunctionSummary Callee = EffectsOf(Target);
					for (EFunctionEffect E : Callee.Effects)
					{
						Acc.Effects.Add(E);

						if (!Acc.StackTrace.Contains(E))
						{
							Acc.StackTrace.Add(E, Callee.StackTrace.FindRef(E));
						}
					}
				}
			}

			for (const FNextExecNode& Next : FAnalyzer::GetNextExecNode(Node))
			{
				Walk(Next.Node);
			}
		};

	Walk(Entry);

	for (EFunctionEffect E : Acc.Effects)
	{
		Acc.StackTrace.FindChecked(E).Insert(Fn->GetName(), 0);
	}

	EffectsCache.Add(Fn, Acc);
	return Acc;
}

UEdGraph* FEffectResolver::FindFunctionGraph(UFunction* Fn)
{
	UClass* Owner = Fn->GetOwnerClass();
	UBlueprint* BP = UBlueprint::GetBlueprintFromClass(Owner);
	if (!BP) return nullptr;

	for (UEdGraph* Graph : BP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == Fn->GetFName())
		{
			return Graph;
		}
	}

	return nullptr;
}

FAnalyzer::FAnalyzer()
{
	Rules.Push(MakeUnique<FExpensiveHotNodeRule>());
	Rules.Push(MakeUnique<FCastRule>());
	Rules.Push(MakeUnique<FDeadCodeRule>());
	Rules.Push(MakeUnique<FLatentNodeInLoopRule>());
	Rules.Push(MakeUnique<FGraphNodeCountRule>());
	Rules.Push(MakeUnique<FFunctionArgumentRule>());
	Rules.Push(MakeUnique<FEmptyEventHandlerRule>());
	Rules.Push(MakeUnique<FLoopNestingRule>());
	Rules.Push(MakeUnique<FRedundantBranchRule>());
	Rules.Push(MakeUnique<FUnusedVariableRule>());
}

FAnalyzerResult FAnalyzer::Analyze(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	return Analyze(CompilationContext.Blueprint);
}

FAnalyzerResult FAnalyzer::Analyze(const UBlueprint* Blueprint)
{
	EffectResolver.ClearCache();

	const FAnalysisContext Context = BuildContext(Blueprint);

	TArray<FSuggestionContext> Suggestions;

	// Blueprint scope
	for (const auto& Rule : Rules)
	{
		Rule->Check(Blueprint, Suggestions, Context);
	}

	// Graph scope
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (auto& Graph : Graphs)
	{
		for (const auto& Rule : Rules)
		{
			Rule->Check(Graph, Suggestions, Context);
		}
	}

	// Nodes scope
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

	for (auto& Page : Blueprint->FunctionGraphs)
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
			if (Context.Covers(Node->NodeGuid, Inherit)) return;

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

	Context.EffectResolver = &EffectResolver;
	Context.Settings = GetDefault<UBPCheckSettings>();

	return Context;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPCheckCoreModule, BPCheckCore)
// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPCheckEditor.h"
#include "ToolMenus.h"
#include "BPCheckCore.h"
#include "BlueprintCompilationManager.h"
#include "DetailCategoryBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Images/SImage.h"
#include "Styling/StyleColors.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Suggestions.h"
#include "PropertyCustomizationHelpers.h"
#include "IDetailGroup.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Styling/SlateIconFinder.h"

#include "Suggestions.generated.h"
#include "Algo/AllOf.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "FBPCheckModule"

DEFINE_LOG_CATEGORY(LogBPCheckEditor);

static const FName BPCheckTabId = "BPCheckSuggestions";

class SBPCheckSuggestionsPanel;

UEdGraphNode* BPCheck::Editor::ResolveNode(const UBlueprint* Blueprint, FGuid NodeGuid)
{
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);

	for (auto* Graph : Graphs)
	{
		for (auto Node : Graph->Nodes)
		{
			if (Node->NodeGuid == NodeGuid) return Node;
		}
	}

	return nullptr;
}

UEdGraph* BPCheck::Editor::ResolveGraph(const UBlueprint* Blueprint, FGuid GraphGuid)
{
	if (!Blueprint || !GraphGuid.IsValid()) return nullptr;

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);

	UEdGraph* const* Found = Graphs.FindByPredicate([&GraphGuid](const UEdGraph* G)
		{
			return G && G->GraphGuid == GraphGuid;
		});

	return Found ? *Found : nullptr;
}

void FBPCheckEditorModule::StartupModule()
{
	UE_LOG(LogBPCheckEditor, Log, TEXT("BPCheck-Editor started."));
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBPCheckEditorModule::CreateMenuItem));

	UBPCompilerExtension* Extension = NewObject<UBPCompilerExtension>();
	FBlueprintCompilationManager::RegisterCompilerExtension(UBlueprint::StaticClass(), Extension);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(BPCheckTabId, FOnSpawnTab::CreateRaw(this, &FBPCheckEditorModule::CreateSuggestionsTab)).SetDisplayName(LOCTEXT("BPCheckSuggestionsList", "BPCheck Suggestions"));

	FPropertyEditorModule& PE =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PE.RegisterCustomClassLayout(
		UBPCheckSettings::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(
			&FBPCheckSettingsCustomization::MakeInstance));
	PE.NotifyCustomizationModuleChanged();

	SettingsChangedHandle = GetMutableDefault<UBPCheckSettings>()->OnSettingsChanged.AddRaw(this, &FBPCheckEditorModule::OnSettingsChanged);
}

void FBPCheckEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor")
			.UnregisterCustomClassLayout(UBPCheckSettings::StaticClass()->GetFName());
	}

	if (UObjectInitialized())
		GetMutableDefault<UBPCheckSettings>()->OnSettingsChanged.Remove(SettingsChangedHandle);
}

void FBPCheckEditorModule::CreateMenuItem()
{
	FToolMenuOwnerScoped Owner(this);

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	FToolMenuSection& Section = ToolBar->FindOrAddSection("BPCheck");

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"BPCheckButton",
		FUIAction(FExecuteAction::CreateRaw(this, &FBPCheckEditorModule::OnButtonClicked)),
		LOCTEXT("BPCheckLabel", "BPCheck"),
		LOCTEXT("BPCheckTooltip", "See BPCheck Suggestions"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.ViewOptions")
	));

	FNewToolMenuDelegate Delegate = FNewToolMenuDelegate::CreateRaw(this, &FBPCheckEditorModule::BuildNodeSection);

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UK2Node::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			continue;
		}

		const FName MenuName = *FString::Printf(TEXT("GraphEditor.GraphNodeContextMenu.%s"), *It->GetName());
		UToolMenus::Get()->ExtendMenu(MenuName)->AddDynamicSection("BPCheck", Delegate);
	}
}

void FBPCheckEditorModule::OnButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(BPCheckTabId);
}

void FBPCheckEditorModule::OnSettingsChanged()
{
	if (TSharedPtr<SBPCheckSuggestionsPanel> P = SuggestionsPanel.Pin())
	{
		P->SetResults(BuildTree());
	}

	ApplyNodeMarkers();
}

void FBPCheckEditorModule::SetSuggestionsForBlueprint(const UBlueprint* Blueprint, TArray<FSuggestionContext>& NewSuggestions)
{
	Suggestions.Add(Blueprint, MoveTemp(NewSuggestions));

	if (TSharedPtr<SBPCheckSuggestionsPanel> P = SuggestionsPanel.Pin())
	{
		P->SetResults(BuildTree());
	}
}

TSharedRef<SDockTab> FBPCheckEditorModule::CreateSuggestionsTab(const FSpawnTabArgs&)
{
	TSharedRef<SBPCheckSuggestionsPanel> Panel = SNew(SBPCheckSuggestionsPanel);
	SuggestionsPanel = Panel;
	Panel->SetResults(BuildTree());
	return SNew(SDockTab).TabRole(NomadTab)[Panel];
}

namespace ColumnIds
{
	static const FName Severity("Severity");
	static const FName Code("Code");
	static const FName Description("Description");
}

static const FTableRowStyle& GetRowStyle()
{
	static FTableRowStyle Style = FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
	static bool bInit = false;
	if (!bInit)
	{
		static FSlateColorBrush Even(FLinearColor(0.020f, 0.020f, 0.022f, 1.f));
		static FSlateColorBrush Odd(FLinearColor(0.035f, 0.035f, 0.040f, 1.f));
		Style.SetEvenRowBackgroundBrush(Even).SetEvenRowBackgroundHoveredBrush(Even);
		Style.SetOddRowBackgroundBrush(Odd).SetOddRowBackgroundHoveredBrush(Odd);
		bInit = true;
	}
	return Style;
}

// Claude helped with this UI work.
class SSuggestionRow : public SMultiColumnTableRow<TSharedPtr<FResultTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SSuggestionRow) {}
		SLATE_ARGUMENT(TSharedPtr<FResultTreeItem>, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& Owner)
	{
		Item = InArgs._Item;
		FSuperRowType::Construct(FSuperRowType::FArguments().Style(&GetRowStyle()), Owner);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
	{
		if (Column == ColumnIds::Severity)
		{
			return SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage).Image(GetIcon()).ColorAndOpacity(GetTint())
				];
		}
		if (Column == ColumnIds::Code)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SExpanderArrow, SharedThis(this)).IndentAmount(12)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(FText::FromString(Item->Label))
						.Font(FCoreStyle::GetDefaultFontStyle(Item->bIsGroup ? "Bold" : "Regular", 9))
				];
		}
		if (Column == ColumnIds::Description)
		{
			return SNew(STextBlock)
				.Text(FText::FromString(Item->Description))
				.ColorAndOpacity(FSlateColor(FStyleColors::Foreground));
		}
		return SNullWidget::NullWidget;
	}

private:
	const FSlateBrush* GetIcon() const
	{
		if (Item->bIsGroup && Item->Icon) return Item->Icon;

		switch (Item->Severity)
		{
		case EItemSeverity::Error:   return FAppStyle::Get().GetBrush("Icons.Error");
		case EItemSeverity::Warning: return FAppStyle::Get().GetBrush("Icons.Warning");
		case EItemSeverity::Group:   return FAppStyle::Get().GetBrush("Icons.FolderClosed");
		default:                     return FAppStyle::Get().GetBrush("Icons.Info");
		}
	}

	FSlateColor GetTint() const
	{
		if (Item->bIsGroup && Item->Icon) return FSlateColor::UseForeground();

		switch (Item->Severity)
		{
		case EItemSeverity::Error:   return FStyleColors::Error;
		case EItemSeverity::Warning: return FStyleColors::Warning;
		case EItemSeverity::Group:   return FStyleColors::AccentGray;
		default:                     return FStyleColors::AccentBlue;
		}
	}

	TSharedPtr<FResultTreeItem> Item;
};

TArray<TSharedPtr<FResultTreeItem>> FBPCheckEditorModule::BuildTree() const
{
	TArray<TSharedPtr<FResultTreeItem>> Roots;
	const UBPCheckSettings* Settings = GetDefault<UBPCheckSettings>();

	auto GetGraphBrush = [](const UEdGraph* Graph) -> const FSlateBrush*
		{
			if (const UEdGraphSchema* Schema = Graph->GetSchema())
			{
				switch (Schema->GetGraphType(Graph))
				{
				case GT_Function: return FAppStyle::Get().GetBrush("GraphEditor.Function_16x");
				case GT_Macro:    return FAppStyle::Get().GetBrush("GraphEditor.Macro_16x");
				default:          break;
				}
			}
			return FAppStyle::Get().GetBrush("GraphEditor.EventGraph_16x");
		};


	for (const auto& Pair : Suggestions)
	{
		const UBlueprint* BP = Pair.Key;
		if (!BP || Pair.Value.Num() == 0) continue;

		TSharedPtr<FResultTreeItem> Group = MakeShared<FResultTreeItem>();
		Group->bIsGroup = true;
		Group->Severity = EItemSeverity::Group;
		Group->Target = BP;
		Group->Icon = FSlateIconFinder::FindIconBrushForClass(UBlueprint::StaticClass());

		TMap<const UEdGraph*, TSharedPtr<FResultTreeItem>> GraphGroups;
		auto GetGraphGroup = [&GraphGroups, &GetGraphBrush](const UEdGraph* G)
			{
				TSharedPtr<FResultTreeItem>& Slot = GraphGroups.FindOrAdd(G);
				if (!Slot.IsValid())
				{
					Slot = MakeShared<FResultTreeItem>();
					Slot->bIsGroup = true;
					Slot->Severity = EItemSeverity::Group;
					Slot->Target = G;
					Slot->Label = G->GetName();
					Slot->Icon = GetGraphBrush(G);
				}
				return Slot;
			};

		int32 Count = 0;
		for (const FSuggestionContext& S : Pair.Value)
		{
			if (!Settings->IsSuggestionEnabled(S.Suggestion))
			{
				continue;
			}

			if (S.Scope == ESuggestionScope::Node && Settings->IsSuppressed(BP, { S.NodeGuid }, S.Suggestion))
			{
				continue;
			}

			if (S.Scope == ESuggestionScope::Graph && Settings->IsSuppressed(BP, { S.Graph->GraphGuid }, S.Suggestion))
			{
				continue;
			}

			const FSuggestionInfo& Info = GetSuggestionInfo(S.Suggestion);
			TSharedPtr<FResultTreeItem> Leaf = MakeShared<FResultTreeItem>();
			TSharedPtr<FResultTreeItem> Parent = Group;
			FStringFormatNamedArguments Args;

			switch (S.Scope)
			{
			case ESuggestionScope::Node:
			{
				const UEdGraphNode* Node = BPCheck::Editor::ResolveNode(BP, S.NodeGuid);
				if (!Node) continue;
				Leaf->Target = Node;
				Args.Add(TEXT("Node"), FStringFormatArg(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
				Parent = GetGraphGroup(Node->GetGraph());
				break;
			}
			case ESuggestionScope::Graph:
			{
				const UEdGraph* Graph = S.Graph.Get();
				if (!Graph) continue;
				Leaf->Target = Graph;
				Args.Add(TEXT("Graph"), FStringFormatArg(Graph->GetName()));
				Parent = GetGraphGroup(Graph);
				break;
			}
			default:
				Leaf->Target = BP;
				break;
			}

			Args.Add(TEXT("Blueprint"), FStringFormatArg(BP->GetName()));
			Args.Add(TEXT("Variable"), FStringFormatArg(S.Subject));
			// TODO(Seb): These, but maybe with a different approach?
			// Args.Add(TEXT("Value"), FStringFormatArg(S.Value));
			// Args.Add(TEXT("Limit"), FStringFormatArg(S.Limit));

			Leaf->Label = Info.SuggestionCode;
			Leaf->Description = FString::Format(*Info.SuggestionDescription, Args);
			Leaf->Severity = EItemSeverity::Warning;

			Leaf->Blueprint = BP;
			Leaf->Suggestion = S.Suggestion;
			Leaf->NodeGuid = S.NodeGuid;
			if (S.Graph.IsValid())
			{
				Leaf->GraphGuid = S.Graph->GraphGuid;
			}
			Leaf->Scope = S.Scope;

			Parent->Children.Add(Leaf);
			++Count;
		}

		if (Count == 0) continue;

		TArray<TSharedPtr<FResultTreeItem>> GraphItems;
		GraphGroups.GenerateValueArray(GraphItems);
		GraphItems.Sort([](const TSharedPtr<FResultTreeItem>& A, const TSharedPtr<FResultTreeItem>& B)
			{
				return A->Label < B->Label;
			});
		for (const TSharedPtr<FResultTreeItem>& G : GraphItems)
		{
			G->Label = FString::Printf(TEXT("%s (%d)"), *G->Label, G->Children.Num());
			Group->Children.Add(G);
		}

		Group->Label = FString::Printf(TEXT("%s (%d)"), *BP->GetName(), Count);
		Roots.Add(Group);
	}
	return Roots;
}

void FBPCheckEditorModule::ApplyNodeMarkers()
{
	TSet<UEdGraph*> Dirtied;
	const UBPCheckSettings* Settings = GetDefault<UBPCheckSettings>();

	for (const auto& Pair : Suggestions)
	{
		const UBlueprint* BP = Pair.Key;
		if (!BP) continue;

		for (const auto& Suggestion : Pair.Value)
		{
			UEdGraphNode* Node = BPCheck::Editor::ResolveNode(BP, Suggestion.NodeGuid);

			if (!Node) continue;

			if (Settings->IsSuggestionEnabled(Suggestion.Suggestion) && !Settings->IsSuppressed(BP, { Suggestion.NodeGuid }, Suggestion.Suggestion))
			{
				const FSuggestionInfo& Info = GetSuggestionInfo(Suggestion.Suggestion);
				FStringFormatNamedArguments Args;
				Args.Add(TEXT("Node"), FStringFormatArg(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
				Args.Add(TEXT("Variable"), FStringFormatArg(Suggestion.Subject));

				const FString Desc = FString::Format(*Info.SuggestionDescription, Args);

				Node->bHasCompilerMessage = true;
				Node->ErrorType = static_cast<int32>(EMessageSeverity::Info);
				Node->ErrorMsg = FString::Printf(TEXT("BPCheck: %s\n%s"), *Info.SuggestionCode, *Desc);
				Dirtied.Add(Node->GetGraph());
			}
			else if (Node->ErrorMsg.StartsWith(TEXT("BPCheck:")))
			{
				Node->bHasCompilerMessage = false;
				Node->ErrorMsg.Empty();
				Dirtied.Add(Node->GetGraph());
			}
		}
	}

	for (UEdGraph* Graph : Dirtied)
	{
		Graph->NotifyGraphChanged();
	}
}

void FBPCheckEditorModule::BuildNodeSection(UToolMenu* InMenu)
{
	if (InMenu->FindSection("BPCheckSection")) return;

	const UGraphNodeContextMenuContext* Ctx = InMenu->FindContext<UGraphNodeContextMenuContext>();
	if (!Ctx || !Ctx->Node) return;

	const UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(Ctx->Node);
	if (!BP) return;

	const FGuid NodeGuid = Ctx->Node->NodeGuid;
	// Suggestions
	TArray<FSuggestionContext> SuggestionsForNode = GetSuggestionsForNode(NodeGuid);

	FToolMenuSection& Section = InMenu->AddSection("BPCheckSection", INVTEXT("BPCheck"));

	UBPCheckSettings* Settings = GetMutableDefault<UBPCheckSettings>();

	for (auto& Suggestion : SuggestionsForNode)
	{
		auto& Info = GetSuggestionInfo(Suggestion.Suggestion);

		Section.AddSubMenu(
			FName(*FString::Printf(TEXT("BPCheck_%s"), *Info.SuggestionCode)),
			FText::FromString(StaticEnum<ESuggestion>()->GetDisplayNameTextByValue(static_cast<int64>(Suggestion.Suggestion)).ToString()),
			FText::GetEmpty(),
			FNewToolMenuDelegate::CreateLambda([BPPath = FSoftObjectPath(BP), NodeGuid, BP, Suggestion, Settings](UToolMenu* Sub)
				{
					FToolMenuSection& S = Sub->AddSection("Actions");
					S.AddMenuEntry("SilenceThis", INVTEXT("Silence for this node"), INVTEXT("Silence suggestion for this node"),
						FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([=]()
							{
								Settings->Suppress(BP, { NodeGuid }, Suggestion.Suggestion);
							})));
					S.AddMenuEntry("SilenceForBlueprint", INVTEXT("Silence for this Blueprint"), INVTEXT("Silence suggestion for this Blueprint"),
						FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([=]()
							{
								Settings->Suppress(BP, {}, Suggestion.Suggestion);
							})));
					S.AddMenuEntry("SilenceForProject", INVTEXT("Silence globally"), INVTEXT("Silence suggestion for project"),
						FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([=]()
							{
								Settings->DisableSuggestion(Suggestion.Suggestion);
							})));
				})
		);
	}
}

TArray<FSuggestionContext> FBPCheckEditorModule::GetSuggestionsForNode(FGuid NodeGuid)
{
	TArray<FSuggestionContext> Out;
	for (auto& Entry : Suggestions)
	{
		for (auto& Value : Entry.Value)
		{
			if (Value.NodeGuid == NodeGuid)
			{
				Out.Push(Value);
			}
		}
	}

	return Out;
}

void SBPCheckSuggestionsPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SAssignNew(TreeView, STreeView<TSharedPtr<FResultTreeItem>>)
				.TreeItemsSource(&RootNodes)
				.OnGenerateRow(this, &SBPCheckSuggestionsPanel::OnGenerateRow)
				.OnGetChildren(this, &SBPCheckSuggestionsPanel::OnGetChildren)
				.SelectionMode(ESelectionMode::Single)
				.OnMouseButtonDoubleClick(this, &SBPCheckSuggestionsPanel::OnItemDoubleClicked)
				.OnContextMenuOpening(this, &SBPCheckSuggestionsPanel::OnContextMenuOpening)
				.HeaderRow(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(ColumnIds::Severity)
					.DefaultLabel(FText::GetEmpty()).FixedWidth(28.0f)
					+ SHeaderRow::Column(ColumnIds::Code)
					.DefaultLabel(LOCTEXT("ColCode", "Code")).FillWidth(0.25f)
					+ SHeaderRow::Column(ColumnIds::Description)
					.DefaultLabel(LOCTEXT("ColDesc", "Description")).FillWidth(0.75f)
				)
		];
}

void SBPCheckSuggestionsPanel::SetResults(TArray<TSharedPtr<FResultTreeItem>> InNodes)
{
	RootNodes = MoveTemp(InNodes);

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();

		for (const TSharedPtr<FResultTreeItem>& Node : RootNodes)
			TreeView->SetItemExpansion(Node, true);
	}
}

TSharedRef<ITableRow> SBPCheckSuggestionsPanel::OnGenerateRow(TSharedPtr<FResultTreeItem> Node, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(SSuggestionRow, Owner).Item(Node);
}

void SBPCheckSuggestionsPanel::OnGetChildren(TSharedPtr<FResultTreeItem> Node, TArray<TSharedPtr<FResultTreeItem>>& OutChildren)
{
	if (Node.IsValid())
	{
		OutChildren = Node->Children;
	}
}

TSharedPtr<SWidget> SBPCheckSuggestionsPanel::OnContextMenuOpening()
{
	auto Selected = TreeView->GetSelectedItems();
	if (Selected.Num() == 0) return nullptr;

	const bool bAllLeaves = Algo::AllOf(Selected, [](const TSharedPtr<FResultTreeItem>& I) { return !I->bIsGroup; });

	FMenuBuilder Menu(true, nullptr);
	Menu.BeginSection("BPCheckActions", INVTEXT("BPCheck"));

	UBPCheckSettings* Settings = GetMutableDefault<UBPCheckSettings>();

	if (Selected.Num() == 1)
	{
		TSharedPtr<FResultTreeItem> Item = Selected[0];

		if (Item->Scope == ESuggestionScope::Node)
		{
			Menu.AddMenuEntry(INVTEXT("Silence for this node"), INVTEXT("Silence suggestion for this node"),
				FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([=]()
					{
						Settings->Suppress(Item->Blueprint.Get(), { Item->NodeGuid }, Item->Suggestion);
					})));
		}
		else if (Item->Scope == ESuggestionScope::Graph)
		{
			Menu.AddMenuEntry(INVTEXT("Silence for this graph"), INVTEXT("Silence suggestion for this graph"),
				FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([=]()
					{
						Settings->Suppress(Item->Blueprint.Get(), { Item->GraphGuid }, Item->Suggestion);
					})));
		}

		Menu.AddMenuEntry(INVTEXT("Silence for this Blueprint"), INVTEXT("Silence suggestion for this Blueprint"),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([=]()
				{
					Settings->Suppress(Item->Blueprint.Get(), {}, Item->Suggestion);
				})));

		Menu.AddMenuEntry(INVTEXT("Silence globally"), INVTEXT("Silence suggestion for project"),
			FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([=]()
				{
					Settings->DisableSuggestion(Item->Suggestion);
				})));
	}

	Menu.EndSection();

	return Menu.MakeWidget();

}

void SBPCheckSuggestionsPanel::OnItemDoubleClicked(TSharedPtr<FResultTreeItem> Item)
{
	if (!Item.IsValid()) return;

	const UObject* Target = Item->Target.Get();
	if (!Target) return;

	if (const UBlueprint* BP = Cast<UBlueprint>(Target))
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(const_cast<UBlueprint*>(BP));
	}

	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Target);
}

void UBPCompilerExtension::ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	if (!CompilationContext.Blueprint->GetPackage()->GetName().StartsWith("/Game/"))
	{
		// This skips analysis of any engine side Blueprints
		// TODO(Seb): This also skips all plugins, which might be an issue
		return;
	}

	TArray<FGuid> AliveNodes;
	TArray<UEdGraph*> Graphs;
	CompilationContext.Blueprint->GetAllGraphs(Graphs);
	for (auto Graph : Graphs)
	{
		for (auto Node : Graph->Nodes)
		{
			AliveNodes.Push(Node->NodeGuid);
		}
	}

	double Seconds = 0.0;
	FAnalyzerResult Result;
	{
		FScopedDurationTimer Timer(Seconds);
		FAnalyzer Analyzer;
		Result = Analyzer.Analyze(CompilationContext, Data);
	}

	UBPCheckSettings* Settings = GetMutableDefault<UBPCheckSettings>();

	for (auto& Suggestion : Result.Suggestions)
	{
		if (!Settings->IsSuggestionEnabled(Suggestion.Suggestion) || Settings->IsSuppressed(CompilationContext.Blueprint, { Suggestion.NodeGuid }, Suggestion.Suggestion))
		{
			continue;
		}

		const FSuggestionInfo& Info = GetSuggestionInfo(Suggestion.Suggestion);

		if (Suggestion.Scope == ESuggestionScope::Graph)
		{
			TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning);
			Msg->AddToken(FTextToken::Create(FText::FromString(Info.SuggestionCode)));

			FStringFormatNamedArguments Args;
			Args.Add(TEXT("Graph"), FStringFormatArg(Suggestion.Graph->GetName()));
			Args.Add(TEXT("Blueprint"), FStringFormatArg(CompilationContext.Blueprint->GetName()));

			FString Description = FString::Format(*Info.SuggestionDescription, Args);

			Msg->AddToken(FTextToken::Create(FText::FromString(Description)));

			FMessageLog("BPCheck").AddMessage(Msg);
		}
		else if (Suggestion.Scope == ESuggestionScope::Blueprint)
		{
			TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning);
			Msg->AddToken(FTextToken::Create(FText::FromString(Info.SuggestionCode)));

			FStringFormatNamedArguments Args;
			Args.Add(TEXT("Blueprint"), FStringFormatArg(CompilationContext.Blueprint->GetName()));
			Args.Add(TEXT("Variable"), FStringFormatArg(Suggestion.Subject));

			FString Description = FString::Format(*Info.SuggestionDescription, Args);

			Msg->AddToken(FTextToken::Create(FText::FromString(Description)));

			FMessageLog("BPCheck").AddMessage(Msg);
		}
		else if (Suggestion.Scope == ESuggestionScope::Node)
		{
			UEdGraphNode* Node = BPCheck::Editor::ResolveNode(CompilationContext.Blueprint, Suggestion.NodeGuid);

			if (!Node)
			{
				UE_LOG(LogBPCheckEditor, Warning, TEXT("Could not resolve node."));
				return;
			}

			TSharedRef<FTokenizedMessage> Msg = FTokenizedMessage::Create(EMessageSeverity::Warning);
			Msg->AddToken(FTextToken::Create(FText::FromString(Info.SuggestionCode)));

			// Note(Seb): I wasn't able to make it work without adding the Callback, which forces the open in the node editor...
			Msg->AddToken(FUObjectToken::Create(Node, Node->GetNodeTitle(ENodeTitleType::ListView))->OnMessageTokenActivated(
				FOnMessageTokenActivated::CreateLambda([](const TSharedRef<IMessageToken>& Token)
					{
						const TSharedRef<FUObjectToken> Obj = StaticCastSharedRef<FUObjectToken>(Token);
						if (UObject* Resolved = Obj->GetObject().Get())
							FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Resolved);
					})));

			FStringFormatNamedArguments Args;
			Args.Add(TEXT("Node"), FStringFormatArg(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
			FString Description = FString::Format(*Info.SuggestionDescription, Args);

			Msg->AddToken(FTextToken::Create(FText::FromString(Description)));

			Node->bHasCompilerMessage = true;
			Node->ErrorType = static_cast<int32>(EMessageSeverity::Info);
			Node->ErrorMsg = FString::Printf(TEXT("BPCheck: %s\n%s"), *Info.SuggestionCode, *Description);

			FMessageLog("BPCheck").AddMessage(Msg);
		}
	}

	Settings->PruneDeadNodes(CompilationContext.Blueprint, AliveNodes);
	FModuleManager::GetModuleChecked<FBPCheckEditorModule>("BPCheckEditor").SetSuggestionsForBlueprint(CompilationContext.Blueprint, Result.Suggestions);

	Super::ProcessBlueprintCompiled(CompilationContext, Data);
}

// Run using "unreal_bin" "project_path" -run=BPCheck -NoShaderCompile -AllowStdOutLogVerbosity 2>&1 | Select-String "BPCheck"
int32 UBPCheckCommandlet::Main(const FString& String)
{
	UE_LOG(LogBPCheckEditor, Log, TEXT("Running BPCheck Commandlet."));

	auto& AssetRegistryMod = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	auto& AssetRegistry = AssetRegistryMod.Get();

	TArray<FAssetData> FoundAssets;
	AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), FoundAssets);

	TArray<FAnalyzerResult> Results;

	for (const auto& AssetData : FoundAssets)
	{
		if (!AssetData.GetPackage()->GetName().StartsWith("/Game/"))
		{
			// Ditto with the TODO above
			continue;
		}

		UBlueprint* AssetObject = Cast<UBlueprint>(AssetData.GetAsset());

		if (!AssetObject)
		{
			continue;
		}

		FAnalyzer Analyzer;
		Results.Push(Analyzer.Analyze(AssetObject));
	}

	FAnalyzerReport Report;
	Report.Results = MoveTemp(Results);

	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(FJsonObjectConverter::UStructToJsonObject(Report).ToSharedRef(), Writer);
	UE_LOG(LogBPCheckEditor, Log, TEXT("%s"), *Json);

	return 0;
}

FBPCheckSettingsCustomization::~FBPCheckSettingsCustomization()
{
	if (SettingsHandle.IsValid() && UObjectInitialized())
	{
		GetMutableDefault<UBPCheckSettings>()->OnSettingsChanged.Remove(SettingsHandle);
	}
}

// Note(Seb): I did use Claude to help me with designing the layout
void FBPCheckSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& B)
{
	View = B.GetDetailsViewSharedPtr();
	if (!SettingsHandle.IsValid())
	{
		SettingsHandle = GetMutableDefault<UBPCheckSettings>()->OnSettingsChanged.AddLambda(
			[WeakView = View]()
			{
				// Deferred so that it doesn't happen mid-broadcast
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
					[WeakView](float)
					{
						if (TSharedPtr<IDetailsView> V = WeakView.Pin())
						{
							V->ForceRefresh();
						}
						return false;
					}));
			});
	}

	TSharedRef<IPropertyHandle> MapHandle =
		B.GetProperty(GET_MEMBER_NAME_CHECKED(UBPCheckSettings, SuggestionEnabled));
	B.HideProperty(MapHandle);

	const UEnum* Enum = StaticEnum<ESuggestion>();

	auto GetNodeLabel = [](const FSuppression& S) -> FText
		{
			if (const UBlueprint* BP = Cast<UBlueprint>(S.Blueprint.ResolveObject()))
			{
				if (const UEdGraphNode* N = BPCheck::Editor::ResolveNode(BP, S.TargetGuid.GetValue()))
				{
					return N->GetNodeTitle(ENodeTitleType::ListView);
				}

				if (const UEdGraph* G = BPCheck::Editor::ResolveGraph(BP, S.TargetGuid.GetValue()))
				{
					return FText::FromString(G->GetName());
				}
			}
			return FText::FromString(S.TargetGuid.GetValue().ToString().Left(8));
		};

	auto GetRuleName = [](ESuggestion Suggestion) -> FString
		{
			return FString::Printf(TEXT("%s (%s)"),
				*StaticEnum<ESuggestion>()->GetDisplayNameTextByValue(static_cast<int64>(Suggestion)).ToString(),
				*GetSuggestionInfo(Suggestion).SuggestionCode);
		};

	auto AddRuleRow = [GetRuleName](IDetailGroup& Group, ESuggestion Suggestion, const FText& Scope, bool bSubdued, FSimpleDelegate OnRemove, const FText& RemoveTip)
		{
			const FText Name = FText::FromString(GetRuleName(Suggestion));

			Group.AddWidgetRow()
				.FilterString(Name)
				.NameContent()
				[
					SNew(STextBlock).Text(Name)
				]
				.ValueContent().HAlign(HAlign_Fill)
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(Scope)
								.ColorAndOpacity(bSubdued ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground())
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							PropertyCustomizationHelpers::MakeDeleteButton(OnRemove, RemoveTip)
						]
				];
		};

	auto RemoveSuppression = [](const FSuppression& S)
		{
			return FSimpleDelegate::CreateLambda([S]()
				{
					GetMutableDefault<UBPCheckSettings>()->Unsuppress(S);
				});
		};

	const UBPCheckSettings* Settings = GetDefault<UBPCheckSettings>();
	IDetailCategoryBuilder& SupCat = B.EditCategory("Suppressions");

	TArray<ESuggestion> Muted;
	TArray<ESuggestion> Active;
	for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
	{
		const ESuggestion S = static_cast<ESuggestion>(Enum->GetValueByIndex(i));
		(Settings->IsSuggestionEnabled(S) ? Active : Muted).Add(S);
	}

	const bool bAnything = !Muted.IsEmpty() || !Settings->Suppressions.IsEmpty();

	SupCat.HeaderContent(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			SNew(SComboButton)
				.HasDownArrow(true)
				.IsEnabled(!Active.IsEmpty())
				.ButtonContent()[SNew(STextBlock).Text(INVTEXT("Mute rule"))]
				.OnGetMenuContent_Lambda([Active, GetRuleName]() -> TSharedRef<SWidget>
					{
						FMenuBuilder Menu(true, nullptr);
						for (ESuggestion S : Active)
						{
							Menu.AddMenuEntry(
								FText::FromString(GetRuleName(S)),
								FText::FromString(GetSuggestionInfo(S).SuggestionDescription),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([S]()
									{
										GetMutableDefault<UBPCheckSettings>()->SetSuggestionEnabled(S, false);
									})));
						}
						return Menu.MakeWidget();
					})
		]
	+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
				.Text(INVTEXT("Clear all"))
				.IsEnabled(bAnything)
				.OnClicked_Lambda([]()
					{
						GetMutableDefault<UBPCheckSettings>()->ClearAll();
						return FReply::Handled();
					})
		]);

	if (!bAnything)
	{
		SupCat.AddCustomRow(INVTEXT("none"))
			.WholeRowContent()[SNew(STextBlock).Text(INVTEXT("No suppressions"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())];
		return;
	}

	TArray<FSuppression> ProjectWide;
	TMap<FSoftObjectPath, TArray<FSuppression>> ByBlueprint;
	for (const FSuppression& S : Settings->Suppressions)
	{
		if (S.Blueprint.IsNull()) ProjectWide.Add(S);
		else                      ByBlueprint.FindOrAdd(S.Blueprint).Add(S);
	}
	ByBlueprint.KeySort([](const FSoftObjectPath& L, const FSoftObjectPath& R)
		{
			return L.LexicalLess(R);
		});

	const int32 ProjectWideCount = Muted.Num() + ProjectWide.Num();
	if (ProjectWideCount > 0)
	{
		IDetailGroup& G = SupCat.AddGroup("ProjectWide",
			FText::FromString(FString::Printf(TEXT("Project-wide (%d)"), ProjectWideCount)), false, true);

		for (ESuggestion S : Muted)
		{
			AddRuleRow(G, S, INVTEXT("Rule disabled"), true,
				FSimpleDelegate::CreateLambda([S]()
					{
						GetMutableDefault<UBPCheckSettings>()->SetSuggestionEnabled(S, true);
					}),
				INVTEXT("Re-enable rule"));
		}
		for (const FSuppression& S : ProjectWide)
		{
			AddRuleRow(G, S.Suggestion, INVTEXT("All Blueprints"), true, RemoveSuppression(S), INVTEXT("Remove suppression"));
		}
	}

	for (const auto& Pair : ByBlueprint)
	{
		TArray<const FSuppression*> BlueprintWide, Nodes;
		for (const FSuppression& S : Pair.Value)
		{
			(S.TargetGuid.IsSet() ? Nodes : BlueprintWide).Add(&S);
		}

		IDetailGroup& Group = SupCat.AddGroup(
			FName(*Pair.Key.ToString()),
			FText::FromString(FString::Printf(TEXT("%s (%d)"), *Pair.Key.GetAssetName(), Pair.Value.Num())),
			false, true);

		for (const FSuppression* S : BlueprintWide)
		{
			AddRuleRow(Group, S->Suggestion, INVTEXT("Entire Blueprint"), true, RemoveSuppression(*S), INVTEXT("Remove suppression"));
		}

		if (!Nodes.IsEmpty())
		{
			IDetailGroup& NodeGroup = Group.AddGroup("Nodes",
				FText::FromString(FString::Printf(TEXT("Nodes (%d)"), Nodes.Num())), true);
			for (const FSuppression* S : Nodes)
			{
				AddRuleRow(NodeGroup, S->Suggestion, GetNodeLabel(*S), false, RemoveSuppression(*S), INVTEXT("Remove suppression"));
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBPCheckEditorModule, BPCheckEditor)
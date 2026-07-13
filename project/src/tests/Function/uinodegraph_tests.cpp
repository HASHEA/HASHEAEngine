#include "doctest.h"

#include "Function/Gui/UINodeGraph.h"

TEST_CASE("UINodeGraphModel builds valid output to input links")
{
	AshEngine::UINodeGraphModel graph{};

	AshEngine::UINodeGraphNode source{};
	source.id = 1;
	source.outputPins.push_back(AshEngine::UINodeGraphPin{ 10, AshEngine::UINodePinKind::Output, "Out" });
	graph.nodes.push_back(source);

	AshEngine::UINodeGraphNode target{};
	target.id = 2;
	target.inputPins.push_back(AshEngine::UINodeGraphPin{ 20, AshEngine::UINodePinKind::Input, "In" });
	graph.nodes.push_back(target);

	AshEngine::UINodeGraphLinkPins pins{};
	CHECK(graph.TryBuildLink(10, 20, pins));
	CHECK(pins.outputPin == 10);
	CHECK(pins.inputPin == 20);

	CHECK(graph.ConnectPins(42, 10, 20));
	REQUIRE(graph.links.size() == 1);
	CHECK(graph.links[0].id == 42);
	CHECK(graph.links[0].startPin == 10);
	CHECK(graph.links[0].endPin == 20);
}

TEST_CASE("UINodeGraphModel keeps input pins single-linked by default")
{
	AshEngine::UINodeGraphModel graph{};

	AshEngine::UINodeGraphNode sourceA{};
	sourceA.id = 1;
	sourceA.outputPins.push_back(AshEngine::UINodeGraphPin{ 10, AshEngine::UINodePinKind::Output, "OutA" });
	graph.nodes.push_back(sourceA);

	AshEngine::UINodeGraphNode sourceB{};
	sourceB.id = 2;
	sourceB.outputPins.push_back(AshEngine::UINodeGraphPin{ 11, AshEngine::UINodePinKind::Output, "OutB" });
	graph.nodes.push_back(sourceB);

	AshEngine::UINodeGraphNode target{};
	target.id = 3;
	target.inputPins.push_back(AshEngine::UINodeGraphPin{ 20, AshEngine::UINodePinKind::Input, "In" });
	graph.nodes.push_back(target);

	CHECK(graph.ConnectPins(100, 10, 20));
	CHECK(graph.ConnectPins(101, 11, 20));

	REQUIRE(graph.links.size() == 1);
	CHECK(graph.links[0].id == 101);
	CHECK(graph.links[0].startPin == 11);
	CHECK(graph.links[0].endPin == 20);
}

TEST_CASE("UINodeGraphModel removes connected links when deleting a node")
{
	AshEngine::UINodeGraphModel graph{};

	AshEngine::UINodeGraphNode source{};
	source.id = 1;
	source.outputPins.push_back(AshEngine::UINodeGraphPin{ 10, AshEngine::UINodePinKind::Output, "Out" });
	graph.nodes.push_back(source);

	AshEngine::UINodeGraphNode target{};
	target.id = 2;
	target.inputPins.push_back(AshEngine::UINodeGraphPin{ 20, AshEngine::UINodePinKind::Input, "In" });
	graph.nodes.push_back(target);

	CHECK(graph.ConnectPins(100, 10, 20));
	CHECK(graph.RemoveNode(1));

	CHECK(graph.nodes.size() == 1);
	CHECK(graph.nodes[0].id == 2);
	CHECK(graph.links.empty());
}

TEST_CASE("UINodeGraphModel can break links connected to a node without deleting it")
{
	AshEngine::UINodeGraphModel graph{};

	AshEngine::UINodeGraphNode source{};
	source.id = 1;
	source.outputPins.push_back(AshEngine::UINodeGraphPin{ 10, AshEngine::UINodePinKind::Output, "Out" });
	graph.nodes.push_back(source);

	AshEngine::UINodeGraphNode target{};
	target.id = 2;
	target.inputPins.push_back(AshEngine::UINodeGraphPin{ 20, AshEngine::UINodePinKind::Input, "In" });
	graph.nodes.push_back(target);

	CHECK(graph.ConnectPins(100, 10, 20));
	CHECK(graph.RemoveLinksConnectedToNode(1));

	CHECK(graph.nodes.size() == 2);
	CHECK(graph.links.empty());
}

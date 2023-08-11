/*
    This file is part of solidity.
    solidity is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    solidity is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * Unit tests for the algorithm to find dominators from a graph.
 */
#include <libyul/backends/evm/Dominator.h>

#include <test/libsolidity/util/SoltestErrors.h>

#include <boost/test/unit_test.hpp>

using namespace solidity::yul;

namespace solidity::yul::test
{

struct ImmediateDominatorTest
{
	struct Vertex {
		std::string data;
		std::vector<Vertex*> successors;

		bool operator<(Vertex const& _other) const
		{
			return data < _other.data;
		}
	};

	typedef std::pair<std::string, std::string> edge;

	struct ForEachVertexSuccessorTest {
		template<typename Callable>
		void operator()(Vertex _v, Callable&& _callable) const
		{
			for (auto const& w: _v.successors)
				_callable(*w);
		}
	};

	size_t numVertices;
	Vertex* entry;
	std::map<std::string, Vertex*> vertices;
	std::vector<size_t> expectedIdom;
	std::map<std::string, size_t> expectedDFSIndices;
};

class DominatorFixture
{
	typedef ImmediateDominatorTest::Vertex Vertex;
protected:
	static ImmediateDominatorTest const* generateGraph(
		std::vector<std::string> _vertices,
		std::vector<ImmediateDominatorTest::edge> _edges,
		std::vector<size_t> _expectedIdom,
		std::map<std::string, size_t> _expectedDFSIndices
	)
	{
		soltestAssert(_edges.size() > 0);

		ImmediateDominatorTest* graph = new ImmediateDominatorTest();
		for (std::string v: _vertices)
			graph->vertices.insert(make_pair(v, new Vertex{v, std::vector<Vertex*>{}}));
		graph->entry = graph->vertices[_vertices[0]];

		soltestAssert(_vertices.size() > 0 && _vertices.size() == graph->vertices.size());

		graph->numVertices = _vertices.size();
		for (auto const& [from, to]: _edges)
			graph->vertices[from]->successors.push_back(graph->vertices[to]);

		graph->expectedIdom = _expectedIdom;
		graph->expectedDFSIndices = _expectedDFSIndices;
		return graph;
	}
};

BOOST_AUTO_TEST_SUITE(Dominators)

BOOST_FIXTURE_TEST_CASE(immediate_dominator, DominatorFixture)
{
	typedef ImmediateDominatorTest::edge edge;
	std::vector<ImmediateDominatorTest const*> inputGraph(9);

	//            A
	//            │
	//            ▼
	//        ┌───B
	//        │   │
	//        ▼   │
	//        C ──┼───┐
	//        │   │   │
	//        ▼   │   ▼
	//        D◄──┘   G
	//        │       │
	//        ▼       ▼
	//        E       H
	//        │       │
	//        └──►F◄──┘
	inputGraph[0] = generateGraph(
		{ "A", "B", "C", "D", "E", "F", "G", "H" },
		{
			edge("A", "B"),
			edge("B", "C"),
			edge("B", "D"),
			edge("C", "D"),
			edge("C", "G"),
			edge("D", "E"),
			edge("E", "F"),
			edge("G", "H"),
			edge("H", "F")
		},
		{0, 0, 1, 1, 3, 1, 2, 6},
		{
			{"A", 0},
			{"B", 1},
			{"C", 2},
			{"D", 3},
			{"E", 4},
			{"F", 5},
			{"G", 6},
			{"H", 7}
		}
	);

	//    ┌────►A──────┐
	//    │     │      ▼
	//    │ B◄──┘   ┌──D──┐
	//    │ │       │     │
	//    │ ▼       ▼     ▼
	//    └─C◄───┐  E     F
	//      │    │  │     │
	//      └───►G◄─┴─────┘
	inputGraph[1] = generateGraph(
		{ "A", "B", "C", "D", "E", "F", "G" },
		{
			edge("A", "B"),
			edge("B", "C"),
			edge("C", "G"),
			edge("C", "A"),
			edge("A", "D"),
			edge("D", "E"),
			edge("D", "F"),
			edge("E", "G"),
			edge("F", "G"),
			edge("G", "C")
		},
		{0, 0, 0, 0, 0, 4, 4},
		{
			{"A", 0},
			{"B", 1},
			{"C", 2},
			{"G", 3},
			{"D", 4},
			{"E", 5},
			{"F", 6}
		}
	);

	//    ┌─────────┐
	//    │         ▼
	//    │     ┌───A───┐
	//    │     │       │
	//    │     ▼       ▼
	//    │ ┌──►C◄───── B──┬──────┐
	//    │ │   │       ▲  │      │
	//    │ │   │  ┌────┘  │      │
	//    │ │   ▼  │       ▼      ▼
	//    │ │   D──┘  ┌───►E◄─────I
	//    │ │   ▲     │    │      │
	//    │ │   │     │    ├───┐  │
	//    │ │   │     │    │   │  │
	//    │ │   │     │    ▼   │  ▼
	//    │ └───┼─────┼────F   └─►H
	//    │     │     │    │      │
	//    │     │     │    │      │
	//    │     │     │    │      │
	//    │     └─────┴─G◄─┴──────┘
	//    │             │
	//    └─────────────┘
	inputGraph[2] = generateGraph(
		{ "A", "B", "C", "D", "E", "F", "G", "H", "I" },
		{
			edge("A", "B"),
			edge("A", "C"),
			edge("B", "C"),
			edge("B", "I"),
			edge("B", "E"),
			edge("C", "D"),
			edge("D", "B"),
			edge("E", "H"),
			edge("E", "F"),
			edge("F", "G"),
			edge("F", "C"),
			edge("G", "E"),
			edge("G", "A"),
			edge("G", "D"),
			edge("H", "G"),
			edge("I", "E"),
			edge("I", "H")
		},
		{0, 0, 0, 0, 1, 1, 1, 1, 5},
		{
			{"A", 0},
			{"B", 1},
			{"C", 2},
			{"D", 3},
			{"I", 4},
			{"E", 5},
			{"H", 6},
			{"G", 7},
			{"F", 8}
		}
	);

	// T. Lengauer and R. E. Tarjan pg. 122 fig. 1
	// ref: https://www.cs.princeton.edu/courses/archive/spr03/cs423/download/dominators.pdf
	inputGraph[3] = generateGraph(
		{ "R", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "L", "K" },
		{
			edge("R", "B"),
			edge("R", "A"),
			edge("R", "C"),
			edge("B", "A"),
			edge("B", "D"),
			edge("B", "E"),
			edge("A", "D"),
			edge("D", "L"),
			edge("L", "H"),
			edge("E", "H"),
			edge("H", "E"),
			edge("H", "K"),
			edge("K", "I"),
			edge("K", "R"),
			edge("C", "F"),
			edge("C", "G"),
			edge("F", "I"),
			edge("G", "I"),
			edge("G", "J"),
			edge("J", "I"),
			edge("I", "K"),
		},
		{0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 9, 9, 11},
		{
			{"R", 0},
			{"B", 1},
			{"A", 2},
			{"D", 3},
			{"L", 4},
			{"H", 5},
			{"E", 6},
			{"K", 7},
			{"I", 8},
			{"C", 9},
			{"F", 10},
			{"G", 11},
			{"J", 12}
		}
	);

	// Extracted from Loukas Georgiadis Dissertation - Linear-Time Algorithms for Dominators and Related Problems
	// pg. 12 Fig. 2.2
	// ref: https://www.cs.princeton.edu/techreports/2005/737.pdf
	inputGraph[4] = generateGraph(
		{ "R", "W", "X1", "X2", "X3", "X4", "X5", "X6", "X7", "Y" },
		{
			edge("R", "W"),
			edge("R", "Y"),
			edge("W", "X1"),
			edge("Y", "X7"),
			edge("X1", "X2"),
			edge("X2", "X1"),
			edge("X2", "X3"),
			edge("X3", "X2"),
			edge("X3", "X4"),
			edge("X4", "X3"),
			edge("X4", "X5"),
			edge("X5", "X4"),
			edge("X5", "X6"),
			edge("X6", "X5"),
			edge("X6", "X7"),
			edge("X7", "X6")
		},
		{0, 0, 0, 0, 0, 0, 0, 0, 0 , 0},
		{
			{"R", 0},
			{"W", 1},
			{"X1", 2},
			{"X2", 3},
			{"X3", 4},
			{"X4", 5},
			{"X5", 6},
			{"X6", 7},
			{"X7", 8},
			{"Y", 9}
		}
	);

	// Worst-case families for k = 3
	// Example itworst(3) pg. 26 fig. 2.9
	// ref: https://www.cs.princeton.edu/techreports/2005/737.pdf
	inputGraph[5] = generateGraph(
		{ "R", "W1", "W2", "W3", "X1", "X2", "X3", "Y1", "Y2", "Y3", "Z1", "Z2", "Z3" },
		{
			edge("R", "W1"),
			edge("R", "X1"),
			edge("R", "Z3"),
			edge("W1", "W2"),
			edge("W2", "W3"),
			edge("X1", "X2"),
			edge("X2", "X3"),
			edge("X3", "Y1"),
			edge("Y1", "W1"),
			edge("Y1", "W2"),
			edge("Y1", "W3"),
			edge("Y1", "Y2"),
			edge("Y2", "W1"),
			edge("Y2", "W2"),
			edge("Y2", "W3"),
			edge("Y2", "Y3"),
			edge("Y3", "W1"),
			edge("Y3", "W2"),
			edge("Y3", "W3"),
			edge("Y3", "Z1"),
			edge("Z1", "Z2"),
			edge("Z2", "Z1"),
			edge("Z2", "Z3"),
			edge("Z3", "Z2")
		},
		{0, 0, 0, 0, 0, 4, 5, 6, 7, 8, 0, 0, 0},
		{
			{"R", 0},
			{"W1", 1},
			{"W2", 2},
			{"W3", 3},
			{"X1", 4},
			{"X2", 5},
			{"X3", 6},
			{"Y1", 7},
			{"Y2", 8},
			{"Y3", 9},
			{"Z1", 10},
			{"Z2", 11},
			{"Z3", 12}
		}
	);


	// Worst-case families for k = 3
	// Example idfsquad(3) pg. 26 fig. 2.9
	// ref: https://www.cs.princeton.edu/techreports/2005/737.pdf
	inputGraph[6] = generateGraph(
		{ "R", "X1", "X2", "X3", "Y1", "Y2", "Y3", "Z1", "Z2", "Z3" },
		{
			edge("R", "X1"),
			edge("R", "Z1"),
			edge("X1", "Y1"),
			edge("X1", "X2"),
			edge("X2", "X3"),
			edge("X2", "Y2"),
			edge("X3", "Y3"),
			edge("Y1", "Z1"),
			edge("Y1", "Z2"),
			edge("Z1", "Y1"),
			edge("Y2", "Z2"),
			edge("Y2", "Z3"),
			edge("Z2", "Y2"),
			edge("Y3", "Z3"),
			edge("Z3", "Y3")
		},
		{0, 0, 0, 0, 0, 0, 0, 0, 1, 8},
		{
			{"R", 0},
			{"X1", 1},
			{"Y1", 2},
			{"Z1", 3},
			{"Z2", 4},
			{"Y2", 5},
			{"Z3", 6},
			{"Y3", 7},
			{"X2", 8},
			{"X3", 9}
		}
	);

	// Worst-case families for k = 3
	// Example ibfsquad(3) pg. 26 fig. 2.9
	// ref: https://www.cs.princeton.edu/techreports/2005/737.pdf
	inputGraph[7] = generateGraph(
		{ "R", "W", "X1", "X2", "X3", "Y", "Z" },
		{
			edge("R", "W"),
			edge("R", "Y"),
			edge("W", "X1"),
			edge("W", "X2"),
			edge("W", "X3"),
			edge("Y", "Z"),
			edge("Z", "X3"),
			edge("X3", "X2"),
			edge("X2", "X1")
		},
		{0, 0, 0, 0, 0, 0, 5},
		{
			{"R", 0},
			{"W", 1},
			{"X1", 2},
			{"X2", 3},
			{"X3", 4},
			{"Y", 5},
			{"Z", 6}
		}
	);

	// Worst-case families for k = 3
	// Example sncaworst(3) pg. 26 fig. 2.9
	// ref: https://www.cs.princeton.edu/techreports/2005/737.pdf
	inputGraph[8] = generateGraph(
		{ "R", "X1", "X2", "X3", "Y1", "Y2", "Y3" },
		{
			edge("R", "X1"),
			edge("R", "Y1"),
			edge("R", "Y2"),
			edge("R", "Y3"),
			edge("X1", "X2"),
			edge("X2", "X3"),
			edge("X3", "Y1"),
			edge("X3", "Y2"),
			edge("X3", "Y3")
		},
		{0, 0, 1, 2, 0, 0, 0},
		{
			{"R", 0},
			{"X1", 1},
			{"X2", 2},
			{"X3", 3},
			{"Y1", 4},
			{"Y2", 5},
			{"Y3", 6},
		}
	);

	for (ImmediateDominatorTest const* test: inputGraph)
	{
		Dominator<
			ImmediateDominatorTest::Vertex,
			ImmediateDominatorTest::ForEachVertexSuccessorTest
		> dominatorFinder(*test->entry, test->numVertices);

		for (auto const&[v, idx]: dominatorFinder.vertexIndices())
			BOOST_CHECK(test->expectedDFSIndices.at(v.data) == idx);
		BOOST_TEST(dominatorFinder.immediateDominators() == test->expectedIdom);
	}

}

BOOST_AUTO_TEST_SUITE_END()
}

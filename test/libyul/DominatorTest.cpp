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

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

using namespace solidity::yul;

namespace solidity::yul::test
{

struct ImmediateDominatorTest
{
	struct Vertex {
		std::string name;
		std::vector<Vertex*> successors;

		bool operator<(Vertex const& _other) const
		{
			return name < _other.name;
		}
	};

	typedef std::pair<std::string, std::string> Edge;

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
	static ImmediateDominatorTest const* prepareTestDefinition(
		std::vector<std::string> _vertices,
		std::vector<ImmediateDominatorTest::Edge> _edges,
		std::vector<size_t> _expectedIdom,
		std::map<std::string, size_t> _expectedDFSIndices
	)
	{
		soltestAssert(_edges.size() > 0);

		ImmediateDominatorTest* test = new ImmediateDominatorTest();
		for (std::string name: _vertices)
			test->vertices.insert(make_pair(name, new Vertex{name, std::vector<Vertex*>{}}));
		test->entry = test->vertices[_vertices[0]];

		soltestAssert(_vertices.size() > 0 && _vertices.size() == test->vertices.size());

		test->numVertices = _vertices.size();
		for (auto const& [from, to]: _edges)
			test->vertices[from]->successors.push_back(test->vertices[to]);

		test->expectedIdom = _expectedIdom;
		test->expectedDFSIndices = _expectedDFSIndices;
		return test;
	}

	std::map<std::string, size_t> toDFSIndices(std::map<Vertex, size_t> const& _vertexIndices)
	{
		auto convertIndex = [](std::pair<Vertex, size_t> const& _pair) -> std::pair<std::string, size_t>
		{
			return {_pair.first.name, _pair.second};
		};
		return _vertexIndices | ranges::views::transform(convertIndex) | ranges::to<std::map>;
	}
};

typedef ImmediateDominatorTest::Edge Edge;
typedef Dominator<
	ImmediateDominatorTest::Vertex,
	ImmediateDominatorTest::ForEachVertexSuccessorTest
> DominatorFinder;

BOOST_AUTO_TEST_SUITE(Dominators)

BOOST_FIXTURE_TEST_CASE(immediate_dominator_1, DominatorFixture)
{
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
	ImmediateDominatorTest const* test = prepareTestDefinition(
		{"A", "B", "C", "D", "E", "F", "G", "H"},
		{
			Edge("A", "B"),
			Edge("B", "C"),
			Edge("B", "D"),
			Edge("C", "D"),
			Edge("C", "G"),
			Edge("D", "E"),
			Edge("E", "F"),
			Edge("G", "H"),
			Edge("H", "F")
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
	DominatorFinder dominatorFinder(*test->entry, test->numVertices);
	BOOST_TEST(toDFSIndices(dominatorFinder.vertexIndices()) == test->expectedDFSIndices);
	BOOST_TEST(dominatorFinder.immediateDominators() == test->expectedIdom);
}

BOOST_FIXTURE_TEST_CASE(immediate_dominator_2, DominatorFixture)
{
	//    ┌────►A──────┐
	//    │     │      ▼
	//    │ B◄──┘   ┌──D──┐
	//    │ │       │     │
	//    │ ▼       ▼     ▼
	//    └─C◄───┐  E     F
	//      │    │  │     │
	//      └───►G◄─┴─────┘
	ImmediateDominatorTest const* test = prepareTestDefinition(
		{"A", "B", "C", "D", "E", "F", "G"},
		{
			Edge("A", "B"),
			Edge("B", "C"),
			Edge("C", "G"),
			Edge("C", "A"),
			Edge("A", "D"),
			Edge("D", "E"),
			Edge("D", "F"),
			Edge("E", "G"),
			Edge("F", "G"),
			Edge("G", "C")
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
	DominatorFinder dominatorFinder(*test->entry, test->numVertices);
	BOOST_TEST(toDFSIndices(dominatorFinder.vertexIndices()) == test->expectedDFSIndices);
	BOOST_TEST(dominatorFinder.immediateDominators() == test->expectedIdom);
}

BOOST_FIXTURE_TEST_CASE(immediate_dominator_3, DominatorFixture)
{
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
	ImmediateDominatorTest const* test = prepareTestDefinition(
		{"A", "B", "C", "D", "E", "F", "G", "H", "I"},
		{
			Edge("A", "B"),
			Edge("A", "C"),
			Edge("B", "C"),
			Edge("B", "I"),
			Edge("B", "E"),
			Edge("C", "D"),
			Edge("D", "B"),
			Edge("E", "H"),
			Edge("E", "F"),
			Edge("F", "G"),
			Edge("F", "C"),
			Edge("G", "E"),
			Edge("G", "A"),
			Edge("G", "D"),
			Edge("H", "G"),
			Edge("I", "E"),
			Edge("I", "H")
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
	DominatorFinder dominatorFinder(*test->entry, test->numVertices);
	BOOST_TEST(toDFSIndices(dominatorFinder.vertexIndices()) == test->expectedDFSIndices);
	BOOST_TEST(dominatorFinder.immediateDominators() == test->expectedIdom);
}

BOOST_FIXTURE_TEST_CASE(langauer_tarjan_p122_fig1, DominatorFixture)
{
	// T. Lengauer and R. E. Tarjan pg. 122 fig. 1
	// ref: https://www.cs.princeton.edu/courses/archive/spr03/cs423/download/dominators.pdf
	ImmediateDominatorTest const* test = prepareTestDefinition(
		{"R", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "L", "K"},
		{
			Edge("R", "B"),
			Edge("R", "A"),
			Edge("R", "C"),
			Edge("B", "A"),
			Edge("B", "D"),
			Edge("B", "E"),
			Edge("A", "D"),
			Edge("D", "L"),
			Edge("L", "H"),
			Edge("E", "H"),
			Edge("H", "E"),
			Edge("H", "K"),
			Edge("K", "I"),
			Edge("K", "R"),
			Edge("C", "F"),
			Edge("C", "G"),
			Edge("F", "I"),
			Edge("G", "I"),
			Edge("G", "J"),
			Edge("J", "I"),
			Edge("I", "K"),
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
	DominatorFinder dominatorFinder(*test->entry, test->numVertices);
	BOOST_TEST(toDFSIndices(dominatorFinder.vertexIndices()) == test->expectedDFSIndices);
	BOOST_TEST(dominatorFinder.immediateDominators() == test->expectedIdom);
}

BOOST_FIXTURE_TEST_CASE(loukas_georgiadis, DominatorFixture)
{
	// Extracted from Loukas Georgiadis Dissertation - Linear-Time Algorithms for Dominators and Related Problems
	// pg. 12 Fig. 2.2
	// ref: https://www.cs.princeton.edu/techreports/2005/737.pdf
	ImmediateDominatorTest const* test = prepareTestDefinition(
		{"R", "W", "X1", "X2", "X3", "X4", "X5", "X6", "X7", "Y"},
		{
			Edge("R", "W"),
			Edge("R", "Y"),
			Edge("W", "X1"),
			Edge("Y", "X7"),
			Edge("X1", "X2"),
			Edge("X2", "X1"),
			Edge("X2", "X3"),
			Edge("X3", "X2"),
			Edge("X3", "X4"),
			Edge("X4", "X3"),
			Edge("X4", "X5"),
			Edge("X5", "X4"),
			Edge("X5", "X6"),
			Edge("X6", "X5"),
			Edge("X6", "X7"),
			Edge("X7", "X6")
		},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
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
	DominatorFinder dominatorFinder(*test->entry, test->numVertices);
	BOOST_TEST(toDFSIndices(dominatorFinder.vertexIndices()) == test->expectedDFSIndices);
	BOOST_TEST(dominatorFinder.immediateDominators() == test->expectedIdom);
}

BOOST_FIXTURE_TEST_CASE(itworst, DominatorFixture)
{
	// Worst-case families for k = 3
	// Example itworst(3) pg. 26 fig. 2.9
	// ref: https://www.cs.princeton.edu/techreports/2005/737.pdf
	ImmediateDominatorTest const* test = prepareTestDefinition(
		{"R", "W1", "W2", "W3", "X1", "X2", "X3", "Y1", "Y2", "Y3", "Z1", "Z2", "Z3"},
		{
			Edge("R", "W1"),
			Edge("R", "X1"),
			Edge("R", "Z3"),
			Edge("W1", "W2"),
			Edge("W2", "W3"),
			Edge("X1", "X2"),
			Edge("X2", "X3"),
			Edge("X3", "Y1"),
			Edge("Y1", "W1"),
			Edge("Y1", "W2"),
			Edge("Y1", "W3"),
			Edge("Y1", "Y2"),
			Edge("Y2", "W1"),
			Edge("Y2", "W2"),
			Edge("Y2", "W3"),
			Edge("Y2", "Y3"),
			Edge("Y3", "W1"),
			Edge("Y3", "W2"),
			Edge("Y3", "W3"),
			Edge("Y3", "Z1"),
			Edge("Z1", "Z2"),
			Edge("Z2", "Z1"),
			Edge("Z2", "Z3"),
			Edge("Z3", "Z2")
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
	DominatorFinder dominatorFinder(*test->entry, test->numVertices);
	BOOST_TEST(toDFSIndices(dominatorFinder.vertexIndices()) == test->expectedDFSIndices);
	BOOST_TEST(dominatorFinder.immediateDominators() == test->expectedIdom);
}

BOOST_FIXTURE_TEST_CASE(idfsquad, DominatorFixture)
{
	// Worst-case families for k = 3
	// Example idfsquad(3) pg. 26 fig. 2.9
	// ref: https://www.cs.princeton.edu/techreports/2005/737.pdf
	ImmediateDominatorTest const* test = prepareTestDefinition(
		{"R", "X1", "X2", "X3", "Y1", "Y2", "Y3", "Z1", "Z2", "Z3"},
		{
			Edge("R", "X1"),
			Edge("R", "Z1"),
			Edge("X1", "Y1"),
			Edge("X1", "X2"),
			Edge("X2", "X3"),
			Edge("X2", "Y2"),
			Edge("X3", "Y3"),
			Edge("Y1", "Z1"),
			Edge("Y1", "Z2"),
			Edge("Z1", "Y1"),
			Edge("Y2", "Z2"),
			Edge("Y2", "Z3"),
			Edge("Z2", "Y2"),
			Edge("Y3", "Z3"),
			Edge("Z3", "Y3")
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
	DominatorFinder dominatorFinder(*test->entry, test->numVertices);
	BOOST_TEST(toDFSIndices(dominatorFinder.vertexIndices()) == test->expectedDFSIndices);
	BOOST_TEST(dominatorFinder.immediateDominators() == test->expectedIdom);
}

BOOST_FIXTURE_TEST_CASE(ibsfquad, DominatorFixture)
{
	// Worst-case families for k = 3
	// Example ibfsquad(3) pg. 26 fig. 2.9
	// ref: https://www.cs.princeton.edu/techreports/2005/737.pdf
	ImmediateDominatorTest const* test = prepareTestDefinition(
		{"R", "W", "X1", "X2", "X3", "Y", "Z"},
		{
			Edge("R", "W"),
			Edge("R", "Y"),
			Edge("W", "X1"),
			Edge("W", "X2"),
			Edge("W", "X3"),
			Edge("Y", "Z"),
			Edge("Z", "X3"),
			Edge("X3", "X2"),
			Edge("X2", "X1")
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
	DominatorFinder dominatorFinder(*test->entry, test->numVertices);
	BOOST_TEST(toDFSIndices(dominatorFinder.vertexIndices()) == test->expectedDFSIndices);
	BOOST_TEST(dominatorFinder.immediateDominators() == test->expectedIdom);
}

BOOST_FIXTURE_TEST_CASE(sncaworst, DominatorFixture)
{
	// Worst-case families for k = 3
	// Example sncaworst(3) pg. 26 fig. 2.9
	// ref: https://www.cs.princeton.edu/techreports/2005/737.pdf
	ImmediateDominatorTest const* test = prepareTestDefinition(
		{"R", "X1", "X2", "X3", "Y1", "Y2", "Y3"},
		{
			Edge("R", "X1"),
			Edge("R", "Y1"),
			Edge("R", "Y2"),
			Edge("R", "Y3"),
			Edge("X1", "X2"),
			Edge("X2", "X3"),
			Edge("X3", "Y1"),
			Edge("X3", "Y2"),
			Edge("X3", "Y3")
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
	DominatorFinder dominatorFinder(*test->entry, test->numVertices);
	BOOST_TEST(toDFSIndices(dominatorFinder.vertexIndices()) == test->expectedDFSIndices);
	BOOST_TEST(dominatorFinder.immediateDominators() == test->expectedIdom);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace solidity::yul::test

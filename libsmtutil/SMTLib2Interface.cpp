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
// SPDX-License-Identifier: GPL-3.0

#include <libsmtutil/SMTLib2Interface.h>

#include <libsolutil/Keccak256.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <range/v3/algorithm/find_if.hpp>

#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using namespace solidity;
using namespace solidity::util;
using namespace solidity::frontend;
using namespace solidity::smtutil;

SMTLib2Interface::SMTLib2Interface(
	[[maybe_unused]] std::map<h256, std::string> _queryResponses,
	ReadCallback::Callback _smtCallback,
	SMTSolverChoice _enabledSolvers,
	std::optional<unsigned> _queryTimeout
):
	SolverInterface(_queryTimeout),
	m_smtCallback(std::move(_smtCallback)),
	m_enabledSolvers(_enabledSolvers)
{
	reset();
}

void SMTLib2Interface::reset()
{
	m_accumulatedOutput.clear();
	m_accumulatedOutput.emplace_back();
	m_variables.clear();
	m_userSorts.clear();
	m_sortNames.clear();
	write("(set-option :produce-models true)");
	if (m_queryTimeout)
		write("(set-option :timeout " + std::to_string(*m_queryTimeout) + ")");
	write("(set-logic ALL)");
}

void SMTLib2Interface::push()
{
	m_accumulatedOutput.emplace_back();
}

void SMTLib2Interface::pop()
{
	smtAssert(!m_accumulatedOutput.empty(), "");
	m_accumulatedOutput.pop_back();
}

void SMTLib2Interface::declareVariable(std::string const& _name, SortPointer const& _sort)
{
	smtAssert(_sort, "");
	if (_sort->kind == Kind::Function)
		declareFunction(_name, _sort);
	else if (!m_variables.count(_name))
	{
		m_variables.emplace(_name, _sort);
		write("(declare-fun |" + _name + "| () " + toSmtLibSort(*_sort) + ')');
	}
}

void SMTLib2Interface::declareFunction(std::string const& _name, SortPointer const& _sort)
{
	smtAssert(_sort, "");
	smtAssert(_sort->kind == Kind::Function, "");
	// TODO Use domain and codomain as key as well
	if (!m_variables.count(_name))
	{
		auto const& fSort = std::dynamic_pointer_cast<FunctionSort>(_sort);
		std::string domain = toSmtLibSort(fSort->domain);
		std::string codomain = toSmtLibSort(*fSort->codomain);
		m_variables.emplace(_name, _sort);
		write(
			"(declare-fun |" +
			_name +
			"| " +
			domain +
			" " +
			codomain +
			")"
		);
	}
}

void SMTLib2Interface::addAssertion(Expression const& _expr)
{
	write("(assert " + toSExpr(_expr) + ")");
}

namespace // Helpers for querying solvers using SMT callback
{
	auto resultFromSolverResponse (std::string const& response) {
		CheckResult result;
		// TODO proper parsing
		if (boost::starts_with(response, "sat"))
			result = CheckResult::SATISFIABLE;
		else if (boost::starts_with(response, "unsat"))
			result = CheckResult::UNSATISFIABLE;
		else if (boost::starts_with(response, "unknown"))
			result = CheckResult::UNKNOWN;
		else
			result = CheckResult::ERROR;
		return result;
	}

	bool solverAnswered(CheckResult result)
	{
		return result == CheckResult::SATISFIABLE || result == CheckResult::UNSATISFIABLE;
	}

	std::vector<std::string> parseValues(std::string::const_iterator _start, std::string::const_iterator _end)
	{
		std::vector<std::string> values;
		while (_start < _end)
		{
			auto valStart = std::find(_start, _end, ' ');
			if (valStart < _end)
				++valStart;
			auto valEnd = std::find(valStart, _end, ')');
			values.emplace_back(valStart, valEnd);
			_start = std::find(valEnd, _end, '(');
		}

		return values;
	}

	std::vector<std::string> parseValues(std::string const& solverAnswer)
	{
		return parseValues(std::find(solverAnswer.cbegin(), solverAnswer.cend(), '\n'), solverAnswer.cend());
	}
}

std::pair<CheckResult, std::vector<std::string>> SMTLib2Interface::check(std::vector<Expression> const& _expressionsToEvaluate)
{
	auto query = boost::algorithm::join(m_accumulatedOutput, "\n") + checkSatAndGetValuesCommand(_expressionsToEvaluate);

	std::vector<std::string> solverCommands;
	if (m_enabledSolvers.z3)
		solverCommands.emplace_back("z3 rlimit=1000000");
	if (m_enabledSolvers.cvc4)
		solverCommands.emplace_back("cvc4");

	CheckResult lastResult = CheckResult::ERROR;
	std::vector<std::string> finalValues;
	smtAssert(m_smtCallback);
	for (auto const& s: solverCommands)
	{
		auto callBackResult = m_smtCallback(ReadCallback::kindString(ReadCallback::Kind::SMTQuery) + ' ' + s, query);
		if (not callBackResult.success)
			continue;
		auto const& response = callBackResult.responseOrErrorMessage;
		CheckResult result = resultFromSolverResponse(response);
		if (solverAnswered(result))
		{
			if (!solverAnswered(lastResult))
			{
				lastResult = result;
				if (result == CheckResult::SATISFIABLE)
					finalValues = parseValues(response);
			}
			else if (lastResult != result)
			{
				lastResult = CheckResult::CONFLICTING;
				break;
			}
		}
		else if (result == CheckResult::UNKNOWN && lastResult == CheckResult::ERROR)
			lastResult = result;
	}
	if (lastResult == CheckResult::ERROR)
		m_unhandledQueries.push_back(query);
	return std::make_pair(lastResult, finalValues);
}

std::string SMTLib2Interface::toSExpr(Expression const& _expr)
{
	if (_expr.arguments.empty())
		return _expr.name;

	std::string sexpr = "(";
	if (_expr.name == "int2bv")
	{
		size_t size = std::stoul(_expr.arguments[1].name);
		auto arg = toSExpr(_expr.arguments.front());
		auto int2bv = "(_ int2bv " + std::to_string(size) + ")";
		// Some solvers treat all BVs as unsigned, so we need to manually apply 2's complement if needed.
		sexpr += std::string("ite ") +
			"(>= " + arg + " 0) " +
			"(" + int2bv + " " + arg + ") " +
			"(bvneg (" + int2bv + " (- " + arg + ")))";
	}
	else if (_expr.name == "bv2int")
	{
		auto intSort = std::dynamic_pointer_cast<IntSort>(_expr.sort);
		smtAssert(intSort, "");

		auto arg = toSExpr(_expr.arguments.front());
		auto nat = "(bv2nat " + arg + ")";

		if (!intSort->isSigned)
			return nat;

		auto bvSort = std::dynamic_pointer_cast<BitVectorSort>(_expr.arguments.front().sort);
		smtAssert(bvSort, "");
		auto size = std::to_string(bvSort->size);
		auto pos = std::to_string(bvSort->size - 1);

		// Some solvers treat all BVs as unsigned, so we need to manually apply 2's complement if needed.
		sexpr += std::string("ite ") +
			"(= ((_ extract " + pos + " " + pos + ")" + arg + ") #b0) " +
			nat + " " +
			"(- (bv2nat (bvneg " + arg + ")))";
	}
	else if (_expr.name == "const_array")
	{
		smtAssert(_expr.arguments.size() == 2, "");
		auto sortSort = std::dynamic_pointer_cast<SortSort>(_expr.arguments.at(0).sort);
		smtAssert(sortSort, "");
		auto arraySort = std::dynamic_pointer_cast<ArraySort>(sortSort->inner);
		smtAssert(arraySort, "");
		sexpr += "(as const " + toSmtLibSort(*arraySort) + ") ";
		sexpr += toSExpr(_expr.arguments.at(1));
	}
	else if (_expr.name == "tuple_get")
	{
		smtAssert(_expr.arguments.size() == 2, "");
		auto tupleSort = std::dynamic_pointer_cast<TupleSort>(_expr.arguments.at(0).sort);
		size_t index = std::stoul(_expr.arguments.at(1).name);
		smtAssert(index < tupleSort->members.size(), "");
		sexpr += "|" + tupleSort->members.at(index) + "| " + toSExpr(_expr.arguments.at(0));
	}
	else if (_expr.name == "tuple_constructor")
	{
		auto tupleSort = std::dynamic_pointer_cast<TupleSort>(_expr.sort);
		smtAssert(tupleSort, "");
		sexpr += "|" + tupleSort->name + "|";
		for (auto const& arg: _expr.arguments)
			sexpr += " " + toSExpr(arg);
	}
	else
	{
		sexpr += _expr.name;
		for (auto const& arg: _expr.arguments)
			sexpr += " " + toSExpr(arg);
	}
	sexpr += ")";
	return sexpr;
}

std::string SMTLib2Interface::toSmtLibSort(Sort const& _sort)
{
	if (!m_sortNames.count(&_sort))
	{
		auto smtLibName = sortToString(_sort);
		m_sortNames[&_sort] = smtLibName;
	}
	return m_sortNames.at(&_sort);
}

std::string SMTLib2Interface::sortToString(Sort const& _sort)
{
	switch (_sort.kind)
	{
	case Kind::Int:
		return "Int";
	case Kind::Bool:
		return "Bool";
	case Kind::BitVector:
		return "(_ BitVec " + std::to_string(dynamic_cast<BitVectorSort const&>(_sort).size) + ")";
	case Kind::Array:
	{
		auto const& arraySort = dynamic_cast<ArraySort const&>(_sort);
		smtAssert(arraySort.domain && arraySort.range, "");
		return "(Array " + toSmtLibSort(*arraySort.domain) + ' ' + toSmtLibSort(*arraySort.range) + ')';
	}
	case Kind::Tuple:
	{
		auto const& tupleSort = dynamic_cast<TupleSort const&>(_sort);
		std::string tupleName = "|" + tupleSort.name + "|";
		auto isName = [&](auto entry) { return entry.first == tupleName; };
		if (ranges::find_if(m_userSorts, isName) == m_userSorts.end())
		{
			std::string decl("(declare-datatypes ((" + tupleName + " 0)) (((" + tupleName);
			smtAssert(tupleSort.members.size() == tupleSort.components.size(), "");
			for (unsigned i = 0; i < tupleSort.members.size(); ++i)
				decl += " (|" + tupleSort.members.at(i) + "| " + toSmtLibSort(*tupleSort.components.at(i)) + ")";
			decl += "))))";
			m_userSorts.emplace_back(tupleName, decl);
			write(decl);
		}

		return tupleName;
	}
	default:
		smtAssert(false, "Invalid SMT sort");
	}
}

std::string SMTLib2Interface::toSmtLibSort(std::vector<SortPointer> const& _sorts)
{
	std::string ssort("(");
	for (auto const& sort: _sorts)
		ssort += toSmtLibSort(*sort) + " ";
	ssort += ")";
	return ssort;
}

void SMTLib2Interface::write(std::string _data)
{
	smtAssert(!m_accumulatedOutput.empty(), "");
	m_accumulatedOutput.back() += std::move(_data) + "\n";
}

std::string SMTLib2Interface::checkSatAndGetValuesCommand(std::vector<Expression> const& _expressionsToEvaluate)
{
	std::string command;
	if (_expressionsToEvaluate.empty())
		command = "(check-sat)\n";
	else
	{
		// TODO make sure these are unique
		for (size_t i = 0; i < _expressionsToEvaluate.size(); i++)
		{
			auto const& e = _expressionsToEvaluate.at(i);
			smtAssert(e.sort->kind == Kind::Int || e.sort->kind == Kind::Bool, "Invalid sort for expression to evaluate.");
			command += "(declare-const |EVALEXPR_" + std::to_string(i) + "| " + (e.sort->kind == Kind::Int ? "Int" : "Bool") + ")\n";
			command += "(assert (= |EVALEXPR_" + std::to_string(i) + "| " + toSExpr(e) + "))\n";
		}
		command += "(check-sat)\n";
		command += "(get-value (";
		for (size_t i = 0; i < _expressionsToEvaluate.size(); i++)
			command += "|EVALEXPR_" + std::to_string(i) + "| ";
		command += "))\n";
	}

	return command;
}

std::string SMTLib2Interface::dumpQuery(std::vector<Expression> const& _expressionsToEvaluate)
{
	return boost::algorithm::join(m_accumulatedOutput, "\n") +
		checkSatAndGetValuesCommand(_expressionsToEvaluate);
}

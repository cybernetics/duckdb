#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/planner/expression/bound_between_expression.hpp"
#include "duckdb/common/operator/comparison_operators.hpp"
#include "duckdb/common/vector_operations/ternary_executor.hpp"

namespace duckdb {
using namespace std;

struct BothInclusiveBetweenOperator {
	template <class T> static inline bool Operation(T input, T lower, T upper) {
		return GreaterThanEquals::Operation<T>(input, lower) && LessThanEquals::Operation<T>(input, upper);
	}
};

struct LowerInclusiveBetweenOperator {
	template <class T> static inline bool Operation(T input, T lower, T upper) {
		return GreaterThanEquals::Operation<T>(input, lower) && LessThan::Operation<T>(input, upper);
	}
};

struct UpperInclusiveBetweenOperator {
	template <class T> static inline bool Operation(T input, T lower, T upper) {
		return GreaterThan::Operation<T>(input, lower) && LessThanEquals::Operation<T>(input, upper);
	}
};

struct ExclusiveBetweenOperator {
	template <class T> static inline bool Operation(T input, T lower, T upper) {
		return GreaterThan::Operation<T>(input, lower) && LessThan::Operation<T>(input, upper);
	}
};

template <class OP>
static idx_t between_loop_type_switch(Vector &input, Vector &lower, Vector &upper, const SelectionVector *sel,
                                      idx_t count, SelectionVector *true_sel, SelectionVector *false_sel) {
	switch (input.type.InternalType()) {
	case PhysicalType::BOOL:
	case PhysicalType::INT8:
		return TernaryExecutor::Select<int8_t, int8_t, int8_t, OP>(input, lower, upper, sel, count, true_sel,
		                                                           false_sel);
	case PhysicalType::INT16:
		return TernaryExecutor::Select<int16_t, int16_t, int16_t, OP>(input, lower, upper, sel, count, true_sel,
		                                                              false_sel);
	case PhysicalType::INT32:
		return TernaryExecutor::Select<int32_t, int32_t, int32_t, OP>(input, lower, upper, sel, count, true_sel,
		                                                              false_sel);
	case PhysicalType::INT64:
		return TernaryExecutor::Select<int64_t, int64_t, int64_t, OP>(input, lower, upper, sel, count, true_sel,
		                                                              false_sel);
	case PhysicalType::INT128:
		return TernaryExecutor::Select<hugeint_t, hugeint_t, hugeint_t, OP>(input, lower, upper, sel, count, true_sel,
		                                                                    false_sel);
	case PhysicalType::FLOAT:
		return TernaryExecutor::Select<float, float, float, OP>(input, lower, upper, sel, count, true_sel, false_sel);
	case PhysicalType::DOUBLE:
		return TernaryExecutor::Select<double, double, double, OP>(input, lower, upper, sel, count, true_sel,
		                                                           false_sel);
	case PhysicalType::VARCHAR:
		return TernaryExecutor::Select<string_t, string_t, string_t, OP>(input, lower, upper, sel, count, true_sel,
		                                                                 false_sel);
	default:
		throw InvalidTypeException(input.type, "Invalid type for BETWEEN");
	}
}

unique_ptr<ExpressionState> ExpressionExecutor::InitializeState(BoundBetweenExpression &expr,
                                                                ExpressionExecutorState &root) {
	auto result = make_unique<ExpressionState>(expr, root);
	result->AddChild(expr.input.get());
	result->AddChild(expr.lower.get());
	result->AddChild(expr.upper.get());
	result->Finalize();
	return result;
}

void ExpressionExecutor::Execute(BoundBetweenExpression &expr, ExpressionState *state, const SelectionVector *sel,
                                 idx_t count, Vector &result) {
	// resolve the children
	Vector input, lower, upper;
	input.Reference(state->intermediate_chunk.data[0]);
	lower.Reference(state->intermediate_chunk.data[1]);
	upper.Reference(state->intermediate_chunk.data[2]);

	Execute(*expr.input, state->child_states[0].get(), sel, count, input);
	Execute(*expr.lower, state->child_states[1].get(), sel, count, lower);
	Execute(*expr.upper, state->child_states[2].get(), sel, count, upper);

	Vector intermediate1(LogicalType::BOOLEAN);
	Vector intermediate2(LogicalType::BOOLEAN);

	if (expr.upper_inclusive && expr.lower_inclusive) {
		VectorOperations::GreaterThanEquals(input, lower, intermediate1, count);
		VectorOperations::LessThanEquals(input, upper, intermediate2, count);
	} else if (expr.lower_inclusive) {
		VectorOperations::GreaterThanEquals(input, lower, intermediate1, count);
		VectorOperations::LessThan(input, upper, intermediate2, count);
	} else if (expr.upper_inclusive) {
		VectorOperations::GreaterThan(input, lower, intermediate1, count);
		VectorOperations::LessThanEquals(input, upper, intermediate2, count);
	} else {
		VectorOperations::GreaterThan(input, lower, intermediate1, count);
		VectorOperations::LessThan(input, upper, intermediate2, count);
	}
	VectorOperations::And(intermediate1, intermediate2, result, count);
}

idx_t ExpressionExecutor::Select(BoundBetweenExpression &expr, ExpressionState *state, const SelectionVector *sel,
                                 idx_t count, SelectionVector *true_sel, SelectionVector *false_sel) {
	// resolve the children
	Vector input, lower, upper;
	input.Reference(state->intermediate_chunk.data[0]);
	lower.Reference(state->intermediate_chunk.data[1]);
	upper.Reference(state->intermediate_chunk.data[2]);

	Execute(*expr.input, state->child_states[0].get(), sel, count, input);
	Execute(*expr.lower, state->child_states[1].get(), sel, count, lower);
	Execute(*expr.upper, state->child_states[2].get(), sel, count, upper);

	if (expr.upper_inclusive && expr.lower_inclusive) {
		return between_loop_type_switch<BothInclusiveBetweenOperator>(input, lower, upper, sel, count, true_sel,
		                                                              false_sel);
	} else if (expr.lower_inclusive) {
		return between_loop_type_switch<LowerInclusiveBetweenOperator>(input, lower, upper, sel, count, true_sel,
		                                                               false_sel);
	} else if (expr.upper_inclusive) {
		return between_loop_type_switch<UpperInclusiveBetweenOperator>(input, lower, upper, sel, count, true_sel,
		                                                               false_sel);
	} else {
		return between_loop_type_switch<ExclusiveBetweenOperator>(input, lower, upper, sel, count, true_sel, false_sel);
	}
}

} // namespace duckdb

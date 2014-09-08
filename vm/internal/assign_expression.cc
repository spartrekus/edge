#include "assign_expression.h"

#include "compilation.h"
#include "evaluation.h"
#include "../public/environment.h"
#include "../public/value.h"

namespace afc {
namespace vm {

namespace {

class AssignExpression : public Expression {
 public:
  AssignExpression(const string& symbol, unique_ptr<Expression> value)
      : symbol_(symbol), value_(std::move(value)) {}

  const VMType& type() { return value_->type(); }

  pair<Continuation, unique_ptr<Value>> Evaluate(const Evaluation& evaluation) {
    return value_->Evaluate(Evaluation(evaluation, Continuation(
        [evaluation, symbol_](unique_ptr<Value> value) {
          evaluation.environment->Define(
            symbol_, unique_ptr<Value>(new Value(*value.get())));
          return std::move(
              make_pair(evaluation.continuation, std::move(value)));
        })));
  }

 private:
  const string symbol_;
  unique_ptr<Expression> value_;
};

}

unique_ptr<Expression> NewAssignExpression(
    Compilation* compilation, const string& type, const string& symbol,
    unique_ptr<Expression> value) {
  if (value == nullptr) {
    return nullptr;
  }
  const VMType* type_def = compilation->environment->LookupType(type);
  if (type_def == nullptr) {
    compilation->errors.push_back("Unknown type: \"" + symbol + "\"");
    return nullptr;
  }
  compilation->environment
      ->Define(symbol, unique_ptr<Value>(new Value(value->type())));
  if (!(*type_def == value->type())) {
    compilation->errors.push_back(
        "Unable to assign a value of type \"" + value->type().ToString()
        + "\" to a variable of type \"" + type_def->ToString() + "\".");
    return nullptr;
  }
  return unique_ptr<Expression>(new AssignExpression(symbol, std::move(value)));
}

unique_ptr<Expression> NewAssignExpression(
    Compilation* compilation, const string& symbol,
    unique_ptr<Expression> value) {
  if (value == nullptr) {
    return nullptr;
  }
  auto obj = compilation->environment->Lookup(symbol);
  if (obj == nullptr) {
    compilation->errors.push_back("Variable not found: \"" + symbol + "\"");
    return nullptr;
  }
  if (!(obj->type == value->type())) {
    compilation->errors.push_back(
        "Unable to assign a value of type \"" + value->type().ToString()
        + "\" to a variable of type \"" + obj->type.ToString() + "\".");
    return nullptr;
  }

  return unique_ptr<Expression>(new AssignExpression(symbol, std::move(value)));
}

}  // namespace vm
}  // namespace afc

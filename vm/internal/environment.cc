#include "../public/environment.h"

#include "string.h"
#include "../public/types.h"
#include "../public/value.h"

namespace afc {
namespace vm {

namespace {

Environment* BuildDefaultEnvironment() {
  Environment* environment = new Environment();
  RegisterStringType(environment);
  environment->DefineType(
      "bool", unique_ptr<ObjectType>(new ObjectType(VMType::Bool())));
  environment->DefineType(
      "int", unique_ptr<ObjectType>(new ObjectType(VMType::Integer())));
  return environment;
}

}  // namespace

Environment::Environment()
    : table_(new map<string, unique_ptr<Value>>),
      object_types_(new map<string, unique_ptr<ObjectType>>),
      parent_environment_(nullptr) {}

Environment::Environment(Environment* parent_environment)
    : table_(new map<string, unique_ptr<Value>>),
      object_types_(new map<string, unique_ptr<ObjectType>>),
      parent_environment_(parent_environment) {}

/* static */ Environment* Environment::DefaultEnvironment() {
  static Environment* environment = BuildDefaultEnvironment();
  return environment;
}

const ObjectType* Environment::LookupObjectType(const string& symbol) {
  auto it = object_types_->find(symbol);
  if (it != object_types_->end()) {
    return it->second.get();
  }
  if (parent_environment_ != nullptr) {
    return parent_environment_->LookupObjectType(symbol);
  }
  return nullptr;
}

const VMType* Environment::LookupType(const string& symbol) {
  if (symbol == "void") {
    return &VMType::Void();
  } else if (symbol == "bool") {
    return &VMType::Bool();
  } else if (symbol == "int") {
    return &VMType::Integer();
  } else if (symbol == "string") {
    return &VMType::String();
  }

  auto object_type = LookupObjectType(symbol);
  return object_type == nullptr ? nullptr : &object_type->type();
}

void Environment::DefineType(
    const string& name, unique_ptr<ObjectType> value) {
  auto it = object_types_->insert(make_pair(name, nullptr));
  it.first->second = std::move(value);
}

Value* Environment::Lookup(const string& symbol) {
  auto it = table_->find(symbol);
  if (it != table_->end()) {
    return it->second.get();
  }
  if (parent_environment_ != nullptr) {
    return parent_environment_->Lookup(symbol);
  }

  return nullptr;
}

void Environment::Define(const string& symbol, unique_ptr<Value> value) {
  auto it = table_->insert(make_pair(symbol, nullptr));
  it.first->second = std::move(value);
}

}  // namespace vm
}  // namespace afc

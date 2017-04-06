#include "string.h"

#include <cassert>

#include <glog/logging.h>

#include "../public/environment.h"
#include "../public/types.h"
#include "../public/value.h"

namespace afc {
namespace vm {

void RegisterStringType(Environment* environment) {
  unique_ptr<ObjectType> string_type(new ObjectType(VMType::String()));
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args.size() == 1);
          assert(args[0]->type == VMType::VM_STRING);
          return Value::NewInteger(args[0]->str.size());
        };
    string_type->AddField(L"size", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args.size() == 1);
          assert(args[0]->type == VMType::VM_STRING);
          return Value::NewInteger(args[0]->str.size());
        };
    string_type->AddField(L"match", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args.size() == 3);
          assert(args[0]->type == VMType::VM_STRING);
          if (args[1]->integer < 0
              || args[2]->integer < 0
              || (static_cast<size_t>(args[1]->integer + args[2]->integer)
                  > args[0]->str.size())) {
            return Value::NewString(L"");
          }
          return Value::NewString(
              args[0]->str.substr(args[1]->integer, args[2]->integer));
        };
    string_type->AddField(L"substr", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_BOOLEAN));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          return Value::NewBool(args[0]->str.empty());
        };
    string_type->AddField(L"empty", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_BOOLEAN));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          assert(args[1]->type == VMType::VM_STRING);
          return Value::NewBool(
              args[1]->str.size() <= args[0]->str.size()
              && (std::mismatch(args[1]->str.begin(),
                                args[1]->str.end(),
                                args[0]->str.begin()).first
                  == args[1]->str.end()));
        };
    string_type->AddField(L"starts_with", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          assert(args[1]->type == VMType::VM_STRING);
          assert(args[2]->type == VMType::VM_INTEGER);
          size_t pos = args[0]->str.find(args[1]->str, args[2]->integer);
          if (pos == wstring::npos) {
            return Value::NewInteger(-1);
          }
          return Value::NewInteger(pos);
        };
    string_type->AddField(L"find", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          assert(args[1]->type == VMType::VM_STRING);
          assert(args[2]->type == VMType::VM_INTEGER);
          size_t pos = args[0]->str.find_last_of(args[1]->str, args[2]->integer);
          if (pos == wstring::npos) {
            return Value::NewInteger(-1);
          }
          return Value::NewInteger(pos);
        };
    string_type->AddField(L"find_last_of", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          assert(args[1]->type == VMType::VM_STRING);
          assert(args[2]->type == VMType::VM_INTEGER);
          size_t pos = args[0]->str.find_last_not_of(args[1]->str, args[2]->integer);
          if (pos == wstring::npos) {
            return Value::NewInteger(-1);
          }
          return Value::NewInteger(pos);
        };
    string_type->AddField(L"find_last_not_of", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          assert(args[1]->type == VMType::VM_STRING);
          assert(args[2]->type == VMType::VM_INTEGER);
          size_t pos =
              args[0]->str.find_first_of(args[1]->str, args[2]->integer);
          if (pos == wstring::npos) {
            return Value::NewInteger(-1);
          }
          return Value::NewInteger(pos);
        };
    string_type->AddField(L"find_first_of", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback = [](vector<unique_ptr<Value>> args) {
          assert(args[0]->type == VMType::VM_STRING);
          assert(args[1]->type == VMType::VM_STRING);
          assert(args[2]->type == VMType::VM_INTEGER);
          size_t pos = args[0]->str.find_first_not_of(args[1]->str, args[2]->integer);
          if (pos == wstring::npos) {
            return Value::NewInteger(-1);
          }
          return Value::NewInteger(pos);
        };
    string_type->AddField(L"find_first_not_of", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback = [](vector<unique_ptr<Value>> args) {
      CHECK_EQ(args.size(), 1);
      CHECK_EQ(args[0]->type.type, VMType::VM_STRING);
      auto output = Value::NewString(std::move(args[0]->str));
      for (auto& i : output->str) {
        i = std::tolower(i, std::locale(""));
      }
      return std::move(output);
    };
    string_type->AddField(L"tolower", std::move(callback));
  }
  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->callback = [](vector<unique_ptr<Value>> args) {
      CHECK_EQ(args.size(), 1);
      CHECK_EQ(args[0]->type.type, VMType::VM_STRING);
      auto output = Value::NewString(std::move(args[0]->str));
      for (auto& i : output->str) {
        i = std::toupper(i, std::locale(""));
      }
      return std::move(output);
    };
    string_type->AddField(L"toupper", std::move(callback));
  }

  environment->DefineType(L"string", std::move(string_type));

  {
    unique_ptr<Value> callback(new Value(VMType::FUNCTION));
    callback->type.type_arguments.push_back(VMType(VMType::VM_STRING));
    callback->type.type_arguments.push_back(VMType(VMType::VM_INTEGER));
    callback->callback = [](vector<unique_ptr<Value>> args) {
      CHECK_EQ(args.size(), 1);
      CHECK_EQ(args[0]->type.type, VMType::VM_INTEGER);
      return Value::NewString(std::to_wstring(args[0]->integer));
    };
    environment->Define(L"tostring", std::move(callback));
  }
}

}  // namespace vm
}  // namespace afc

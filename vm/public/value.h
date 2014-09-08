#ifndef __AFC_VM_PUBLIC_VALUE_H__
#define __AFC_VM_PUBLIC_VALUE_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "types.h"

namespace afc {
namespace vm {

using std::function;
using std::shared_ptr;
using std::string;
using std::vector;

struct Value {
  Value(const VMType::Type& t) : type(t) {}
  Value(const VMType& t) : type(t) {}

  static unique_ptr<Value> NewVoid();
  static unique_ptr<Value> NewBool(bool value);
  static unique_ptr<Value> NewInteger(int value);
  static unique_ptr<Value> NewString(const string& value);
  static unique_ptr<Value> NewObject(const string& name,
                                     const shared_ptr<void>& value);

  VMType type;

  bool boolean;
  int integer;
  string str;
  function<unique_ptr<Value>(vector<unique_ptr<Value>>)> callback;
  shared_ptr<void> user_value;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_VALUE_H__
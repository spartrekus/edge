%name Cpp

%extra_argument { Environment* environment }

%token_type { Value* }

%left LPAREN RPAREN.
%left PLUS MINUS.
%left DIVIDE TIMES.

%type main { Value* }
%destructor main { delete $$; }

main(A) ::= program(B) . {
  if (B == nullptr) {
    A = nullptr;
  } else {
    A = B;
    B = nullptr;
  }
}

%type program { Value* }
%destructor program { delete $$; }

program(A) ::= expr(B) . {
  if (B == nullptr) {
    A = nullptr;
  } else {
    A = B->Evaluate(environment).release();
  }
}

program(A) ::= program(B) SEMICOLON expr(C) . {
  class AppendExpression : public Expression {
   public:
    AppendExpression(unique_ptr<Expression> e0, unique_ptr<Expression> e1)
        : e0_(std::move(e0)), e1_(std::move(e1)) {}

    const VMType& type() {
      return e1_->type();
    }

    unique_ptr<Value> Evaluate(Environment* environment) {
      e0_->Evaluate(environment);
      return std::move(e1_->Evaluate(environment));
    }

   private:
    unique_ptr<Expression> e0_;
    unique_ptr<Expression> e1_;
  };

  if (B == nullptr || C == nullptr) {
    A = nullptr;
  } else {
    A = C->Evaluate(environment).release();
  }
}

%type expr { Expression* }
%destructor expr { delete $$; }

expr(A) ::= LPAREN expr(B) RPAREN. {
  A = B;
  B = nullptr;
}

expr(A) ::= expr(B) LPAREN expr(C) RPAREN. {
  class Evaluator : public Expression {
   public:
    Evaluator(unique_ptr<Expression> func, unique_ptr<Expression> arg0)
        : func_(std::move(func)), arg0_(std::move(arg0)) {
      assert(func_ != nullptr);
      assert(arg0_ != nullptr);
    }

    const VMType& type() {
      return func_->type().type_arguments[0];
    }

    unique_ptr<Value> Evaluate(Environment* environment) {
      auto func = std::move(func_->Evaluate(environment));
      return std::move(func->function1(arg0_->Evaluate(environment)));
    }

   private:
    unique_ptr<Expression> func_;
    unique_ptr<Expression> arg0_;
  };

  VMType c_type = C->type();
  VMType b_arg_type = B->type().type_arguments[1];
  if (B == nullptr
      || C == nullptr
      || B->type().type != VMType::FUNCTION
      || B->type().type_arguments.size() != 2
      || !(B->type().type_arguments[1] == C->type())) {
    A = nullptr;
  } else {
    A = new Evaluator(unique_ptr<Expression>(B), unique_ptr<Expression>(C));
    B = nullptr;
    C = nullptr;
    assert(B == nullptr);
  }
}


// Basic operators

expr(A) ::= expr(B) PLUS expr(C). {
  A = new BinaryOperator(
      unique_ptr<Expression>(B),
      unique_ptr<Expression>(C),
      integer_type(),
      [](const Value& a, const Value& b, Value* output) {
        output->integer = a.integer + b.integer;
      });
  B = nullptr;
  C = nullptr;
}

expr(A) ::= expr(B) MINUS expr(C). {
  A = new BinaryOperator(
      unique_ptr<Expression>(B),
      unique_ptr<Expression>(C),
      integer_type(),
      [](const Value& a, const Value& b, Value* output) {
        output->integer = a.integer - b.integer;
      });
  B = nullptr;
  C = nullptr;
}

expr(A) ::= expr(B) TIMES expr(C). {
  A = new BinaryOperator(
      unique_ptr<Expression>(B),
      unique_ptr<Expression>(C),
      integer_type(),
      [](const Value& a, const Value& b, Value* output) {
        output->integer = a.integer * b.integer;
      });
  B = nullptr;
  C = nullptr;
}

//expr(A) ::= expr(B) DIVIDE expr(C). {
//  A = new Value(VMType::VM_INTEGER);
//  if (C->integer != 0) {
//    A->integer = B->integer / C->integer;
//  } else {
//    std::cout << "divide by zero" << std::endl;
//  }
//}  /* end of DIVIDE */


// Atomic types

expr(A) ::= INTEGER(B). {
  assert(B->type.type == VMType::VM_INTEGER);
  A = new ConstantExpression(unique_ptr<Value>(B));
  B = nullptr;
}

expr(A) ::= STRING(B). {
  assert(B->type.type == VMType::VM_STRING);
  A = new ConstantExpression(unique_ptr<Value>(B));
  B = nullptr;
  assert(B == nullptr);
}

expr(A) ::= SYMBOL(B). {
  assert(B->type.type == VMType::VM_SYMBOL);
  auto result = environment->Lookup(B->str);
  if (result != nullptr) {
    // TODO: This is wrong, the value may actually change, duh.
    A = new ConstantExpression(unique_ptr<Value>(result));
  } else {
    A = nullptr;
  }
}

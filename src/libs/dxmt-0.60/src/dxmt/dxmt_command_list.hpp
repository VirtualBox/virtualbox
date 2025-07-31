#pragma once
#include <concepts>
#include <functional>

namespace dxmt {

template <typename F, typename context>
concept CommandWithContext = requires(F f, context &ctx) {
  { f(ctx) } -> std::same_as<void>;
};

namespace impl {
template <typename context> class CommandBase {
public:
  virtual void invoke(context &) = 0;
  virtual ~CommandBase() noexcept {};
  CommandBase<context> *next = nullptr;
};

template <typename context, typename F> class LambdaCommand final : public CommandBase<context> {
public:
  void
  invoke(context &ctx) final {
    std::invoke(func, ctx);
  };
  ~LambdaCommand() noexcept final = default;
  LambdaCommand(F &&ff) : CommandBase<context>(), func(std::forward<F>(ff)) {}
  LambdaCommand(const LambdaCommand &copy) = delete;
  LambdaCommand &operator=(const LambdaCommand &copy_assign) = delete;

private:
  F func;
};

template <typename context> class EmtpyCommand final : public CommandBase<context> {
public:
  void invoke(context &ctx) final{/* nop */};
  ~EmtpyCommand() noexcept = default;
};

template <typename value_t> struct LinkedListNode {
  value_t value;
  LinkedListNode *next;
};

} // namespace impl

template <typename Context> class CommandList {

  impl::EmtpyCommand<Context> empty;
  impl::CommandBase<Context> *list_end;

public:
  CommandList() : list_end(&empty) {
    empty.next = nullptr;
  }
  ~CommandList() {
    reset();
  }

  void
  reset() {
    impl::CommandBase<Context> *cur = empty.next;
    while (cur) {
      auto next = cur->next;
      cur->~CommandBase<Context>(); // call destructor
      cur = next;
    }
    empty.next = nullptr;
    list_end = &empty;
  }

  CommandList(const CommandList &copy) = delete;
  CommandList(CommandList &&move) {
    this->reset();
    empty.next = move.empty.next;
    list_end = move.list_end;
    move.empty.next = nullptr;
    move.list_end = nullptr;
  }

  CommandList& operator=(CommandList&& move) {
    this->reset();
    empty.next = move.empty.next;
    list_end = move.list_end;
    move.empty.next = nullptr;
    move.list_end = nullptr;
    return *this;
  }

  template <CommandWithContext<Context> Fn>
  constexpr unsigned
  calculateCommandSize() {
    using command_t = impl::LambdaCommand<Context, Fn>;
    return sizeof(command_t);
  }

  template <CommandWithContext<Context> Fn>
  unsigned
  emit(Fn &&cmd, void *buffer) {
    using command_t = impl::LambdaCommand<Context, Fn>;
    auto cmd_h = new (buffer) command_t(std::forward<Fn>(cmd));
    list_end->next = cmd_h;
    list_end = cmd_h;
    return sizeof(command_t);
  }

  void append(CommandList &&list) {
    list_end->next = list.empty.next;
    list_end = list.list_end;
    list.empty.next = nullptr;
    list.list_end = nullptr;
  }

  void
  execute(Context &context) {
    impl::CommandBase<Context> *cur = &empty;
    while (cur) {
      cur->invoke(context);
      cur = cur->next;
    }
  };
};

}; // namespace dxmt

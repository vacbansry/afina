#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char curr_pos;
    ctx.Low = &curr_pos;
    ctx.High = StackBottom;
    std::size_t size = ctx.High - ctx.Low;
    if (size > std::get<1>(ctx.Stack)) {
        delete std::get<0>(ctx.Stack);
        std::get<0>(ctx.Stack) = new char[size];
        std::get<1>(ctx.Stack) = size;
    }
    memcpy(std::get<0>(ctx.Stack), ctx.Low, size);
}

void Engine::Restore(context &ctx) {
    char curr_pos;
    if (&curr_pos >= ctx.Low) {
        Restore(ctx);
    }
    memcpy(ctx.Low, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    context *calling = alive;
    if (calling == cur_routine && calling != nullptr) {
        calling = calling->next;
    }

    if (calling == nullptr) {
        return;
    }
    sched(calling);
}

void Engine::sched(void *routine_) {
    context *ctx = static_cast<context *>(routine_);
    if (ctx != nullptr) {
        if (setjmp(cur_routine->Environment) > 0) {
            return;
        }
        Store(*cur_routine);
    }
    cur_routine = ctx;
    Restore(*cur_routine);
}

} // namespace Coroutine
} // namespace Afina

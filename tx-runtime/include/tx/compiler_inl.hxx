#pragma once

#include "tx/compiler.hxx"
//
#include "tx/chunk.hxx"
#include "tx/common.hxx"
#include "tx/debug.hxx"
#include "tx/object.hxx"
#include "tx/scanner.hxx"
#include "tx/utils.hxx"
#include "tx/vm.hxx"

#include <limits>
#include <ranges>
#include <functional>

namespace tx {

static inline constexpr bool
identifiers_equal(const Token& lhs, const Token& rhs) {
    return lhs.lexeme == rhs.lexeme;
}

// clang-format off
inline constexpr std::array opcode_stack_effect_table = {
    // NOLINTNEXTLINE(*-macro-usage)
    #define TX_OPCODE(name, length, stack_effect) stack_effect,
    #include "tx/opcodes.inc"
    #undef TX_OPCODE
};
// clang-format on

inline constexpr i32 get_opcode_stack_effect(OpCode opc, size_t operand) {
    using enum OpCode;
    if (opc == CALL || opc == END_SCOPE || opc == END_SCOPE_LONG) {
        return -operand;
    }
    return gsl::at(opcode_stack_effect_table, to_underlying(opc));
}

inline constexpr bool ParseResult::is_block_expr() const noexcept {
    switch (token_type) {
        case LEFT_BRACE:
        case IF:
        case LOOP: return true;
        default: return false;
    };
}

[[nodiscard]] inline ObjFunction*
Parser::compile(std::string_view file_path, std::string_view source) noexcept {
    assert(scanner == nullptr);
    Scanner lex(parent_vm, source);
    scanner = &lex;
    module_file_path = file_path;
    Compiler comp{};
    begin_compiler(comp, FunctionType::SCRIPT, std::nullopt);
    advance();
    while (!match(TokenType::END_OF_FILE)) { statement(); }
    emit_instruction(OpCode::NIL);
    auto& fun = end_compiler();
    comp.destroy(parent_vm);
    // TODO: Maybe do not do this at all
    if (!parent_vm.options.allow_end_compile_with_undefined_global) {
        for (const auto& [name, idx] : parent_vm.global_indices) {
            const auto& global =
                parent_vm.global_signatures[size_cast(idx.as_int())];
            if (!global.is_defined) {
                error(
                    FMT_STRING("Global variable '{}' declared but not defined."
                    ),
                    name.as_object().as<ObjString>()
                );
            }
        }
    }
    module_file_path = std::string_view();
    scanner = nullptr;
    return had_error ? nullptr : &fun;
}

inline void Parser::error_at_impl1(const Token& token) noexcept {
    panic_mode = true;
    had_error = true;
    fmt::print(stderr, FMT_STRING("Syntax error"));
    if (token.type == TokenType::END_OF_FILE) {
        fmt::print(stderr, FMT_STRING(" at end: "));
    } else if (token.type == TokenType::ERROR) {
        fmt::print(stderr, FMT_STRING(": "));
    } else {
        fmt::print(stderr, FMT_STRING(" at '{:s}': "), token.lexeme);
    }
}

// FIXME: optionally underlying other token and more lines
inline void Parser::error_at_impl2(const Token& token) noexcept {
    fmt::print(stderr, FMT_STRING("\n"));
    size_t line_number = token.line;
    bool is_at_end = (token.type == TokenType::END_OF_FILE)
                     || (token.type == TokenType::ERROR
                         // cppcheck-suppress mismatchingContainerExpression
                         && token.lexeme.cend() == scanner->source.cend());
    if (is_at_end) { line_number -= 1; }
    const auto line = get_text_of_line(scanner->source, line_number);
    i64 col = 1;
    std::size_t len = 1;
    bool mark_start = true;
    assert(
        // cppcheck-suppress mismatchingContainerExpression
        scanner->source.cbegin() <= token.lexeme.cbegin()
        // cppcheck-suppress mismatchingContainerExpression
        // cppcheck-suppress knownConditionTrueFalse ; FIXME: False positive?
        && token.lexeme.cend() <= scanner->source.cend()
    );
    // FIXME: utf8 distance not byte distance
    // cppcheck-suppress mismatchingContainerExpression
    col = std::distance(line.cbegin(), token.lexeme.cbegin());
    len = token.lexeme.size();
    auto blanks = col;
    if (col < 0 || is_at_end) {
        // cppcheck-suppress mismatchingContainerExpression
        col = std::distance(line.cbegin(), token.lexeme.cend()) - 1;
        blanks = col + 1 - static_cast<i64>(len);
        if (blanks < 0) {
            len = static_cast<std::size_t>(col) + 1;
            blanks = col + 1 - static_cast<i64>(len);
        }
        mark_start = false;
    }
    if (col < 0) { col = 0; }
    if (token.type == TokenType::END_OF_FILE) { col -= 1; }

    fmt::print(
        stderr,
        FMT_STRING("  {:s}:{:d}:{:d}\n"),
        module_file_path.empty() ? "<unknown>" : module_file_path,
        line_number,
        col + 1
    );
    const auto line_num_digits = count_digit(token.line) + 2;
    fmt::print(
        stderr,
        FMT_STRING("{0: >{2}d} | {1:s}\n"),
        line_number,
        line,
        line_num_digits
    );
    const auto* fmt = mark_start ? "{0: >{3}} | {1: >{4}}{2:~<{5}}\n"
                                : "{0: >{3}} | {1: >{4}}{2:~>{5}}\n";
    fmt::print(
        stderr,
        fmt::runtime(fmt),
        "",
        "",
        "^",
        line_num_digits,
        blanks,
        len
    );
}

inline constexpr void Parser::advance() noexcept {
    previous = current;
    for (;;) {
        current = scanner->scan_token();
        if constexpr (HAS_DEBUG_FEATURES) {
            if (parent_vm.options.print_tokens) { print_token(current); }
        }
        if (current.type != TokenType::ERROR) { break; }
        error_at_current(
            FMT_STRING("{:s}"),
            current.value.as_object().as<ObjString>()
        );
    }
}

inline constexpr void
Parser::consume(TokenType type, std::string_view message) noexcept {
    if (check(type)) {
        advance();
        return;
    }
    error_at_current(FMT_STRING("{:s}"), message);
}

[[nodiscard]] inline constexpr bool Parser::check(TokenType type
) const noexcept {
    return current.type == type;
}

[[nodiscard]] inline constexpr bool Parser::match(TokenType type) noexcept {
    if (!check(type)) { return false; }
    advance();
    return true;
}

[[nodiscard]] inline constexpr Chunk& Parser::current_chunk() const noexcept {
    return current_compiler->function->chunk;
}

[[nodiscard]] inline constexpr size_t Parser::add_constant(Value value
) noexcept {
    // if (had_error) { return -1; }
    const auto* existing = current_compiler->constant_indices.get(value);
    if (existing != nullptr) { return static_cast<size_t>(existing->as_int()); }
    // TODO: error if too much constants
    parent_vm.push(value);
    auto idx = current_chunk().add_constant(parent_vm, value);
    current_compiler->constant_indices
        .set(parent_vm, value, Value{static_cast<int_t>(idx)});
    parent_vm.pop();
    return idx;
}

// TODO: pass long and short opcode instead of adding 1
inline constexpr void
Parser::emit_var_length_instruction(OpCode opc, size_t idx) noexcept {
    assert(idx < size_cast(1U << 24U));
    if (idx < size_cast(1U << 8U)) {
        assert(1 == get_byte_count_following_opcode(opc));
        emit_instruction<1>(opc, idx);
        return;
    }
    if (idx < size_cast(1U << 24U)) {
        opc = OpCode(to_underlying(opc) + 1);
        assert(3 == get_byte_count_following_opcode(opc));
        emit_instruction<3>(opc, idx);
        return;
    }
    unreachable();
}

inline constexpr void Parser::emit_constant(Value value) noexcept {
    auto idx = add_constant(value);
    emit_var_length_instruction(OpCode::CONSTANT, idx);
}

inline constexpr void
Parser::emit_closure(Compiler& compiler, ObjFunction& function) noexcept {
    emit_var_length_instruction(
        OpCode::CLOSURE,
        add_constant(Value{&function})
    );
    assert(function.upvalue_count == compiler.upvalues.size());
    for (const auto& upvalue : compiler.upvalues) {
        assert(upvalue.index >= 0);
        assert(upvalue.index < size_cast(1U << 24U));
        const u8 length = [](usize idx) {
            u8 result = 0;
            while (idx != 0) {
                idx >>= 8U;
                ++result;
            }
            return result == 0 ? u8{1} : result;
        }(static_cast<usize>(upvalue.index));
        assert(length == 1);
        assert(length >= 1);
        assert(length <= 3);
        const u8 flags = static_cast<u8>(
            static_cast<u32>(upvalue.is_local << 7U) | (length & 0b01111111U)
        );
        assert(length == (flags & 0b01111111U));
        assert(upvalue.is_local == bool(flags & 0b10000000U));
        emit_bytes(flags);
        if (length == 1) {
            current_chunk().write_multibyte_operand<1>(
                parent_vm,
                previous.line,
                upvalue.index
            );
            continue;
        }
        if (length == 2) {
            current_chunk().write_multibyte_operand<2>(
                parent_vm,
                previous.line,
                upvalue.index
            );
            continue;
        }
        if (length == 3) {
            current_chunk().write_multibyte_operand<3>(
                parent_vm,
                previous.line,
                upvalue.index
            );
            continue;
        }
        unreachable();
    }
}

[[nodiscard]] inline constexpr size_t Parser::emit_jump(OpCode instruction
) noexcept {
    emit_instruction<2>(instruction, 0xffff);
    return current_chunk().code.size() - 2;
}

inline constexpr void Parser::emit_loop(size_t loop_start) noexcept {
    auto offset = current_chunk().code.size() - loop_start + 2 + 1;
    if (offset > std::numeric_limits<u16>::max()) {
        error(FMT_STRING("Loop body too large."));
    }
    emit_instruction<2>(OpCode::LOOP, offset);
}

inline constexpr void Parser::patch_jump(i32 offset) noexcept {
    auto jump_distance = current_chunk().code.size() - offset - 2;
    if (jump_distance > std::numeric_limits<u16>::max()) {
        error(FMT_STRING("Too much code to jump over."));
    }
    auto* ptr = &current_chunk().code[offset];
    ::tx::write_multibyte_operand<2>(ptr, jump_distance);
}

inline void Parser::begin_compiler(
    Compiler& compiler,
    FunctionType type,
    std::optional<std::string_view> name_opt
) noexcept {
    compiler.enclosing = current_compiler;
    compiler.function_type = type;
    compiler.function = allocate_object<ObjFunction>(
        parent_vm,
        compiler.locals.size(),
        module_file_path
    );
    current_compiler = &compiler;
    // Reserve first local for methods, use empty string as name to prevent use
    compiler.locals.emplace_back(parent_vm, Token{.lexeme = ""}, 0, true);
    if (type != FunctionType::SCRIPT && name_opt.has_value()) {
        compiler.function->name = make_string(
            parent_vm,
            !parent_vm.options.allow_pointer_to_source_content,
            name_opt.value()
        );
    }
}

[[nodiscard]] inline constexpr ObjFunction& Parser::end_compiler() noexcept {
    emit_instruction(OpCode::RETURN);
    auto& fun = *current_compiler->function;
    if constexpr (HAS_DEBUG_FEATURES) {
        if (parent_vm.get_options().print_bytecode) {
            if (!had_error) {
                // fmt::print(FMT_STRING("Argument count: {:d}\n"), fun.arity);
                // fmt::print(
                //     FMT_STRING("Upvalue count: {:d}\n"),
                //     fun.upvalue_count
                // );
                // fmt::print(FMT_STRING("Max slots: {:d}\n"), fun.max_slots);
                disassemble_chunk(current_chunk(), fun.get_display_name());
            }
        }
    }
    current_compiler = current_compiler->enclosing;
    return fun;
}

inline constexpr void Parser::begin_scope() noexcept {
    current_compiler->scope_depth++;
}

inline constexpr void Parser::end_scope() noexcept {
    size_t scope_local_count = 0;
    current_compiler->scope_depth--;
    while (!current_compiler->locals.empty()
           && current_compiler->locals.back().depth
                  > current_compiler->scope_depth) {
        ++scope_local_count;
        current_compiler->locals.pop_back();
    }
    if (scope_local_count > 0) {
        emit_var_length_instruction(OpCode::END_SCOPE, scope_local_count);
    }
}

inline constexpr void
Parser::begin_loop(Loop& loop, bool is_loop_expr) noexcept {
    loop.enclosing = current_compiler->innermost_loop;
    loop.start = current_chunk().code.size();
    loop.is_loop_expr = is_loop_expr;
    loop.scope_depth = current_compiler->scope_depth;
    current_compiler->innermost_loop = &loop;
}

inline constexpr void Parser::patch_jumps_in_innermost_loop() noexcept {
    for (size_t i = current_compiler->innermost_loop->start;
         i < current_chunk().code.size();) {
        ByteCode& btc = current_chunk().code[i];
        if (btc.as_opcode() == OpCode::CLOSURE
            || btc.as_opcode() == OpCode::CLOSURE_LONG) {
            const auto constant_idx = [&]() {
                if (btc.as_opcode() == OpCode::CLOSURE) {
                    return read_multibyte_operand<1>(
                        std::next(current_chunk().code.begin(), i + 1)
                    );
                }
                if (btc.as_opcode() == OpCode::CLOSURE_LONG) {
                    return read_multibyte_operand<3>(
                        std::next(current_chunk().code.begin(), i + 1)
                    );
                }
                unreachable();
            }();
            const auto& fun = current_chunk()
                                  .constants[constant_idx]
                                  .as_object()
                                  .as<ObjFunction>();
            i += 1 + 1;
            for (auto j = 0; j < fun.upvalue_count; ++j) {
                auto [is_local, index, len] = read_closure_operand(
                    std::next(current_chunk().code.begin(), i)
                );
                i += len;
            }
            continue;
        }
        if (btc.as_opcode() == OpCode::END) {
            btc = ByteCode{OpCode::JUMP};
            patch_jump(i + 1);
        }
        i += 1 + get_byte_count_following_opcode(btc.as_opcode());
    }
}

inline constexpr void Parser::end_loop() noexcept {
    patch_jumps_in_innermost_loop();
    current_compiler->innermost_loop = current_compiler->innermost_loop
                                           ->enclosing;
}

inline constexpr TypeInfo
Parser::binary(TypeInfo lhs, bool /*can_assign*/) noexcept {
    auto token_type = previous.type;
    auto rule = get_rule(token_type);
    auto rhs = parse_precedence(
        static_cast<Precedence>(to_underlying(rule.precedence) + 1)
    );
    (void)lhs;
    (void)rhs;
    switch (token_type) {
        using enum TokenType;
        case BANG_EQUAL: emit_instruction(OpCode::NOT_EQUAL); break;
        case EQUAL_EQUAL: emit_instruction(OpCode::EQUAL); break;
        case LEFT_CHEVRON: emit_instruction(OpCode::LESS); break;
        case LESS_EQUAL: emit_instruction(OpCode::LESS_EQUAL); break;
        case RIGHT_CHEVRON: emit_instruction(OpCode::GREATER); break;
        case GREATER_EQUAL: emit_instruction(OpCode::GREATER_EQUAL); break;
        case PLUS: emit_instruction(OpCode::ADD); break;
        case MINUS: emit_instruction(OpCode::SUBSTRACT); break;
        case STAR: emit_instruction(OpCode::MULTIPLY); break;
        case SLASH: emit_instruction(OpCode::DIVIDE); break;
        default: unreachable();
    }
    return TypeInfo{};
}

[[nodiscard]] inline constexpr u8 Parser::argument_list() {
    u8 arg_count = 0;
    do {
        if (check(RIGHT_PAREN)) { break; }
        expression();
        if (arg_count == 255) {
            error(FMT_STRING("Can't have more than 255 arguments."));
        }
        ++arg_count;
    } while (match(COMMA));
    consume(RIGHT_PAREN, "Expect ')' after arguments.");
    return arg_count;
}

inline constexpr TypeInfo
Parser::call(TypeInfo lhs, bool /*can_assign*/) noexcept {
    // TODO: check last expresion is callable
    auto arg_count = argument_list();
    emit_instruction<1>(OpCode::CALL, arg_count);
    (void)lhs;
    return TypeInfo{};
}

inline constexpr TypeInfo Parser::grouping(bool /*can_assign*/) noexcept {
    auto result = expression();
    (void)result;
    consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
    return TypeInfo{};
}

inline constexpr TypeInfo Parser::literal(bool /*can_assign*/) noexcept {
    switch (previous.type) {
        case NIL: emit_instruction(OpCode::NIL); break;
        case FALSE: emit_instruction(OpCode::FALSE); break;
        case TRUE: emit_instruction(OpCode::TRUE); break;
        case FLOAT_LITERAL:
        case INTEGER_LITERAL:
        case STRING_LITERAL: emit_constant(previous.value); break;
        default: unreachable();
    }
    return TypeInfo{};
}

inline constexpr i32
Parser::resolve_local(Compiler& compiler, const Token& name) noexcept {
    // TODO: use ranges, enumerate+reverse
    for (i32 i = compiler.locals.size() - 1; i >= 0; --i) {
        const Local& local = compiler.locals[i];
        if (identifiers_equal(name, local.name)) {
            if (local.depth == -1) {
                error(FMT_STRING(
                    "Can't read local variable in its own initializer."
                ));
            }
            return i;
        }
    }
    return -1;
}

inline constexpr size_t Parser::add_upvalue(
    Compiler& compiler,
    size_t index,
    bool is_local,
    bool is_const
) noexcept {
    auto upvalue_count = compiler.function->upvalue_count;
    for (size_t i = 0; i < upvalue_count; ++i) {
        const auto& upvalue = compiler.upvalues[i];
        if (upvalue.index == index && upvalue.is_local == is_local) {
            return i;
        }
    }
    if (upvalue_count == MAX_UPVALUES) {
        error(FMT_STRING("Too much closure variables in function."));
        return 0;
    }
    compiler.upvalues.emplace_back(parent_vm, index, is_local, is_const);
    return compiler.function->upvalue_count++;
}

inline constexpr i32
Parser::resolve_upvalue(Compiler& compiler, const Token& name) noexcept {
    if (compiler.enclosing == nullptr) { return -1; }
    auto local_idx = resolve_local(*compiler.enclosing, name);
    if (local_idx != -1) {
        auto& local = compiler.enclosing->locals[local_idx];
        local.is_captured = true;
        return add_upvalue(compiler, local_idx, true, local.is_const);
    }
    auto upvalue_idx = resolve_upvalue(*compiler.enclosing, name);
    if (upvalue_idx != -1) {
        const auto& upvalue = compiler.enclosing->upvalues[upvalue_idx];
        return add_upvalue(compiler, upvalue_idx, false, upvalue.is_const);
    }
    return -1;
}

inline constexpr TypeInfo
Parser::named_variable(const Token& name, bool can_assign) noexcept {
    OpCode get_op{};
    OpCode set_op{};
    auto is_const = true;
    auto idx = resolve_local(*current_compiler, name);
    if (idx != -1) {
        get_op = OpCode::GET_LOCAL;
        set_op = OpCode::SET_LOCAL;
        is_const = current_compiler->locals[idx].is_const;
    } else if ((idx = resolve_upvalue(*current_compiler, name)) != -1) {
        get_op = OpCode::GET_UPVALUE;
        set_op = OpCode::SET_UPVALUE;
        is_const = current_compiler->upvalues[idx].is_const;
    } else if ((idx = resolve_global(name)) != -1) {
        get_op = OpCode::GET_GLOBAL;
        set_op = OpCode::SET_GLOBAL;
        is_const = parent_vm.global_signatures[idx].is_const;
        if (current_compiler->scope_depth == 0) {
            if (!parent_vm.global_signatures[idx].is_defined) {
                error(FMT_STRING(
                    "Use of forward declared global before definition."
                ));
            }
        }
    } else {
        error(FMT_STRING("Cannot find value with this name in current scope."));
        return TypeInfo{};
    }
    if (can_assign && match(EQUAL)) {
        if (is_const) { error(FMT_STRING("Immutable assignment target.")); }
        auto rhs = expression();
        (void)rhs;
        emit_var_length_instruction(set_op, idx);
    } else {
        emit_var_length_instruction(get_op, idx);
    }
    return TypeInfo{};
}

inline constexpr TypeInfo Parser::variable(bool can_assign) noexcept {
    return named_variable(previous, can_assign);
}

inline constexpr TypeInfo Parser::unary(bool /*can_assign*/) noexcept {
    auto token_type = previous.type;
    auto rhs = parse_precedence(Precedence::UNARY);
    switch (token_type) {
        case TokenType::BANG: emit_instruction(OpCode::NOT); break;
        case TokenType::MINUS: emit_instruction(OpCode::NEGATE); break;
        default: unreachable();
    }
    (void)rhs;
    return TypeInfo{};
}

inline constexpr TypeInfo Parser::block_no_scope() noexcept {
    bool has_final_expression = false;
    while (!check(RIGHT_BRACE) && !check(END_OF_FILE)) {
        auto expr_opt = statement_or_expression();
        if (expr_opt.has_value()) {
            switch (current.type) {
                case RIGHT_BRACE: has_final_expression = true; break;
                case SEMICOLON:
                    advance();
                    emit_instruction(OpCode::POP);
                    continue;
                default:
                    if (expr_opt.value().is_block_expr()) {
                        emit_instruction(OpCode::POP);
                        continue;
                    }
                    error(FMT_STRING(
                        "Expect ';' or '}}' after expression inside block."
                    ));
            }
        }
    }
    consume(RIGHT_BRACE, "Expect '}' after block.");
    if (!has_final_expression) { emit_instruction(OpCode::NIL); }
    return TypeInfo{};
}

inline constexpr TypeInfo Parser::block(bool /*can_assign*/) noexcept {
    begin_scope();
    auto result = block_no_scope();
    end_scope();
    return result;
}

inline constexpr TypeInfo Parser::if_expr(bool /*can_assign*/) noexcept {
    expression();
    auto then_jump = emit_jump(OpCode::JUMP_IF_FALSE);
    emit_instruction(OpCode::POP);
    consume(LEFT_BRACE, "Expect '{' before if body.");
    auto then_result = block();
    (void)then_result;
    auto else_jump = emit_jump(OpCode::JUMP);
    patch_jump(then_jump);
    emit_instruction(OpCode::POP);
    TypeInfo else_result;
    if (match(ELSE)) {
        switch (current.type) {
            case IF:
                advance();
                else_result = if_expr();
                break;
            case LEFT_BRACE:
                advance();
                else_result = block();
                break;
            default:
                error_at_current(FMT_STRING("Expect '{{' before else body."));
        }
    } else {
        else_result = TypeInfo{};
        emit_instruction(OpCode::NIL);
    }
    (void)else_result;
    patch_jump(else_jump);
    return TypeInfo{};
}

inline constexpr TypeInfo Parser::loop_expr(bool /*can_assign*/) noexcept {
    Loop loop{};
    begin_loop(loop, true);
    consume(LEFT_BRACE, "Expect '{' after 'loop'.");
    auto result = block();
    (void)result;  // TODO: make sure no final expression as it is meaningless
    emit_instruction(OpCode::POP);
    emit_loop(loop.start);
    end_loop();
    return TypeInfo{};
}

inline TypeInfo Parser::fn_expr(bool /*can_assign*/) noexcept {
    consume(LEFT_PAREN, "Expect '(' after 'fn'.");
    auto params = parameter_list();
    consume(LEFT_BRACE, "Expect '{' before function body.");
    function_body(FunctionType::FUNCTION, "", params);
    params.destroy(parent_vm);
    return TypeInfo{};
}

inline constexpr TypeInfo
Parser::and_(TypeInfo lhs, bool /*can_assign*/) noexcept {
    auto end_jump = emit_jump(OpCode::JUMP_IF_FALSE);
    emit_instruction(OpCode::POP);
    auto rhs = parse_precedence(Precedence::AND);
    (void)lhs;
    (void)rhs;
    patch_jump(end_jump);
    return TypeInfo{};
}

inline constexpr TypeInfo
Parser::or_(TypeInfo lhs, bool /*can_assign*/) noexcept {
    auto else_jump = emit_jump(OpCode::JUMP_IF_FALSE);
    auto end_jump = emit_jump(OpCode::JUMP);
    patch_jump(else_jump);
    emit_instruction(OpCode::POP);
    auto rhs = parse_precedence(Precedence::OR);
    (void)lhs;
    (void)rhs;
    patch_jump(end_jump);
    return TypeInfo{};
}

inline constexpr const ParseRule& Parser::get_rule(TokenType token_type
) noexcept {
    return ParseRules::get_rule(token_type);
}

inline constexpr ParseResult
Parser::parse_precedence(Precedence precedence, bool do_advance) noexcept {
    if (do_advance) { advance(); }
    auto token_type = previous.type;
    const ParsePrefixFn prefix_rule = get_rule(previous.type).prefix;
    if (prefix_rule == nullptr) {
        error(FMT_STRING("Expect expression."));
        return ParseResult{.token_type = ERROR, .type_info = {}};
    }
    const bool can_assign = precedence <= Precedence::ASSIGNMENT;
    auto result = std::invoke(prefix_rule, *this, can_assign);
    while (precedence <= get_rule(current.type).precedence) {
        advance();
        token_type = previous.type;
        const ParseInfixFn infix_rule = get_rule(previous.type).infix;
        result = std::invoke(infix_rule, *this, result, can_assign);
    }
    if (can_assign && match(EQUAL)) {
        error(FMT_STRING("Invalid assignment target."));
    }
    return ParseResult{.token_type = token_type, .type_info = result};
}

inline i32 Parser::resolve_global(const Token& name) noexcept {
    auto identifier = Value{make_string(
        parent_vm,
        !parent_vm.get_options().allow_pointer_to_source_content,
        name.lexeme
    )};
    auto* index = parent_vm.global_indices.get(identifier);
    if (index != nullptr) { return static_cast<size_t>(index->as_int()); }
    return -1;
}

inline size_t Parser::add_global(Value identifier, Global sig) noexcept {
    parent_vm.push(identifier);
    auto result = parent_vm.define_global(identifier, sig, Value{val_none});
    parent_vm.pop();
    return result;
}

inline size_t Parser::declare_global_variable(bool is_const) noexcept {
    assert(current_compiler->scope_depth == 0);
    Global sig{.is_const = is_const};
    const auto& name = previous;
    auto identifier = Value{make_string(
        parent_vm,
        !parent_vm.get_options().allow_pointer_to_source_content,
        name.lexeme
    )};
    const auto* idx_ptr = parent_vm.global_indices.get(identifier);
    if (idx_ptr != nullptr) {
        const auto idx = size_cast(idx_ptr->as_int());
        const auto global = parent_vm.global_signatures[idx];
        if (sig == global) {
            if (!global.is_defined) {
                // definition of a (forward) declared but not yet defined var
            } else if (!parent_vm.options.allow_global_redefinition) {
                error(FMT_STRING("Redefinition of global variable."));
            }
        } else {
            error(FMT_STRING("Redeclaration of global variable."));
        }
    }
    return idx_ptr == nullptr ? add_global(identifier, sig)
                              : size_cast(idx_ptr->as_int());
}

inline constexpr void
Parser::add_local(const Token& name, bool is_const) noexcept {
    if (current_compiler->locals.size() == LOCALS_MAX) {
        error(FMT_STRING("Too many local variables in function."));
        return;
    }
    current_compiler->locals.emplace_back(parent_vm, name, -1, is_const);
}

inline constexpr void
Parser::declare_local_variable(const Token& name, bool is_const) noexcept {
    assert(current_compiler->scope_depth > 0);
    // const auto& name = previous;
    // TODO: use ranges
    for (auto i = current_compiler->locals.size() - 1; i >= 0; --i) {
        const auto& local = current_compiler->locals[i];
        if (local.depth != -1 && local.depth < current_compiler->scope_depth) {
            break;
        }
        if (identifiers_equal(name, local.name)) {
            error_at(
                name,
                FMT_STRING("Already a variable with this name in this scope.")
            );
        }
    }
    add_local(name, is_const);
}

inline constexpr i32 Parser::parse_variable(std::string_view error_message
) noexcept {
    auto is_const = previous.type != VAR;
    if (!match(TokenType::IDENTIFIER)) {
        error_at_current(FMT_STRING("{:s}"), error_message);
        return -1;
    }
    if (current_compiler->scope_depth > 0) {
        declare_local_variable(previous, is_const);
        return -1;
    }
    return declare_global_variable(is_const);
}

inline constexpr void Parser::mark_initialized(i32 global_idx) noexcept {
    if (current_compiler->scope_depth == 0) {
        assert(global_idx >= 0);
        parent_vm.global_signatures[global_idx].is_defined = true;
        return;
    }
    current_compiler->locals.back().depth = current_compiler->scope_depth;
}

inline constexpr void Parser::define_variable(i32 global_idx) noexcept {
    mark_initialized(global_idx);
    if (current_compiler->scope_depth == 0) {
        assert(global_idx >= 0);
        emit_var_length_instruction(OpCode::DEFINE_GLOBAL, global_idx);
    }
}

inline constexpr ParseResult Parser::expression(bool do_advance) noexcept {
    return parse_precedence(Precedence::ASSIGNMENT, do_advance);
}

inline constexpr ParameterList Parser::parameter_list() noexcept {
    ParameterList result;
    do {
        if (check(RIGHT_PAREN)) { break; }
        if (result.size() == 255) {
            error_at_current(FMT_STRING("Can't have more than 255 parameters.")
            );
        }
        // TODO: need in, out or inout (in by default)
        // what about out for immutable types? -> error
        // FIXME: duplicate code with parse_variable, use consume? in both
        // places
        auto is_const = previous.type != OUT;
        if (!match(TokenType::IDENTIFIER)) {
            error_at_current(FMT_STRING("Expect parameter name."));
        }
        result.emplace_back(parent_vm, previous, is_const);
    } while (match(COMMA));
    consume(RIGHT_PAREN, "Expect ')' after parameters.");
    return result;
}

inline void Parser::function_body(
    FunctionType type,
    std::string_view name,
    const ParameterList& params
) noexcept {
    Compiler compiler;
    begin_compiler(compiler, type, name);
    begin_scope();
    current_compiler->function->arity = params.size();
    for (const auto& param : params) {
        declare_local_variable(param.name, param.is_const);
        define_variable(-1);
    }
    auto block_result = block_no_scope();
    (void)block_result;
    // end_scope(); // NOTE: Not necessary
    auto& function = end_compiler();
    emit_closure(compiler, function);
    compiler.destroy(parent_vm);
}

inline void Parser::fn_declaration() noexcept {
    auto global_idx = parse_variable("Expect funtion name.");
    auto name = previous.lexeme;
    consume(LEFT_PAREN, "Expect '(' after function name.");
    auto params = parameter_list();
    if (match(LEFT_BRACE)) {
        mark_initialized(global_idx);
        function_body(FunctionType::FUNCTION, name, params);
        define_variable(global_idx);
        // NOTE: Do not permit trailing semicolon after inline fn definition
        // (for now)
        // (void)match(SEMICOLON);
    } else {
        consume(SEMICOLON, "Expect '{' or ';' after function declaration.");
    }
    params.destroy(parent_vm);
}

inline constexpr void Parser::var_declaration() noexcept {
    auto global_idx = parse_variable("Expect variable name.");
    if (match(TokenType::EQUAL)) {
        expression();
        define_variable(global_idx);
    } else {
        if (current_compiler->scope_depth > 0) {
            error(FMT_STRING(
                "Local variable should be initialized in declaration."
            ));
        }
    }
    consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");
}

inline constexpr void Parser::while_statement() noexcept {
    Loop loop{};
    begin_loop(loop);
    expression();
    auto exit_jump = emit_jump(OpCode::JUMP_IF_FALSE);
    emit_instruction(OpCode::POP);
    consume(LEFT_BRACE, "Expect '{' before while body.");
    const auto block_result = block();
    (void)block_result;
    emit_instruction(OpCode::POP);
    emit_loop(loop.start);
    patch_jump(exit_jump);
    emit_instruction(OpCode::POP);
    end_loop();
    (void)match(SEMICOLON);
}

inline constexpr void Parser::emit_pop_innermost_loop(bool skip_top_expression
) noexcept {
    size_t loop_local_count = 0;
    for (size_t i = current_compiler->locals.size() - 1;
         i >= 0
         && current_compiler->locals[i].depth
                > current_compiler->innermost_loop->scope_depth;
         --i) {
        if (!skip_top_expression) { emit_instruction(OpCode::POP); }
        ++loop_local_count;
    }
    if (skip_top_expression && loop_local_count > 0) {
        emit_var_length_instruction(OpCode::END_SCOPE, loop_local_count);
    }
}

inline constexpr void Parser::break_statement() noexcept {
    if (current_compiler->innermost_loop == nullptr) {
        error(FMT_STRING("Can't use 'break' outside of a loop."));
    }
    const bool is_loop_expr = current_compiler->innermost_loop->is_loop_expr;
    if (is_loop_expr) {
        if (match(SEMICOLON)) {
            emit_instruction(OpCode::NIL);
        } else {
            expression();
            consume(SEMICOLON, "Expect ; after break return expression.");
        }
    } else {
        consume(SEMICOLON, "Expect ; after 'break'.");
    }
    emit_pop_innermost_loop(is_loop_expr);
    // emit jump with placeholder opcode that will need patching later
    (void)emit_jump(OpCode::END);
}

inline constexpr void Parser::continue_statement() noexcept {
    if (current_compiler->innermost_loop == nullptr) {
        error(FMT_STRING("Can't use 'continue' outside of a loop."));
    }
    consume(SEMICOLON, "Expect ; after 'continue'.");
    emit_pop_innermost_loop(false);
    emit_loop(current_compiler->innermost_loop->start);
}

inline constexpr void Parser::return_statement() noexcept {
    if (current_compiler->function_type == FunctionType::SCRIPT) {
        error(FMT_STRING("Can't return from top-level code."));
    }
    if (match(SEMICOLON)) {
        emit_instruction(OpCode::NIL);
    } else {
        expression();
        consume(SEMICOLON, "Expect ';' after return value.");
    }
    emit_instruction(OpCode::RETURN);
}

inline constexpr void Parser::synchronize() noexcept {
    panic_mode = false;
    while (current.type != TokenType::END_OF_FILE) {
        if (previous.type == TokenType::SEMICOLON) { return; }
        switch (current.type) {
            using enum TokenType;
            case STRUCT:
            case FN:
            case LET:
            case VAR:
            case IF:
            case MATCH:
            case LOOP:
            case WHILE:
            case FOR:
            case RETURN:
            case IMPORT: return;
            default:;  // Do nothing.
        }
        advance();
    }
}

inline constexpr std::optional<ParseResult> Parser::statement_or_expression(
) noexcept {
    // NOTE: Do not allow useless ; (for now)
    // if (match(SEMICOLON)) { return true; }
    if (match(FN)) {
        if (check(IDENTIFIER)) {
            fn_declaration();
            return std::nullopt;
        }
        if (!check(LEFT_PAREN)) {
            error(FMT_STRING("Expect function name or '(' after 'fn'."));
        }
        return expression(false);
    }
    if (match(VAR) || match(LET)) {
        var_declaration();
        return std::nullopt;
    }
    if (match(RETURN)) {
        return_statement();
        return std::nullopt;
    }
    if (match(WHILE)) {
        while_statement();
        return std::nullopt;
    }
    if (match(BREAK)) {
        break_statement();
        return std::nullopt;
    }
    if (match(CONTINUE)) {
        continue_statement();
        return std::nullopt;
    }
    return expression();
}

inline constexpr void Parser::statement() noexcept {
    auto expr_opt = statement_or_expression();
    if (expr_opt.has_value()) {
        if (expr_opt.value().is_block_expr()) {
            (void)match(SEMICOLON);
        } else {
            consume(TokenType::SEMICOLON, "Expect ';' after expression.");
        }
        emit_instruction(OpCode::POP);
    }
    if (panic_mode) { synchronize(); }
}

}  // namespace tx

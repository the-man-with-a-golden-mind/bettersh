#include "vm.h"

#include "disasm.h"
#include "map.h"
#include "module.h"
#include "vm_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vm_fail(VM *vm, const char *msg) {
    if (vm->failed) {
        return;
    }
    vm->failed = 1;
    vm->status = 2;
    int line = vm->line > 0 ? vm->line : 1;
    const char *file = vm->filename ? vm->filename : "bettersh";
    if (vm->nframes > 0) {
        Frame *fr = &vm->frames[vm->nframes - 1];
        if (fr->chunk && fr->chunk->source_name && fr->chunk->source_name->len > 0) {
            file = fr->chunk->source_name->data;
        }
    }
    fprintf(stderr, "%s:%d:1: %s\n", file, line, msg ? msg : "error");
}

int vm_truthy(const Value *value) {
    if (!value) {
        return 0;
    }
    if (value->kind == VAL_NULL) {
        return 0;
    }
    if (value->kind == VAL_BOOL) {
        return value->u.b != 0;
    }
    if (value->kind == VAL_INT) {
        return value->u.i != 0;
    }
    if (value->kind == VAL_FLOAT) {
        return value->u.f != 0.0;
    }
    return 1;
}

int vm_push(VM *vm, Value *value) {
    if (vm->sp >= vm->cap) {
        int cap = vm->cap > 0 ? vm->cap * 2 : 64;
        vm->stack = xrealloc(vm->stack, (size_t)cap * sizeof(Value *));
        vm->cap = cap;
    }
    vm->stack[vm->sp++] = value ? value : value_null();
    return 0;
}

Value *vm_pop(VM *vm) {
    if (vm->nframes <= 0) {
        vm_fail(vm, "stack underflow");
        return NULL;
    }
    Frame *fr = &vm->frames[vm->nframes - 1];
    if (vm->sp <= fr->base + fr->nlocals) {
        vm_fail(vm, "stack underflow");
        return NULL;
    }
    return vm->stack[--vm->sp];
}

static int slot_captured(const Chunk *chunk, int slot) {
    if (!chunk || slot < 0 || slot > 255) {
        return 0;
    }
    return (chunk->captured[slot >> 3] & (uint8_t)(1u << (slot & 7))) != 0;
}

static void open_cells(VM *vm, Frame *fr) {
    int n = fr->nlocals;
    int any = 0;
    for (int i = 0; i < n; i++) {
        if (slot_captured(fr->chunk, i)) {
            any = 1;
            break;
        }
    }
    if (!any) {
        fr->cells = NULL;
        return;
    }
    fr->cells = xmalloc((size_t)n * sizeof(Cell *));
    for (int i = 0; i < n; i++) {
        fr->cells[i] = NULL;
        if (slot_captured(fr->chunk, i)) {
            fr->cells[i] = cell_new(vm->stack[fr->base + i]);
        }
    }
}

static void close_cells(Frame *fr) {
    if (!fr->cells) {
        return;
    }
    for (int i = 0; i < fr->nlocals; i++) {
        cell_unref(fr->cells[i]);
        fr->cells[i] = NULL;
    }
    free(fr->cells);
    fr->cells = NULL;
}

int vm_call(VM *vm, int argc) {
    if (vm->sp < argc + 1) {
        vm_fail(vm, "stack underflow");
        return -1;
    }
    Value *fn = vm->stack[vm->sp - argc - 1];
    if (!fn || fn->kind != VAL_FN || !fn->u.fn || !fn->u.fn->chunk) {
        vm_fail(vm, "not a function");
        return -1;
    }
    VmFunc *rec = fn->u.fn;
    if (argc < (int)rec->nrequired || argc > (int)rec->nparams || (int)rec->nlocals < (int)rec->nparams) {
        vm_fail(vm, "wrong number of arguments");
        return -1;
    }
    if (vm->nframes >= VM_FRAMES) {
        vm_fail(vm, "recursion depth exceeded");
        return -1;
    }
    int base = vm->sp - argc;
    Frame *fr = &vm->frames[vm->nframes++];
    fr->chunk = rec->chunk;
    fr->ip = 0;
    fr->base = base;
    fr->nlocals = (int)rec->nlocals;
    fr->argc = argc;
    fr->cells = NULL;
    while (vm->sp < base + fr->nlocals) {
        vm_push(vm, value_null());
    }
    open_cells(vm, fr);
    return 0;
}

void vm_return(VM *vm) {
    if (vm->nframes <= 0) {
        vm_fail(vm, "stack underflow");
        return;
    }
    Frame *fr = &vm->frames[vm->nframes - 1];
    if (vm->sp <= fr->base + fr->nlocals) {
        vm_fail(vm, "stack underflow");
        return;
    }
    Value *result = vm->stack[--vm->sp];
    /* Drop the frame's cell refs before the stack drops the values. A function
       stored in its own capture cell is then freed by the stack unref, which
       releases the cell. */
    close_cells(fr);
    while (vm->sp > fr->base) {
        value_unref(vm->stack[--vm->sp]);
    }
    int base = fr->base;
    vm->nframes--;
    if (vm->nframes == 0) {
        value_unref(result);
        vm->stop = 1;
        return;
    }
    if (base <= 0) {
        value_unref(result);
        vm_fail(vm, "stack underflow");
        return;
    }
    value_unref(vm->stack[base - 1]);
    vm->stack[base - 1] = result;
    vm->sp = base;
    if (!vm->failed && !vm->stop) {
        vm->status = 0;
    }
}

static int64_t iadd(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a + (uint64_t)b);
}

static int64_t isub(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a - (uint64_t)b);
}

static int as_float(const Value *value, double *out) {
    if (value->kind == VAL_INT) {
        *out = (double)value->u.i;
        return 1;
    }
    if (value->kind == VAL_FLOAT) {
        *out = value->u.f;
        return 1;
    }
    return 0;
}

int vm_addsub(VM *vm, int is_add) {
    Value *right = vm_pop(vm);
    Value *left = vm_pop(vm);
    if (!left || !right) {
        value_unref(left);
        value_unref(right);
        return -1;
    }
    if (left->kind == VAL_INT && right->kind == VAL_INT) {
        int64_t n = is_add ? iadd(left->u.i, right->u.i) : isub(left->u.i, right->u.i);
        value_unref(left);
        value_unref(right);
        vm_push(vm, value_int(n));
        return 0;
    }
    double ld = 0;
    double rd = 0;
    if (!as_float(left, &ld) || !as_float(right, &rd)) {
        value_unref(left);
        value_unref(right);
        vm_fail(vm, "bad operand");
        return -1;
    }
    value_unref(left);
    value_unref(right);
    vm_push(vm, value_float(is_add ? ld + rd : ld - rd));
    return 0;
}

static void dispatch(VM *vm, Frame *fr, Chunk *chunk, uint32_t ip, int width, uint8_t op) {
    switch (op) {
    case OP_CONST_INT:
        vm_push(vm, value_int(chunk_i64(chunk->code, ip + 1)));
        return;
    case OP_LOAD_LOCAL: {
        uint16_t slot = chunk_u16(chunk->code, ip + 1);
        if ((int)slot >= fr->nlocals) {
            vm_fail(vm, "bad local");
            return;
        }
        Value *value = vm->stack[fr->base + (int)slot];
        value_ref(value);
        vm_push(vm, value);
        return;
    }
    case OP_LOAD_UP: {
        uint16_t idx = chunk_u16(chunk->code, ip + 1);
        if (fr->base <= 0) {
            vm_fail(vm, "bad capture");
            return;
        }
        Value *self = vm->stack[fr->base - 1];
        if (!self || self->kind != VAL_FN || !self->u.fn || idx >= self->u.fn->nups ||
            !self->u.fn->ups[idx]) {
            vm_fail(vm, "bad capture");
            return;
        }
        Value *value = self->u.fn->ups[idx]->value;
        value_ref(value);
        vm_push(vm, value);
        return;
    }
    case OP_JUMP_GE_ARGC: {
        uint8_t need = chunk->code[ip + 1];
        uint16_t target = chunk_u16(chunk->code, ip + 2);
        fr->ip = (uint8_t)fr->argc >= need ? target : ip + (uint32_t)width;
        return;
    }
    case OP_ADD:
        vm_addsub(vm, 1);
        return;
    case OP_SUB:
        vm_addsub(vm, 0);
        return;
    case OP_JUMP:
        fr->ip = chunk_u16(chunk->code, ip + 1);
        return;
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE: {
        Value *value = vm_pop(vm);
        if (!value) {
            return;
        }
        int yes = vm_truthy(value);
        value_unref(value);
        int take = op == OP_JUMP_IF_TRUE ? yes : !yes;
        fr->ip = take ? chunk_u16(chunk->code, ip + 1) : ip + (uint32_t)width;
        return;
    }
    case OP_RETURN:
        vm_return(vm);
        return;
    case OP_CONST: {
        uint16_t idx = chunk_u16(chunk->code, ip + 1);
        if (idx >= chunk->consts_len || !chunk->consts[idx]) {
            vm_fail(vm, "bad const");
            return;
        }
        value_ref(chunk->consts[idx]);
        vm_push(vm, chunk->consts[idx]);
        return;
    }
    case OP_TRUE:
        vm_push(vm, value_bool(1));
        return;
    case OP_FALSE:
        vm_push(vm, value_bool(0));
        return;
    case OP_NULL:
        vm_push(vm, value_null());
        return;
    case OP_STORE_LOCAL: {
        uint16_t slot = chunk_u16(chunk->code, ip + 1);
        Value *value = vm_pop(vm);
        if (!value) {
            return;
        }
        if ((int)slot >= fr->nlocals) {
            value_unref(value);
            vm_fail(vm, "bad local");
            return;
        }
        value_unref(vm->stack[fr->base + (int)slot]);
        vm->stack[fr->base + (int)slot] = value;
        if (fr->cells && fr->cells[slot]) {
            value_unref(fr->cells[slot]->value);
            value_ref(value);
            fr->cells[slot]->value = value;
        }
        return;
    }
    case OP_POP:
        value_unref(vm_pop(vm));
        return;
    case OP_NEG:
    case OP_NOT:
        vm_unary(vm, op);
        return;
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_EQ:
    case OP_NE:
    case OP_LT:
    case OP_LE:
    case OP_GT:
    case OP_GE:
        vm_binop(vm, op);
        return;
    case OP_CALL: {
        int argc = chunk->code[ip + 1];
        vm_call(vm, argc);
        return;
    }
    case OP_BUILTIN: {
        int id = chunk->code[ip + 1];
        int argc = chunk->code[ip + 2];
        if (vm_builtin(vm, id, argc) == 0 && !vm->failed && !vm->stop) {
            vm->status = 0;
        }
        return;
    }
    case OP_MAKE_FN:
        vm_make_fn(vm, chunk, chunk_u16(chunk->code, ip + 1));
        return;
    case OP_ARRAY:
        vm_array(vm, (int)chunk_u16(chunk->code, ip + 1));
        return;
    case OP_MAP:
        vm_map(vm, (int)chunk_u16(chunk->code, ip + 1));
        return;
    case OP_INDEX:
        vm_index(vm);
        return;
    case OP_FIELD:
        vm_field(vm, chunk_u16(chunk->code, ip + 1));
        return;
    case OP_METHOD: {
        int id = (int)chunk_u16(chunk->code, ip + 1);
        int argc = chunk->code[ip + 3];
        int rc = vm_method(vm, id, argc);
        if (rc == 0 && !vm->failed && !vm->stop) {
            vm->status = 0;
        }
        return;
    }
    case OP_CAT:
        vm_cat(vm);
        return;
    case OP_EXEC:
        vm_exec(vm, chunk_u16(chunk->code, ip + 1), 0, 0);
        return;
    case OP_EXEC_PIPE:
        vm_exec(vm, chunk_u16(chunk->code, ip + 1), 0, 0);
        return;
    case OP_EXEC_SPLICE:
        vm_exec(vm, chunk_u16(chunk->code, ip + 1), 1, chunk_u16(chunk->code, ip + 3));
        return;
    case OP_IMPORT: {
        uint16_t idx = chunk_u16(chunk->code, ip + 1);
        Value *spec;
        Value *mod = NULL;
        if (idx >= chunk->consts_len || !chunk->consts[idx] || chunk->consts[idx]->kind != VAL_STR) {
            vm_fail(vm, "bad import");
            return;
        }
        spec = chunk->consts[idx];
        if (module_load(vm->mods, vm->jobs, vm->arena, vm->filename, spec->u.str->data, vm->line, 0,
                        vm->argc, vm->argv, vm->envp, &mod) != 0) {
            vm->failed = 1;
            vm->status = 2;
            return;
        }
        vm_push(vm, mod);
        return;
    }
    default:
        vm_fail(vm, "bad opcode");
        return;
    }
}

void vm_loop(VM *vm, int mark) {
    while (!vm->failed && !vm->stop && vm->nframes >= mark) {
        Frame *fr;
        if (vm->step_limit > 0) {
            vm->steps++;
            if (vm->steps > vm->step_limit) {
                vm->stop = 1;
                return;
            }
        }
        fr = &vm->frames[vm->nframes - 1];
        Chunk *chunk = fr->chunk;
        if (!chunk || fr->ip >= chunk->code_len) {
            if (vm->nframes == 1) {
                return;
            }
            vm_push(vm, value_null());
            vm_return(vm);
            continue;
        }
        uint32_t ip = fr->ip;
        int width = chunk_width(chunk->code, chunk->code_len, ip);
        if (width <= 0) {
            vm->line = 1;
            vm_fail(vm, "bad opcode");
            return;
        }
        vm->line = 1;
        if (chunk->lines && ip < chunk->code_len) {
            vm->line = (int)chunk->lines[ip];
            if (vm->line < 1) {
                vm->line = 1;
            }
        }
        if (vm->trace) {
            fputs("> ", stderr);
            disasm_ins(chunk, ip, stderr);
        }
        uint8_t op = chunk->code[ip];
        int jump = op == OP_JUMP || op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE || op == OP_RETURN ||
                   op == OP_JUMP_GE_ARGC;
        if (!jump) {
            fr->ip = ip + (uint32_t)width;
        }
        dispatch(vm, fr, chunk, ip, width, op);
    }
}

int vm_run(Arena *arena, const char *filename, const char *src, size_t src_len, Chunk *chunk,
           int argc, char **argv, char **envp, int trace, ModCache *mods, Jobs *jobs,
           const ShellStart *shstart, int step_limit) {
    VM vm;
    memset(&vm, 0, sizeof vm);
    if (step_limit > 0) {
        vm.step_limit = step_limit;
    }
    vm.arena = arena;
    vm.filename = filename ? filename : "bettersh";
    vm.src = src ? src : "";
    vm.src_len = src_len;
    vm.argc = argc;
    vm.argv = argv;
    vm.envp = envp;
    vm.trace = trace;
    vm.mods = mods;
    vm.jobs = jobs;
    vm.shstart = shstart;
    vm.cap = 64;
    vm.stack = xmalloc((size_t)vm.cap * sizeof(Value *));
    int nlocals = chunk ? (int)chunk->max_locals : 0;
    vm.frames[0].chunk = chunk;
    vm.frames[0].ip = 0;
    vm.frames[0].base = 0;
    vm.frames[0].nlocals = nlocals;
    vm.frames[0].argc = 0;
    vm.frames[0].cells = NULL;
    vm.nframes = 1;
    for (int i = 0; i < nlocals; i++) {
        vm_push(&vm, value_null());
    }
    open_cells(&vm, &vm.frames[0]);
    vm_loop(&vm, 1);
    for (int i = 0; i < vm.nframes; i++) {
        close_cells(&vm.frames[i]);
    }
    for (int i = 0; i < vm.sp; i++) {
        value_unref(vm.stack[i]);
        vm.stack[i] = NULL;
    }
    vm.sp = 0;
    if (vm.shell_on) {
        if (!vm.failed) {
            vm.shell.status = vm.status;
        }
        eval_end(&vm.shell);
        if (!vm.failed) {
            vm.status = vm.shell.status;
        }
    }
    free(vm.stack);
    if (vm.failed) {
        return 2;
    }
    return vm.status & 255;
}

int vm_run_module(Arena *arena, const char *filename, const char *src, size_t src_len, Chunk *chunk,
                  int argc, char **argv, char **envp, ModCache *mods, Jobs *jobs, Value **exported) {
    VM vm;
    Map *map;
    Frame *fr;
    int i;
    memset(&vm, 0, sizeof vm);
    *exported = NULL;
    vm.arena = arena;
    vm.filename = filename ? filename : "bettersh";
    vm.src = src ? src : "";
    vm.src_len = src_len;
    vm.argc = argc;
    vm.argv = argv;
    vm.envp = envp;
    vm.mods = mods;
    vm.jobs = jobs;
    vm.cap = 64;
    vm.stack = xmalloc((size_t)vm.cap * sizeof(Value *));
    vm.frames[0].chunk = chunk;
    vm.frames[0].ip = 0;
    vm.frames[0].base = 0;
    vm.frames[0].nlocals = chunk ? (int)chunk->max_locals : 0;
    vm.frames[0].argc = 0;
    vm.frames[0].cells = NULL;
    vm.nframes = 1;
    for (i = 0; i < vm.frames[0].nlocals; i++) {
        vm_push(&vm, value_null());
    }
    open_cells(&vm, &vm.frames[0]);
    vm_loop(&vm, 1);
    if (!vm.failed) {
        map = map_new();
        fr = &vm.frames[0];
        for (i = 0; i < fr->nlocals; i++) {
            const char *name = "";
            Value *slot;
            if (fr->chunk && fr->chunk->local_names && (uint32_t)i < fr->chunk->local_len &&
                fr->chunk->local_names[i]) {
                name = fr->chunk->local_names[i]->data;
            }
            if (!name[0] || name[0] == '.') {
                continue;
            }
            slot = (fr->base + i < vm.sp) ? vm.stack[fr->base + i] : NULL;
            if (!slot) {
                slot = value_null();
            } else {
                value_ref(slot);
            }
            map_set(map, name, slot);
        }
        *exported = value_map(map);
    }
    for (i = 0; i < vm.nframes; i++) {
        close_cells(&vm.frames[i]);
    }
    for (i = 0; i < vm.sp; i++) {
        value_unref(vm.stack[i]);
        vm.stack[i] = NULL;
    }
    vm.sp = 0;
    if (vm.shell_on) {
        if (!vm.failed) {
            vm.shell.status = vm.status;
        }
        eval_end(&vm.shell);
    }
    free(vm.stack);
    if (vm.failed) {
        value_unref(*exported);
        *exported = NULL;
        return -1;
    }
    return 0;
}

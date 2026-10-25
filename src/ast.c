#include "ast.h"

#include <string.h>

static void indent_to(FILE *out, int level) {
    for (int i = 0; i < level; i++) {
        fputc(' ', out);
    }
}

static void dump_str(FILE *out, const char *text) {
    fputc('"', out);
    if (!text) {
        text = "";
    }
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        unsigned char c = *p;
        if (c == '\\' || c == '"') {
            fprintf(out, "\\%c", c);
        } else if (c == '\n') {
            fputs("\\n", out);
        } else if (c == '\t') {
            fputs("\\t", out);
        } else if (c == '\r') {
            fputs("\\r", out);
        } else if (c < 32 || c > 126) {
            fprintf(out, "\\x%02x", c);
        } else {
            fputc(c, out);
        }
    }
    fputc('"', out);
}

static const char *param_op_spell(ParamOp op) {
    switch (op) {
    case P_MINUS:
        return "-";
    case P_COLON_MINUS:
        return ":-";
    case P_EQ:
        return "=";
    case P_COLON_EQ:
        return ":=";
    case P_PLUS:
        return "+";
    case P_COLON_PLUS:
        return ":+";
    case P_QUEST:
        return "?";
    case P_COLON_QUEST:
        return ":?";
    case P_HASH:
        return "#";
    case P_HASH_HASH:
        return "##";
    case P_PCT:
        return "%";
    case P_PCT_PCT:
        return "%%";
    case P_COLON_CONCAT:
        return ":}";
    case P_LEN:
    case P_NONE:
    case P_SUBSCRIPT:
        return "";
    }
    return "";
}

static const char *sep_name(SepKind sep) {
    switch (sep) {
    case SEP_SEMI:
        return "semi";
    case SEP_AMP:
        return "amp";
    case SEP_NL:
        return "nl";
    case SEP_NONE:
        return "none";
    }
    return "none";
}

static const char *redir_name(RedirOp op) {
    switch (op) {
    case REDIR_IN:
        return "<";
    case REDIR_OUT:
        return ">";
    case REDIR_APPEND:
        return ">>";
    case REDIR_IN_DUP:
        return "<&";
    case REDIR_OUT_DUP:
        return ">&";
    case REDIR_RW:
        return "<>";
    case REDIR_CLOBBER:
        return ">|";
    case REDIR_HERE:
        return "<<";
    case REDIR_HERE_DASH:
        return "<<-";
    case REDIR_HERESTR:
        return "<<<";
    }
    return "?";
}

static void dump_part(FILE *out, int level, const WordPart *part);
static void dump_word(FILE *out, int level, const Word *word, int assign_prefix);
static void dump_node(FILE *out, int level, const Node *node);
static void dump_redirs(FILE *out, int level, Redirect **redirs, uint32_t n);

static void dump_part(FILE *out, int level, const WordPart *part) {
    indent_to(out, level);
    switch (part->kind) {
    case PART_LIT:
        fputs(part->quoted ? "lit q " : "lit ", out);
        dump_str(out, part->text);
        fputc('\n', out);
        break;
    case PART_PARAM:
        fputs("param ", out);
        dump_str(out, part->text ? part->text : "");
        if (part->op == P_LEN) {
            fputs(" len", out);
        } else if (part->op != P_NONE) {
            fprintf(out, " %s", param_op_spell(part->op));
        }
        if (part->quoted) {
            fputs(" q", out);
        }
        fputc('\n', out);
        if (part->nested) {
            dump_word(out, level + 2, part->nested, 0);
        }
        break;
    case PART_CMDSUB:
        fputs(part->quoted ? "cmdsub q\n" : "cmdsub\n", out);
        if (part->child) {
            dump_node(out, level + 2, part->child);
        }
        break;
    case PART_ARITH:
        fputs(part->quoted ? "arith q " : "arith ", out);
        dump_str(out, part->text);
        fputc('\n', out);
        break;
    }
}

static void dump_word(FILE *out, int level, const Word *word, int assign_prefix) {
    indent_to(out, level);
    fputs("word\n", out);
    if (!word) {
        return;
    }
    if (assign_prefix && word->is_assign) {
        WordPart name;
        WordPart eq;
        memset(&name, 0, sizeof name);
        memset(&eq, 0, sizeof eq);
        name.kind = PART_LIT;
        name.text = word->assign_name ? word->assign_name : "";
        eq.kind = PART_LIT;
        eq.text = "=";
        dump_part(out, level + 2, &name);
        dump_part(out, level + 2, &eq);
    }
    for (uint32_t i = 0; i < word->nparts; i++) {
        dump_part(out, level + 2, &word->parts[i]);
    }
}

static void dump_simple(FILE *out, int level, const Node *node) {
    indent_to(out, level);
    fputs("simple\n", out);
    for (uint32_t i = 0; i < node->u.simple.nassigns; i++) {
        Word *word = node->u.simple.assigns[i];
        indent_to(out, level + 2);
        fputs("assign ", out);
        dump_str(out, word && word->assign_name ? word->assign_name : "");
        fputc('\n', out);
        dump_word(out, level + 4, word, 0);
    }
    if (node->u.simple.nargv > 0) {
        indent_to(out, level + 2);
        fputs("argv\n", out);
        for (uint32_t i = 0; i < node->u.simple.nargv; i++) {
            dump_word(out, level + 4, node->u.simple.argv[i], 1);
        }
    }
    dump_redirs(out, level, node->u.simple.redirs, node->u.simple.nredirs);
}

static void dump_redirs(FILE *out, int level, Redirect **redirs, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        const Redirect *redir = redirs[i];
        indent_to(out, level + 2);
        fprintf(out, "redir %s", redir_name(redir->op));
        if (redir->fd >= 0) {
            fprintf(out, " fd=%d", redir->fd);
        }
        if (redir->noexpand) {
            fputs(" noexpand", out);
        }
        fputc('\n', out);
        indent_to(out, level + 4);
        fputs("target\n", out);
        dump_word(out, level + 6, redir->word, 1);
        if (redir->op == REDIR_HERE || redir->op == REDIR_HERE_DASH) {
            indent_to(out, level + 4);
            fputs("body\n", out);
            dump_word(out, level + 6, redir->heredoc, 0);
        }
    }
}

static void dump_node(FILE *out, int level, const Node *node) {
    if (!node) {
        indent_to(out, level);
        fputs("empty\n", out);
        return;
    }
    switch (node->kind) {
    case NODE_PROGRAM:
        indent_to(out, level);
        fputs("program\n", out);
        for (uint32_t i = 0; i < node->u.program.n; i++) {
            indent_to(out, level + 2);
            fprintf(out, "item %s\n", sep_name(node->u.program.items[i].sep));
            dump_node(out, level + 4, node->u.program.items[i].node);
        }
        break;
    case NODE_AND_OR:
        indent_to(out, level);
        fputs(node->u.andor.op == ANDOR_AND ? "and\n" : "or\n", out);
        dump_node(out, level + 2, node->u.andor.left);
        dump_node(out, level + 2, node->u.andor.right);
        break;
    case NODE_PIPELINE:
        indent_to(out, level);
        fputs("pipe\n", out);
        if (node->u.pipeline.bang) {
            indent_to(out, level + 2);
            fputs("bang\n", out);
        }
        for (uint32_t i = 0; i < node->u.pipeline.n; i++) {
            dump_node(out, level + 2, node->u.pipeline.cmds[i]);
        }
        break;
    case NODE_SIMPLE:
        dump_simple(out, level, node);
        break;
    case NODE_BRACE:
    case NODE_SUBSHELL:
        indent_to(out, level);
        fputs(node->kind == NODE_BRACE ? "brace\n" : "subshell\n", out);
        dump_node(out, level + 2, node->u.group.body);
        dump_redirs(out, level, node->u.group.redirs, node->u.group.nredirs);
        break;
    case NODE_IF:
        indent_to(out, level);
        fputs("if\n", out);
        indent_to(out, level + 2);
        fputs("cond\n", out);
        dump_node(out, level + 4, node->u.ifc.cond);
        indent_to(out, level + 2);
        fputs("then\n", out);
        dump_node(out, level + 4, node->u.ifc.body);
        if (node->u.ifc.otherwise) {
            indent_to(out, level + 2);
            fputs("else\n", out);
            dump_node(out, level + 4, node->u.ifc.otherwise);
        }
        dump_redirs(out, level, node->u.ifc.redirs, node->u.ifc.nredirs);
        break;
    case NODE_WHILE:
        indent_to(out, level);
        fputs(node->u.loop.until ? "until\n" : "while\n", out);
        indent_to(out, level + 2);
        fputs("cond\n", out);
        dump_node(out, level + 4, node->u.loop.cond);
        indent_to(out, level + 2);
        fputs("do\n", out);
        dump_node(out, level + 4, node->u.loop.body);
        dump_redirs(out, level, node->u.loop.redirs, node->u.loop.nredirs);
        break;
    case NODE_FOR:
        indent_to(out, level);
        fputs("for ", out);
        dump_str(out, node->u.forloop.name ? node->u.forloop.name : "");
        fputc('\n', out);
        if (node->u.forloop.has_in) {
            indent_to(out, level + 2);
            fputs("in\n", out);
            for (uint32_t i = 0; i < node->u.forloop.nwords; i++) {
                dump_word(out, level + 4, node->u.forloop.words[i], 1);
            }
        }
        indent_to(out, level + 2);
        fputs("do\n", out);
        dump_node(out, level + 4, node->u.forloop.body);
        dump_redirs(out, level, node->u.forloop.redirs, node->u.forloop.nredirs);
        break;
    case NODE_CASE:
        indent_to(out, level);
        fputs("case\n", out);
        indent_to(out, level + 2);
        fputs("word\n", out);
        dump_word(out, level + 4, node->u.cas.word, 1);
        for (uint32_t i = 0; i < node->u.cas.narms; i++) {
            const CaseArm *arm = &node->u.cas.arms[i];
            indent_to(out, level + 2);
            fputs("arm\n", out);
            for (uint32_t p = 0; p < arm->npats; p++) {
                indent_to(out, level + 4);
                fputs("pat\n", out);
                dump_word(out, level + 6, arm->pats[p], 1);
            }
            indent_to(out, level + 4);
            fputs("body\n", out);
            dump_node(out, level + 6, arm->body);
        }
        dump_redirs(out, level, node->u.cas.redirs, node->u.cas.nredirs);
        break;
    case NODE_FUNC:
        indent_to(out, level);
        fputs("func ", out);
        dump_str(out, node->u.func.name ? node->u.func.name : "");
        fputc('\n', out);
        dump_node(out, level + 2, node->u.func.body);
        break;
    }
}

void ast_dump(const Node *node, FILE *out) {
    dump_node(out, 0, node);
}
